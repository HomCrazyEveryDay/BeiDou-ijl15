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

static unsigned retainedReports;
namespace ClientLog {
void Emergency(const char* format, ...)
{
    if (strstr(format, "pool.retained")) ++retainedReports;
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
static bool guarded;
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

static void TermPcom() {
    auto term = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(reinterpret_cast<HMODULE>(pcom), "PcTermModule"));
    Require(term != nullptr, "PCOM term export");
    __try { term(); } __except(Capture(GetExceptionInformation())) {}
}

int main(int argc, char** argv)
{
    Require(argc == 3, "arguments: client-directory case");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* mode = argv[2];
    const bool baseline = strcmp(mode, "baseline-guard") == 0;
    const bool preclear = strcmp(mode, "preclear-late-term") == 0;
    const bool uninitialized = strcmp(mode, "uninitialized") == 0;
    const bool propagation = strcmp(mode, "propagate") == 0;
    const bool initialized = preclear || strncmp(mode, "late-term-", 10) == 0;
    guarded = strstr(mode, "heap") == nullptr;
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
    if (!baseline && !preclear) {
        CheckRejectedInstallations(client);
        Require(PcomExitFix::Install(reinterpret_cast<HMODULE>(client), reinterpret_cast<HMODULE>(pcom)), "install production fix");
        Require(PcomExitFix::Install(reinterpret_cast<HMODULE>(client), reinterpret_cast<HMODULE>(pcom)), "repeated install harmless");
        Require(!PcomExitFix::Install(nullptr, reinterpret_cast<HMODULE>(pcom)), "different instance after install rejected");
        Require(!ListReady() && !allocated, "install has no allocator or List side effects");
    }
    if (initialized) {
        auto init = reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(reinterpret_cast<HMODULE>(pcom), "PcInitModule"));
        Require(init && SUCCEEDED(init()), "real PCOM init retains module path string");
    }
    unsigned entries = 0;
    auto alloc = reinterpret_cast<void*(__stdcall*)(unsigned)>(Native(0x9F2503));
    auto free = reinterpret_cast<void(__stdcall*)(void*)>(Native(0x9F2514));
    void* outstanding = nullptr;
    if (!uninitialized) {
        reinterpret_cast<void*(__cdecl*)()>(pcom + 0x6BF8)();
        entries = Entries();
        Require(entries == 371, "real List populated");
        auto value = alloc(41); memset(value, 0x5A, 41); free(value);
        outstanding = alloc(41); memset(outstanding, 0xA5, 41);
        Require(Entries() == entries && PoolFlags() == 0 && retainedReports == 0, "normal allocator unchanged");
    }
    if (preclear) {
        DestroyList(); Require(!faultCode && Entries() == 0, "old fix successfully clears List");
    }
    DestroyPool();
    if (baseline || preclear) {
        Require(!faultCode && PoolFlags() == 1 && freed == allocated, "baseline destroys pool");
        if (preclear) TermPcom(); else DestroyList();
        Require(faultCode == EXCEPTION_ACCESS_VIOLATION && faultAddress == Native(0x4031FE),
            "exact late-free fault reproduced, including after successful List cleanup");
    } else {
        Require(!faultCode && PoolFlags() == 0 && freed == 0 && retainedReports == 1, "pool survives CRT exit callback");
        DestroyPool();
        Require(!faultCode && retainedReports == 1, "repeat callback harmless and diagnostics bounded");
        if (uninitialized) Require(!ListReady() && !allocated, "exit never initializes List");
        else {
            Require(Entries() == entries && *static_cast<BYTE*>(outstanding) == 0xA5, "all late owners still valid");
            free(outstanding);
            auto late = alloc(41); free(late);
            if (initialized) { TermPcom(); Require(!faultCode, "PCOM path released after EXE exit callback"); }
            if (propagation) {
                auto entry = reinterpret_cast<BYTE*>(Native(0x9F2514));
                entry[0] = 0x0F; entry[1] = 0x0B; FlushInstructionCache(GetCurrentProcess(), entry, 2);
            }
            DestroyList();
            if (propagation) Require(faultCode == EXCEPTION_ILLEGAL_INSTRUCTION && faultAddress == Native(0x9F2514), "unrelated cleanup exception propagates");
            else {
                Require(!faultCode && Entries() == 0, "native PCOM late cleanup succeeds");
                DestroyList(); Require(!faultCode, "repeat native PCOM cleanup succeeds");
            }
        }
        if (!propagation) {
            // Test-only final reclamation, after ALL consumers have gone. The
            // shipping fix leaves this allocation to Windows process teardown.
            reinterpret_cast<void(__thiscall*)(void*)>(Native(0x797B64))(reinterpret_cast<void*>(Native(0xBF0B00)));
            Require(freed == allocated, "backing blocks remain valid and reclaimable");
        }
    }
    printf("PASS PcomExitFixTest %s entries=%u blocks=%u freed=%u observedException=%08lX\n", mode, entries, allocated, freed, faultCode);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
