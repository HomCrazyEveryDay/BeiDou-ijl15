// Exercise the shipped SOUND_DX8 CRT finalizer and native stop loop without
// starting the game, opening an audio device or playing any sound.
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "../ezorsia/SoundExitFix.h"
#include "../ezorsia/SoundAllocationFallback.h"

static void Require(bool condition, const char* message) {
    if (condition) return;
    std::printf("FAIL %s error=%lu\n", message, GetLastError());
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 2);
}
namespace ClientLog {
void Emergency(const char* format, ...) {
    char line[512]; va_list args; va_start(args, format);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args); va_end(args);
    // Also safe during DLL teardown, after the EXE CRT has been cleaned up.
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\n", 1, &written, nullptr);
}
}
struct Worker {
    DWORD vtable = 0, threadId = 0;
    HANDLE thread = nullptr;
    DWORD owner = 0, recursion = 0;
    volatile LONG state = 0;
    HANDLE event = nullptr;
    DWORD padding[24]{};
} worker;
static_assert(offsetof(Worker, state) == 0x14 && offsetof(Worker, event) == 0x18, "native worker ABI");
using Stop = void(__thiscall*)(void*);
Stop nativeStop;
volatile LONG destroyed = 0;
HANDLE workerReady = nullptr;
DWORD WINAPI Respond(void*) {
    SetEvent(workerReady);
    if (WaitForSingleObject(worker.event, 5000) != WAIT_OBJECT_0) return 3;
    Sleep(100); // The original Stop must really wait for this acknowledgement.
    InterlockedExchange(&worker.state, 0);
    return 0;
}
void* __fastcall DestroyEngine(void* object, void*, unsigned flags) {
    Require(flags == 1, "native finalizer invokes scalar destructor with delete flag");
    InterlockedIncrement(&destroyed);
    nativeStop(&worker); // Real shipped +115B, including SetEvent/Sleep(1) loop.
    return object;
}

int main(int argc, char** argv) {
    Require(argc == 3, "arguments: sound-dll case");
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const bool baseline = !strcmp(argv[2], "baseline");
    const bool runtime = !strcmp(argv[2], "runtime");
    const bool unload = !strcmp(argv[2], "unload");
    if (!baseline) Require(SoundAllocationFallback::Install(), "install existing loader hook");
    auto module = LoadLibraryA(argv[1]);
    Require(module != nullptr, "load shipped SOUND_DX8");
    auto* base = reinterpret_cast<BYTE*>(module);
    if (!baseline) {
        Require(base[0x2d7a] == 0xe9, "exit fix automatically installed through production loader path");
        Require(SoundExitFix::Install(module), "repeated install");
        Require(!SoundExitFix::Install(nullptr), "reject missing module");
        Require(!SoundExitFix::Install(GetModuleHandleW(nullptr)), "reject other PE image");
    } else Require(base[0x2d7a] == 0xb9, "baseline retains original callback");
    nativeStop = reinterpret_cast<Stop>(base + 0x115b);
    auto finalizer = reinterpret_cast<void(__cdecl*)()>(base + 0x2d7a);
    auto singletonEngine = reinterpret_cast<void**>(base + 0x1f8e0);
    Require(*singletonEngine == nullptr, "sound singleton initially has no engine");
    void* table[] = {reinterpret_cast<void*>(&DestroyEngine)};
    void** engine = table;
    *singletonEngine = &engine;
    worker.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    workerReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(worker.event != nullptr, "create native-compatible stop event");
    worker.thread = CreateThread(nullptr, 0, Respond, nullptr, 0, &worker.threadId);
    Require(worker.thread != nullptr, "create real decoder stand-in thread");
    Require(WaitForSingleObject(workerReady, 2000) == WAIT_OBJECT_0, "worker started before loader-lock cleanup");
    CloseHandle(workerReady);
    if (runtime || unload) {
        const auto start = GetTickCount64();
        if (unload) Require(FreeLibrary(module) != FALSE, "ordinary FreeLibrary");
        else { finalizer(); *singletonEngine = nullptr; }
        Require(destroyed == 1 && worker.state == 0, "native destructor and acknowledgement preserved");
        Require(GetTickCount64() - start >= 80, "native stop actually waits for a live worker");
        Require(WaitForSingleObject(worker.thread, 2000) == WAIT_OBJECT_0, "live worker exits normally");
        CloseHandle(worker.thread); CloseHandle(worker.event);
        if (unload) {
            // A DLL can unload and reload at the same base. Reinstall the hook
            // instead of treating the old base address as proof it is patched.
            module = LoadLibraryA(argv[1]);
            Require(module != nullptr && reinterpret_cast<BYTE*>(module)[0x2d7a] == 0xe9,
                "reloaded module receives exit fix");
            Require(FreeLibrary(module) != FALSE, "unload empty reloaded module");
        }
        std::printf("PASS %s nativeCleanup=1\n", argv[2]);
        return 0;
    }
    std::printf("READY %s nativeFinalizer=1 nativeStopLoop=1\n", argv[2]);
    // ExitProcess kills Respond before SOUND_DX8's registered CRT callback.
    // Baseline hangs in its real native loop; fixed must exit with this code.
    ExitProcess(0);
}
