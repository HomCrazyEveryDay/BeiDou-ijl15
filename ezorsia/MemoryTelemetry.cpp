#include "stdafx.h"
#include "MemoryTelemetry.h"
#include "MemoryStatus.h"
#include "ClientLog.h"

namespace {
constexpr DWORD kDefaultIntervalSeconds = 60;
constexpr DWORD kMinimumIntervalSeconds = 10;
constexpr DWORD kMaximumIntervalSeconds = 3600;
// Below these thresholds the next allocation spike can fail outright:
// sample faster and periodically record which allocations hold the space.
constexpr ULONGLONG kLowFreeBytes = 768 * MemoryStatus::kMiB;
constexpr ULONGLONG kLowLargestFreeBytes = 256 * MemoryStatus::kMiB;
constexpr DWORD kLowIntervalSeconds = 15;
constexpr ULONGLONG kTopDetailIntervalMs = 10 * 60 * 1000;

DWORD g_intervalSeconds = kDefaultIntervalSeconds;
ULONGLONG g_startTick = 0;

DWORD ReadIntervalSeconds()
{
    WCHAR value[16]{};
    const DWORD length = GetEnvironmentVariableW(L"BEIDOU_MEMORY_TELEMETRY_SECONDS", value, ARRAYSIZE(value));
    if (length == 0 || length >= ARRAYSIZE(value)) return kDefaultIntervalSeconds;
    WCHAR* end = nullptr;
    const unsigned long parsed = wcstoul(value, &end, 10);
    if (end == value || *end != L'\0') return kDefaultIntervalSeconds;
    if (parsed == 0) return 0;
    if (parsed < kMinimumIntervalSeconds) return kMinimumIntervalSeconds;
    return parsed > kMaximumIntervalSeconds ? kMaximumIntervalSeconds : parsed;
}

void WriteTop(const MemoryStatus::Snapshot& snapshot)
{
    for (unsigned i = 0; i < snapshot.topCount; ++i) {
        char line[512]{};
        MemoryStatus::FormatAllocation(snapshot.top[i], line, ARRAYSIZE(line));
        ClientLog::Append(ClientLog::Component::Memory, "memory_top index=%u %s", i, line);
    }
}

DWORD WINAPI SampleLoop(LPVOID)
{
    ULONGLONG lastTopDetail = 0;
    bool topWritten = false;
    ULONGLONG minimumLargestFree = ~0ull;
    for (;;) {
        MemoryStatus::Snapshot snapshot;
        MemoryStatus::Capture(snapshot);
        char summary[512]{};
        MemoryStatus::FormatSummary(snapshot, summary, ARRAYSIZE(summary));
        if (snapshot.largestFree < minimumLargestFree) minimumLargestFree = snapshot.largestFree;
        const ULONGLONG now = GetTickCount64();
        const bool low = snapshot.valid
            && (snapshot.freeBytes < kLowFreeBytes || snapshot.largestFree < kLowLargestFreeBytes);
        ClientLog::Append(ClientLog::Component::Memory, "memory_sample sinceStartSec=%llu low=%d minLargestFreeMiB=%llu %s",
            (now - g_startTick) / 1000, low ? 1 : 0, minimumLargestFree / MemoryStatus::kMiB, summary);
        // First sample after login shows the baseline; later ones only while space is tight.
        if (!topWritten || (low && now - lastTopDetail >= kTopDetailIntervalMs)) {
            WriteTop(snapshot);
            topWritten = true;
            lastTopDetail = now;
        }
        const DWORD seconds = low && g_intervalSeconds > kLowIntervalSeconds ? kLowIntervalSeconds : g_intervalSeconds;
        Sleep(seconds * 1000);
    }
}
}

void MemoryTelemetry::Start()
{
    g_intervalSeconds = ReadIntervalSeconds();
    g_startTick = GetTickCount64();
    HANDLE thread = nullptr;
    if (g_intervalSeconds != 0) {
        thread = CreateThread(nullptr, 64 * 1024, SampleLoop, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (thread) {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
            CloseHandle(thread);
        }
    }
    ClientLog::Emergency("memory_telemetry_config enabled=%d intervalSec=%lu lowIntervalSec=%lu started=%d",
        g_intervalSeconds != 0 ? 1 : 0, g_intervalSeconds, kLowIntervalSeconds, thread ? 1 : 0);
}
