#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/NpcDialogFocusFix.h"

namespace CrashReporter { void RecordEvent(const char*, const char*, ...) {} }
static DWORD N(DWORD address) { return address + 0x30000000; }
static void Require(bool ok, const char* message) {
    if (!ok) { std::printf("FAIL %s\n", message); std::exit(1); }
}
static BYTE* Map(const char* path) {
    FILE* file = nullptr;
    Require(fopen_s(&file, path, "rb") == 0, "open client");
    fseek(file, 0, SEEK_END);
    std::vector<BYTE> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read client");
    fclose(file);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    auto* base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(N(0x400000)),
        nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(base == reinterpret_cast<void*>(N(0x400000)), "map image without starting game");
    memcpy(base, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(base + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    return base;
}
static void Jump(DWORD address, void* target) {
    auto* site = reinterpret_cast<BYTE*>(N(address));
    site[0] = 0xe9;
    const DWORD relative = reinterpret_cast<DWORD>(target) - N(address) - 5;
    memcpy(site + 1, &relative, 4);
}
static void Relocate(DWORD operand) {
    auto* value = reinterpret_cast<DWORD*>(N(operand));
    *value = N(*value);
}
static BYTE manager[0x100], dialog[0x80], underlying[0x80], stage[0x20], user[0x3140];
static DWORD wndTable[14], uiTable[19], managerTable[19], stageTable[19];
static int destroyCalls, stageKeys, swallowedKeys, remainingModal;
static bool changeStage, changeUser, changeManager, failDestroy;
template<class T> static T& Field(BYTE* object, unsigned offset) {
    return *reinterpret_cast<T*>(object + offset);
}
static int __fastcall Accept(void*, void*, int) { return 1; }
static int __fastcall Enabled(void*, void*) { return 1; }
static int __fastcall WindowKind(void*, void*, DWORD type) { return type == 0xbf1608; }
static int __fastcall NoKind(void*, void*, DWORD) { return 0; }
static void __fastcall StageKey(void*, void*, unsigned key, unsigned) {
    Require(key >= VK_LEFT && key <= VK_DOWN, "native arrow reaches field unchanged");
    ++stageKeys;
}
static void __fastcall Swallow(void*, void*, unsigned, unsigned) { ++swallowedKeys; }
static void* __fastcall FindNothing(void*, void*, void*, void*) { return nullptr; }
static void __fastcall Destroy(void* object, void*) {
    Require(object == dialog, "close call ABI");
    ++destroyCalls;
    if (failDestroy) RaiseException(0xe0424242, 0, 0, nullptr);
    Field<DWORD>(dialog, 0x14) = 0;
    Field<DWORD>(manager, 0x74) = remainingModal;
    // Real CWndMan::RemoveWnd chooses the top registered window and runs the
    // real SetActiveWnd / SetFocus. Only rendering and registry allocation
    // are stand-ins; their callbacks cannot repair focus for the test.
    reinterpret_cast<void(__thiscall*)(void*, void*)>(N(0x9e3454))(manager, dialog + 4);
    if (changeStage) *reinterpret_cast<void**>(N(0xbeded4)) = nullptr;
    if (changeUser) *reinterpret_cast<void**>(N(0xbebf98)) = nullptr;
    if (changeManager) *reinterpret_cast<void**>(N(0xbec20c)) = nullptr;
}
static DWORD closeEntry = N(0x9ab8ef);
static void InvokeClose() {
    // Execute the native completed/layer checks and result assignment, then
    // its actual patched CALL. Stop at the original local-string cleanup.
    DWORD frame[16]{};
    DWORD* fp = frame + 8;
    fp[2] = 2; // Cancel/escape; the shared close tail also serves normal OK.
    __asm {
        push ebp
        push ebx
        push esi
        mov ebp, fp
        xor ebx, ebx
        mov esi, offset dialog
        call dword ptr [closeEntry]
        pop esi
        pop ebx
        pop ebp
    }
}
static bool CloseFaults() {
    __try { InvokeClose(); }
    __except(GetExceptionCode() == 0xe0424242 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static void Arrows() {
    auto* focused = Field<BYTE*>(manager, 0x88);
    Require(focused != nullptr, "a live keyboard target exists");
    auto table = *reinterpret_cast<DWORD**>(focused);
    auto onKey = reinterpret_cast<void(__thiscall*)(void*, unsigned, unsigned)>(table[0]);
    for (unsigned key = VK_LEFT; key <= VK_DOWN; ++key) {
        onKey(focused, key, 0);
        onKey(focused, key, 0x80000000);
    }
}
static void Reset() {
    memset(manager, 0, sizeof(manager));
    memset(dialog, 0, sizeof(dialog));
    memset(underlying, 0, sizeof(underlying));
    Field<DWORD*>(manager, 4) = managerTable;
    Field<DWORD*>(stage, 4) = stageTable;
    Field<DWORD*>(dialog, 0) = Field<DWORD*>(underlying, 0) = wndTable;
    Field<DWORD*>(dialog, 4) = Field<DWORD*>(underlying, 4) = uiTable;
    Field<DWORD>(dialog, 0x14) = 1;
    Field<BYTE*>(manager, 0x80) = dialog;
    Field<BYTE*>(manager, 0x88) = dialog + 4;
    *reinterpret_cast<void**>(N(0xbec20c)) = manager;
    *reinterpret_cast<void**>(N(0xbebf98)) = user;
    *reinterpret_cast<void**>(N(0xbeded4)) = stage;
    *reinterpret_cast<DWORD*>(N(0xbf1650)) = 1;
    static void* top = underlying;
    *reinterpret_cast<void***>(N(0xbf1658)) = &top;
    destroyCalls = stageKeys = swallowedKeys = remainingModal = 0;
    changeStage = changeUser = changeManager = failDestroy = false;
}
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Require(argc == 2, "client path");
    BYTE* base = Map(argv[1]);
    wndTable[10] = reinterpret_cast<DWORD>(&Accept);
    uiTable[0] = reinterpret_cast<DWORD>(&Swallow);
    uiTable[1] = reinterpret_cast<DWORD>(&Accept);
    uiTable[8] = reinterpret_cast<DWORD>(&Enabled);
    uiTable[18] = reinterpret_cast<DWORD>(&WindowKind);
    memcpy(managerTable, uiTable, sizeof(uiTable));
    managerTable[0] = N(0x9e2f05);
    managerTable[1] = N(0x9e2f20);
    managerTable[18] = reinterpret_cast<DWORD>(&NoKind);
    stageTable[0] = reinterpret_cast<DWORD>(&StageKey);
    stageTable[1] = reinterpret_cast<DWORD>(&Accept);
    // Relocate the globals used by these exact native functions. Relative
    // calls stay valid in the isolated image; RTTI identities stay original.
    for (DWORD operand : {0x9e34b5u, 0x9e3519u, 0x9e3520u, 0x9e32fdu,
        0x9e3333u, 0x9e3361u, 0x9e33bbu, 0x9e2f06u, 0x9e2f21u}) Relocate(operand);
    *reinterpret_cast<void**>(N(0xbec33c)) = nullptr;
    Jump(0x9e4f28, FindNothing);
    Jump(0x9e00af, Destroy);
    *reinterpret_cast<BYTE*>(N(0x9ab90d)) = 0xc3;
    Reset(); InvokeClose(); Arrows();
    Require(destroyCalls == 1 && stageKeys == 0 && swallowedKeys == 8,
        "baseline: native dialog close leaves arrows with another CWnd");
    Require(!NpcDialogFocusFix::Install(nullptr), "null image rejected");
    base[0x5ab908] ^= 1;
    Require(!NpcDialogFocusFix::Install(reinterpret_cast<HMODULE>(base)), "unexpected close instruction rejected");
    base[0x5ab908] ^= 1;
    Require(NpcDialogFocusFix::Install(reinterpret_cast<HMODULE>(base)), "install");
    Require(NpcDialogFocusFix::Install(reinterpret_cast<HMODULE>(base)), "idempotent install");
    Reset(); InvokeClose(); Arrows();
    Require(destroyCalls == 1 && stageKeys == 8 && swallowedKeys == 0,
        "all four arrow down/up events reach the field immediately without a click");
    Require(Field<DWORD>(dialog, 0x70) == 1 && Field<DWORD>(dialog, 0x6c) == 2, "native result retained");
    InvokeClose(); Require(destroyCalls == 1, "repeated close does nothing");
    Reset(); remainingModal = 1; InvokeClose();
    Require(Field<BYTE*>(manager, 0x88) == underlying + 4, "nested modal keeps its focus");
    Reset(); *reinterpret_cast<void**>(N(0xbebf98)) = nullptr; InvokeClose();
    Require(Field<BYTE*>(manager, 0x88) == underlying + 4, "login/cash shop retains native behavior");
    Reset(); changeStage = true; InvokeClose();
    Require(Field<BYTE*>(manager, 0x88) == underlying + 4, "stage change during destruction is not refocused");
    Reset(); changeUser = true; InvokeClose();
    Require(Field<BYTE*>(manager, 0x88) == underlying + 4, "user change during destruction is not refocused");
    Reset(); changeManager = true; InvokeClose();
    Require(Field<BYTE*>(manager, 0x88) == underlying + 4, "manager change during destruction is not refocused");
    Reset(); Field<DWORD>(dialog, 0x14) = 0; InvokeClose();
    Require(destroyCalls == 0, "uncreated dialog never reaches the hook");
    Reset(); failDestroy = true;
    Require(CloseFaults(), "native destruction exceptions propagate");
    std::puts("PASS: native close tail, native focus cleanup/switch/field forwarding, four arrows without click, modal and lifecycle boundaries, signature rejection");
}
