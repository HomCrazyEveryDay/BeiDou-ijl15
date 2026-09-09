#include "stdafx.h"
#include "ShadowPartnerDamageSync.h"
#include "ShadowPartnerAttack.h"

#include <climits>

namespace {
constexpr int kPendingCount = 128;
constexpr DWORD kPendingMs = 2500;
struct PendingDamage {
    void* mob;
    int objectId;
    int localDamage;
    int lineIndex;
    DWORD recordedAt;
    int serverDamage;
    bool critical;
    bool serverReady;
    bool impacted;
    int pursuitLine;
    bool remote;
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

bool IsLive(PendingDamage& pending, DWORD now) {
    if (pending.mob && now - pending.recordedAt > kPendingMs) {
        pending = {};
    }
    return pending.mob != nullptr;
}
}

namespace ShadowPartnerDamageSync {
void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size) {
    __try {
        if (!data || size < 30 || data[0] != 0x2D || data[1] != 0) {
            return;
        }
        const int targets = data[3] >> 4;
        const int lines = data[3] & 0x0F;
        const int copied = ShadowPartnerAttack::CopiedAttackCount(ReadI32(data + 4), lines);
        const unsigned long stride = 22 + lines * 4;
        if (copied == 0 || targets == 0 || size < 30 + targets * stride) {
            return;
        }
        for (int target = 0; target < targets; ++target) {
            const unsigned char* entry = data + 30 + target * stride;
            const int objectId = ReadI32(entry);
            void* mob = FindMob(objectId);
            if (!mob) {
                continue;
            }
            for (int line = copied; line < lines; ++line) {
                const int damage = ReadI32(entry + 18 + line * 4) & INT_MAX;
                const DWORD now = GetTickCount();
                AcquireSRWLockExclusive(&g_pendingLock);
                g_pending[g_nextPending] = {mob, objectId, damage, line, now, 0, false, false, false};
                g_nextPending = (g_nextPending + 1) % kPendingCount;
                ReleaseSRWLockExclusive(&g_pendingLock);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

void TrackIncomingAttackPacket(const unsigned char* data, unsigned long size) {
    __try {
        if (!data || size < 26 || data[4] != 0xBB || data[5] != 0) return;
        const int targets = data[10] >> 4;
        const int lines = data[10] & 0x0F;
        const bool hasSkill = data[12] > 0;
        const int copied = ShadowPartnerAttack::CopiedAttackCount(hasSkill ? ReadI32(data + 13) : 0, lines);
        const unsigned long header = hasSkill ? 26 : 22;
        const unsigned long stride = 5 + lines * 4;
        const unsigned long trailer = header + targets * stride + 4;
        if (!copied || !targets || size < trailer + 5 || ReadI32(data + trailer) != 0x31504853) return;
        const int pursuitTargets = data[trailer + 4];
        const unsigned long pursuitStride = 5 + copied * 4;
        if (!pursuitTargets || pursuitTargets > targets || size != trailer + 5 + pursuitTargets * pursuitStride) return;
        for (int target = 0; target < pursuitTargets; ++target) {
            if (data[trailer + 5 + target * pursuitStride + 4] != copied) return;
        }
        for (int target = 0; target < targets; ++target) {
            const unsigned char* entry = data + header + target * stride;
            const int objectId = ReadI32(entry);
            const unsigned char* pursuit = nullptr;
            for (int extra = 0; extra < pursuitTargets; ++extra) {
                const auto* candidate = data + trailer + 5 + extra * pursuitStride;
                if (ReadI32(candidate) == objectId) pursuit = candidate + 5;
            }
            void* mob = pursuit ? FindMob(objectId) : nullptr;
            if (!mob) continue;
            AcquireSRWLockExclusive(&g_pendingLock);
            for (int line = copied; line < lines; ++line) {
                const int stored = ReadI32(pursuit + (line - copied) * 4);
                g_pending[g_nextPending] = {mob, objectId, ReadI32(entry + 5 + line * 4) & INT_MAX,
                    line, GetTickCount(), stored & INT_MAX, stored < 0, true, false, line + copied, true};
                g_nextPending = (g_nextPending + 1) % kPendingCount;
            }
            ReleaseSRWLockExclusive(&g_pendingLock);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
}

bool TrackServerDamage(int objectId, int damage, bool critical, int lineIndex, int pursuitLine) {
    if (!objectId || damage < 0) {
        return false;
    }
    bool readyToRender = false;
    const DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&g_pendingLock);
    // Replies follow outgoing packet order; consume the oldest unacknowledged matching line.
    for (int offset = 0; offset < kPendingCount; ++offset) {
        PendingDamage& pending = g_pending[(g_nextPending + offset) % kPendingCount];
        if (!IsLive(pending, now) || pending.remote || pending.objectId != objectId
            || pending.lineIndex != lineIndex || pending.serverReady) {
            continue;
        }
        if (pending.impacted) {
            readyToRender = true;
            pending = {};
        } else {
            pending.serverDamage = damage;
            pending.critical = critical;
            pending.serverReady = true;
            pending.pursuitLine = pursuitLine;
        }
        break;
    }
    ReleaseSRWLockExclusive(&g_pendingLock);
    return readyToRender;
}

LocalResult ResolveAtNativeImpact(void* mob, int damage, int lineIndex, int& displayedDamage, bool& critical, int& pursuitLine) {
    pursuitLine = 0;
    if (!mob || damage < 0) {
        return LocalResult::Unchanged;
    }
    LocalResult result = LocalResult::Unchanged;
    const DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&g_pendingLock);
    for (int offset = 0; offset < kPendingCount; ++offset) {
        PendingDamage& pending = g_pending[(g_nextPending + offset) % kPendingCount];
        if (!IsLive(pending, now) || pending.mob != mob || pending.localDamage != damage
            || pending.lineIndex != lineIndex || pending.impacted || pending.remote != (g_incomingDepth > 0)) {
            continue;
        }
        if (pending.serverReady) {
            displayedDamage = pending.serverDamage;
            critical = pending.critical;
            pursuitLine = pending.pursuitLine;
            result = pending.remote ? LocalResult::RemotePursuit : LocalResult::Resolved;
            pending = {};
        } else {
            pending.impacted = true;
            result = LocalResult::WaitForServer;
        }
        break;
    }
    ReleaseSRWLockExclusive(&g_pendingLock);
    return result;
}

void BeginIncomingPacket() { ++g_incomingDepth; }
void EndIncomingPacket() {
    if (g_incomingDepth > 0 && --g_incomingDepth == 0) {
        AcquireSRWLockExclusive(&g_pendingLock);
        for (PendingDamage& pending : g_pending) if (pending.remote) pending = {};
        ReleaseSRWLockExclusive(&g_pendingLock);
    }
}
void Reset() {
    AcquireSRWLockExclusive(&g_pendingLock);
    for (PendingDamage& pending : g_pending) pending = {};
    g_nextPending = 0;
    ReleaseSRWLockExclusive(&g_pendingLock);
}
}
