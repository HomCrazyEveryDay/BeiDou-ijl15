#pragma once
#include "ClientLog.h"

// Opt-in, aggregate-only probe. Game callbacks never write files or wait on locks.
namespace PetLootTiming {
using Scan = void(__thiscall*)(void*, void*, void*);
using Property = int(__thiscall*)(void*, int);
using Update = void(__thiscall*)(void*);
using DropUpdate = void(__thiscall*)(void*, int);
static DropUpdate dropUpdate = reinterpret_cast<DropUpdate>(0x00504BFF);
static Scan scan = reinterpret_cast<Scan>(0x0050483A);
static Property property = reinterpret_cast<Property>(0x005D5224);
static Update update = reinterpret_cast<Update>(0x0070403D);
static thread_local unsigned scanDepth = 0;
static volatile LONG active = 0;
static bool cacheEnabled = true;
struct CacheEntry { void* provider; int itemId; int result; bool valid; };
// Immutable WZ property only. Never cache ownership, inventory space or pickup results.
// Thread-local direct mapping bounds memory and requires no lock/heap allocation.
static thread_local CacheEntry cache[4096]{};
static volatile LONG cacheHits = 0, cacheMisses = 0;
static int Lookup(void* info, int itemId) {
    if (!cacheEnabled || itemId <= 0) return property(info, itemId);
    CacheEntry& entry = cache[static_cast<unsigned>(itemId) % ARRAYSIZE(cache)];
    if (entry.valid && entry.provider == info && entry.itemId == itemId) {
        if (active) InterlockedIncrement(&cacheHits);
        return entry.result;
    }
    if (active) InterlockedIncrement(&cacheMisses);
    const int result = property(info, itemId); // exceptions propagate; no failed entry stored
    entry = {info, itemId, result, true};
    return result;
}
struct alignas(8) Counter {
    volatile LONG64 calls = 0, ticks = 0, maximum = 0;
};
static Counter scans, properties, updates, outsideProperties, dropUpdates, dropGaps;
static thread_local void* lastDropPool = nullptr;
static thread_local LONGLONG lastDropStart = 0;
static LARGE_INTEGER frequency{};
static void Add(Counter& counter, LONGLONG ticks) {
    InterlockedIncrement64(&counter.calls);
    InterlockedExchangeAdd64(&counter.ticks, ticks);
    LONG64 old = InterlockedCompareExchange64(&counter.maximum, 0, 0);
    while (ticks > old) {
        const LONG64 seen = InterlockedCompareExchange64(&counter.maximum, ticks, old);
        if (seen == old) break;
        old = seen;
    }
}
static void __fastcall ScanHook(void* pool, void*, void* pet, void* position) {
    if (!active && !cacheEnabled) { scan(pool, pet, position); return; }
    const bool timing = active != 0;
    LARGE_INTEGER begin{}, end{};
    if (timing) QueryPerformanceCounter(&begin);
    ++scanDepth;
    __try { scan(pool, pet, position); }
    __finally {
        --scanDepth;
        if (timing) { QueryPerformanceCounter(&end); Add(scans, end.QuadPart - begin.QuadPart); }
    }
}
static int __fastcall PropertyHook(void* info, void*, int itemId) {
    const bool insideScan = scanDepth != 0;
    if (!active) return Lookup(info, itemId);
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    int result;
    __try { result = Lookup(info, itemId); }
    __finally {
        QueryPerformanceCounter(&end);
        Add(insideScan ? properties : outsideProperties, end.QuadPart - begin.QuadPart);
    }
    return result;
}
static void __fastcall UpdateHook(void* pet, void*) {
    if (!active) { update(pet); return; }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    __try { update(pet); }
    __finally {
        QueryPerformanceCounter(&end);
        Add(updates, end.QuadPart - begin.QuadPart);
    }
}
static void Emit(const char* stage, Counter& counter) {
    const auto calls = InterlockedExchange64(&counter.calls, 0);
    const auto ticks = InterlockedExchange64(&counter.ticks, 0);
    const auto maximum = InterlockedExchange64(&counter.maximum, 0);
    ClientLog::Append(ClientLog::Component::Trace,
        "event=pet_loot_timing stage=%s calls=%lld totalMs=%.3f maxMs=%.3f windowMs=2000 approximate=1",
        stage, calls, ticks * 1000.0 / frequency.QuadPart, maximum * 1000.0 / frequency.QuadPart);
}
static void __fastcall DropUpdateHook(void* pool, void*, int time) {
    if (!active) { dropUpdate(pool, time); return; }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    if (lastDropPool == pool && lastDropStart)
        Add(dropGaps, begin.QuadPart - lastDropStart);
    lastDropPool = pool;
    lastDropStart = begin.QuadPart;
    __try { dropUpdate(pool, time); }
    __finally {
        QueryPerformanceCounter(&end);
        Add(dropUpdates, end.QuadPart - begin.QuadPart);
    }
}
static DWORD WINAPI Writer(void*) {
    // At most 2100 aggregate rows / 10 minutes; no per-item paths or packet data.
    for (unsigned i = 0; i < 300; ++i) {
        Sleep(2000);
        Emit("pet_update", updates);
        Emit("pickup_scan", scans);
        Emit("pickup_property", properties);
        Emit("property_outside_scan", outsideProperties);
        Emit("drop_update", dropUpdates);
        Emit("drop_update_gap", dropGaps);
        ClientLog::Append(ClientLog::Component::Trace,
            "event=pet_loot_cache enabled=%d hits=%ld misses=%ld capacity=4096",
            cacheEnabled, InterlockedExchange(&cacheHits, 0), InterlockedExchange(&cacheMisses, 0));
    }
    InterlockedExchange(&active, 0);
    ClientLog::Append(ClientLog::Component::Trace, "event=pet_loot_timing stopped=duration_limit");
    return 0;
}
static void Install() {
    wchar_t config[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, config, ARRAYSIZE(config));
    if (!length || length >= ARRAYSIZE(config)) return;
    wchar_t* slash = wcsrchr(config, L'\\');
    if (!slash || wcscpy_s(slash + 1, ARRAYSIZE(config) - (slash + 1 - config), L"pet-loot-timing.ini")) return;
    const bool timingEnabled = GetPrivateProfileIntW(L"debug", L"enablePetLootTiming", 0, config) != 0;
    cacheEnabled = GetPrivateProfileIntW(L"debug", L"enablePetLootCache", 1, config) != 0;
    if (!timingEnabled && !cacheEnabled) return;
    const BYTE scanBytes[] = {0xB8,0x30,0x7C,0xA8,0x00};
    const BYTE propertyBytes[] = {0xB8,0x90,0x4A,0xA9,0x00};
    const BYTE updateBytes[] = {0xB8,0x18,0xDA,0xAA,0x00};
    const BYTE dropBytes[] = {0xB8,0x1C,0x7D,0xA8,0x00};
    if (memcmp(reinterpret_cast<void*>(scan), scanBytes, 5) ||
        memcmp(reinterpret_cast<void*>(property), propertyBytes, 5) ||
        memcmp(reinterpret_cast<void*>(update), updateBytes, 5) ||
        (timingEnabled && memcmp(reinterpret_cast<void*>(dropUpdate), dropBytes, 5)) ||
        !QueryPerformanceFrequency(&frequency) || !frequency.QuadPart) {
        ClientLog::Append(ClientLog::Component::Trace, "event=pet_loot_timing install=signature_mismatch");
        return;
    }
    const bool a = Memory::SetHook(true, reinterpret_cast<void**>(&scan), ScanHook);
    const bool b = a && Memory::SetHook(true, reinterpret_cast<void**>(&property), PropertyHook);
    const bool c = b && Memory::SetHook(true, reinterpret_cast<void**>(&update), UpdateHook);
    const bool d = c && (!timingEnabled || Memory::SetHook(true, reinterpret_cast<void**>(&dropUpdate), DropUpdateHook));
    if (!d) {
        if (c) Memory::SetHook(false, reinterpret_cast<void**>(&update), UpdateHook);
        if (b) Memory::SetHook(false, reinterpret_cast<void**>(&property), PropertyHook);
        if (a) Memory::SetHook(false, reinterpret_cast<void**>(&scan), ScanHook);
        ClientLog::Append(ClientLog::Component::Trace, "event=pet_loot_timing install=hook_failed");
        return;
    }
    ClientLog::Append(ClientLog::Component::Trace, "event=pet_loot_cache install=ok enabled=%d capacity=4096 scope=all_property_callers", cacheEnabled);
    if (!timingEnabled) return;
    InterlockedExchange(&active, 1);
    HANDLE thread = CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr);
    if (!thread) InterlockedExchange(&active, 0);
    else CloseHandle(thread);
    ClientLog::Append(ClientLog::Component::Trace,
        "event=pet_loot_timing install=%s probeVersion=3 intervalMs=2000 durationSeconds=600 property=info/pickUpBlock",
        thread ? "ok" : "writer_failed");
}
}
