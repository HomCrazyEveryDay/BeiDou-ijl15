#include "stdafx.h"
#include "SnipeDamageSync.h"

namespace {
constexpr unsigned short kOpcodeRangedAttack = 0x002D;
constexpr int kMarksmanSnipe = 3221007;
constexpr int kMarksmanPiercingArrow = 3221001;
constexpr DWORD kMobPoolPtr = 0x00BEBFA4;
constexpr DWORD kFindMobAddr = 0x00441AE8;
constexpr DWORD kPendingMs = 2500;
constexpr int kPendingCount = 64;

struct PendingLocalDamage {
    void* mob;
    int skillId;
    int damage;
    DWORD expiresAt;
};

using FindMob_t = void* (__thiscall*)(void* pThis, int objectId);
static FindMob_t g_FindMob = reinterpret_cast<FindMob_t>(kFindMobAddr);
static PendingLocalDamage g_pending[kPendingCount] = {};
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

static void AddPendingDamage(void* mob, int skillId, int damage) {
    if (mob == nullptr || damage <= 0) {
        return;
    }

    const DWORD now = GetTickCount();
    g_pending[g_nextPending] = { mob, skillId, damage, now + kPendingMs };
    g_nextPending = (g_nextPending + 1) % kPendingCount;
}

static void* FindTargetMob(int objectId) {
    DWORD mobPool = 0;
    if (!TryReadDword(kMobPoolPtr, mobPool) || mobPool == 0) {
        return nullptr;
    }

    return g_FindMob(reinterpret_cast<void*>(mobPool), objectId);
}
}

namespace SnipeDamageSync {
void TrackOutgoingAttackPacket(unsigned char* data, unsigned long size) {
    __try {
        if (data == nullptr || size < 52) {
            return;
        }
        if (ReadU16(data) != kOpcodeRangedAttack) {
            return;
        }
        const int skillId = ReadI32(data + 4);
        if (skillId != kMarksmanSnipe && skillId != kMarksmanPiercingArrow) {
            return;
        }

        const unsigned char numAttackedAndDamage = data[3];
        const int numAttacked = (numAttackedAndDamage >> 4) & 0x0F;
        const int numDamage = numAttackedAndDamage & 0x0F;
        if (numAttacked <= 0 || numDamage <= 0) {
            return;
        }

        const bool piercingArrow = skillId == kMarksmanPiercingArrow;
        const unsigned long targetBase = piercingArrow ? 34 : 30;
        const unsigned long targetStride = 22 + static_cast<unsigned long>(numDamage) * 4;
        for (int i = 0; i < numAttacked; ++i) {
            const unsigned long targetOffset = targetBase + targetStride * static_cast<unsigned long>(i);
            if (targetOffset + 4 > size) {
                return;
            }
            void* mob = FindTargetMob(ReadI32(data + targetOffset));
            const unsigned long damageBase = targetOffset + 18;
            for (int j = 0; j < numDamage; ++j) {
                const unsigned long damageOffset = damageBase + static_cast<unsigned long>(j) * 4;
                if (damageOffset + 4 > size) {
                    return;
                }

                const int localDamage = ReadI32(data + damageOffset);
                AddPendingDamage(mob, skillId, localDamage);
            }
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
        PendingLocalDamage& pending = g_pending[i];
        if (pending.mob == nullptr) {
            continue;
        }
        if (static_cast<int>(pending.expiresAt - now) <= 0) {
            pending.mob = nullptr;
            continue;
        }
        if (pending.mob == mob && pending.damage == damage) {
            pending.mob = nullptr;
            return true;
        }
    }
    return false;
}
}
