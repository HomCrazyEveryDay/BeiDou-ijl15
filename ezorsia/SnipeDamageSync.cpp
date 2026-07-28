#include "stdafx.h"
#include "SnipeDamageSync.h"

namespace {
constexpr unsigned short kOpcodeRangedAttack = 0x002D;
constexpr int kMarksmanSnipe = 3221007;
constexpr DWORD kMobPoolPtr = 0x00BEBFA4;
constexpr DWORD kFindMobAddr = 0x00441AE8;
constexpr DWORD kPendingMs = 2500;
constexpr int kPendingCount = 8;

struct PendingSnipeTarget {
    void* mob;
    DWORD expiresAt;
};

using FindMob_t = void* (__thiscall*)(void* pThis, int objectId);
static FindMob_t g_FindMob = reinterpret_cast<FindMob_t>(kFindMobAddr);
static PendingSnipeTarget g_pending[kPendingCount] = {};
static int g_nextPending = 0;

static unsigned short ReadU16(const unsigned char* ptr) {
    return static_cast<unsigned short>(ptr[0] | (ptr[1] << 8));
}

static int ReadI32(const unsigned char* ptr) {
    return static_cast<int>(
        static_cast<unsigned int>(ptr[0]) |
        (static_cast<unsigned int>(ptr[1]) << 8) |
        (static_cast<unsigned int>(ptr[2]) << 16) |
        (static_cast<unsigned int>(ptr[3]) << 24));
}

static bool TryReadDword(DWORD address, DWORD& out) {
    __try {
        out = *reinterpret_cast<DWORD*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

static void AddPendingMob(void* mob) {
    if (mob == nullptr) {
        return;
    }

    const DWORD now = GetTickCount();
    g_pending[g_nextPending] = { mob, now + kPendingMs };
    g_nextPending = (g_nextPending + 1) % kPendingCount;
}

static void TrackTargetOid(int objectId) {
    DWORD mobPool = 0;
    if (!TryReadDword(kMobPoolPtr, mobPool) || mobPool == 0) {
        return;
    }

    void* mob = g_FindMob(reinterpret_cast<void*>(mobPool), objectId);
    AddPendingMob(mob);
}
}

namespace SnipeDamageSync {
void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size) {
    __try {
        if (data == nullptr || size < 52) {
            return;
        }
        if (ReadU16(data) != kOpcodeRangedAttack) {
            return;
        }
        if (ReadI32(data + 4) != kMarksmanSnipe) {
            return;
        }

        const unsigned char numAttackedAndDamage = data[3];
        const int numAttacked = (numAttackedAndDamage >> 4) & 0x0F;
        const int numDamage = numAttackedAndDamage & 0x0F;
        if (numAttacked <= 0 || numDamage <= 0) {
            return;
        }

        const unsigned long targetBase = 30;
        const unsigned long targetStride = 18 + static_cast<unsigned long>(numDamage) * 4;
        for (int i = 0; i < numAttacked; ++i) {
            const unsigned long targetOffset = targetBase + targetStride * static_cast<unsigned long>(i);
            if (targetOffset + 4 > size) {
                return;
            }
            TrackTargetOid(ReadI32(data + targetOffset));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

bool ShouldSuppressLocalDamage(void* mob, int damage) {
    if (mob == nullptr || damage <= 0) {
        return false;
    }

    const DWORD now = GetTickCount();
    for (int i = 0; i < kPendingCount; ++i) {
        PendingSnipeTarget& pending = g_pending[i];
        if (pending.mob == nullptr) {
            continue;
        }
        if (static_cast<int>(pending.expiresAt - now) <= 0) {
            pending.mob = nullptr;
            continue;
        }
        if (pending.mob == mob) {
            pending.mob = nullptr;
            return true;
        }
    }
    return false;
}
}
