#include "stdafx.h"
#include "IntegratedFinalAttack.h"

#include <algorithm>
#include <climits>
#include <cstddef>

namespace {
constexpr unsigned short kOpcodeCloseRangeAttack = 0x002C;
constexpr unsigned short kOpcodeUpdateIntegratedFinalAttack = 0x1003;
constexpr unsigned char kSettingsVersion = 3;
constexpr DWORD kMobPoolPtr = 0x00BEBFA4;
constexpr DWORD kFindMobAddr = 0x00441AE8;
constexpr DWORD kPendingDamageTtlMs = 2500;
constexpr int kPendingDamageCount = 64;

constexpr int kHeroBrandish = 1121008;
constexpr int kSpearCrusher = 1311001;
constexpr int kPoleArmCrusher = 1311002;
constexpr int kSpearFury = 1311003;
constexpr int kPoleArmFury = 1311004;
constexpr int kChargedBlow = 1211002;
constexpr int kPaladinBlast = 1221009;

constexpr unsigned char kWeaponSword = 1;
constexpr unsigned char kWeaponAxe = 2;
constexpr unsigned char kWeaponBluntWeapon = 5;

struct CInPacket {
    int Loopback;
    int State;
    void* Data;
    unsigned short DataLen;
    unsigned short RawSeq;
    unsigned int Unknown;
    unsigned int Offset;
};
static_assert(offsetof(CInPacket, DataLen) == 0x0C, "Unexpected CInPacket data length offset");

struct PassiveSettings {
    int probability = 0;
    int damage = 0;
};

struct Settings {
    PassiveSettings sword;
    PassiveSettings axe;
    PassiveSettings spear;
    PassiveSettings poleArm;
    PassiveSettings pageSword;
    PassiveSettings pageBluntWeapon;
    int brandishDamage = 0;
    int spearCrusherDamage = 0;
    int poleArmCrusherDamage = 0;
    int spearFuryDamage = 0;
    int poleArmFuryDamage = 0;
    int chargedBlowDamage = 0;
    int paladinBlastDamage = 0;
    unsigned char weaponType = 0;
    bool ready = false;
};

struct PendingDamage {
    void* mob = nullptr;
    int originalDamage = 0;
    int additionalDamage = 0;
    int lineIndex = 0;
    DWORD expiresAt = 0;
};

using FindMob_t = void* (__thiscall*)(void* pThis, int objectId);
FindMob_t g_findMob = reinterpret_cast<FindMob_t>(kFindMobAddr);
Settings g_settings;
PendingDamage g_pendingDamage[kPendingDamageCount] = {};
int g_nextPendingDamage = 0;
unsigned int g_randomState = 0;

unsigned short ReadU16(const unsigned char* ptr) {
    return static_cast<unsigned short>(ptr[0] | (ptr[1] << 8));
}

int ReadI32(const unsigned char* ptr) {
    return static_cast<int>(
        static_cast<unsigned int>(ptr[0]) |
        (static_cast<unsigned int>(ptr[1]) << 8) |
        (static_cast<unsigned int>(ptr[2]) << 16) |
        (static_cast<unsigned int>(ptr[3]) << 24));
}

void WriteI32(unsigned char* ptr, int value) {
    const unsigned int raw = static_cast<unsigned int>(value);
    ptr[0] = static_cast<unsigned char>(raw & 0xFF);
    ptr[1] = static_cast<unsigned char>((raw >> 8) & 0xFF);
    ptr[2] = static_cast<unsigned char>((raw >> 16) & 0xFF);
    ptr[3] = static_cast<unsigned char>((raw >> 24) & 0xFF);
}

bool TryReadDword(DWORD address, DWORD& out) {
    __try {
        out = *reinterpret_cast<DWORD*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

void* FindTargetMob(int objectId) {
    DWORD mobPool = 0;
    if (!TryReadDword(kMobPoolPtr, mobPool) || mobPool == 0) {
        return nullptr;
    }
    return g_findMob(reinterpret_cast<void*>(mobPool), objectId);
}

unsigned int NextRandom() {
    if (g_randomState == 0) {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        g_randomState = static_cast<unsigned int>(counter.LowPart)
            ^ GetTickCount()
            ^ GetCurrentThreadId()
            ^ static_cast<unsigned int>(reinterpret_cast<ULONG_PTR>(&g_randomState));
        if (g_randomState == 0) {
            g_randomState = 0xA341316C;
        }
    }

    unsigned int value = g_randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    g_randomState = value;
    return value;
}

bool Roll(int probability) {
    if (probability <= 0) {
        return false;
    }
    return probability >= 100 || static_cast<int>(NextRandom() % 100) < probability;
}

int DecodeDamage(int rawDamage) {
    return rawDamage < 0 ? rawDamage + INT_MAX : rawDamage;
}

int EncodeDamage(int damage, bool critical) {
    return critical ? damage - INT_MAX : damage;
}

void ClearExpiredPendingDamage(DWORD now) {
    for (int i = 0; i < kPendingDamageCount; ++i) {
        PendingDamage& pending = g_pendingDamage[i];
        if (pending.mob != nullptr && static_cast<int>(pending.expiresAt - now) <= 0) {
            pending.mob = nullptr;
        }
    }
}

void AddPendingDamage(void* mob, int originalDamage, int additionalDamage, int lineIndex) {
    if (mob == nullptr || originalDamage <= 0 || additionalDamage <= 0) {
        return;
    }

    const DWORD now = GetTickCount();
    ClearExpiredPendingDamage(now);
    g_pendingDamage[g_nextPendingDamage] = {
        mob,
        originalDamage,
        additionalDamage,
        lineIndex,
        now + kPendingDamageTtlMs
    };
    g_nextPendingDamage = (g_nextPendingDamage + 1) % kPendingDamageCount;
}

bool GetAttackSettings(int skillId, PassiveSettings& passive, int& mainSkillDamage) {
    if (!g_settings.ready) {
        return false;
    }

    switch (skillId) {
    case kHeroBrandish:
        if (g_settings.weaponType == kWeaponSword) {
            passive = g_settings.sword;
        } else if (g_settings.weaponType == kWeaponAxe) {
            passive = g_settings.axe;
        } else {
            return false;
        }
        mainSkillDamage = g_settings.brandishDamage;
        return true;
    case kSpearCrusher:
        passive = g_settings.spear;
        mainSkillDamage = g_settings.spearCrusherDamage;
        return true;
    case kPoleArmCrusher:
        passive = g_settings.poleArm;
        mainSkillDamage = g_settings.poleArmCrusherDamage;
        return true;
    case kSpearFury:
        passive = g_settings.spear;
        mainSkillDamage = g_settings.spearFuryDamage;
        return true;
    case kPoleArmFury:
        passive = g_settings.poleArm;
        mainSkillDamage = g_settings.poleArmFuryDamage;
        return true;
    case kChargedBlow:
        if (g_settings.weaponType == kWeaponSword) {
            passive = g_settings.pageSword;
        } else if (g_settings.weaponType == kWeaponBluntWeapon) {
            passive = g_settings.pageBluntWeapon;
        } else {
            return false;
        }
        mainSkillDamage = g_settings.chargedBlowDamage;
        return true;
    case kPaladinBlast:
        if (g_settings.weaponType == kWeaponSword) {
            passive = g_settings.pageSword;
        } else if (g_settings.weaponType == kWeaponBluntWeapon) {
            passive = g_settings.pageBluntWeapon;
        } else {
            return false;
        }
        mainSkillDamage = g_settings.paladinBlastDamage;
        return true;
    default:
        return false;
    }
}

bool IsMatchingDamage(int actual, int expected) {
    const int difference = actual - expected;
    return difference >= -1 && difference <= 1;
}
}

namespace IntegratedFinalAttack {
bool HandlePacket(void* rawPacket) {
    CInPacket* packet = reinterpret_cast<CInPacket*>(rawPacket);
    if (packet == nullptr) {
        return false;
    }

    __try {
        if (packet->Data == nullptr || packet->DataLen < 6) {
            return false;
        }
        const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
        if (ReadU16(data + 4) != kOpcodeUpdateIntegratedFinalAttack) {
            return false;
        }
        if (packet->DataLen < 40 || data[6] != kSettingsVersion) {
            g_settings = Settings{};
            return true;
        }

        Settings settings;
        settings.sword = { data[7], ReadU16(data + 8) };
        settings.axe = { data[10], ReadU16(data + 11) };
        settings.spear = { data[13], ReadU16(data + 14) };
        settings.poleArm = { data[16], ReadU16(data + 17) };
        settings.pageSword = { data[19], ReadU16(data + 20) };
        settings.pageBluntWeapon = { data[22], ReadU16(data + 23) };
        settings.brandishDamage = ReadU16(data + 25);
        settings.spearCrusherDamage = ReadU16(data + 27);
        settings.poleArmCrusherDamage = ReadU16(data + 29);
        settings.spearFuryDamage = ReadU16(data + 31);
        settings.poleArmFuryDamage = ReadU16(data + 33);
        settings.chargedBlowDamage = ReadU16(data + 35);
        settings.paladinBlastDamage = ReadU16(data + 37);
        settings.weaponType = data[39];
        settings.ready = true;
        g_settings = settings;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_settings = Settings{};
        return true;
    }
}

bool BuildOutgoingAttackPacket(const unsigned char* data, unsigned long size, std::vector<unsigned char>& output) {
    output.clear();
    __try {
        if (data == nullptr || size < 47 || ReadU16(data) != kOpcodeCloseRangeAttack) {
            return false;
        }

        const int skillId = ReadI32(data + 4);
        PassiveSettings passive;
        int mainSkillDamage = 0;
        if (!GetAttackSettings(skillId, passive, mainSkillDamage)
            || passive.damage <= 0
            || mainSkillDamage <= 0
            || !Roll(passive.probability)) {
            return false;
        }

        const unsigned char numAttackedAndDamage = data[3];
        const int numAttacked = (numAttackedAndDamage >> 4) & 0x0F;
        const int numDamage = numAttackedAndDamage & 0x0F;
        if (numAttacked <= 0 || numDamage <= 0 || numDamage >= 0x0F) {
            return false;
        }

        constexpr unsigned long targetBase = 25;
        constexpr unsigned long damageBaseInTarget = 18;
        constexpr unsigned long targetFooterSize = 4;
        const unsigned long targetStride = damageBaseInTarget
            + static_cast<unsigned long>(numDamage) * 4
            + targetFooterSize;
        const unsigned long targetsEnd = targetBase
            + targetStride * static_cast<unsigned long>(numAttacked);
        if (targetsEnd > size) {
            return false;
        }

        const unsigned long damageOffset = targetBase
            + damageBaseInTarget
            + static_cast<unsigned long>(numDamage - 1) * 4;
        if (damageOffset + 4 > size) {
            return false;
        }

        void* mob = FindTargetMob(ReadI32(data + targetBase));
        if (mob == nullptr) {
            return false;
        }

        const int rawDamage = ReadI32(data + damageOffset);
        const bool critical = rawDamage < 0;
        const int originalDamage = DecodeDamage(rawDamage);
        if (originalDamage <= 0) {
            return false;
        }

        long long extraDamage = (static_cast<long long>(originalDamage) * passive.damage
            + mainSkillDamage / 2) / mainSkillDamage;
        if (extraDamage >= INT_MAX) {
            extraDamage = INT_MAX - 1;
        }
        if (extraDamage <= 0) {
            return false;
        }

        const int additionalDamage = static_cast<int>(extraDamage);
        const unsigned long addedBytes = static_cast<unsigned long>(numAttacked) * 4;
        output.resize(size + addedBytes);
        std::copy(data, data + targetBase, output.begin());
        output[3] = static_cast<unsigned char>((numAttacked << 4) | (numDamage + 1));

        unsigned long sourceOffset = targetBase;
        unsigned long outputOffset = targetBase;
        const unsigned long targetDataSize = targetStride - targetFooterSize;
        for (int i = 0; i < numAttacked; ++i) {
            std::copy(
                data + sourceOffset,
                data + sourceOffset + targetDataSize,
                output.begin() + outputOffset);
            outputOffset += targetDataSize;
            WriteI32(
                output.data() + outputOffset,
                i == 0 ? EncodeDamage(additionalDamage, critical) : 0);
            outputOffset += 4;
            std::copy(
                data + sourceOffset + targetDataSize,
                data + sourceOffset + targetStride,
                output.begin() + outputOffset);
            outputOffset += targetFooterSize;
            sourceOffset += targetStride;
        }
        std::copy(data + targetsEnd, data + size, output.begin() + outputOffset);

        AddPendingDamage(mob, originalDamage, additionalDamage, numDamage - 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        output.clear();
        return false;
    }
}

bool TakeAdditionalDisplayedDamage(void* mob, int damage, int lineIndex, int& additionalDamage) {
    additionalDamage = 0;
    if (mob == nullptr || damage <= 0) {
        return false;
    }

    const DWORD now = GetTickCount();
    ClearExpiredPendingDamage(now);
    for (int i = 0; i < kPendingDamageCount; ++i) {
        PendingDamage& pending = g_pendingDamage[i];
        if (pending.mob == mob
            && pending.lineIndex == lineIndex
            && IsMatchingDamage(damage, pending.originalDamage)) {
            additionalDamage = pending.additionalDamage;
            pending.mob = nullptr;
            return true;
        }
    }
    return false;
}
}
