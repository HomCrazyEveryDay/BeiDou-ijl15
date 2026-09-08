#include "stdafx.h"
#include "HurricaneDamageSync.h"

#include <climits>

namespace {
constexpr int kHurricane = 3121004;
constexpr int kPendingCount = 128;
constexpr DWORD kPendingMs = 2500;

struct PendingDamage {
    void* mob;
    int damage;
    DWORD recordedAt;
};

PendingDamage g_pending[kPendingCount] = {};
int g_nextPending = 0;
thread_local int g_incomingDepth = 0;
SRWLOCK g_pendingLock = SRWLOCK_INIT;

int ReadI32(const unsigned char* data) {
    return static_cast<int>(static_cast<unsigned int>(data[0])
        | (static_cast<unsigned int>(data[1]) << 8)
        | (static_cast<unsigned int>(data[2]) << 16)
        | (static_cast<unsigned int>(data[3]) << 24));
}

void* FindMob(int objectId) {
    using FindMobFunc = void* (__thiscall*)(void*, int);
    void* pool = *reinterpret_cast<void**>(0x00BEBFA4);
    return pool ? reinterpret_cast<FindMobFunc>(0x00441AE8)(pool, objectId) : nullptr;
}
}

namespace HurricaneDamageSync {
void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size) {
    __try {
        if (!data || size < 34 || data[0] != 0x2D || data[1] != 0 || ReadI32(data + 4) != kHurricane) {
            return;
        }
        const int targets = data[3] >> 4;
        const int lines = data[3] & 0x0F;
        const unsigned long stride = 22 + lines * 4;
        if (targets <= 0 || lines <= 0 || size < 34 + targets * stride) {
            return;
        }
        // Hurricane has the native channeling timestamp before its first target.
        for (int target = 0; target < targets; ++target) {
            const unsigned char* entry = data + 34 + target * stride;
            void* mob = FindMob(ReadI32(entry));
            if (!mob) {
                continue;
            }
            for (int line = 0; line < lines; ++line) {
                const int rawDamage = ReadI32(entry + 18 + line * 4);
                const int damage = rawDamage < 0 ? rawDamage + INT_MAX : rawDamage;
                const DWORD now = GetTickCount();
                AcquireSRWLockExclusive(&g_pendingLock);
                g_pending[g_nextPending] = {mob, damage, now};
                g_nextPending = (g_nextPending + 1) % kPendingCount;
                ReleaseSRWLockExclusive(&g_pendingLock);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

bool ShouldSuppressLocalDamage(void* mob, int damage) {
    if (!mob || damage < 0 || g_incomingDepth > 0) {
        return false;
    }
    const DWORD now = GetTickCount();
    bool suppress = false;
    AcquireSRWLockExclusive(&g_pendingLock);
    for (int offset = 0; offset < kPendingCount; ++offset) {
        PendingDamage& pending = g_pending[(g_nextPending + offset) % kPendingCount];
        if (pending.mob && now - pending.recordedAt > kPendingMs) {
            pending = {};
        }
        if (pending.mob == mob && pending.damage == damage) {
            pending = {};
            suppress = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_pendingLock);
    return suppress;
}

void BeginIncomingPacket() {
    ++g_incomingDepth;
}

void EndIncomingPacket() {
    if (g_incomingDepth > 0) {
        --g_incomingDepth;
    }
}

void Reset() {
    AcquireSRWLockExclusive(&g_pendingLock);
    for (PendingDamage& pending : g_pending) {
        pending = {};
    }
    g_nextPending = 0;
    ReleaseSRWLockExclusive(&g_pendingLock);
}
}
