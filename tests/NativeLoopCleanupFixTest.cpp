#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/NativeLoopCleanupFix.h"
#include "../ezorsia/NativeExitDiagnostics.cpp"

// Execute native loop setup/cleanup in an isolated image, not the game entry.
// Substitute only the game-loop body and the free observer. The latter calls
// the real CRT free once the production patch guarantees a null argument.
static DWORD observed, calls, target, poison, lastMessage, cleanupTarget;
static DWORD savedEbx, savedEsi, savedEdi;
static int stop = 1, depth = 0, nesting = 1, maximumDepth = 0;
static bool requireNull = false;
static BYTE application[256]{};
static DWORD N(DWORD address) { return address + 0x30000000; }

static void Require(bool ok, const char* message) {
    if (!ok) { printf("FAIL %s error=%lu\n", message, GetLastError()); exit(1); }
}
static void __cdecl CaptureFree(DWORD value) {
    observed = value;
    ++calls;
    if (requireNull) {
        Require(value == 0, "native cleanup never frees stale stack data");
        std::free(reinterpret_cast<void*>(value));
    }
}
static void Capture(const TargetedCrashSnapshot::Snapshot&, unsigned) {
    Require(false, "normal modal cleanup produces no exception snapshot");
}
static void Write(DWORD address, const void* bytes, SIZE_T size) {
    DWORD old = 0, ignored = 0;
    auto* destination = reinterpret_cast<void*>(address);
    Require(VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old) != FALSE, "test write permission");
    memcpy(destination, bytes, size);
    Require(FlushInstructionCache(GetCurrentProcess(), destination, size) != FALSE, "test instruction cache");
    Require(VirtualProtect(destination, size, old, &ignored) != FALSE, "test restore permission");
}
static void Redirect(DWORD from, DWORD to, BYTE opcode = 0xe9) {
    BYTE bytes[] = {opcode,0,0,0,0};
    const DWORD relative = to - from - 5;
    memcpy(bytes + 1, &relative, sizeof(relative));
    Write(from, bytes, sizeof(bytes));
}
static BYTE* Map(const char* path) {
    FILE* file = nullptr;
    Require(fopen_s(&file, path, "rb") == 0, "open client");
    fseek(file, 0, SEEK_END);
    std::vector<BYTE> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read client");
    fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    auto* image = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(N(0x400000)),
        nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(image == reinterpret_cast<void*>(N(0x400000)), "map without launching client");
    memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(image + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    return image;
}
__declspec(naked) static void Invoke() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        sub esp, 2000h
        mov edi, esp
        mov eax, poison
        mov ecx, 800h
        rep stosd
        add esp, 2000h
        mov ebx, 12345678h
        mov esi, 23456789h
        mov edi, 3456789ah
        push offset stop
        mov ecx, offset application
        call dword ptr [target]
        mov savedEbx, ebx
        mov savedEsi, esi
        mov savedEdi, edi
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static void __cdecl Body() {
    ++depth;
    if (depth > maximumDepth) maximumDepth = depth;
    if (depth < nesting) Invoke();
    --depth;
}
__declspec(naked) static void NativeBody() {
    __asm {
        pushfd
        pushad
        mov eax, lastMessage
        mov [ebp-98h], eax
        call Body
        popad
        popfd
        jmp dword ptr [cleanupTarget]
    }
}
static void Exercise(const char* label) {
    for (DWORD value : {0UL, 0xFFFFFF00UL, 0x12345678UL}) {
        poison = value;
        calls = 0;
        maximumDepth = 0;
        Invoke();
        Require(calls == static_cast<DWORD>(nesting) && maximumDepth == nesting && depth == 0,
            "every nested native loop completes exactly one cleanup");
        Require(savedEbx == 0x12345678 && savedEsi == 0x23456789 && savedEdi == 0x3456789a,
            "callee-saved registers preserved");
        Require(stop == 1, "native stop flag preserved");
        Require(observed == (requireNull ? 0 : poison), "free receives expected native local");
        printf("%s depth=%d stack=%08lX freeArgument=%08lX\n", label, nesting, poison, observed);
    }
}
static void RejectedImages(BYTE* image) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    Require(!NativeLoopCleanupFix::Install(nullptr), "reject null image");
    Require(!NativeLoopCleanupFix::Install(reinterpret_cast<HMODULE>(1)), "reject unreadable image");
    const DWORD offsets[] = {static_cast<DWORD>(dos->e_lfanew) + 8, 0x5f5c50,
        0x5f5ca3, 0x5f5ca8, 0x5f6965, 0x5f6969};
    for (DWORD offset : offsets) {
        const BYTE saved = image[offset];
        image[offset] ^= 1;
        BYTE before[12];
        memcpy(before, image + 0x5f5ca3, sizeof(before));
        Require(!NativeLoopCleanupFix::Install(reinterpret_cast<HMODULE>(image)), "reject changed image/code");
        Require(memcmp(before, image + 0x5f5ca3, sizeof(before)) == 0, "rejection leaves patch site intact");
        image[offset] = saved;
    }
}
int main(int argc, char** argv) {
    Require(argc == 2, "EXE path");
    auto* image = Map(argv[1]);
    ClientLog::Initialize();
    RejectedImages(image);

    // Relocate only the two singleton operands actually executed by setup.
    *reinterpret_cast<DWORD*>(N(0xbe7914)) = 0;
    const DWORD socket = N(0xbe7914);
    Write(N(0x9f5c8c), &socket, sizeof(socket));
    Write(N(0x9f5c9a), &socket, sizeof(socket));
    cleanupTarget = N(0x9f6965);
    Redirect(N(0x9f5fdb), reinterpret_cast<DWORD>(&NativeBody));
    BYTE nativeFree[5];
    memcpy(nativeFree, reinterpret_cast<void*>(N(0x9f6968)), sizeof(nativeFree));
    Redirect(N(0x9f6968), reinterpret_cast<DWORD>(&CaptureFree), 0xe8);
    target = N(0x9f5c50);
    Exercise("baseline");

    // Restore the authentic cleanup for the production installer's signature.
    Write(N(0x9f6968), nativeFree, sizeof(nativeFree));
    DWORD old = 0;
    Require(VirtualProtect(image + 0x5f5c00, 0x100, PAGE_EXECUTE_READ, &old) != FALSE, "read-only code page");
    Require(NativeLoopCleanupFix::Install(reinterpret_cast<HMODULE>(image)), "install production fix");
    Require(NativeLoopCleanupFix::Install(reinterpret_cast<HMODULE>(image)), "idempotent install");
    Require(!NativeLoopCleanupFix::Install(nullptr), "idempotence does not accept another image");
    MEMORY_BASIC_INFORMATION memory{};
    Require(VirtualQuery(image + 0x5f5ca3, &memory, sizeof(memory)) == sizeof(memory)
        && memory.Protect == PAGE_EXECUTE_READ, "restore original page protection");
    Redirect(N(0x9f6968), reinterpret_cast<DWORD>(&CaptureFree), 0xe8);
    requireNull = true;
    Exercise("fixed diagnostics-disabled");
    nesting = 3;
    Exercise("fixed nested dialogs");

    Require(LoadLibraryW(L"user32.dll") != nullptr, "load diagnostic dependency");
    Require(NativeExitDiagnostics::Install(reinterpret_cast<HMODULE>(image), Capture), "real diagnostic hook coexists");
    Exercise("fixed diagnostics-enabled nested dialogs");
    nesting = 1;
    Exercise("fixed diagnostics-enabled");

    // Preserve the actual native WM_QUIT branch and PostQuitMessage behavior.
    const DWORD postQuit = reinterpret_cast<DWORD>(&PostQuitMessage);
    Write(N(0xbf041c), &postQuit, sizeof(postQuit));
    const DWORD postQuitSlot = N(0xbf041c);
    Write(N(0x9f697b), &postQuitSlot, sizeof(postQuitSlot));
    lastMessage = WM_QUIT;
    calls = 0;
    Invoke();
    MSG message{};
    Require(calls == 1 && PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)
        && message.wParam == 0, "native WM_QUIT return still posts original exit code");
    Require(NativeExitDiagnostics::exceptionReports == 0 && !NativeExitDiagnostics::activeRun,
        "no false exception reports and nested diagnostic state restored");
    puts("PASS native loop cleanup: stale pointer reproduced, production fix, nested modal loops, diagnostics on/off, WM_QUIT, version guards, page protection");
}
