// Address-space exhaustion evidence and SOUND_DX8 predecode fallback.
// Modes run in separate processes so each starts with a clean address space:
//   --sound <Sound_DX8.dll>   patch sites, injected allocation failure, native adoption
//   --dump                    dump reserve, memoryAtFault report and telemetry log
#include <windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "../ezorsia/ClientLog.h"
#include "../ezorsia/CrashReporter.h"
#include "../ezorsia/MemoryStatus.h"
#include "../ezorsia/MemoryTelemetry.h"
#include "../ezorsia/SoundAllocationFallback.h"

namespace {
int g_failures = 0;
void Require(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++g_failures;
}

void* g_held[80000];
size_t g_heldCount = 0;
void* g_heapHeld[80000];
size_t g_heapHeldCount = 0;

// Reserve every free range, then fill the remaining heap segments: the state
// the client reaches when HeapAlloc returns NULL and DbgHelp cannot allocate.
void Exhaust()
{
    for (SIZE_T size = 256u * 1024 * 1024; size >= 64 * 1024; size /= 2) {
        while (g_heldCount < ARRAYSIZE(g_held)) {
            void* block = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
            if (!block) break;
            g_held[g_heldCount++] = block;
        }
    }
    HANDLE heap = GetProcessHeap();
    for (SIZE_T size = 64 * 1024; size >= 64; size /= 2) {
        while (g_heapHeldCount < ARRAYSIZE(g_heapHeld)) {
            void* block = HeapAlloc(heap, 0, size);
            if (!block) break;
            g_heapHeld[g_heapHeldCount++] = block;
        }
    }
}

void Restore()
{
    HANDLE heap = GetProcessHeap();
    while (g_heapHeldCount) HeapFree(heap, 0, g_heapHeld[--g_heapHeldCount]);
    while (g_heldCount) VirtualFree(g_held[--g_heldCount], 0, MEM_RELEASE);
}

// ---- stub harness: enter the patched code with a fake native frame ----
DWORD g_stub = 0;
DWORD g_seenEsi = 0, g_seenEdi = 0, g_seenEbx = 0, g_seenEbp = 0;

__declspec(naked) void ResumeTarget()
{
    __asm {
        mov g_seenEsi, esi
        mov g_seenEdi, edi
        mov g_seenEbx, ebx
        mov g_seenEbp, ebp
        pop edi
        pop esi
        pop ebx
        pop ebp
        mov eax, 1
        ret 4
    }
}

__declspec(naked) void SkipTarget()
{
    __asm {
        mov g_seenEsi, esi
        mov g_seenEdi, edi
        mov g_seenEbx, ebx
        mov g_seenEbp, ebp
        pop edi
        pop esi
        pop ebx
        pop ebp
        mov eax, 2
        ret 4
    }
}

// Returns 1 when the stub resumes predecode (+AC25), 2 when it skips (+ACA1).
__declspec(naked) DWORD __stdcall RunStub(DWORD frame)
{
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, dword ptr [esp + 20]
        mov esi, 11111111h
        mov edi, 22222222h
        mov ebx, 33333333h
        jmp dword ptr [g_stub]
    }
}

DWORD g_frame[64];
DWORD Decide(DWORD decodedBytes)
{
    DWORD* ebp = g_frame + 32;
    ebp[-4] = decodedBytes; // [ebp-10h]
    const DWORD result = RunStub(reinterpret_cast<DWORD>(ebp));
    Require(g_seenEsi == 0x11111111 && g_seenEdi == 0x22222222 && g_seenEbx == 0x33333333
        && g_seenEbp == reinterpret_cast<DWORD>(ebp), "stub preserves esi/edi/ebx/ebp");
    return result;
}

using HeapAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, SIZE_T);
HeapAllocFn g_realHeapAlloc = nullptr;
volatile LONG g_slotCalls = 0;
LONG g_slotSucceedCalls = 0; // calls allowed to succeed; later calls return NULL

// Fault injector for the module's HeapAlloc slot: the first N calls succeed.
LPVOID WINAPI InjectedHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    const LONG call = InterlockedIncrement(&g_slotCalls);
    return call <= g_slotSucceedCalls ? g_realHeapAlloc(heap, flags, bytes) : nullptr;
}

using Rebuild = DWORD(__thiscall*)(void* holder, DWORD count, void* init);
using ReleaseArray = void(__thiscall*)(void* holder);

int RunSound(const wchar_t* path)
{
    Require(SoundAllocationFallback::Install(), "install hooks LoadLibraryA");
    char narrow[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    HMODULE module = LoadLibraryA(narrow);
    Require(module != nullptr, "Sound_DX8.dll loads through LoadLibraryA");
    if (!module) return 1;
    const DWORD base = reinterpret_cast<DWORD>(module);
    const auto* code = reinterpret_cast<const BYTE*>(base + 0xAC18);
    Require(code[0] == 0xE9, "patched after LoadLibraryA returned: jmp at +AC18");
    bool nops = true;
    for (int i = 5; i < 13; ++i) nops = nops && code[i] == 0x90;
    Require(nops, "remaining 8 bytes are NOP");
    const auto* after = reinterpret_cast<const BYTE*>(base + 0xAC25);
    Require(after[0] == 0x8D && after[1] == 0x8D && after[6] == 0xE8, "+AC25 decoder construction untouched");
    const auto* allocator = reinterpret_cast<const BYTE*>(base + 0x1A2D);
    Require(allocator[0] == 0xE8 && allocator[5] == 0x90 && allocator[6] == 0x89 && allocator[7] == 0x18,
        "+1A2D HeapAlloc slot call redirected, +1A33 untouched");
    DWORD relative = 0;
    std::memcpy(&relative, code + 1, sizeof(relative));
    g_stub = reinterpret_cast<DWORD>(code) + 5 + relative;

    using GetConfig = DWORD(__cdecl*)();
    const DWORD config = reinterpret_cast<GetConfig>(base + 0x4AEE)();
    Require(config != 0, "native configuration singleton available");
    if (!config) return 1;
    DWORD& limit = *reinterpret_cast<DWORD*>(config + 0x38);
    const DWORD originalLimit = limit;
    SoundAllocationFallback::RedirectForTest(reinterpret_cast<DWORD>(&ResumeTarget), reinterpret_cast<DWORD>(&SkipTarget));
    auto& slot = *reinterpret_cast<HeapAllocFn*>(base + 0x1F98C);
    g_realHeapAlloc = slot;
    Require(g_realHeapAlloc != nullptr, "module HeapAlloc slot initialized");
    const auto rebuild = reinterpret_cast<Rebuild>(base + 0x5B45);
    const auto releaseArray = reinterpret_cast<ReleaseArray>(base + 0x228F);
    constexpr DWORD kClip = 228096; // player's clip, HeapAlloc request 228104
    BYTE init = 0;

    limit = 0x100000;
    Require(Decide(0x100001) == 2 && !SoundAllocationFallback::PendingForTest(), "native limit: oversize clip keeps native skip");

    // Reviewer's injection: the first (stub) allocation succeeds, every later
    // HeapAlloc through the module slot fails.
    g_slotCalls = 0;
    g_slotSucceedCalls = 1;
    slot = InjectedHeapAlloc;
    Require(Decide(kClip) == 1, "injected: real allocation done before decoder, predecode resumes");
    auto* pending = static_cast<BYTE*>(SoundAllocationFallback::PendingForTest());
    Require(pending != nullptr && g_slotCalls == 1, "injected: one slot allocation, buffer pending");
    DWORD holder[1]{};
    const DWORD data = rebuild(holder, kClip, &init);
    Require(data != 0 && data == reinterpret_cast<DWORD>(pending) + 8 && holder[0] == data,
        "injected: native +5B45 adopts the buffer instead of a failing second HeapAlloc");
    Require(g_slotCalls == 1, "injected: native allocator made no further slot call");
    DWORD sizeHeader = 0, countHeader = 0;
    std::memcpy(&sizeHeader, pending, 4);
    std::memcpy(&countHeader, pending + 4, 4);
    Require(sizeHeader == kClip + 4 && countHeader == kClip, "headers match native layout (+1A33 size, +5B6F count)");
    Require(!SoundAllocationFallback::PendingForTest(), "buffer consumed exactly once");
    releaseArray(holder);
    Require(holder[0] == 0 && HeapValidate(GetProcessHeap(), 0, nullptr), "native +228F frees adopted buffer, heap valid");

    // The real allocation itself fails: skip before any decoder exists.
    g_slotCalls = 0;
    g_slotSucceedCalls = 0;
    Require(Decide(kClip) == 2 && !SoundAllocationFallback::PendingForTest(), "allocation failure takes +ACA1 with nothing pending");
    slot = g_realHeapAlloc;

    // Unrelated sizes pass straight through to the module slot.
    const DWORD other = rebuild(holder, 300000, &init);
    Require(other != 0 && holder[0] == other, "pass-through allocation for other requests");
    releaseArray(holder);

    // Exhausted address space and heap.
    Exhaust();
    Require(Decide(kClip) == 2 && !SoundAllocationFallback::PendingForTest(), "exhausted: clip falls back instead of writing NULL");
    Restore();
    Require(Decide(kClip) == 1 && SoundAllocationFallback::PendingForTest(), "after release: predecode resumes");
    // An unconsumed buffer is freed on the next decision, never leaked or reused stale.
    Require(Decide(kClip) == 1 && HeapValidate(GetProcessHeap(), 0, nullptr), "stale pending buffer replaced, heap valid");
    const DWORD again = rebuild(holder, kClip, &init);
    Require(again != 0 && !SoundAllocationFallback::PendingForTest(), "replacement buffer adopted");
    releaseArray(holder);
    Require(Decide(0) == 1 && !SoundAllocationFallback::PendingForTest(), "empty clip allocates nothing");
    limit = originalLimit;
    return g_failures ? 1 : 0;
}

std::string ReadFileText(const std::wstring& path)
{
    std::string text;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read) text.append(buffer, read);
    CloseHandle(file);
    return text;
}

std::wstring FindLog(const wchar_t* pattern)
{
    WIN32_FIND_DATAW data{};
    const std::wstring directory = ClientLog::Directory();
    HANDLE find = FindFirstFileW((directory + L"\\" + pattern).c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return L"";
    std::wstring best;
    ULONGLONG bestSize = 0;
    do {
        const ULONGLONG size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        if (best.empty() || size > bestSize) { best = directory + L"\\" + data.cFileName; bestSize = size; }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return best;
}

ULONGLONG FileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

TargetedCrashSnapshot::Snapshot g_snapshot;

int RunDump()
{
    ClientLog::Initialize();
    SetEnvironmentVariableW(L"BEIDOU_MEMORY_TELEMETRY_SECONDS", L"10");
    MemoryTelemetry::Start();
    Sleep(500);
    const std::string telemetry = ReadFileText(FindLog(L"ijl15-memory-*.log"));
    Require(telemetry.find("memory_sample sinceStartSec=") != std::string::npos
        && telemetry.find("largestFreeMiB=") != std::string::npos, "telemetry writes memory_sample");
    Require(telemetry.find("memory_top index=0 ") != std::string::npos, "telemetry writes baseline top allocations");

    CrashReporter::Install(true, "triage", false);

    // Control: without releasing anything, DbgHelp under the same exhaustion.
    // Build the path first: std::wstring itself throws bad_alloc once exhausted.
    const std::wstring control = std::wstring(ClientLog::Directory()) + L"\\control.dmp";
    Exhaust();
    HANDLE controlFile = CreateFileW(control.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    BOOL controlWritten = FALSE;
    DWORD controlError = 0;
    if (controlFile != INVALID_HANDLE_VALUE) {
        controlWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), controlFile,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo),
            nullptr, nullptr, nullptr);
        controlError = GetLastError();
        CloseHandle(controlFile);
    } else controlError = GetLastError();
    std::printf("INFO control dump without reserve: written=%d error=0x%08lX createFile=%d\n",
        controlWritten, controlError, controlFile != INVALID_HANDLE_VALUE);

    MemoryStatus::Snapshot exhausted;
    MemoryStatus::Capture(exhausted);
    Require(exhausted.valid && exhausted.largestFree < 64 * 1024 * 2, "capture works while exhausted");

    g_snapshot.record.ExceptionCode = 0xC0000005;
    g_snapshot.record.ExceptionAddress = reinterpret_cast<void*>(0x51801A33);
    g_snapshot.context.ContextFlags = CONTEXT_FULL;
    g_snapshot.context.Eip = 0x51801A33;
    g_snapshot.threadId = GetCurrentThreadId();
    // Same order as a fatal client exit: main-loop report, then the unhandled
    // filter's full triage dump, both while the space is still exhausted.
    CrashReporter::CaptureMainLoopException(g_snapshot, 1);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = 0xC0000005;
    record.ExceptionAddress = reinterpret_cast<void*>(0x51801A33);
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_POINTERS pointers{&record, &context};
    CrashReporter::CaptureHandledException("test", "exhausted", &pointers);
    Restore();

    const std::wstring dump = FindLog(L"ijl15-crash-*_main-loop-1-*.dmp");
    const std::wstring report = FindLog(L"ijl15-crash-*_main-loop-1-*.txt");
    const std::wstring handledDump = FindLog(L"ijl15-crash-*_handled-*.dmp");
    std::printf("INFO dump bytes with reserve: main-loop=%llu handled(triage)=%llu\n", FileSize(dump), FileSize(handledDump));
    Require(FileSize(dump) > 0, "first dump is written under exhaustion");
    Require(FileSize(handledDump) > 0, "second (triage) dump is also written under exhaustion");
    const std::string text = ReadFileText(report);
    Require(text.find("dumpWritten=true") != std::string::npos, "report records dumpWritten=true");
    Require(text.find("memoryAtFault=vaUsedMiB=") != std::string::npos, "report has memoryAtFault");
    Require(text.find("memoryTop00=base=") != std::string::npos, "report has top allocations");
    const std::string emergency = ReadFileText(FindLog(L"ijl15-emergency-*.log"));
    Require(emergency.find("dump_memory_reserve sliceMiB=8 slices=4 requested=4") != std::string::npos, "reserve made at install");
    Require(emergency.find("memory_at_fault type=native_main_loop_escape") != std::string::npos, "emergency log has memory_at_fault");
    Require(emergency.find("memory_at_fault type=handled") != std::string::npos, "second report also logs memory_at_fault");
    size_t released = 0;
    for (size_t at = emergency.find("reserveReleased=1"); at != std::string::npos; at = emergency.find("reserveReleased=1", at + 1)) ++released;
    std::printf("INFO dumps that released reserve: %zu\n", released);
    Require(emergency.find("written=0") == std::string::npos, "no dump write failed");
    Require(emergency.find("memory_telemetry_config enabled=1 intervalSec=10") != std::string::npos, "telemetry config logged");
    return g_failures ? 1 : 0;
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc >= 3 && !wcscmp(argv[1], L"--sound")) return RunSound(argv[2]);
    if (argc >= 2 && !wcscmp(argv[1], L"--dump")) return RunDump();
    std::printf("usage: --sound <Sound_DX8.dll> | --dump\n");
    return 2;
}
