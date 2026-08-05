#include "stdafx.h"
#include "AbsoluteDefenseSync.h"

namespace {
constexpr unsigned short kOpcodeUpdateWeakenedAbsoluteDefense = 0x1004;
constexpr unsigned short kRecvCloseRangeAttack = 0x002C;
constexpr unsigned short kRecvRangedAttack = 0x002D;
constexpr unsigned short kRecvMagicAttack = 0x002E;
constexpr unsigned short kSendCloseRangeAttack = 0x00BA;
constexpr unsigned short kSendRangedAttack = 0x00BB;
constexpr unsigned short kSendMagicAttack = 0x00BC;
constexpr DWORD kOutgoingAttackTtlMs = 1200;
constexpr DWORD kMobPoolPtr = 0x00BEBFA4;
constexpr DWORD kFindMobAddr = 0x00441AE8;
constexpr DWORD kMobStatOffset = 0x1A0;
constexpr int kTrackedTargetCount = 256;
static DWORD g_weaponImmunityNormalDamage = 0x0078E4A8;
static DWORD g_weaponImmunityBlockedDamage = 0x0078E4CB;
static DWORD g_magicImmunityNormalDamage = 0x007918D1;
static DWORD g_magicImmunityBlockedDamage = 0x007918C3;

enum class AttackKind {
    Unknown,
    Weapon,
    Magic
};

struct TrackedTarget {
    int objectId = 0;
    void* mob = nullptr;
    unsigned char weaponDamagePercent = 0;
    unsigned char magicDamagePercent = 0;
};

using FindMob_t = void* (__thiscall*)(void* pThis, int objectId);
static FindMob_t g_FindMob = reinterpret_cast<FindMob_t>(kFindMobAddr);
static TrackedTarget g_targets[kTrackedTargetCount] = {};
static AttackKind g_outgoingAttackKind = AttackKind::Unknown;
static DWORD g_outgoingAttackTick = 0;
static AttackKind g_incomingAttackKind = AttackKind::Unknown;

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

static void* FindMob(int objectId) {
    __try {
        DWORD mobPool = *reinterpret_cast<DWORD*>(kMobPoolPtr);
        if (mobPool == 0) {
            return nullptr;
        }
        return g_FindMob(reinterpret_cast<void*>(mobPool), objectId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static AttackKind GetAttackKind(unsigned short opcode, bool outgoing) {
    if (opcode == (outgoing ? kRecvMagicAttack : kSendMagicAttack)) {
        return AttackKind::Magic;
    }
    if (opcode == (outgoing ? kRecvCloseRangeAttack : kSendCloseRangeAttack)
        || opcode == (outgoing ? kRecvRangedAttack : kSendRangedAttack)) {
        return AttackKind::Weapon;
    }
    return AttackKind::Unknown;
}

static void UpdateTarget(int objectId, unsigned char weaponPercent, unsigned char magicPercent) {
    const bool enabled = weaponPercent > 0 || magicPercent > 0;
    int emptyIndex = -1;
    for (int i = 0; i < kTrackedTargetCount; ++i) {
        if (g_targets[i].objectId == objectId) {
            g_targets[i] = enabled
                ? TrackedTarget{objectId, FindMob(objectId), weaponPercent, magicPercent}
                : TrackedTarget{};
            return;
        }
        if (emptyIndex < 0 && g_targets[i].objectId == 0) {
            emptyIndex = i;
        }
    }

    if (enabled) {
        const int targetIndex = emptyIndex >= 0 ? emptyIndex : static_cast<unsigned int>(objectId) % kTrackedTargetCount;
        g_targets[targetIndex] = {objectId, FindMob(objectId), weaponPercent, magicPercent};
    }
}

static AttackKind GetCurrentAttackKind() {
    if (g_incomingAttackKind != AttackKind::Unknown) {
        return g_incomingAttackKind;
    }
    if (g_outgoingAttackKind != AttackKind::Unknown
        && GetTickCount() - g_outgoingAttackTick <= kOutgoingAttackTtlMs) {
        return g_outgoingAttackKind;
    }
    return AttackKind::Unknown;
}

static int GetCurrentDamagePercent(const TrackedTarget& target) {
    const AttackKind kind = GetCurrentAttackKind();
    if (kind == AttackKind::Magic) {
        return target.magicDamagePercent;
    }
    if (kind == AttackKind::Weapon) {
        return target.weaponDamagePercent;
    }
    return 0;
}

static bool __stdcall ShouldBypassImmunity(void* mobStat, int magic) {
    if (mobStat == nullptr) {
        return false;
    }

    void* mob = reinterpret_cast<unsigned char*>(mobStat) - kMobStatOffset;
    const AttackKind kind = magic != 0 ? AttackKind::Magic : AttackKind::Weapon;
    for (int i = 0; i < kTrackedTargetCount; ++i) {
        TrackedTarget& target = g_targets[i];
        if (target.objectId == 0) {
            continue;
        }
        if (target.mob == nullptr) {
            target.mob = FindMob(target.objectId);
        }
        if (target.mob == mob
            && (magic != 0 ? target.magicDamagePercent : target.weaponDamagePercent) > 0) {
            g_outgoingAttackKind = kind;
            g_outgoingAttackTick = GetTickCount();
            return true;
        }
    }
    return false;
}

__declspec(naked) void WeaponImmunityBypassCave() {
    __asm {
        cmp dword ptr[ebp - 2Ch], eax
        pop ecx
        pop ecx
        jle normalDamage

        pushad
        push 0
        push dword ptr[ebp + 18h]
        call ShouldBypassImmunity
        test eax, eax
        popad
        jnz normalDamage
        jmp dword ptr[g_weaponImmunityBlockedDamage]

    normalDamage:
        jmp dword ptr[g_weaponImmunityNormalDamage]
    }
}

__declspec(naked) void MagicImmunityBypassCave() {
    __asm {
        cmp ebx, eax
        pop ecx
        pop ecx
        jle normalDamage

        pushad
        push 1
        push dword ptr[ebp + 18h]
        call ShouldBypassImmunity
        test eax, eax
        popad
        jnz normalDamage
        jmp dword ptr[g_magicImmunityBlockedDamage]

    normalDamage:
        jmp dword ptr[g_magicImmunityNormalDamage]
    }
}
}

namespace AbsoluteDefenseSync {
void InstallImmunityBypassHooks() {
    Memory::CodeCave(WeaponImmunityBypassCave, 0x0078E4A1, 7);
    Memory::CodeCave(MagicImmunityBypassCave, 0x007918BD, 6);
}

bool HandlePacket(const unsigned char* data, unsigned long size) {
    __try {
        if (data == nullptr || size < 6 || ReadU16(data + 4) != kOpcodeUpdateWeakenedAbsoluteDefense) {
            return false;
        }
        if (size < 12) {
            return true;
        }

        UpdateTarget(ReadI32(data + 6), data[10], data[11]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size) {
    if (data == nullptr || size < 2) {
        return;
    }
    const AttackKind kind = GetAttackKind(ReadU16(data), true);
    if (kind != AttackKind::Unknown) {
        g_outgoingAttackKind = kind;
        g_outgoingAttackTick = GetTickCount();
    }
}

void BeginIncomingAttackPacket(const unsigned char* data, unsigned long size) {
    g_incomingAttackKind = data != nullptr && size >= 6
        ? GetAttackKind(ReadU16(data + 4), false)
        : AttackKind::Unknown;
}

void EndIncomingAttackPacket() {
    g_incomingAttackKind = AttackKind::Unknown;
}

bool ShouldSuppressLocalDamage(void* mob) {
    if (mob == nullptr) {
        return false;
    }

    for (int i = 0; i < kTrackedTargetCount; ++i) {
        TrackedTarget& target = g_targets[i];
        if (target.objectId == 0) {
            continue;
        }
        if (target.mob == nullptr) {
            target.mob = FindMob(target.objectId);
        }
        if (target.mob == mob) {
            return GetCurrentDamagePercent(target) > 0;
        }
    }
    return false;
}

void Reset() {
    for (int i = 0; i < kTrackedTargetCount; ++i) {
        g_targets[i] = {};
    }
    g_outgoingAttackKind = AttackKind::Unknown;
    g_outgoingAttackTick = 0;
    g_incomingAttackKind = AttackKind::Unknown;
}
}
