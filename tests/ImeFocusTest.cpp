#include <windows.h>
#include <imm.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include "Memory.h"

static void Need(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static HWND gameWindow = reinterpret_cast<HWND>(1), otherWindow = reinterpret_cast<HWND>(2);
static HWND foreground = gameWindow;
static HIMC saved = reinterpret_cast<HIMC>(101), associated, otherContext = reinterpret_cast<HIMC>(102);
static unsigned otherTouches, releases, clears;
static bool composing;
static void Focus(void* target);
static void* focusOnCancel;
static HWND WINAPI Foreground() { return foreground; }
static HIMC WINAPI GetContext(HWND hwnd) {
    if (hwnd == otherWindow) { ++otherTouches; return otherContext; }
    if (hwnd != gameWindow) std::fprintf(stderr, "Unexpected HWND: %p\n", hwnd);
    Need(hwnd == gameWindow, "IME request targets game HWND"); return associated;
}
static HIMC WINAPI Associate(HWND hwnd, HIMC value) {
    if (hwnd == otherWindow) { ++otherTouches; HIMC old = otherContext; otherContext = value; return old; }
    Need(hwnd == gameWindow, "association targets game HWND");
    HIMC old = associated; associated = value; return old;
}
static BOOL WINAPI Release(HWND hwnd, HIMC context) {
    Need(hwnd == gameWindow && (!context || context == saved), "native context release pairing");
    ++releases; return TRUE;
}
static BOOL WINAPI Notify(HIMC context, DWORD action, DWORD index, DWORD value) {
    Need(!context || context == saved, "cancel composition uses saved context");
    Need(action == NI_COMPOSITIONSTR && index == CPS_CANCEL && !value, "native composition cancellation");
    composing = false;
    if (focusOnCancel) { void* target = focusOnCancel; focusOnCancel = nullptr; Focus(target); }
    return TRUE;
}
#define GetForegroundWindow Foreground
#define ImmGetContext GetContext
#define ImmAssociateContext Associate
#define ImmReleaseContext Release
#include "ImeUnderTest.h"
#undef GetForegroundWindow
#undef ImmGetContext
#undef ImmAssociateContext
#undef ImmReleaseContext

// Same patch primitive as Memory.cpp; production FixIme hooks and installation
// are compiled verbatim. Only the OS IME calls and rendering dependencies differ.
void Memory::FillBytes(DWORD at, unsigned char value, int size) { memset(reinterpret_cast<void*>(at), value, size); }
void Memory::WriteByte(DWORD at, unsigned char value) { *reinterpret_cast<BYTE*>(at) = value; }
void Memory::WriteInt(DWORD at, unsigned value) { *reinterpret_cast<DWORD*>(at) = value; }
void Memory::CodeCave(void* target, DWORD at, int size) {
    FillBytes(at, 0x90, size); WriteByte(at, 0xe9); WriteInt(at + 1, reinterpret_cast<DWORD>(target) - at - 5);
}
template<class T> static T& At(DWORD addr) { return *reinterpret_cast<T*>(addr); }
template<class T> static T& Field(BYTE* p, unsigned offset) { return *reinterpret_cast<T*>(p + offset); }
static BYTE manager[0x200], single[0x200], multi[0x200], password[0x200], unrelated[0x200], custom[0x200];
static DWORD singleTable[19], multiTable[19], rootTable[19], customTable[19], baseTable[16];
static void* nextOnDestroy;
static int __fastcall Accept(void*, void*, int) { return 1; }
static int __fastcall IsEnabled(void*, void*) { return 1; }
static int __fastcall IsKind(void*, void*, DWORD) { return 0; }
static void __fastcall ClearText(void*, void*, const char*) { ++clears; }
static void __fastcall Invalidate(void*, void*, void*) {}
static void __fastcall OnDestroy(void*, void*) { if (nextOnDestroy) Focus(nextOnDestroy); }
#ifndef IME_BASELINE
static int __fastcall CustomFocus(void* self, void*, int focused) {
    if (!focused) DisableImeForControl(self);
    return 1;
}
#endif
static void __fastcall Active(void* self, void*, void* window) {
    Need(self == manager, "SetActiveWnd receiver"); Field<void*>(manager, 0x80) = window;
}
static void Focus(void* target) {
    reinterpret_cast<void(__thiscall*)(void*, void*)>(0x009E3264)(manager, target);
}
static void* __fastcall FindEmpty(void*, void*, void*, int) { return nullptr; }
static void __fastcall ClearRef(void*, void*, int value) { Need(value == 0, "Destroy clears native reference"); }
static void Destroy(BYTE* control) { reinterpret_cast<void(__thiscall*)(void*)>(0x004DFE6B)(control); }

static void MapImage(const char* path) {
    FILE* f = nullptr; Need(!fopen_s(&f, path, "rb"), "open native fixture");
    fseek(f, 0, SEEK_END); std::vector<BYTE> bytes(ftell(f)); rewind(f);
    Need(fread(bytes.data(), 1, bytes.size(), f) == bytes.size(), "read fixture"); fclose(f);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Need(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386
        && nt->FileHeader.TimeDateStamp == 0x4b7c15c9 && nt->OptionalHeader.ImageBase == 0x400000
        && nt->OptionalHeader.SizeOfImage == 0xa94000, "supported v83 executable");
    auto image = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000), nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Need(image == reinterpret_cast<void*>(0x30400000), "map fixture without running its entry point");
    memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(image + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    // The v83 executable has no relocation directory. Rebase only absolute
    // operands in the functions exercised here; relative calls remain valid.
    for (DWORD operand : {0x004CA063u, 0x004CA07Au, 0x004CA085u, 0x004D32CAu, 0x004D32D5u,
        0x009E860Du, 0x009E8618u, 0x009E8637u, 0x009E8642u, 0x009E8650u,
        0x009E8669u, 0x009E867Eu, 0x009E8687u, 0x009E32FDu, 0x009E3333u,
        0x009E3361u, 0x004DFE81u, 0x009E34B5u}) {
        auto value = reinterpret_cast<DWORD*>(operand);
        Need(*value >= 0x00B00000 && *value < 0x00C00000, "known native absolute operand");
        *value += 0x30000000;
    }
    Need(!memcmp(reinterpret_cast<void*>(0x004CA05B), "\x39\x7c\x24\x0c\x74\x30", 6), "single loss gate");
    Need(!memcmp(reinterpret_cast<void*>(0x004D32C1), "\x83\x7c\x24\x08\x00\x74\x1a", 7), "multi loss gate");
    // The first loop advances EDI (47); the other two advance ESI (46).
    // Both increments must be independent one-byte instructions before patching.
    Need(!memcmp(reinterpret_cast<void*>(0x008D54A6), "\x74\x0b\xc6\x47\x01\x20\xc6\x07\x20\x47\x47", 11), "key text native patch bytes");
    Need(!memcmp(reinterpret_cast<void*>(0x00937225), "\x74\x18\xc6\x46\x01\x20\xc6\x06\x20\x46\x46", 11), "chat text native patch bytes");
    Need(!memcmp(reinterpret_cast<void*>(0x00531EE8), "\x74\x0b\xc6\x46\x01\x20\xc6\x06\x20\x46\x46", 11), "group text native patch bytes");
}
static void Dependencies() {
    At<void*>(0x00BEC20C) = manager; At<void*>(0x00BEBF98) = nullptr; At<void*>(0x00BEC33C) = nullptr;
    // Keep native EnableIME and CancelIME instructions, replace their OS imports.
    At<void*>(0x00BF05F4) = GetContext; At<void*>(0x00BF05EC) = Release;
    At<void*>(0x00BF05D8) = Associate; At<void*>(0x00BF05F8) = Notify;
    Memory::CodeCave(Invalidate, 0x009E04C9, 5);
    Memory::CodeCave(Active, 0x009E337E, 5);
    // Execute native RemoveWnd, including its direct focus clear without blur.
    // The fixture has no registered control list, so only its lookup is stubbed.
    Memory::CodeCave(FindEmpty, 0x009E4F28, 5);
    Memory::CodeCave(Invalidate, 0x009E06F6, 5);
    Memory::CodeCave(ClearRef, 0x0041FE69, 5);
    for (auto table : {singleTable, multiTable, rootTable, customTable}) {
        table[1] = reinterpret_cast<DWORD>(Accept); table[8] = reinterpret_cast<DWORD>(IsEnabled);
        table[15] = reinterpret_cast<DWORD>(ClearText); table[18] = reinterpret_cast<DWORD>(IsKind);
    }
    singleTable[1] = 0x004CA043; multiTable[1] = 0x004D32AB;
#ifndef IME_BASELINE
    customTable[1] = reinterpret_cast<DWORD>(CustomFocus);
#endif
    Field<DWORD*>(custom, 4) = customTable;
    baseTable[5] = reinterpret_cast<DWORD>(OnDestroy);
    Field<DWORD*>(manager, 4) = rootTable;
    Field<HWND>(manager, 0xAC) = gameWindow; Field<HIMC>(manager, 0xB0) = saved;
    for (auto control : {single, multi, password, unrelated}) {
        Field<DWORD*>(control, 0) = baseTable; Field<DWORD*>(control, 4) = control == multi ? multiTable : singleTable;
        Field<int>(control, 0x14) = 1; Field<int>(control + 4, 0x24) = 1;
    }
    Field<int>(password + 4, 0x80) = 1;
    Field<void*>(manager, 0x88) = manager + 4;
    nextOnDestroy = nullptr; associated = nullptr;
}
// Exercise the actual three native filtering loops against a guard page. The
// production patch must preserve every byte and stop at the first terminator.
struct TextCursor { char* esi; char* edi; };
__declspec(naked) static void __cdecl RunTextLoop(DWORD entry, char* text, TextCursor* cursor) {
    __asm {
        push ebx
        push esi
        push edi
        mov esi, [esp + 20]
        mov edi, esi
        xor ebx, ebx
        call dword ptr [esp + 16]
        mov eax, [esp + 24]
        mov [eax], esi
        mov [eax + 4], edi
        pop edi
        pop esi
        pop ebx
        ret
    }
}
static DWORD faultAddress;
static int CaptureTextFault(EXCEPTION_POINTERS* error) {
    faultAddress = reinterpret_cast<DWORD>(error->ExceptionRecord->ExceptionAddress);
    return error->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
static bool TryTextLoop(DWORD entry, char* text, TextCursor& cursor) {
    __try { RunTextLoop(entry, text, &cursor); return true; }
    __except (CaptureTextFault(GetExceptionInformation())) { return false; }
}
static void CheckTextBounds(bool baseline) {
    struct Loop { DWORD entry, fault, end, importOperand; bool usesEdi; };
    const Loop loops[] = {
        {0x008D5495, 0x008D5497, 0x008D54C3, 0x008D54A0, true},
        {0x00937214, 0x00937216, 0x0093724F, 0x0093721F},
        {0x00531ED7, 0x00531ED9, 0x00531F05, 0x00531EE2}
    };
    SYSTEM_INFO info; GetSystemInfo(&info);
    auto page = static_cast<char*>(VirtualAlloc(nullptr, info.dwPageSize * 2,
        MEM_RESERVE, PAGE_NOACCESS));
    Need(page && VirtualAlloc(page, info.dwPageSize, MEM_COMMIT, PAGE_READWRITE), "guarded string allocation");
    unsigned cases = 0, faults = 0;
    for (const auto& loop : loops) {
        // Raw fixture VA intentionally has no 0x00 prefix: ps1 must not rebase
        // this comparison. The operand is then explicitly redirected to our IAT.
        constexpr DWORD nativeDbcsImport = 0xAF0064, imageRebase = 0x30000000;
        Need(At<DWORD>(loop.importOperand) == nativeDbcsImport, "native DBCS import operand");
        At<DWORD>(loop.importOperand) = nativeDbcsImport + imageRebase;
        At<void*>(nativeDbcsImport + imageRebase) = reinterpret_cast<void*>(&IsDBCSLeadByte);
        Memory::WriteByte(loop.end, 0xC3); // Stop before the unrelated string-release/UI code.
        for (unsigned size : {0u, 1u, 2u, 3u, 4u, 5u, 31u, 32u, 1023u, 1024u}) {
            for (bool mixed : {false, true}) {
                char* text = page + info.dwPageSize - size - 1;
                std::vector<char> expected(size + 1, 'A');
                if (mixed) for (unsigned j = 0; j + 1 < size; j += 3) {
                    expected[j] = '\xD6'; expected[j + 1] = '\xD0'; // GBK Chinese plus ASCII.
                }
                expected[size] = 0;
                memcpy(text, expected.data(), expected.size());
                TextCursor cursor{};
                const bool ok = TryTextLoop(loop.entry, text, cursor);
                if (baseline && (size & 1)) {
                    Need(!ok && faultAddress == loop.fault, "old patch reproduces terminator-skipping read at native fault site");
                    ++faults;
                } else {
                    Need(ok, "text loop stops before guard page");
                    Need((loop.usesEdi ? cursor.edi : cursor.esi) == text + size, "text loop reaches the exact terminator");
                }
                Need(!memcmp(text, expected.data(), expected.size()), "Chinese and ASCII bytes preserved");
                ++cases;
            }
        }
    }
    VirtualFree(page, 0, MEM_RELEASE);
    std::printf("%s: %u native text cases across 3 loops, %u expected baseline access violations\n",
        baseline ? "BASELINE REPRODUCED" : "PASS", cases, faults);
}
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Need(argc == 3 || argc == 4, "fixture and baseline|patched arguments");
    MapImage(argv[1]); Dependencies();
    BYTE singleTail[0x1f], multiTail[0x1f];
    memcpy(singleTail, reinterpret_cast<void*>(0x004CA08F), sizeof(singleTail));
    memcpy(multiTail, reinterpret_cast<void*>(0x004D32E0), sizeof(multiTail));
    FixIme::HookNew();
    if (argc == 4 && !strcmp(argv[3], "chat-bounds")) {
        CheckTextBounds(!strcmp(argv[2], "baseline")); return 0;
    }
    Need(!memcmp(singleTail, reinterpret_cast<void*>(0x004CA08F), sizeof(singleTail))
        && !memcmp(multiTail, reinterpret_cast<void*>(0x004D32E0), sizeof(multiTail)), "native return tails intact");
    Focus(single + 4); Need(associated == saved && enabled, "single gain enables IME");
    Focus(manager + 4);
    if (!strcmp(argv[2], "baseline")) {
        Need(associated == saved && enabled, "baseline reproduces single IME retained after return to gameplay");
        Focus(multi + 4); Focus(manager + 4);
        Need(associated == saved && enabled, "baseline reproduces multi IME retained after return to gameplay");
        puts("BASELINE REPRODUCED: both native loss branches leave IME associated during gameplay"); return 0;
    }
    Need(!associated && !enabled, "single blur detaches IME");
    Focus(multi + 4); Need(associated == saved && enabled, "multi gain restores original context");
    Focus(manager + 4); Need(!associated && !enabled, "multi blur detaches IME");
    for (int i = 0; i < 100; ++i) {
        Focus(single + 4); Focus(multi + 4);
        Need(associated == saved && enabled, "single to multi survives gain-before-loss");
        Focus(single + 4); Need(associated == saved && enabled, "multi to single survives gain-before-loss");
        Focus(password + 4); Need(!associated && !enabled, "password stays IME disabled");
        Focus(multi + 4); Need(associated == saved && enabled, "password to edit restores IME");
        Focus(manager + 4); Need(!associated && !enabled, "return to gameplay after repeated switches");
    }
    Focus(single + 4); composing = true; Destroy(unrelated);
    Need(associated == saved && enabled && composing, "destroying unrelated control preserves active composition");
    nextOnDestroy = multi + 4; Destroy(single);
    Need(associated == saved && enabled, "destroying old control after focus transfer preserves new owner");
    nextOnDestroy = nullptr; composing = true; unsigned clearsBeforeDestroy = clears; Destroy(multi);
    Need(!Field<void*>(manager, 0x88) && clears == clearsBeforeDestroy, "native RemoveWnd clears focus without blur callback");
    Need(!associated && !enabled && !composing, "destroying text owner cancels composition and releases IME");
    Field<int>(multi, 0x14) = 1; Focus(multi + 4); foreground = otherWindow;
    Focus(manager + 4); Need(!associated && !enabled && !otherTouches && otherContext == reinterpret_cast<HIMC>(102),
        "loss while another app is foreground touches only game HWND");
    foreground = gameWindow; Focus(single + 4); Need(associated == saved && enabled, "regain after background loss");
    Focus(manager + 4);
#ifndef IME_BASELINE
    Focus(custom + 4); EnableImeForControl(custom + 4); composing = true;
    Focus(manager + 4);
    Need(!associated && !enabled && !composing, "custom input releases IME after manager already changed focus");
    Focus(custom + 4); EnableImeForControl(custom + 4); Focus(single + 4);
    Need(associated == saved && enabled, "custom blur preserves newly focused native text input");
    Focus(custom + 4); EnableImeForControl(custom + 4);
    DisableImeForControl(single + 4);
    Need(associated == saved && enabled, "old native control cannot disable custom input owner");
    composing = true; DisableImeForControl(custom + 4);
    Need(!associated && !enabled && !composing, "closing custom edit cancels composition");
    EnableImeForControl(custom + 4); focusOnCancel = single + 4;
    DisableImeForControl(custom + 4);
    Need(associated == saved && enabled, "synchronous cancel notification preserves transferred owner");
    Focus(manager + 4);
#endif
    At<void*>(0x00BEC20C) = nullptr; DisableIme();
    Need(clears >= 400 && releases > 400, "native blur callback and context cleanup exercised");
    puts("PASS: native focus/IME/RemoveWnd functions, both loss gates, gain-before-loss, password, context restore, destroy ownership and composition, custom input handoff and reentrancy, background HWND, repeated switches");
}
