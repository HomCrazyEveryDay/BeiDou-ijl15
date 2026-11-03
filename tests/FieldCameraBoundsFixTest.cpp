#include <windows.h>
#include <oleauto.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/FieldCameraBoundsFix.h"

// Execute the original GetField -> rectangle clamp -> camera Release block in
// an isolated image. No client entry point, window, login or game process.
static DWORD N(DWORD address) { return address + 0x30000000; }
static DWORD target, beforeStack, afterStack, cleanupState, restoredTick;
static DWORD faultAddress, faultRead, continuations, releases, clamps, kindChecks;
static bool isField;
static BYTE stage[0x110]{};
static void* stageVtable[19]{};
static void* cameraVtable[40]{};
static void** camera = cameraVtable;
static RECT expectedRect{}, observedRect{};
static VARIANT observedOption{}, observedMissing{};

static void Require(bool ok, const char* message) {
    if (!ok) { printf("FAIL %s error=%lu\n", message, GetLastError()); exit(1); }
}
static void Write(DWORD address, const void* bytes, SIZE_T size) {
    auto* destination = reinterpret_cast<void*>(address);
    DWORD old = 0, ignored = 0;
    Require(VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old) != FALSE, "test write permission");
    memcpy(destination, bytes, size);
    Require(FlushInstructionCache(GetCurrentProcess(), destination, size) != FALSE, "test instruction cache");
    Require(VirtualProtect(destination, size, old, &ignored) != FALSE, "test restore permission");
}
static void Redirect(DWORD from, void* to) {
    BYTE jump[] = {0xe9,0,0,0,0};
    const DWORD relative = reinterpret_cast<DWORD>(to) - from - 5;
    memcpy(jump + 1, &relative, 4);
    Write(from, jump, sizeof(jump));
}
static BYTE* Map(const char* path) {
    FILE* file = nullptr;
    Require(fopen_s(&file, path, "rb") == 0, "open client image");
    fseek(file, 0, SEEK_END);
    std::vector<BYTE> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read client image");
    fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    auto* image = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(N(0x400000)),
        nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(image == reinterpret_cast<void*>(N(0x400000)), "map isolated native image");
    memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(image + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    return image;
}
static int __fastcall IsField(void* self, void*, void* type) {
    Require(self == stage + 4 && type == reinterpret_cast<void*>(0xBED758), "native field type check");
    ++kindChecks;
    return isField;
}
static ULONG __stdcall ReleaseCamera(void* self) {
    Require(self == &camera, "release original camera reference");
    ++releases;
    return 1;
}
static HRESULT __stdcall ClampCamera(void* self, VARIANT missing, int left, int top,
    int width, int height, VARIANT option) {
    Require(self == &camera, "clamp original camera object");
    observedRect = {left,top,left + width,top + height};
    observedMissing = missing;
    observedOption = option;
    ++clamps;
    return S_OK;
}
__declspec(naked) static void ResumeAnimations() {
    __asm {
        inc continuations
        mov eax, [ebp-4]
        mov cleanupState, eax
        mov restoredTick, edi
        ret
    }
}
__declspec(naked) static void Invoke() {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 44h
        push ebx
        push esi
        push edi
        mov dword ptr [ebp-4], 0
        mov dword ptr [ebp-10h], offset camera
        mov dword ptr [ebp-14h], 12345678h
        mov beforeStack, esp
        call dword ptr [target]
        mov afterStack, esp
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}
static int Capture(EXCEPTION_POINTERS* info) {
    faultAddress = reinterpret_cast<DWORD>(info->ExceptionRecord->ExceptionAddress);
    faultRead = static_cast<DWORD>(info->ExceptionRecord->ExceptionInformation[1]);
    return info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
static bool TryInvoke() {
    __try { Invoke(); return true; }
    __except(Capture(GetExceptionInformation())) { return false; }
}
static void PrepareRuntime() {
    const DWORD stageSlot = N(0xBEDED4), defaultVariant = N(0xBF0B70), clearSlot = N(0xAF0264);
    Write(N(0x437A0F), &stageSlot, 4);
    Write(N(0x437883), &defaultVariant, 4);
    Write(N(0x402942), &clearSlot, 4);
    *reinterpret_cast<void**>(clearSlot) = reinterpret_cast<void*>(&VariantClear);
    auto* missing = reinterpret_cast<VARIANT*>(defaultVariant);
    VariantInit(missing); missing->vt = VT_ERROR; missing->scode = DISP_E_PARAMNOTFOUND;
    Redirect(N(0x4378F5), reinterpret_cast<void*>(&ResumeAnimations));
    stageVtable[18] = reinterpret_cast<void*>(&IsField);
    *reinterpret_cast<void***>(stage + 4) = stageVtable;
    cameraVtable[2] = reinterpret_cast<void*>(&ReleaseCamera);
    cameraVtable[39] = reinterpret_cast<void*>(&ClampCamera);
    target = N(0x43783C);
}
static void Reset(bool present, bool field, RECT rect) {
    *reinterpret_cast<void**>(N(0xBEDED4)) = present ? stage : nullptr;
    isField = field;
    expectedRect = rect;
    memcpy(stage + 0xF0, &rect, sizeof(rect));
    continuations = releases = clamps = kindChecks = 0;
    cleanupState = restoredTick = beforeStack = afterStack = faultAddress = faultRead = 0;
}
static void Exercise(bool present, bool field, RECT rect) {
    Reset(present, field, rect);
    Require(TryInvoke(), "native bounds block returns without access violation");
    Require(clamps == (present && field ? 1UL : 0UL), "only a real field applies its rectangle");
    Require(kindChecks == (present ? 1UL : 0UL), "native stage type discrimination preserved");
    Require(releases == 1 && continuations == 1, "camera released once and other animations continue");
    Require(beforeStack == afterStack && cleanupState == MAXDWORD && restoredTick == 0x12345678,
        "stack, unwind state and native tick restoration preserved");
    if (clamps) {
        Require(!memcmp(&observedRect, &expectedRect, sizeof(RECT)), "native rectangle coordinates unchanged");
        Require(observedMissing.vt == VT_ERROR && observedMissing.scode == DISP_E_PARAMNOTFOUND
            && observedOption.vt == VT_I4 && observedOption.lVal == 1, "native clamp VARIANT arguments unchanged");
    }
}
static void RejectChangedImage(BYTE* image) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    Require(!FieldCameraBoundsFix::Install(nullptr), "reject null image");
    Require(!FieldCameraBoundsFix::Install(reinterpret_cast<HMODULE>(1)), "reject unreadable image");
    const DWORD offsets[] = {static_cast<DWORD>(dos->e_lfanew) + 8, 0x3783c, 0x37841,
        0x37847, 0x378e6, 0x37a0c, 0x37a2d};
    for (DWORD offset : offsets) {
        image[offset] ^= 1;
        BYTE before[6]; memcpy(before, image + 0x37841, sizeof(before));
        Require(!FieldCameraBoundsFix::Install(reinterpret_cast<HMODULE>(image)), "reject unknown version or instruction sequence");
        Require(!memcmp(before, image + 0x37841, sizeof(before)), "rejected install leaves patch site intact");
        image[offset] ^= 1;
    }
}
int main(int argc, char** argv) {
    Require(argc == 2, "client EXE path");
    BYTE* image = Map(argv[1]);
    RejectChangedImage(image);
    PrepareRuntime();
    for (bool present : {false,true}) {
        Reset(present, false, {-100,-50,500,700});
        Require(!TryInvoke() && faultAddress == N(0x43786D) && faultRead == 0xF4,
            "reproduce production fault for missing field and non-field stage");
    }
    Exercise(true, true, {-100,-50,500,700});
    puts("PASS baseline: both absent-field states reproduce 0043786D/read F4; real field succeeds");

    // Restore the getter operand so the installer sees the authentic EXE bytes.
    const DWORD originalSlot = 0xBEDED4;
    Write(N(0x437A0F), &originalSlot, 4);
    DWORD old = 0;
    Require(VirtualProtect(image + 0x37000, 0x1000, PAGE_EXECUTE_READ, &old) != FALSE, "protect native code page");
    Require(FieldCameraBoundsFix::Install(reinterpret_cast<HMODULE>(image)), "install production guard");
    Require(FieldCameraBoundsFix::Install(reinterpret_cast<HMODULE>(image)), "idempotent installation");
    Require(!FieldCameraBoundsFix::Install(nullptr), "installed guard rejects another image");
    MEMORY_BASIC_INFORMATION memory{};
    Require(VirtualQuery(image + 0x37841, &memory, sizeof(memory)) == sizeof(memory)
        && memory.Protect == PAGE_EXECUTE_READ, "installer restores original page protection");
    PrepareRuntime();
    for (unsigned i = 0; i < 100; ++i) {
        Exercise(false, false, {});
        Exercise(true, false, {});
        Exercise(true, true, {-100,-50,500,700});
        Exercise(true, true, {30,20,30,20});
        Exercise(true, true, {-3000,-2000,-1000,-500});
    }
    puts("PASS fixed: 500 field/shop/unbound transitions; native clamp, cleanup, continuation and stack checks");
}
