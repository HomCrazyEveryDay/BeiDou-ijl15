// Isolated native allocator + production DLL replica tests. Does not execute
// BeiDou.exe's entry point, initialize the game, open a window or connect.
#include "stdafx.h"
#include "PoolGuard.h"
#include "ClientLog.h"
#include "CrashReporter.h"
#include "MapleClientCollectionTypes/ZAllocEx.h"
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <vector>

static void Require(bool ok, const char* why) {
    if (!ok) { printf("FAIL %s error=%lu\n", why, GetLastError()); TerminateProcess(GetCurrentProcess(), 2); }
}
static unsigned reports, dumps;
static bool failLogger, failDump;
#ifndef POOL_GUARD_REAL_REPORT
namespace ClientLog {
EmergencyBatch::EmergencyBatch() {}
EmergencyBatch::~EmergencyBatch() {}
void Emergency(const char* format, ...) {
    if (failLogger) RaiseException(0xE0451234, 0, 0, nullptr);
    if (strstr(format, "pool_guard fault=")) {
        ++reports;
        va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
    }
}
}
namespace CrashReporter {
void CaptureDiagnosticSnapshot(EXCEPTION_POINTERS* e, const WCHAR* tag, const char* kind) {
    ++dumps;
    Require(e->ContextRecord->Eip == reinterpret_cast<DWORD>(e->ExceptionRecord->ExceptionAddress), "snapshot identifies detector context");
    Require(wcsstr(tag, L"_pool-guard-") && !strcmp(kind, "pool_guard_detection"), "unique detection artifact tag");
    if (failDump) RaiseException(0xE0451235, 0, 0, nullptr);
}
}
#endif

constexpr DWORD delta = 0x30000000;
static DWORD Native(DWORD address) { return address + delta; }
static BYTE* MapImage(const char* path) {
    FILE* f = nullptr; Require(fopen_s(&f, path, "rb") == 0, "open fixture");
    fseek(f, 0, SEEK_END); std::vector<BYTE> bytes(ftell(f)); rewind(f);
    Require(fread(bytes.data(), 1, bytes.size(), f) == bytes.size(), "read fixture"); fclose(f);
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp == 0x4B7C15C9 && nt->OptionalHeader.SizeOfImage == 0xA94000, "known v83 fixture");
    BYTE* base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x400000)), nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(base == reinterpret_cast<BYTE*>(Native(0x400000)), "isolated image address");
    memcpy(base, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(base + sections[i].VirtualAddress, bytes.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
    // Decoded absolute operands in the three alloc/free bodies and shared lock
    // and carve helpers. All other code stays inert; no PE entry executes.
    const DWORD operands[] = {0x403066,0x4030CD,0x4030D4,0x40316B,0x403177,0x403183,
        0x4031B2,0x4031B9,0x403262,0x403269,0x420757,0x4207BE,0x4207C5,
        0x4222AC,0x422315,0x42231C,0x406448,0x40644F,0x405DD1,0x405DD8};
    for (DWORD operand : operands) *reinterpret_cast<DWORD*>(Native(operand)) += delta;
    *reinterpret_cast<void**>(Native(0xBF032C)) = reinterpret_cast<void*>(&GetProcessHeap);
    *reinterpret_cast<void**>(Native(0xBF0324)) = reinterpret_cast<void*>(&HeapAlloc);
    *reinterpret_cast<void**>(Native(0xBF0328)) = reinterpret_cast<void*>(&HeapFree);
    *reinterpret_cast<void**>(Native(0xBF02F4)) = reinterpret_cast<void*>(&Sleep);
    // Native CRT selects the LOCK CMPXCHG variant on multiprocessor systems.
    *reinterpret_cast<void**>(Native(0xBDC9D0)) = reinterpret_cast<void*>(Native(0x796F51));
    for (DWORD instance : {0xBF0B00, 0xBF0A90, 0xBF0BA8}) memset(reinterpret_cast<void*>(Native(instance)), 0, 0x2C);
    FlushInstructionCache(GetCurrentProcess(), base, nt->OptionalHeader.SizeOfImage);
    return base;
}

using Alloc = void*(__fastcall*)(void*, void*, size_t);
using Free = void(__fastcall*)(void*, void*, void*);
static constexpr DWORD instances[] = {0xBF0B00, 0xBF0A90, 0xBF0BA8};
static constexpr DWORD allocs[] = {0x403065, 0x420756, 0x4222AB};
static constexpr DWORD frees[] = {0x4031ED, 0x4063D3, 0x405D5A};
static constexpr DWORD buckets[][4] = {{16,32,64,128},{29,45,77,141},{46,78,142,270}};
static void* NAlloc(int pool, size_t n) { return reinterpret_cast<Alloc>(Native(allocs[pool]))(reinterpret_cast<void*>(Native(instances[pool])), nullptr, n); }
static void NFree(int pool, void* p) { reinterpret_cast<Free>(Native(frees[pool]))(reinterpret_cast<void*>(Native(instances[pool])), nullptr, p); }
static void* DAlloc(int pool, size_t n) {
    if (pool == 0) return ZAllocEx<ZAllocAnonSelector>::GetInstance()->Alloc(n);
    if (pool == 1) return ZAllocEx<ZAllocStrSelector<char>>::GetInstance()->Alloc(n);
    return ZAllocEx<ZAllocStrSelector<wchar_t>>::GetInstance()->Alloc(n);
}
static void DFree(int pool, void* p) {
    if (pool == 0) ZAllocEx<ZAllocAnonSelector>::GetInstance()->Free(static_cast<void**>(p));
    else if (pool == 1) ZAllocEx<ZAllocStrSelector<char>>::GetInstance()->Free(static_cast<void**>(p));
    else ZAllocEx<ZAllocStrSelector<wchar_t>>::GetInstance()->Free(static_cast<void**>(p));
}
static void Healthy(unsigned rounds) {
    for (unsigned j = 0; j < rounds; ++j) for (int pool = 0; pool < 3; ++pool) for (DWORD size : buckets[pool]) {
        void* p = NAlloc(pool, size); memset(p, 0xA3, size); DFree(pool, p);
        p = DAlloc(pool, size); memset(p, 0x5D, size); NFree(pool, p);
    }
}
static void Faults(int pool, bool replica) {
    auto allocate = replica ? DAlloc : NAlloc;
    auto release = replica ? DFree : NFree;
    DWORD size = buckets[pool][0];
    // A live payload that happens to match all in-band markers is not freed.
    auto* p = static_cast<DWORD*>(allocate(pool, size));
    p[1] = PoolGuard::Canary(p); p[2] = 0x12345678; p[3] = 5;
    LONG before = PoolGuard::Snapshot().reports;
    release(pool, p); Require(PoolGuard::Snapshot().reports == before, "live canary collision accepted");
    release(pool, p); Require(PoolGuard::Snapshot().doubleFree > 0 && PoolGuard::Snapshot().reports == before + 1, "double free detected once");
    Require(allocate(pool, size) == p, "double free did not duplicate list");
    release(pool, p); p[1] ^= 1;
    before = PoolGuard::Snapshot().reports;
    void* fresh = allocate(pool, size);
    Require(fresh != p && PoolGuard::Snapshot().reports == before + 1, "freed-block write quarantined");
    p = static_cast<DWORD*>(fresh); release(pool, p); p[0] = reinterpret_cast<DWORD>(p);
    before = PoolGuard::Snapshot().reports;
    fresh = allocate(pool, size);
    Require(fresh != p && PoolGuard::Snapshot().reports == before + 1, "self-cycle quarantined");
    p = static_cast<DWORD*>(fresh); release(pool, p); p[-1] = size + 1;
    before = PoolGuard::Snapshot().reports;
    fresh = allocate(pool, size);
    Require(fresh != p && PoolGuard::Snapshot().reports == before + 1, "free header corruption quarantined");
    p = static_cast<DWORD*>(fresh); p[-1] = ~size;
    before = PoolGuard::Snapshot().reports;
    release(pool, p); fresh = allocate(pool, size);
    Require(fresh == p && PoolGuard::Snapshot().reports == before, "native complemented header accepted");
    release(pool, fresh);
    auto* guardPage = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    Require(guardPage != nullptr, "unreadable-link fixture");
    p[0] = reinterpret_cast<DWORD>(guardPage);
    before = PoolGuard::Snapshot().reports;
    fresh = allocate(pool, size);
    Require(fresh != p && PoolGuard::Snapshot().reports == before + 1, "unreadable successor contained");
    VirtualFree(guardPage, 0, MEM_RELEASE);
    release(pool, fresh);
}
static double Benchmark(unsigned rounds) {
    LARGE_INTEGER begin{}, end{}, freq{}; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < rounds; ++i) { void* p = NAlloc(0, 32); static_cast<DWORD*>(p)[0] = i; NFree(0, p); }
    QueryPerformanceCounter(&end); return (end.QuadPart - begin.QuadPart) * 1e9 / freq.QuadPart / rounds;
}

int main(int argc, char** argv) {
    Require(argc == 2, "client exe argument"); setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    BYTE* image = MapImage(argv[1]);
#ifdef POOL_GUARD_REAL_REPORT
    // Exercise the actual process-entry timing (CRT lock callback not ready)
    // and the production artifact writer in a separate executable.
    *reinterpret_cast<void**>(Native(0xBDC9D0)) = nullptr;
    CrashReporter::Install(true, "mini", false);
    Require(PoolGuard::Install(reinterpret_cast<HMODULE>(image), true), "pre-CRT install");
    *reinterpret_cast<void**>(Native(0xBDC9D0)) = reinterpret_cast<void*>(Native(0x796F51));
    for (int i = 0; i < 3; ++i) {
        auto* sample = static_cast<DWORD*>(NAlloc(0, 32)); NFree(0, sample); sample[1] ^= 1;
        Require(NAlloc(0, 32) != sample, "real capture quarantines sample");
    }
    Require(PoolGuard::Snapshot().freedBlockWritten == 3, "real capture continued after faults");
    puts("PASS real pool diagnostic capture; three reports, at most two dumps");
    return 0;
#endif
    Require(!PoolGuard::Enabled(), "probe disabled before install");
    Healthy(10);
    auto* p = static_cast<DWORD*>(DAlloc(0, 16)); p[1] = 0xABCDEF01; DFree(0, p);
    Require(p[1] == 0xABCDEF01, "disabled replica leaves payload alone");
    const double baseline = Benchmark(1000000);
    puts("PASS disabled baseline and pre-install allocations");
    image[allocs[2] - 0x400000] ^= 1;
    Require(!PoolGuard::Install(reinterpret_cast<HMODULE>(image), true), "wrong signature rejected");
    image[allocs[2] - 0x400000] ^= 1;
    Require(!PoolGuard::Enabled() && image[allocs[0]-0x400000] == 0xB8, "reject leaves hooks disabled");
    Require(PoolGuard::Install(reinterpret_cast<HMODULE>(image), true), "guard installs all six hooks");
    Require(PoolGuard::Install(reinterpret_cast<HMODULE>(image), true), "idempotent install");
    Healthy(500);
    puts("PASS installed and cross-pool transfers");
    Require(PoolGuard::Snapshot().reports == 0, "cross-pool native/replica sizes healthy");
    for (int pool = 0; pool < 3; ++pool) {
        void* big = NAlloc(pool, 2048); memset(big, 0x65, 2048); NFree(pool, big); NFree(pool, nullptr);
    }
    std::thread threads[4];
    for (auto& t : threads) t = std::thread([] { Healthy(1500); });
    for (auto& t : threads) t.join();
    Require(PoolGuard::Snapshot().reports == 0, "four-thread mixed allocator stress healthy");
    const double guarded = Benchmark(1000000);
    printf("PASS healthy all 12 buckets, native/replica transfers, large/null, 4 threads; benchmark pair baseline=%.1fns guarded=%.1fns delta=%.1fns\n", baseline, guarded, guarded-baseline);
    // Fault in the evidence sink must neither replace the allocation result
    // nor keep its pool lock/thread recursion flag held.
    p = static_cast<DWORD*>(NAlloc(0, 16)); NFree(0, p); p[1] ^= 1;
    failLogger = true; SetLastError(0x1234); NAlloc(0, 16); failLogger = false;
    Require(GetLastError() == 0x1234, "logger fault contained and last error restored");
    p = static_cast<DWORD*>(NAlloc(0, 16)); NFree(0, p); p[1] ^= 1;
    failDump = true; NAlloc(0, 16); failDump = false;
    for (int pool = 0; pool < 3; ++pool) { Faults(pool, false); Faults(pool, true); }
    Require(reports <= 24 && dumps == 2, "report and snapshot budgets");
    auto stats = PoolGuard::Snapshot();
    Require(stats.doubleFree == 6 && stats.freedBlockWritten >= 8 && stats.freeListLinkBad >= 6 && stats.headerMismatch >= 6, "fault categories captured");
    printf("PASS PoolGuardTest doubleFree=%ld freedWrite=%ld link=%ld header=%ld reports=%u snapshots=%u\n", stats.doubleFree,stats.freedBlockWritten,stats.freeListLinkBad,stats.headerMismatch,reports,dumps);
    return 0;
}
