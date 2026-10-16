#pragma once

#include <windows.h>
#include <psapi.h>
#include <strsafe.h>

#pragma comment(lib, "user32.lib")

// Address-space evidence for allocation failures. A 32-bit client can exhaust
// its own virtual address space while the machine still has plenty of RAM, so
// private bytes alone cannot explain HeapAlloc/CreateDIBSection/MiniDump
// failures. Capture uses only VirtualQuery and fixed stack storage: it must
// still work when the heap and address space are already exhausted.
namespace MemoryStatus {

constexpr unsigned kTopAllocations = 8;
constexpr ULONGLONG kMiB = 1024ull * 1024ull;

struct Allocation {
    ULONG_PTR base = 0;
    ULONGLONG size = 0;
    ULONGLONG committed = 0;
    DWORD type = 0;
};

struct Snapshot {
    bool valid = false;
    ULONGLONG userSpace = 0;
    ULONGLONG freeBytes = 0;
    ULONGLONG largestFree = 0;
    ULONGLONG commitPrivate = 0, commitMapped = 0, commitImage = 0;
    ULONGLONG reservePrivate = 0, reserveMapped = 0, reserveImage = 0;
    DWORD regions = 0;
    ULONGLONG privateUsage = 0, workingSet = 0;
    DWORD gdiObjects = 0, gdiPeak = 0, userObjects = 0, handles = 0;
    // System-wide commit from GetPerformanceInfo (GlobalMemoryStatusEx may report
    // the smaller process/job limit instead).
    ULONGLONG systemCommitTotal = 0, systemCommitLimit = 0, systemPhysicalAvailable = 0;
    Allocation top[kTopAllocations]{};
    unsigned topCount = 0;
};

inline void AddTop(Snapshot& snapshot, const Allocation& allocation)
{
    if (allocation.size == 0) return;
    unsigned position = snapshot.topCount;
    while (position > 0 && snapshot.top[position - 1].size < allocation.size) --position;
    if (position >= kTopAllocations) return;
    const unsigned last = snapshot.topCount < kTopAllocations ? snapshot.topCount : kTopAllocations - 1;
    for (unsigned i = last; i > position; --i) snapshot.top[i] = snapshot.top[i - 1];
    snapshot.top[position] = allocation;
    if (snapshot.topCount < kTopAllocations) ++snapshot.topCount;
}

inline void Capture(Snapshot& snapshot)
{
    snapshot = Snapshot{};
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const ULONG_PTR minimum = reinterpret_cast<ULONG_PTR>(system.lpMinimumApplicationAddress);
    const ULONG_PTR maximum = reinterpret_cast<ULONG_PTR>(system.lpMaximumApplicationAddress);
    snapshot.userSpace = static_cast<ULONGLONG>(maximum - minimum) + 1;

    Allocation current{};
    ULONG_PTR address = minimum;
    while (address <= maximum) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info)
            || info.RegionSize == 0) break;
        ++snapshot.regions;
        const ULONG_PTR allocationBase = reinterpret_cast<ULONG_PTR>(info.AllocationBase);
        if (info.State == MEM_FREE) {
            snapshot.freeBytes += info.RegionSize;
            if (info.RegionSize > snapshot.largestFree) snapshot.largestFree = info.RegionSize;
        } else {
            ULONGLONG* commit = info.Type == MEM_MAPPED ? &snapshot.commitMapped
                : info.Type == MEM_IMAGE ? &snapshot.commitImage : &snapshot.commitPrivate;
            ULONGLONG* reserve = info.Type == MEM_MAPPED ? &snapshot.reserveMapped
                : info.Type == MEM_IMAGE ? &snapshot.reserveImage : &snapshot.reservePrivate;
            *(info.State == MEM_COMMIT ? commit : reserve) += info.RegionSize;
        }
        if (info.State == MEM_FREE || allocationBase != current.base) {
            AddTop(snapshot, current);
            current = Allocation{};
            if (info.State != MEM_FREE) {
                current.base = allocationBase;
                current.type = info.Type;
            }
        }
        if (info.State != MEM_FREE) {
            current.size += info.RegionSize;
            if (info.State == MEM_COMMIT) current.committed += info.RegionSize;
        }
        const ULONG_PTR next = reinterpret_cast<ULONG_PTR>(info.BaseAddress) + info.RegionSize;
        if (next <= address) break;
        address = next;
    }
    AddTop(snapshot, current);

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        snapshot.privateUsage = counters.PrivateUsage;
        snapshot.workingSet = counters.WorkingSetSize;
    }
    snapshot.gdiObjects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    snapshot.gdiPeak = GetGuiResources(GetCurrentProcess(), 2 /* GR_GDIOBJECTS_PEAK */);
    snapshot.userObjects = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    GetProcessHandleCount(GetCurrentProcess(), &snapshot.handles);
    PERFORMANCE_INFORMATION performance{};
    performance.cb = sizeof(performance);
    if (K32GetPerformanceInfo(&performance, sizeof(performance))) {
        const ULONGLONG page = performance.PageSize;
        snapshot.systemCommitTotal = performance.CommitTotal * page;
        snapshot.systemCommitLimit = performance.CommitLimit * page;
        snapshot.systemPhysicalAvailable = performance.PhysicalAvailable * page;
    }
    snapshot.valid = snapshot.regions != 0;
}

inline ULONGLONG UsedBytes(const Snapshot& snapshot)
{
    return snapshot.userSpace > snapshot.freeBytes ? snapshot.userSpace - snapshot.freeBytes : 0;
}

inline void FormatSummary(const Snapshot& snapshot, char* output, size_t outputSize)
{
    StringCchPrintfA(output, outputSize,
        "vaUsedMiB=%llu vaFreeMiB=%llu largestFreeMiB=%llu commitPrivateMiB=%llu commitMappedMiB=%llu "
        "commitImageMiB=%llu reservedMiB=%llu regions=%lu privateUsageMiB=%llu workingSetMiB=%llu "
        "gdi=%lu gdiPeak=%lu user=%lu handles=%lu sysCommitTotalMiB=%llu sysCommitLimitMiB=%llu sysPhysAvailMiB=%llu",
        UsedBytes(snapshot) / kMiB, snapshot.freeBytes / kMiB, snapshot.largestFree / kMiB,
        snapshot.commitPrivate / kMiB, snapshot.commitMapped / kMiB, snapshot.commitImage / kMiB,
        (snapshot.reservePrivate + snapshot.reserveMapped + snapshot.reserveImage) / kMiB,
        snapshot.regions, snapshot.privateUsage / kMiB, snapshot.workingSet / kMiB,
        snapshot.gdiObjects, snapshot.gdiPeak, snapshot.userObjects, snapshot.handles,
        snapshot.systemCommitTotal / kMiB, snapshot.systemCommitLimit / kMiB, snapshot.systemPhysicalAvailable / kMiB);
}

inline const char* TypeName(DWORD type)
{
    return type == MEM_MAPPED ? "mapped" : type == MEM_IMAGE ? "image" : "private";
}

// Keeps the last three path components (for example Data\Item\Install\0301.img
// becomes Item\Install\0301.img) so reports identify resources without
// carrying the player's full directory layout.
inline void FormatAllocation(const Allocation& allocation, char* output, size_t outputSize)
{
    char file[MAX_PATH * 3]{};
    if (allocation.type == MEM_MAPPED || allocation.type == MEM_IMAGE) {
        WCHAR path[MAX_PATH]{};
        const DWORD length = K32GetMappedFileNameW(GetCurrentProcess(),
            reinterpret_cast<LPVOID>(allocation.base), path, ARRAYSIZE(path));
        if (length > 0 && length < ARRAYSIZE(path)) {
            const WCHAR* tail = path + length;
            for (int separators = 0; tail > path; --tail) {
                if (tail[-1] == L'\\' && ++separators == 3) break;
            }
            WideCharToMultiByte(CP_UTF8, 0, tail, -1, file, sizeof(file), nullptr, nullptr);
        }
    }
    StringCchPrintfA(output, outputSize, "base=0x%08lX sizeMiB=%llu commitMiB=%llu type=%s file=%s",
        static_cast<unsigned long>(allocation.base), allocation.size / kMiB, allocation.committed / kMiB,
        TypeName(allocation.type), file[0] ? file : "-");
}

}
