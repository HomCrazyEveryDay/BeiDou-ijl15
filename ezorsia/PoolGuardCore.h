#pragma once
#include <windows.h>

// Shared freelist markers for the native ZAllocEx pools and the DLL replica in
// MapleClientCollectionTypes/ZAllocEx.h. Blocks migrate between those pools, so
// both sides stamp and check the same words:
//   [0] freelist link (owned by the allocator)
//   [1] canary while the block is free, cleared when it is handed out
//   [2] return address that freed it, [3] free sequence number
// Every bucket is at least 16 bytes, so these words stay inside the block.
namespace PoolGuard {
enum class Fault : DWORD {
    DoubleFree = 1,       // separately tracked free without an observed allocation
    FreedBlockWritten,    // a free block's canary changed before reuse
    FreeListLinkBad,      // the next freelist block is unreadable or mis-sized
    HeaderMismatch,       // small header is not an exact bucket size (foreign/overwritten)
};
enum class Action : DWORD {
    SkippedFree = 1,      // the free was not performed; the block leaks
    DroppedFreeList,      // the bucket list was abandoned; fresh blocks are carved
};

inline DWORD Canary(const void* block)
{
    return 0x5A17C0DEu ^ static_cast<DWORD>(reinterpret_cast<UINT_PTR>(block));
}

using ReportFn = void(*)(Fault, Action, const void*, DWORD, const void*);
struct Runtime {
    bool enabled = false;
    ReportFn report = nullptr;
    volatile LONG sequence = 0;
    // Bounded, allocation-free corroboration for double frees. Collisions can
    // lose evidence, but a coincidental canary in live user data is not a fault.
    void* volatile freed[65536]{};
};
inline Runtime& State() { static Runtime state; return state; }
inline bool Enabled() { return State().enabled; }
inline DWORD NextFreeSequence() { return static_cast<DWORD>(InterlockedIncrement(&State().sequence)); }
inline unsigned Slot(const void* p) { return (reinterpret_cast<UINT_PTR>(p) * 2654435761u >> 16) & 65535; }
inline bool TrackedFree(const void* p) { return State().freed[Slot(p)] == p; }
inline void MarkFree(void* p) { InterlockedExchangePointer(&State().freed[Slot(p)], p); }
inline void MarkActive(void* p) { InterlockedCompareExchangePointer(&State().freed[Slot(p)], nullptr, p); }
// Called by the DLL replica after it released its own lock.
inline void ReportReplicaFault(Fault fault, Action action, const void* block, DWORD expectedSize, const void* caller)
{
    if (State().report) State().report(fault, action, block, expectedSize, caller);
}
}
