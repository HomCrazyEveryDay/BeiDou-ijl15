#include "stdafx.h"
#include "PoolGuard.h"
#include "ClientLog.h"
#include "CrashReporter.h"
#include "detours.h"
#include <intrin.h>
#include <strsafe.h>
#pragma intrinsic(_ReturnAddress)
// Keep frame pointers here so a report can walk EBP back to the native caller.
#pragma optimize("y", off)

namespace {
constexpr DWORD kImageBase = 0x00400000;
constexpr int kBuckets = 4;
constexpr DWORD kLockFunction = 0x00403166;   // ZFatalSection::Lock, ecx = pool+4
constexpr DWORD kLockSlot = 0x00BDC9D0;       // set by the EXE CRT before any pool use
constexpr DWORD kNoMatch = 0xFFFF;
constexpr LONG kMaxReports = 24;
constexpr LONG kMaxDumps = 2;
constexpr DWORD kSnapshotCode = 0xE0B0D0A1;

using AllocFn = void*(__fastcall*)(void*, void*, size_t);
using FreeFn = void(__fastcall*)(void*, void*, void*);
using LockFn = void(__fastcall*)(void*, void*);

struct Pool {
    const char* name;
    DWORD instance, allocAddress, freeAddress;
    DWORD sizes[kBuckets];
    BYTE* self;
    AllocFn alloc;
    FreeFn free;
};
// Bucket limits are the exact header values written by the native carve
// (00403196) and compared with <= by the matching Alloc/Free.
Pool g_pools[] = {
    {"anon", 0x00BF0B00, 0x00403065, 0x004031ED, {0x10, 0x20, 0x40, 0x80}},
    {"char", 0x00BF0A90, 0x00420756, 0x004063D3, {0x1D, 0x2D, 0x4D, 0x8D}},
    {"wide", 0x00BF0BA8, 0x004222AB, 0x00405D5A, {0x2E, 0x4E, 0x8E, 0x10E}},
};

// kNoMatch marks absolute operands that differ when the image is mapped elsewhere.
const WORD kAllocCode[3][24] = {
    {0xB8,kNoMatch,kNoMatch,kNoMatch,kNoMatch,0xE8,0x29,0xDB,0x65,0x00,0x83,0xEC,0x0C,0x53,0x8B,0x5D,0x08,0x83,0xFB,0x20,0x56,0x57,0x8B,0xF9},
    {0xB8,kNoMatch,kNoMatch,kNoMatch,kNoMatch,0xE8,0x38,0x04,0x64,0x00,0x83,0xEC,0x0C,0x53,0x8B,0x5D,0x08,0x83,0xFB,0x2D,0x56,0x57,0x8B,0xF9},
    {0xB8,kNoMatch,kNoMatch,kNoMatch,kNoMatch,0xE8,0xE3,0xE8,0x63,0x00,0x83,0xEC,0x0C,0x53,0x8B,0x5D,0x08,0x83,0xFB,0x4E,0x56,0x57,0x8B,0xF9},
};
const WORD kFreeCode[3][40] = {
    {0x55,0x8B,0xEC,0x51,0x53,0x8B,0x5D,0x08,0x85,0xDB,0x56,0x57,0x89,0x4D,0xFC,0x74,0x45,0x8B,0x43,0xFC,
     0x85,0xC0,0x8D,0x53,0xFC,0x7D,0x02,0xF7,0xD0,0x83,0xF8,0x20,0x77,0x0B,0x6A,0x10,0x5E,0x3B,0xF0,0x1B},
    {0x55,0x8B,0xEC,0x51,0x53,0x8B,0x5D,0x08,0x85,0xDB,0x56,0x57,0x89,0x4D,0xFC,0x74,0x45,0x8B,0x43,0xFC,
     0x85,0xC0,0x8D,0x53,0xFC,0x7D,0x02,0xF7,0xD0,0x83,0xF8,0x2D,0x77,0x0B,0x6A,0x1D,0x5E,0x3B,0xF0,0x1B},
    {0x55,0x8B,0xEC,0x51,0x53,0x8B,0x5D,0x08,0x85,0xDB,0x56,0x57,0x89,0x4D,0xFC,0x74,0x47,0x8B,0x43,0xFC,
     0x85,0xC0,0x8D,0x53,0xFC,0x7D,0x02,0xF7,0xD0,0x83,0xF8,0x4E,0x77,0x0B,0x6A,0x2E,0x5E,0x3B,0xF0,0x1B},
};
const WORD kLockCode[] = {0x56,0x8B,0xF1,0xFF,0x15,kNoMatch,kNoMatch,kNoMatch,kNoMatch};

BYTE* g_client = nullptr;
bool g_dumps = false;
bool g_installed = false;
volatile LONG g_reports = 0, g_dumpCount = 0;
volatile LONG g_doubleFree = 0, g_freedBlockWritten = 0, g_freeListLinkBad = 0, g_headerMismatch = 0;
thread_local bool t_reporting = false;

struct Report {
    const char* pool;
    PoolGuard::Fault fault;
    PoolGuard::Action action;
    const BYTE* block;
    DWORD header, expected;
    const void* caller;
    DWORD words[4];
    bool wordsKnown;
};

BYTE* Rebase(DWORD address) { return g_client + (address - kImageBase); }

bool MatchesImage(const BYTE* base)
{
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(*dos))
        || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == 0x4B7C15C9
        && nt->OptionalHeader.SizeOfImage == 0xA94000;
}

template<class T, size_t N> bool MatchesCode(DWORD address, const T (&pattern)[N])
{
    const BYTE* code = Rebase(address);
    for (size_t i = 0; i < N; ++i)
        if (pattern[i] != static_cast<T>(kNoMatch) && code[i] != static_cast<BYTE>(pattern[i])) return false;
    return true;
}

bool ReadWords(const void* address, DWORD* out, unsigned count)
{
    __try {
        for (unsigned i = 0; i < count; ++i) out[i] = static_cast<const volatile DWORD*>(address)[i];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        for (unsigned i = 0; i < count; ++i) out[i] = 0;
        return false;
    }
}

bool LockReady() { return *reinterpret_cast<void* const*>(Rebase(kLockSlot)) != nullptr; }
void Lock(BYTE* self) { reinterpret_cast<LockFn>(Rebase(kLockFunction))(self + 4, nullptr); }
// Mirrors the native unlock (00403155): owner-thread count, clear owner at zero.
void Unlock(BYTE* self)
{
    auto* lock = reinterpret_cast<volatile DWORD*>(self + 4);
    if (--lock[1] == 0) lock[0] = 0;
}

int BucketForSize(const Pool& pool, size_t size)
{
    for (int i = 0; i < kBuckets; ++i) if (size <= pool.sizes[i]) return i;
    return -1;
}

BYTE** Heads(BYTE* self) { return reinterpret_cast<BYTE**>(self + 0xC); }

void Stamp(BYTE* block, DWORD freedBy, DWORD sequence)
{
    auto* words = reinterpret_cast<DWORD*>(block);
    words[1] = PoolGuard::Canary(block);
    words[2] = freedBy;
    words[3] = sequence;
}

// Freshly carved blocks join the freelist without a canary. Stamp the chain
// that the native carve just published; it is bounded by the carve count.
bool StampChain(BYTE* head, DWORD size)
{
    __try {
        for (unsigned i = 0; head && i < 65536; ++i) {
            DWORD header = *reinterpret_cast<DWORD*>(head - 4);
            if (static_cast<LONG>(header) < 0) header = ~header;
            if (header != size || *reinterpret_cast<BYTE**>(head) == head) return false;
            Stamp(head, 0, 0);
            head = *reinterpret_cast<BYTE**>(head);
        }
        return head == nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Capture(Report& report, const Pool& pool, PoolGuard::Fault fault, const BYTE* block, DWORD header, DWORD expected)
{
    report.pool = pool.name;
    report.fault = fault;
    report.block = block;
    report.header = header;
    report.expected = expected;
    report.wordsKnown = ReadWords(block, report.words, 4);
}

// Validates one block that is on a freelist. Returns false and fills report.
bool CheckFreeBlock(const Pool& pool, int bucket, const BYTE* block, Report& report)
{
    DWORD header = 0, words[2]{};
    const DWORD size = pool.sizes[bucket];
    if (!ReadWords(block - 4, &header, 1) || !ReadWords(block, words, 2)) {
        Capture(report, pool, PoolGuard::Fault::FreeListLinkBad, block, header, size);
        return false;
    }
    if ((static_cast<LONG>(header) < 0 ? ~header : header) != size) {
        Capture(report, pool, PoolGuard::Fault::HeaderMismatch, block, header, size);
        return false;
    }
    if (words[1] != PoolGuard::Canary(block)) {
        Capture(report, pool, PoolGuard::Fault::FreedBlockWritten, block, header, size);
        return false;
    }
    return true;
}

bool CheckFreeHead(const Pool& pool, int bucket, const BYTE* head, Report& report)
{
    if (!CheckFreeBlock(pool, bucket, head, report)) return false;
    const auto* next = *reinterpret_cast<const BYTE* const*>(head);
    if (!next) return true;
    if (next == head) {
        Capture(report, pool, PoolGuard::Fault::FreeListLinkBad, head, pool.sizes[bucket], pool.sizes[bucket]);
        return false;
    }
    if (CheckFreeBlock(pool, bucket, next, report)) {
        if (*reinterpret_cast<const BYTE* const*>(next) != head) return true;
        Capture(report, pool, PoolGuard::Fault::FreeListLinkBad, head, pool.sizes[bucket], pool.sizes[bucket]);
        return false;
    }
    // The head is intact; its link or the linked block is not.
    if (report.fault != PoolGuard::Fault::FreedBlockWritten) {
        Capture(report, pool, PoolGuard::Fault::FreeListLinkBad, head, pool.sizes[bucket], pool.sizes[bucket]);
    }
    return false;
}

void Describe(const void* address, char* out, size_t size)
{
    HMODULE module = nullptr;
    char path[MAX_PATH]{};
    if (address && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(address), &module)
        && GetModuleFileNameA(module, path, ARRAYSIZE(path))) {
        const char* name = strrchr(path, '\\');
        StringCchPrintfA(out, size, "%s+%08lX", name ? name + 1 : path,
            static_cast<DWORD>(reinterpret_cast<const BYTE*>(address) - reinterpret_cast<const BYTE*>(module)));
        return;
    }
    StringCchPrintfA(out, size, "%p", address);
}

const char* FaultName(PoolGuard::Fault fault)
{
    switch (fault) {
    case PoolGuard::Fault::DoubleFree: return "double_free";
    case PoolGuard::Fault::FreedBlockWritten: return "freed_block_written";
    case PoolGuard::Fault::FreeListLinkBad: return "freelist_link_bad";
    case PoolGuard::Fault::HeaderMismatch: return "header_mismatch";
    }
    return "unknown";
}

void Count(PoolGuard::Fault fault)
{
    switch (fault) {
    case PoolGuard::Fault::DoubleFree: InterlockedIncrement(&g_doubleFree); break;
    case PoolGuard::Fault::FreedBlockWritten: InterlockedIncrement(&g_freedBlockWritten); break;
    case PoolGuard::Fault::FreeListLinkBad: InterlockedIncrement(&g_freeListLinkBad); break;
    case PoolGuard::Fault::HeaderMismatch: InterlockedIncrement(&g_headerMismatch); break;
    }
}

__declspec(noinline) void WalkFrames()
{
    DWORD* frame = nullptr;
    __asm mov frame, ebp
    char where[MAX_PATH + 32]{};
    for (unsigned i = 0; i < 20 && frame; ++i) {
        DWORD link[2]{};
        if (!ReadWords(frame, link, 2) || !link[1]) break;
        Describe(reinterpret_cast<const void*>(link[1]), where, ARRAYSIZE(where));
        ClientLog::Emergency("pool_guard_frame index=%u return=%s", i, where);
        auto* next = reinterpret_cast<DWORD*>(link[0]);
        if (next <= frame) break;
        frame = next;
    }
}

__declspec(noinline) void WriteSnapshot(const Report& report, LONG index)
{
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = kSnapshotCode;
    // This is a detection-time context, not the original corrupting write.
    record.ExceptionAddress = reinterpret_cast<void*>(context.Eip);
    record.NumberParameters = 3;
    record.ExceptionInformation[0] = static_cast<ULONG_PTR>(report.fault);
    record.ExceptionInformation[1] = reinterpret_cast<ULONG_PTR>(report.block);
    record.ExceptionInformation[2] = reinterpret_cast<ULONG_PTR>(report.caller);
    EXCEPTION_POINTERS pointers{&record, &context};
    WCHAR tag[40]{};
    StringCchPrintfW(tag, ARRAYSIZE(tag), L"_pool-guard-%ld", index);
    CrashReporter::CaptureDiagnosticSnapshot(&pointers, tag, "pool_guard_detection");
}

// Runs after the allocator lock is released. Uses fixed buffers only.
__declspec(noinline) void SubmitBody(const Report& report)
{
    const LONG index = InterlockedIncrement(&g_reports);
    if (index <= kMaxReports) {
        const DWORD savedError = GetLastError();
        ClientLog::EmergencyBatch batch;
        char caller[MAX_PATH + 32]{}, freedBy[MAX_PATH + 32]{};
        Describe(report.caller, caller, ARRAYSIZE(caller));
        const bool markerIntact = report.wordsKnown && report.words[1] == PoolGuard::Canary(report.block);
        // Generic pool data may be user input. Log metadata only, never an
        // ASCII/UTF-16 rendering or raw payload words from arbitrary blocks.
        Describe(markerIntact ? reinterpret_cast<const void*>(report.words[2]) : nullptr, freedBy, ARRAYSIZE(freedBy));
        ClientLog::Emergency(
            "pool_guard fault=%s action=%s pool=%s block=%p header=%08lX expected=%08lX caller=%s report=%ld",
            FaultName(report.fault), report.action == PoolGuard::Action::SkippedFree ? "skipped_free" : "dropped_freelist",
            report.pool, report.block, report.header, report.expected, caller, index);
        ClientLog::Emergency("pool_guard_block known=%d markerIntact=%d freedBy=%s freeSeq=%lu",
            report.wordsKnown ? 1 : 0, markerIntact ? 1 : 0, freedBy, markerIntact ? report.words[3] : 0);
        WalkFrames();
        SetLastError(savedError);
    }
    if (g_dumps) {
        const LONG dumpIndex = InterlockedIncrement(&g_dumpCount);
        if (dumpIndex <= kMaxDumps) WriteSnapshot(report, dumpIndex);
    }
}

void Submit(const Report& report)
{
    Count(report.fault);
    if (t_reporting) return;
    const DWORD savedError = GetLastError();
    t_reporting = true;
    __try { SubmitBody(report); }
    __except (EXCEPTION_EXECUTE_HANDLER) { /* Evidence must not crash the allocator. */ }
    t_reporting = false;
    SetLastError(savedError);
}

template<int I> void* __fastcall GuardedAlloc(void* self, void*, size_t size)
{
    Pool& pool = g_pools[I];
    auto* instance = static_cast<BYTE*>(self);
    const int bucket = BucketForSize(pool, size);
    if (!PoolGuard::Enabled() || bucket < 0 || !LockReady()) return pool.alloc(self, nullptr, size);
    Report report{};
    bool fault = false;
    void* result = nullptr;
    Lock(instance);
    __try {
        BYTE* head = Heads(instance)[bucket];
        if (head && !CheckFreeHead(pool, bucket, head, report)) {
            // Never hand out or follow a corrupt free block; leak the list.
            Heads(instance)[bucket] = nullptr;
            head = nullptr;
            fault = true;
        }
        result = pool.alloc(self, nullptr, size);
        if (result) {
            if (result != head && !StampChain(Heads(instance)[bucket], pool.sizes[bucket])) {
                Capture(report, pool, PoolGuard::Fault::FreeListLinkBad, Heads(instance)[bucket], 0, pool.sizes[bucket]);
                Heads(instance)[bucket] = nullptr;
                fault = true;
            }
            static_cast<DWORD*>(result)[1] = 0;
            PoolGuard::MarkActive(result);
        }
    } __finally {
        Unlock(instance);
    }
    if (fault) {
        report.action = PoolGuard::Action::DroppedFreeList;
        report.caller = _ReturnAddress();
        Submit(report);
    }
    return result;
}

template<int I> void __fastcall GuardedFree(void* self, void*, void* block)
{
    Pool& pool = g_pools[I];
    auto* instance = static_cast<BYTE*>(self);
    auto* p = static_cast<BYTE*>(block);
    DWORD header = 0;
    if (!PoolGuard::Enabled() || !p || !LockReady() || !ReadWords(p - 4, &header, 1)) { pool.free(self, nullptr, block); return; }
    const DWORD normalized = static_cast<LONG>(header) < 0 ? ~header : header;
    const int bucket = BucketForSize(pool, normalized);
    if (bucket < 0) { pool.free(self, nullptr, block); return; }
    const void* caller = _ReturnAddress();
    Report report{};
    bool fault = false;
    Lock(instance);
    __try {
        if (normalized != pool.sizes[bucket]) {
            Capture(report, pool, PoolGuard::Fault::HeaderMismatch, p, header, pool.sizes[bucket]);
            fault = true;
        } else if (PoolGuard::TrackedFree(p)) {
            Capture(report, pool, PoolGuard::Fault::DoubleFree, p, header, pool.sizes[bucket]);
            fault = true;
        } else {
            pool.free(self, nullptr, block);
            Stamp(p, static_cast<DWORD>(reinterpret_cast<UINT_PTR>(caller)), PoolGuard::NextFreeSequence());
            PoolGuard::MarkFree(p);
        }
    } __finally {
        Unlock(instance);
    }
    if (fault) {
        report.action = PoolGuard::Action::SkippedFree;
        report.caller = caller;
        Submit(report);
    }
}

void* const kAllocHooks[] = {reinterpret_cast<void*>(&GuardedAlloc<0>), reinterpret_cast<void*>(&GuardedAlloc<1>), reinterpret_cast<void*>(&GuardedAlloc<2>)};
void* const kFreeHooks[] = {reinterpret_cast<void*>(&GuardedFree<0>), reinterpret_cast<void*>(&GuardedFree<1>), reinterpret_cast<void*>(&GuardedFree<2>)};

// A late install finds blocks already on the freelists. Stamp them; drop a
// list that is already corrupt rather than trusting it.
void StampExistingLists()
{
    if (!LockReady()) return;  // pools are constructed later by the EXE CRT
    for (auto& pool : g_pools) {
        Lock(pool.self);
        Report report{};
        bool fault = false;
        __try {
            for (int bucket = 0; bucket < kBuckets; ++bucket) {
                BYTE* block = Heads(pool.self)[bucket];
                if (!StampChain(block, pool.sizes[bucket])) {
                    Capture(report, pool, PoolGuard::Fault::FreeListLinkBad, block, 0, pool.sizes[bucket]);
                    Heads(pool.self)[bucket] = nullptr;
                    fault = true;
                }
            }
        } __finally {
            Unlock(pool.self);
        }
        if (fault) {
            report.action = PoolGuard::Action::DroppedFreeList;
            Submit(report);
        }
    }
}
}

namespace PoolGuard {
void ReportReplicaFaultImpl(Fault fault, Action action, const void* block, DWORD expectedSize, const void* caller)
{
    Report report{};
    report.pool = "dll";
    report.fault = fault;
    report.action = action;
    report.block = static_cast<const BYTE*>(block);
    ReadWords(report.block - 4, &report.header, 1);
    report.expected = expectedSize;
    report.caller = caller;
    report.wordsKnown = ReadWords(block, report.words, 4);
    Submit(report);
}

bool Install(HMODULE client, bool dumps)
{
    auto* base = reinterpret_cast<BYTE*>(client);
    if (g_installed) return g_client == base;
    if (!MatchesImage(base)) return false;
    g_client = base;
    bool matches = MatchesCode(kLockFunction, kLockCode);
    for (int i = 0; i < 3; ++i)
        matches = matches && MatchesCode(g_pools[i].allocAddress, kAllocCode[i]) && MatchesCode(g_pools[i].freeAddress, kFreeCode[i]);
    if (!matches) {
        g_client = nullptr;
        return false;
    }
    for (auto& pool : g_pools) {
        pool.self = Rebase(pool.instance);
        pool.alloc = reinterpret_cast<AllocFn>(Rebase(pool.allocAddress));
        pool.free = reinterpret_cast<FreeFn>(Rebase(pool.freeAddress));
    }
    g_dumps = dumps;
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR) {
        result = DetourUpdateThread(GetCurrentThread());
        for (int i = 0; result == NO_ERROR && i < 3; ++i) {
            result = DetourAttach(reinterpret_cast<PVOID*>(&g_pools[i].alloc), kAllocHooks[i]);
            if (result == NO_ERROR) result = DetourAttach(reinterpret_cast<PVOID*>(&g_pools[i].free), kFreeHooks[i]);
        }
        if (result == NO_ERROR) result = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    if (result != NO_ERROR) {
        for (auto& pool : g_pools) pool.self = nullptr, pool.alloc = nullptr, pool.free = nullptr;
        g_client = nullptr;
        return false;
    }
    g_installed = true;
    StampExistingLists();
    State().report = ReportReplicaFaultImpl;
    State().enabled = true;
    return true;
}

Counters Snapshot()
{
    return {g_doubleFree, g_freedBlockWritten, g_freeListLinkBad, g_headerMismatch, g_reports};
}
}
