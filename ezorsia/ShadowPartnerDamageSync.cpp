#include "stdafx.h"
#include "ShadowPartnerDamageSync.h"
#include "ShadowPartnerAttack.h"

#include <climits>

namespace {
constexpr int kPendingCount = 128;
constexpr DWORD kPendingMs = 2500;
// GMS083 CMob::AddDamage, 0066B105..0066B128: all supported throwing
// skills use 120 ms per line. Its 60 ms exceptions are bow/crossbow skills.
constexpr DWORD kLineIntervalMs = 120;
constexpr int kMaxCopiedLines = 3;

struct DamageLine {
    int localDamage;
    int serverDamage;
    bool critical;
    bool serverReady;
    bool impacted;
    int pursuitLine;
    int x;
    int y;
    int compact;
};

struct PendingAttack {
    void* mob; // Identity only after impact; never dereferenced by delayed rendering.
    int objectId;
    int copied;
    DWORD recordedAt;
    unsigned long long serial;
    bool remote;
    bool delayed;
    int nextShadow;
    int nextPursuit;
    DWORD lastShownAt;
    DamageLine lines[kMaxCopiedLines];
};

PendingAttack g_pending[kPendingCount] = {};
int g_nextPending = 0;
unsigned long long g_nextSerial = 0;
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

bool ReadImpactPosition(void* mob, DamageLine& line) {
    // Same vector and getters as CMob::ShowDamage (006691D3). Snapshot it
    // while the native callback owns CMob, including its final death hit.
    __try {
        void* position = *reinterpret_cast<void**>(static_cast<unsigned char*>(mob) + 0x120);
        if (!position) return false;
        using GetCoordinate = int (__thiscall*)(void*);
        line.x = reinterpret_cast<GetCoordinate>(0x00403CB7)(position);
        line.y = reinterpret_cast<GetCoordinate>(0x00403CDE)(position);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool DamageResourcesReady(unsigned char* displayer, int first, int second) {
    return *reinterpret_cast<void**>(displayer + first)
        && *reinterpret_cast<void**>(displayer + second);
}

bool RenderLine(const DamageLine& line, int lineIndex) {
    // CMob may already have been removed. Reuse ShowDamage's native animation
    // calls with the impact position, never a retained raw CMob pointer.
    __try {
        auto* displayer = *reinterpret_cast<unsigned char**>(0x00BEBF6C);
        if (!displayer) return false;
        const int y = line.y - 15 - lineIndex * (line.compact ? 15 : 30);
        if (line.serverDamage == 0) {
            using ShowMiss = void (__thiscall*)(void*, int, int, int);
            reinterpret_cast<ShowMiss>(0x00438A21)(displayer, line.x, y, 0);
        } else {
            bool critical = line.critical;
            if (critical && !DamageResourcesReady(displayer, 0x188, 0x18C)) critical = false;
            if (!critical && !DamageResourcesReady(displayer, 0x170, 0x174)) return false;
            using ShowNumber = void (__thiscall*)(void*, int, int, int, int, int);
            reinterpret_cast<ShowNumber>(0x00437D0F)(displayer, line.x, y, line.serverDamage, 0, critical ? 1 : 0);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IsLive(PendingAttack& pending, DWORD now) {
    if (pending.mob && now - pending.recordedAt > kPendingMs) pending = {};
    return pending.mob != nullptr;
}

PendingAttack& AddAttack(void* mob, int objectId, int copied, bool remote) {
    PendingAttack& pending = g_pending[g_nextPending];
    pending = {};
    pending.mob = mob;
    pending.objectId = objectId;
    pending.copied = copied;
    pending.remote = remote;
    pending.recordedAt = GetTickCount();
    pending.serial = ++g_nextSerial;
    g_nextPending = (g_nextPending + 1) % kPendingCount;
    return pending;
}

void FinishIfComplete(PendingAttack& pending) {
    if (pending.nextShadow != pending.copied) return;
    while (pending.nextPursuit < pending.copied
        && pending.lines[pending.nextPursuit].pursuitLine == 0) ++pending.nextPursuit;
    if (pending.nextPursuit == pending.copied) pending = {};
}
}

namespace ShadowPartnerDamageSync {
void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size) {
    __try {
        if (!data || size < 30 || data[0] != 0x2D || data[1] != 0) return;
        const int targets = data[3] >> 4;
        const int lines = data[3] & 0x0F;
        const int copied = ShadowPartnerAttack::CopiedAttackCount(ReadI32(data + 4), lines);
        const unsigned long stride = 22 + lines * 4;
        if (copied == 0 || copied > kMaxCopiedLines || targets == 0 || size < 30 + targets * stride) return;
        for (int target = 0; target < targets; ++target) {
            const unsigned char* entry = data + 30 + target * stride;
            const int objectId = ReadI32(entry);
            void* mob = FindMob(objectId);
            if (!mob) continue;
            AcquireSRWLockExclusive(&g_pendingLock);
            PendingAttack& pending = AddAttack(mob, objectId, copied, false);
            for (int i = 0; i < copied; ++i) {
                pending.lines[i].localDamage = ReadI32(entry + 18 + (i + copied) * 4) & INT_MAX;
            }
            ReleaseSRWLockExclusive(&g_pendingLock);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
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
        if (!copied || copied > kMaxCopiedLines || !targets || size < trailer + 5 || ReadI32(data + trailer) != 0x31504853) return;
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
            PendingAttack& pending = AddAttack(mob, objectId, copied, true);
            for (int i = 0; i < copied; ++i) {
                DamageLine& line = pending.lines[i];
                const int stored = ReadI32(pursuit + i * 4);
                line.localDamage = ReadI32(entry + 5 + (i + copied) * 4) & INT_MAX;
                line.serverDamage = stored & INT_MAX;
                line.critical = stored < 0;
                line.serverReady = true;
                line.pursuitLine = i + lines;
            }
            ReleaseSRWLockExclusive(&g_pendingLock);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
}

void TrackServerDamage(int objectId, int damage, bool critical, int lineIndex, int pursuitLine) {
    if (!objectId || damage < 0) return;
    const DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&g_pendingLock);
    // Replies follow outgoing attack order. Keep the whole target attack so
    // later replies cannot bypass an earlier shadow or insert pursuits first.
    for (int offset = 0; offset < kPendingCount; ++offset) {
        PendingAttack& pending = g_pending[(g_nextPending + offset) % kPendingCount];
        if (!IsLive(pending, now) || pending.remote || pending.objectId != objectId) continue;
        const int i = lineIndex - pending.copied;
        if (i < 0 || i >= pending.copied || pending.lines[i].serverReady) continue;
        DamageLine& line = pending.lines[i];
        line.serverDamage = damage;
        line.critical = critical;
        line.serverReady = true;
        line.pursuitLine = pursuitLine == lineIndex + pending.copied ? pursuitLine : 0;
        break;
    }
    ReleaseSRWLockExclusive(&g_pendingLock);
}

LocalResult ResolveAtNativeImpact(void* mob, int damage, int lineIndex, int compact, int& displayedDamage, bool& critical) {
    if (!mob || damage < 0) return LocalResult::Unchanged;
    LocalResult result = LocalResult::Unchanged;
    const DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&g_pendingLock);
    for (int offset = 0; offset < kPendingCount; ++offset) {
        PendingAttack& pending = g_pending[(g_nextPending + offset) % kPendingCount];
        if (!IsLive(pending, now) || pending.mob != mob || pending.remote != (g_incomingDepth > 0)) continue;
        const int i = lineIndex - pending.copied;
        if (i < 0 || i >= pending.copied) continue;
        DamageLine& line = pending.lines[i];
        if (line.localDamage != damage || line.impacted) continue;
        if (!ReadImpactPosition(mob, line)) {
            pending = {}; // Fail open; never suppress native hits without a safe render position.
            break;
        }
        line.impacted = true;
        line.compact = compact;
        if (pending.remote) {
            // Observer originals remain native. Only append pursuits after all
            // copied callbacks; completed timelines outlive the packet scope.
            pending.lastShownAt = now;
            while (pending.nextShadow < pending.copied && pending.lines[pending.nextShadow].impacted) ++pending.nextShadow;
        } else if (line.serverReady && i == pending.nextShadow
            && (!pending.delayed || i == 0 || now - pending.lastShownAt >= kLineIntervalMs)) {
            displayedDamage = line.serverDamage;
            critical = line.critical;
            pending.lastShownAt = now;
            ++pending.nextShadow;
            result = LocalResult::Resolved;
        } else {
            pending.delayed = true;
            result = LocalResult::Deferred;
        }
        FinishIfComplete(pending);
        break;
    }
    ReleaseSRWLockExclusive(&g_pendingLock);
    return result;
}

void Update() {
    // One due line per target attack per update. Rebase on the actual draw time
    // so a late packet or stalled frame never dumps several overdue lines at once.
    for (int index = 0; index < kPendingCount; ++index) {
        const DWORD now = GetTickCount();
        DamageLine line{};
        unsigned long long serial = 0;
        int lineIndex = 0;
        bool pursuit = false;
        AcquireSRWLockExclusive(&g_pendingLock);
        PendingAttack& pending = g_pending[index];
        if (IsLive(pending, now)) {
            pursuit = pending.nextShadow == pending.copied;
            const int i = pursuit ? pending.nextPursuit : pending.nextShadow;
            if (i < pending.copied && (!pending.remote || pursuit)) {
                const DamageLine& candidate = pending.lines[i];
                const bool due = (!pursuit && i == 0) || now - pending.lastShownAt >= kLineIntervalMs;
                if (candidate.impacted && candidate.serverReady && due) {
                    serial = pending.serial;
                    line = candidate;
                    lineIndex = pursuit ? candidate.pursuitLine : i + pending.copied;
                }
            }
        }
        ReleaseSRWLockExclusive(&g_pendingLock);
        if (!serial || !RenderLine(line, lineIndex)) continue;
        AcquireSRWLockExclusive(&g_pendingLock);
        if (pending.serial == serial) {
            pending.lastShownAt = GetTickCount();
            if (pursuit) ++pending.nextPursuit;
            else ++pending.nextShadow;
            FinishIfComplete(pending);
        }
        ReleaseSRWLockExclusive(&g_pendingLock);
    }
}

void BeginIncomingPacket() { ++g_incomingDepth; }
void EndIncomingPacket() {
    if (g_incomingDepth > 0 && --g_incomingDepth == 0) {
        AcquireSRWLockExclusive(&g_pendingLock);
        for (PendingAttack& pending : g_pending) {
            if (pending.remote && pending.nextShadow != pending.copied) pending = {};
        }
        ReleaseSRWLockExclusive(&g_pendingLock);
    }
}
void Reset() {
    AcquireSRWLockExclusive(&g_pendingLock);
    for (PendingAttack& pending : g_pending) pending = {};
    g_nextPending = 0;
    ReleaseSRWLockExclusive(&g_pendingLock);
}
}
