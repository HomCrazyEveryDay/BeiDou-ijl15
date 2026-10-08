// Run the production fix against the shipped PCOM/List and native allocator,
// in an isolated process. Never execute the game entry point or load ijl15.
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include "../ezorsia/PcomExitFix.h"

static void Require(bool ok, const char* why)
{
    if (!ok) {
        printf("FAIL %s win32=%lu\n", why, GetLastError());
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 2);
    }
}

static unsigned cleanupBegins, cleanupEnds, uninitializedSkips;
namespace ClientLog {
void Emergency(const char* format, ...)
{
    if (strstr(format, "cleanup.begin")) ++cleanupBegins;
    if (strstr(format, "cleanup.end")) ++cleanupEnds;
    if (strstr(format, "list_uninitialized")) ++uninitializedSkips;
    va_list args; va_start(args, format); vprintf(format, args); va_end(args);
    puts("");
}
}
namespace CrashReporter {
void RecordEvent(const char*, const char* format, ...)
{
    va_list args; va_start(args, format); vprintf(format, args); va_end(args);
    puts("");
}
}

constexpr DWORD kDelta = 0x30000000;
static DWORD Native(DWORD original) { return original + kDelta; }
struct Block { BYTE* base; SIZE_T size; bool freed; };
static Block blocks[4096];
static unsigned allocated, freed;
static bool guarded, fixedOrder;
static BYTE* pcom;
static DWORD faultCode, faultAddress;
static BYTE& PoolFlags() { return *reinterpret_cast<BYTE*>(Native(0x00BF00F0)); }
static DWORD Entries() { return *reinterpret_cast<DWORD*>(pcom + 0x2148C); }
static bool ListReady() { return (pcom[0x21460] & 1) != 0; }

static LPVOID WINAPI AllocateBlock(HANDLE heap, DWORD flags, SIZE_T size)
{
    auto* result = static_cast<BYTE*>(guarded
        ? VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
        : HeapAlloc(heap, flags, size));
    Require(result && allocated < ARRAYSIZE(blocks), "allocate backing block");
    blocks[allocated++] = {result, size, false};
    return result;
}

static BOOL WINAPI FreeBlock(HANDLE heap, DWORD flags, LPVOID value)
{
    bool found = false;
    for (unsigned i = allocated; i > 0; --i) {
        auto& block = blocks[i - 1];
        if (block.base != value || block.freed) continue;
        block.freed = true;
        found = true;
        ++freed;
        break;
    }
    Require(found, "no unknown/double backing free");
    if (fixedOrder && (PoolFlags() & 1))
        Require(!ListReady() || Entries() == 0, "List emptied BEFORE backing block destruction");
    return guarded ? VirtualFree(value, 0, MEM_DECOMMIT) : HeapFree(heap, flags, value);
}

static BYTE* MapAllocator(const char* directory)
{
    char path[MAX_PATH]; sprintf_s(path, "%s\\BeiDou.exe", directory);
    FILE* file = nullptr;
    Require(fopen_s(&file, path, "rb") == 0, "open native image");
    fseek(file, 0, SEEK_END); std::vector<BYTE> bytes(ftell(file)); rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read native image");
    fclose(file);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp == 0x4B7C15C9
        && nt->OptionalHeader.ImageBase == 0x400000
        && nt->OptionalHeader.SizeOfImage == 0xA94000, "known allocator image");
    auto* mapped = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x400000)),
        nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(mapped == reinterpret_cast<void*>(Native(0x400000)), "offline mapping");
    memcpy(mapped, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(mapped + sections[i].VirtualAddress, bytes.data() + sections[i].PointerToRawData,
            sections[i].SizeOfRawData);
    // Only the allocator/constructor/destructor subset executes. This EXE has
    // no relocation table; these are its decoded absolute operands, including
    // the native destructor's SEH descriptor. No game initialization executes.
    const DWORD operands[] = {0x403066,0x4030CD,0x4030D4,0x40316B,0x403177,
        0x403183,0x4031B2,0x4031B9,0x403262,0x403269,0x796D53,0x796D5C,
        0x796D62,0x797B65,0x797BDE,0x797BE5,0x9F2508,0x9F2519};
    for (DWORD operand : operands)
        *reinterpret_cast<DWORD*>(Native(operand)) += kDelta;
    mapped[0x661040] = 0xE9;
    *reinterpret_cast<DWORD*>(mapped + 0x661041) = reinterpret_cast<DWORD>(&memset) - Native(0xA61040) - 5;
    *reinterpret_cast<void**>(Native(0xBF032C)) = reinterpret_cast<void*>(&GetProcessHeap);
    *reinterpret_cast<void**>(Native(0xBF0324)) = reinterpret_cast<void*>(&AllocateBlock);
    *reinterpret_cast<void**>(Native(0xBF0328)) = reinterpret_cast<void*>(&FreeBlock);
    *reinterpret_cast<void**>(Native(0xBDC9D0)) = reinterpret_cast<void*>(Native(0x796F31));
    *reinterpret_cast<void**>(Native(0xBF02F4)) = reinterpret_cast<void*>(&Sleep);
    PoolFlags() = 0;
    FlushInstructionCache(GetCurrentProcess(), mapped, nt->OptionalHeader.SizeOfImage);
    reinterpret_cast<void(__thiscall*)(void*)>(Native(0x796EB7))(reinterpret_cast<void*>(Native(0xBF0B00)));
    return mapped;
}

static LONG Capture(EXCEPTION_POINTERS* exception)
{
    faultCode = exception->ExceptionRecord->ExceptionCode;
    faultAddress = reinterpret_cast<DWORD>(exception->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void DestroyPool()
{
    __try { reinterpret_cast<void(__cdecl*)()>(Native(0x796D51))(); }
    __except(Capture(GetExceptionInformation())) {}
}

static void DestroyList()
{
    __try { reinterpret_cast<void(__cdecl*)()>(pcom + 0x6C23)(); }
    __except(Capture(GetExceptionInformation())) {}
}

static void CheckRejectedInstallations(BYTE* client)
{
    auto exe = reinterpret_cast<HMODULE>(client);
    auto dll = reinterpret_cast<HMODULE>(pcom);
    Require(!PcomExitFix::Install(nullptr, dll), "missing client rejected");
    Require(!PcomExitFix::Install(exe, nullptr), "missing PCOM rejected");
    Require(!PcomExitFix::Install(exe, GetModuleHandleW(nullptr)), "other module rejected");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(client);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(client + dos->e_lfanew);
    nt->FileHeader.TimeDateStamp ^= 1;
    Require(!PcomExitFix::Install(exe, dll), "other EXE version rejected");
    nt->FileHeader.TimeDateStamp ^= 1;
    client[0x396D51] ^= 1;
    Require(!PcomExitFix::Install(exe, dll), "changed destructor rejected");
    client[0x396D51] ^= 1;
    auto* freeCallback = reinterpret_cast<DWORD*>(pcom + 0x212B0);
    const DWORD original = *freeCallback;
    *freeCallback = 0;
    Require(!PcomExitFix::Install(exe, dll), "different allocator rejected");
    *freeCallback = original;
    PoolFlags() = 1;
    Require(!PcomExitFix::Install(exe, dll), "late installation rejected");
    PoolFlags() = 0;
    Require(client[0x396D51] == 0xF6, "rejections left entry unchanged");
}

int main(int argc, char** argv)
{
    Require(argc == 3, "arguments: client-directory case");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* mode = argv[2];
    const bool baseline = strcmp(mode, "baseline-guard") == 0;
    const bool uninitialized = strcmp(mode, "uninitialized") == 0;
    const bool propagation = strcmp(mode, "propagate") == 0;
    const bool initialized = strncmp(mode, "initialized-", 12) == 0;
    guarded = strstr(mode, "heap") == nullptr;
    fixedOrder = !baseline;
    BYTE* client = MapAllocator(argv[1]);
    char path[MAX_PATH]; sprintf_s(path, "%s\\PCOM.dll", argv[1]);
    pcom = reinterpret_cast<BYTE*>(LoadLibraryA(path));
    Require(pcom != nullptr, "load real PCOM");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(pcom);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(pcom + dos->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp == 0x4B7C140C
        && nt->OptionalHeader.SizeOfImage == 0x25000, "known PCOM");
    *reinterpret_cast<DWORD*>(pcom + 0x212A8) = Native(0x9F2503);
    *reinterpret_cast<DWORD*>(pcom + 0x212AC) = Native(0x9F2525);
    *reinterpret_cast<DWORD*>(pcom + 0x212B0) = Native(0x9F2514);

    Require(!ListReady(), "List initially uninitialized");
    if (!baseline) {
        CheckRejectedInstallations(client);
        Require(PcomExitFix::Install(reinterpret_cast<HMODULE>(client), reinterpret_cast<HMODULE>(pcom)), "install production fix");
        Require(PcomExitFix::Install(reinterpret_cast<HMODULE>(client), reinterpret_cast<HMODULE>(pcom)), "repeated install harmless");
        Require(!PcomExitFix::Install(nullptr, reinterpret_cast<HMODULE>(pcom)), "different instance after install rejected");
        Require(!ListReady() && !allocated, "install does not initialize List or allocate native memory");
    }

    unsigned entries = 0;
    if (!uninitialized) {
        if (initialized) {
            auto init = reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(reinterpret_cast<HMODULE>(pcom), "PcInitModule"));
            Require(init && SUCCEEDED(init()), "normal PCOM init");
        }
        reinterpret_cast<void*(__cdecl*)()>(pcom + 0x6BF8)();
        entries = Entries();
        Require(ListReady() && entries > 0, "real List populated after install");
        // Normal runtime allocation/free must remain intact and must not clear
        // the table. No instrumented replacement of PCOM's callbacks is used.
        auto alloc = reinterpret_cast<void*(__stdcall*)(unsigned)>(Native(0x9F2503));
        auto free = reinterpret_cast<void(__stdcall*)(void*)>(Native(0x9F2514));
        auto* value = static_cast<BYTE*>(alloc(41));
        Require(value != nullptr, "runtime allocation"); memset(value, 0x5A, 41); free(value);
        Require(Entries() == entries && PoolFlags() == 0 && cleanupBegins == 0, "runtime free unchanged");
        if (initialized) {
            auto term = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(reinterpret_cast<HMODULE>(pcom), "PcTermModule"));
            Require(term != nullptr, "normal PCOM term export"); term();
            Require(Entries() == entries, "PcTermModule alone leaves List populated");
        }
    }

    if (propagation) {
        // Raise a real instruction exception inside the offline native Free.
        // The production wrapper must let it escape and must NOT destroy the
        // backing pool after a failed List cleanup. End this test process after
        // observing it, since the native destructor has partially progressed.
        auto* entry = reinterpret_cast<BYTE*>(Native(0x9F2514));
        entry[0] = 0x0F; entry[1] = 0x0B;
        FlushInstructionCache(GetCurrentProcess(), entry, 2);
        DestroyPool();
        Require(faultCode == EXCEPTION_ILLEGAL_INSTRUCTION && faultAddress == Native(0x9F2514), "cleanup exception propagated");
        Require(PoolFlags() == 0 && freed == 0 && cleanupBegins == 1 && cleanupEnds == 0, "failed cleanup does not destroy pool");
    } else {
        DestroyPool();
        Require(faultCode == 0 && (PoolFlags() & 1), "original pool destructor completed");
        Require(freed == allocated, "all native backing blocks released");
        if (baseline) {
            Require(Entries() == entries, "baseline leaves stale List");
            DestroyList();
            Require(faultCode == EXCEPTION_ACCESS_VIOLATION && faultAddress == Native(0x4031FE), "baseline reproduces exact fault instruction");
        } else if (uninitialized) {
            Require(!ListReady() && !allocated && uninitializedSkips == 1, "shutdown does not initialize List");
            DestroyPool();
            Require(faultCode == 0 && uninitializedSkips == 1, "repeated empty shutdown harmless");
        } else {
            Require(Entries() == 0 && cleanupBegins == 1 && cleanupEnds == 1, "production fix cleared List once");
            DestroyPool(); DestroyList(); DestroyList();
            Require(faultCode == 0 && Entries() == 0 && freed == allocated
                && cleanupBegins == 1 && cleanupEnds == 1, "later PCOM/repeated shutdown harmless");
        }
    }
    printf("PASS PcomExitFixTest %s entries=%u blocks=%u freed=%u observedException=%08lX\n",
        mode, entries, allocated, freed, faultCode);
    // Only the isolated test process: do not rerun loader cleanup after the
    // deliberately faulting baseline/propagation experiments.
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
