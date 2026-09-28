#include "stdafx.h"
#include "ComboTempestDamage.h"

namespace {
constexpr int kTempestCapacity = 128;
constexpr int kTempestMaxLines = 15;
constexpr DWORD kTempestLifetimeMs = 2500;
// Keep the existing server-side Combo Tempest cadence, measured from actual drawing.
constexpr DWORD kTempestIntervalMs = 90;

struct TempestLine { int damage; bool critical; };
struct TempestHit {
    unsigned long long serial;
    DWORD receivedAt;
    DWORD lastShownAt;
    int x, y;
    int count, next;
    TempestLine lines[kTempestMaxLines];
};
TempestHit g_tempestHits[kTempestCapacity] = {};
int g_nextTempest = 0;
unsigned long long g_tempestSerial = 0;
SRWLOCK g_tempestLock = SRWLOCK_INIT;

int ReadTempestInt(const unsigned char* data) {
    int value;
    memcpy(&value, data, sizeof(value));
    return value;
}

bool CaptureTempestPosition(int objectId, int& x, int& y) {
    __try {
        void* pool = *reinterpret_cast<void**>(0x00BEBFA4);
        if (!pool) return false;
        using FindMob = void* (__thiscall*)(void*, int);
        void* mob = reinterpret_cast<FindMob>(0x00441AE8)(pool, objectId);
        if (!mob) return false;
        void* position = *reinterpret_cast<void**>(static_cast<unsigned char*>(mob) + 0x120);
        if (!position) return false;
        using GetCoordinate = int (__thiscall*)(void*);
        x = reinterpret_cast<GetCoordinate>(0x00403CB7)(position);
        y = reinterpret_cast<GetCoordinate>(0x00403CDE)(position);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool RenderTempestLine(const TempestHit& hit) {
    // Reuse CMob::ShowDamage's animation ABI and coordinates, without retaining
    // the CMob pointer. A death packet must not flush all remaining lines at once.
    __try {
        auto* displayer = *reinterpret_cast<unsigned char**>(0x00BEBF6C);
        if (!displayer) return false;
        const TempestLine& line = hit.lines[hit.next];
        const int y = hit.y - 15 - hit.next * 30;
        if (line.damage == 0) {
            using ShowMiss = void (__thiscall*)(void*, int, int, int);
            reinterpret_cast<ShowMiss>(0x00438A21)(displayer, hit.x, y, 0);
        } else {
            bool critical = line.critical;
            if (critical && (!*reinterpret_cast<void**>(displayer + 0x188)
                || !*reinterpret_cast<void**>(displayer + 0x18C))) critical = false;
            if (!critical && (!*reinterpret_cast<void**>(displayer + 0x170)
                || !*reinterpret_cast<void**>(displayer + 0x174))) return false;
            using ShowNumber = void (__thiscall*)(void*, int, int, int, int, int);
            reinterpret_cast<ShowNumber>(0x00437D0F)(displayer, hit.x, y, line.damage, 0, critical ? 1 : 0);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

namespace ComboTempestDamage {
bool HandlePacket(const unsigned char* data, unsigned long size) {
    if (!data || size < 17 || data[4] != 0x01 || data[5] != 0x10 || data[16] != 2) return false;
    // Recognized malformed packets are consumed, never sent to the legacy renderer.
    if (size < 18 || data[15] != 0) return true;
    const int count = data[17];
    if (count <= 0 || count > kTempestMaxLines || size != 18 + (count - 1) * 5) return true;
    TempestHit hit{};
    hit.count = count;
    hit.receivedAt = GetTickCount();
    for (int i = 0; i < count; ++i) {
        const unsigned long offset = i == 0 ? 10 : 18 + (i - 1) * 5;
        hit.lines[i] = {ReadTempestInt(data + offset), data[offset + 4] != 0};
        if (hit.lines[i].damage < 0) return true;
    }
    if (!CaptureTempestPosition(ReadTempestInt(data + 6), hit.x, hit.y)) return true;
    AcquireSRWLockExclusive(&g_tempestLock);
    hit.serial = ++g_tempestSerial;
    g_tempestHits[g_nextTempest] = hit;
    g_nextTempest = (g_nextTempest + 1) % kTempestCapacity;
    ReleaseSRWLockExclusive(&g_tempestLock);
    return true;
}

void Update() {
    for (int i = 0; i < kTempestCapacity; ++i) {
        TempestHit ready{};
        const DWORD now = GetTickCount();
        AcquireSRWLockExclusive(&g_tempestLock);
        TempestHit& pending = g_tempestHits[i];
        if (pending.serial && now - pending.receivedAt > kTempestLifetimeMs) pending = {};
        if (pending.serial && (pending.next == 0 || now - pending.lastShownAt >= kTempestIntervalMs)) ready = pending;
        ReleaseSRWLockExclusive(&g_tempestLock);
        if (!ready.serial || !RenderTempestLine(ready)) continue;
        AcquireSRWLockExclusive(&g_tempestLock);
        if (pending.serial == ready.serial && pending.next == ready.next) {
            pending.lastShownAt = GetTickCount();
            if (++pending.next == pending.count) pending = {};
        }
        ReleaseSRWLockExclusive(&g_tempestLock);
    }
}

void Reset() {
    AcquireSRWLockExclusive(&g_tempestLock);
    for (TempestHit& hit : g_tempestHits) hit = {};
    g_nextTempest = 0;
    ReleaseSRWLockExclusive(&g_tempestLock);
}
}
