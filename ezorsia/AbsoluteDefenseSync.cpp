#include "stdafx.h"
#include "AbsoluteDefenseSync.h"

#include <climits>

namespace {
constexpr unsigned short kOpcodeUpdateWeakenedAbsoluteDefense = 0x1004;
constexpr int kDamagePercent = 30;
constexpr DWORD kMobPoolPtr = 0x00BEBFA4;
constexpr DWORD kFindMobAddr = 0x00441AE8;
constexpr int kTrackedTargetCount = 256;

struct TrackedTarget {
    int objectId = 0;
    void* mob = nullptr;
};

using FindMob_t = void* (__thiscall*)(void* pThis, int objectId);
static FindMob_t g_FindMob = reinterpret_cast<FindMob_t>(kFindMobAddr);
static TrackedTarget g_targets[kTrackedTargetCount] = {};

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

static void UpdateTarget(int objectId, bool enabled) {
    int emptyIndex = -1;
    for (int i = 0; i < kTrackedTargetCount; ++i) {
        if (g_targets[i].objectId == objectId) {
            g_targets[i] = enabled ? TrackedTarget{objectId, FindMob(objectId)} : TrackedTarget{};
            return;
        }
        if (emptyIndex < 0 && g_targets[i].objectId == 0) {
            emptyIndex = i;
        }
    }

    if (enabled) {
        const int targetIndex = emptyIndex >= 0 ? emptyIndex : objectId % kTrackedTargetCount;
        g_targets[targetIndex] = {objectId, FindMob(objectId)};
    }
}
}

namespace AbsoluteDefenseSync {
bool HandlePacket(const unsigned char* data, unsigned long size) {
    __try {
        if (data == nullptr || size < 6 || ReadU16(data + 4) != kOpcodeUpdateWeakenedAbsoluteDefense) {
            return false;
        }
        if (size < 11) {
            return true;
        }

        UpdateTarget(ReadI32(data + 6), data[10] != 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

bool TryScaleDisplayedDamage(void* mob, int damage, int& scaledDamage) {
    scaledDamage = damage;
    if (mob == nullptr || damage <= 0) {
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
            const long long scaled = static_cast<long long>(damage) * kDamagePercent / 100;
            scaledDamage = static_cast<int>(max(1LL, min(scaled, static_cast<long long>(INT_MAX - 1))));
            return true;
        }
    }
    return false;
}

void Reset() {
    for (int i = 0; i < kTrackedTargetCount; ++i) {
        g_targets[i] = {};
    }
}
}
