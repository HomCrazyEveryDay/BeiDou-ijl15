#include "stdafx.h"
#include "AbsoluteDefenseSync.h"
#include "HpMpAlert.h"
#include "CrashReporter.h"
#include "IntegratedFinalAttack.h"
#include "SnipeDamageSync.h"
#include "StackedBuffIcons.h"

#include <cstddef>
#include <vector>

namespace {
constexpr DWORD kSaveGlobalAddr = 0x0049C8E7;
constexpr DWORD kUIStatusBarPtr = 0x00BEBF9C;
constexpr DWORD kHpAlertOffset = 0x80;
constexpr DWORD kMpAlertOffset = 0x84;
constexpr DWORD kClientSocketPtr = 0x00BE7914;
constexpr DWORD kProcessPacketAddr = 0x004965F1;
constexpr DWORD kMobPoolPtr = 0x00BEBFA4;
constexpr DWORD kFindMobAddr = 0x00441AE8;
constexpr DWORD kShowMobDamageAddr = 0x006691D3;
constexpr DWORD kAnimationDisplayerPtr = 0x00BEBF6C;
constexpr DWORD kNormalDamageResourceOffset = 0x170;
constexpr DWORD kNormalDamageResource2Offset = 0x174;
constexpr DWORD kCriticalDamageResourceOffset = 0x188;
constexpr DWORD kCriticalDamageResource2Offset = 0x18C;
constexpr WORD kOpcodeSetHpMpAlert = 0x1000;
constexpr WORD kOpcodeShowMobDamage = 0x1001;
constexpr WORD kOpcodeRemovePlayer = 0x00A1;
constexpr WORD kOpcodeMovePet = 0x00AA;
constexpr WORD kOpcodeMovePlayer = 0x00B9;
constexpr WORD kOpcodeCloseRangeAttack = 0x00BA;
constexpr WORD kOpcodeRangedAttack = 0x00BB;
constexpr WORD kOpcodeMagicAttack = 0x00BC;
constexpr WORD kOpcodeDamagePlayer = 0x00C0;
constexpr WORD kOpcodeSpawnMonster = 0x00EC;
constexpr WORD kOpcodeKillMonster = 0x00ED;
constexpr WORD kOpcodeSpawnMonsterControl = 0x00EE;
constexpr WORD kOpcodeMoveMonster = 0x00EF;
constexpr WORD kOpcodeMoveMonsterResponse = 0x00F0;
constexpr WORD kOpcodeApplyMonsterStatus = 0x00F2;
constexpr WORD kOpcodeCancelMonsterStatus = 0x00F3;
constexpr WORD kOpcodeDamageMonster = 0x00F6;
constexpr unsigned int kBossVenomVisualMask = 0x01000000;
constexpr int kBossVenomVisualDamage = 1;
constexpr int kBossVenomTargetCount = 64;
constexpr DWORD kQueuedDamageTtlMs = 1000;
constexpr size_t kMaxQueuedMobDamage = 128;
// Spread large multi-target bursts across frames so native damage layers remain visually distinct.
constexpr size_t kMaxMobDamagePerFrame = 4;
struct COutPacket {
    int Loopback;
    union {
        unsigned char* Data;
        void* Unk;
        unsigned short* Header;
    };
    unsigned long Size;
    unsigned int Offset;
    int EncryptedByShanda;
};
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
static_assert(offsetof(CInPacket, Offset) == 0x14, "Unexpected CInPacket read offset");

struct QueuedMobDamage {
    int objectId = 0;
    int damage = 0;
    bool critical = false;
    int lineIndex = 0;
    LONG sequence = 0;
    DWORD fieldGeneration = 0;
    DWORD queuedFrame = 0;
    DWORD queuedAt = 0;
    unsigned int retryCount = 0;
};
struct BossVenomVisualTarget {
    int objectId = 0;
    void* mob = nullptr;
};
using SendPacket_t = void(__fastcall*)(void* pThis, void* edx, COutPacket* packet);
static SendPacket_t g_SendPacket = reinterpret_cast<SendPacket_t>(0x0049637B);
using FindMob_t = void* (__thiscall*)(void* pThis, int objectId);
static FindMob_t g_FindMob = reinterpret_cast<FindMob_t>(kFindMobAddr);
using ShowMobDamage_t = void(__fastcall*)(void* pThis, void* edx, int damage, int lineIndex, int extra, int compact);
static ShowMobDamage_t g_ShowMobDamage = reinterpret_cast<ShowMobDamage_t>(kShowMobDamageAddr);
static bool g_renderingServerMobDamage = false;
static volatile LONG g_showMobDamagePacketCount = 0;
static CRITICAL_SECTION g_mobDamageQueueLock;
static bool g_mobDamageQueueLockInitialized = false;
static bool g_mobDamageFieldActive = false;
static DWORD g_mobDamageFieldGeneration = 0;
static DWORD g_mobDamageFrame = 0;
static std::vector<QueuedMobDamage> g_mobDamageQueue;
static BossVenomVisualTarget g_bossVenomTargets[kBossVenomTargetCount] = {};
static bool TryReadDword(DWORD address, DWORD& out) {
    __try {
        out = *reinterpret_cast<DWORD*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}
static bool IsQueuedMobDamageExpired(const QueuedMobDamage& queued, DWORD now) {
    return now - queued.queuedAt > kQueuedDamageTtlMs;
}
static bool ShouldPersistMobDamageSample(LONG sequence) {
    return sequence <= 16 || sequence % 64 == 0;
}
static bool ReadDamageResources(bool critical, DWORD& displayer, DWORD& resource1, DWORD& resource2) {
    displayer = 0;
    resource1 = 0;
    resource2 = 0;
    if (!TryReadDword(kAnimationDisplayerPtr, displayer) || displayer == 0) {
        return false;
    }

    const DWORD firstOffset = critical ? kCriticalDamageResourceOffset : kNormalDamageResourceOffset;
    const DWORD secondOffset = critical ? kCriticalDamageResource2Offset : kNormalDamageResource2Offset;
    return TryReadDword(displayer + firstOffset, resource1)
        && TryReadDword(displayer + secondOffset, resource2)
        && resource1 != 0
        && resource2 != 0;
}
static bool IsCurrentMobDamageField(DWORD fieldGeneration) {
    if (!g_mobDamageQueueLockInitialized) {
        return false;
    }

    EnterCriticalSection(&g_mobDamageQueueLock);
    const bool current = g_mobDamageFieldActive && g_mobDamageFieldGeneration == fieldGeneration;
    LeaveCriticalSection(&g_mobDamageQueueLock);
    return current;
}
static bool QueueMobDamage(QueuedMobDamage queued) {
    if (!g_mobDamageQueueLockInitialized) {
        return false;
    }

    bool droppedOldest = false;
    EnterCriticalSection(&g_mobDamageQueueLock);
    const bool queuedWhileInactive = !g_mobDamageFieldActive;
    queued.fieldGeneration = g_mobDamageFieldGeneration;
    queued.queuedFrame = g_mobDamageFrame;
    queued.queuedAt = GetTickCount();
    if (g_mobDamageQueue.size() >= kMaxQueuedMobDamage) {
        g_mobDamageQueue.erase(g_mobDamageQueue.begin());
        droppedOldest = true;
    }
    g_mobDamageQueue.push_back(queued);
    const size_t queueSize = g_mobDamageQueue.size();
    LeaveCriticalSection(&g_mobDamageQueueLock);

    if (droppedOldest) {
        CrashReporter::RecordEvent("showMobDamage.queue", "drop reason=capacity max=%u", static_cast<unsigned int>(kMaxQueuedMobDamage));
    }
    if (queuedWhileInactive && ShouldPersistMobDamageSample(queued.sequence)) {
        CrashReporter::RecordEvent(
            "showMobDamage.queue",
            "accept seq=%ld whileFieldInactive generation=%lu frame=%lu",
            queued.sequence,
            queued.fieldGeneration,
            queued.queuedFrame);
    }
    CrashReporter::RecordRecentEvent(
        "showMobDamage.queue",
        "queued seq=%ld objectId=%d damage=%d critical=%d line=%d generation=%lu frame=%lu queue=%u",
        queued.sequence,
        queued.objectId,
        queued.damage,
        queued.critical ? 1 : 0,
        queued.lineIndex,
        queued.fieldGeneration,
        queued.queuedFrame,
        static_cast<unsigned int>(queueSize));
    return true;
}
static bool RequeueMobDamage(QueuedMobDamage queued) {
    if (!g_mobDamageQueueLockInitialized) {
        return false;
    }

    const DWORD now = GetTickCount();
    EnterCriticalSection(&g_mobDamageQueueLock);
    if (!g_mobDamageFieldActive
        || g_mobDamageFieldGeneration != queued.fieldGeneration
        || IsQueuedMobDamageExpired(queued, now)
        || g_mobDamageQueue.size() >= kMaxQueuedMobDamage) {
        LeaveCriticalSection(&g_mobDamageQueueLock);
        return false;
    }

    queued.queuedFrame = g_mobDamageFrame;
    ++queued.retryCount;
    g_mobDamageQueue.push_back(queued);
    LeaveCriticalSection(&g_mobDamageQueueLock);
    return true;
}
enum class MobDamageRenderResult {
    Rendered,
    Retry,
    Dropped
};
static MobDamageRenderResult RenderQueuedMobDamage(const QueuedMobDamage& queued) {
    if (!IsCurrentMobDamageField(queued.fieldGeneration)) {
        return MobDamageRenderResult::Dropped;
    }

    DWORD displayer = 0;
    DWORD resource1 = 0;
    DWORD resource2 = 0;
    bool renderCritical = queued.damage > 0 && queued.critical;
    if (queued.damage > 0 && !ReadDamageResources(renderCritical, displayer, resource1, resource2)) {
        if (!renderCritical || !ReadDamageResources(false, displayer, resource1, resource2)) {
            if (queued.retryCount == 0 && ShouldPersistMobDamageSample(queued.sequence)) {
                CrashReporter::RecordEvent(
                    "showMobDamage.resources",
                    "defer seq=%ld critical=%d displayer=0x%08lX resource1=0x%08lX resource2=0x%08lX",
                    queued.sequence,
                    queued.critical ? 1 : 0,
                    displayer,
                    resource1,
                    resource2);
            } else {
                CrashReporter::RecordRecentEvent(
                    "showMobDamage.resources",
                    "defer seq=%ld retry=%u critical=%d displayer=0x%08lX resource1=0x%08lX resource2=0x%08lX",
                    queued.sequence,
                    queued.retryCount,
                    queued.critical ? 1 : 0,
                    displayer,
                    resource1,
                    resource2);
            }
            return MobDamageRenderResult::Retry;
        }
        renderCritical = false;
        if (ShouldPersistMobDamageSample(queued.sequence)) {
            CrashReporter::RecordEvent(
                "showMobDamage.resources",
                "fallback seq=%ld reason=criticalResourcesUnavailable displayer=0x%08lX",
                queued.sequence,
                displayer);
        }
    }

    const char* stage = "readMobPool";
    __try {
        DWORD mobPool = 0;
        if (!TryReadDword(kMobPoolPtr, mobPool) || mobPool == 0) {
            CrashReporter::RecordRecentEvent(
                "showMobDamage.render",
                "drop seq=%ld reason=mobPoolUnavailable objectId=%d",
                queued.sequence,
                queued.objectId);
            return MobDamageRenderResult::Dropped;
        }

        stage = "findMob";
        void* mob = g_FindMob(reinterpret_cast<void*>(mobPool), queued.objectId);
        if (mob == nullptr) {
            CrashReporter::RecordRecentEvent(
                "showMobDamage.render",
                "drop seq=%ld reason=mobNotFound objectId=%d",
                queued.sequence,
                queued.objectId);
            return MobDamageRenderResult::Dropped;
        }

        stage = "showMobDamage";
        CrashReporter::RecordRecentEvent(
            "showMobDamage.render",
            "invoke seq=%ld mob=%p objectId=%d damage=%d critical=%d requestedCritical=%d line=%d generation=%lu",
            queued.sequence,
            mob,
            queued.objectId,
            queued.damage,
            renderCritical ? 1 : 0,
            queued.critical ? 1 : 0,
            queued.lineIndex,
            queued.fieldGeneration);
        g_renderingServerMobDamage = true;
        g_ShowMobDamage(mob, nullptr, queued.damage, queued.lineIndex, renderCritical ? 1 : 0, 0);
        g_renderingServerMobDamage = false;
        if (ShouldPersistMobDamageSample(queued.sequence)) {
            CrashReporter::RecordEvent(
                "showMobDamage.render",
                "complete seq=%ld mob=%p critical=%d requestedCritical=%d retry=%u",
                queued.sequence,
                mob,
                renderCritical ? 1 : 0,
                queued.critical ? 1 : 0,
                queued.retryCount);
        } else {
            CrashReporter::RecordRecentEvent("showMobDamage.render", "complete seq=%ld mob=%p", queued.sequence, mob);
        }
        return MobDamageRenderResult::Rendered;
    } __except (CrashReporter::CaptureHandledException(
        "showMobDamage.exception",
        stage,
        GetExceptionInformation())) {
        g_renderingServerMobDamage = false;
        CrashReporter::RecordEvent(
            "showMobDamage.exception",
            "stage=%s code=0x%08lX seq=%ld objectId=%d damage=%d critical=%d line=%d",
            stage,
            GetExceptionCode(),
            queued.sequence,
            queued.objectId,
            queued.damage,
            queued.critical ? 1 : 0,
            queued.lineIndex);
        return MobDamageRenderResult::Dropped;
    }
}
static unsigned char ClampAlert(int value) {
    if (value < 0) return 0;
    if (value > 20) return 20;
    return static_cast<unsigned char>(value);
}
static unsigned short ReadUInt16LE(const unsigned char* data) {
    return static_cast<unsigned short>(data[0] | (data[1] << 8));
}
static int ReadInt32LE(const unsigned char* data) {
    return static_cast<int>(
        static_cast<unsigned int>(data[0]) |
        (static_cast<unsigned int>(data[1]) << 8) |
        (static_cast<unsigned int>(data[2]) << 16) |
        (static_cast<unsigned int>(data[3]) << 24));
}
static bool ShouldCapturePacketPayload(unsigned short opcode) {
    switch (opcode) {
    case kOpcodeRemovePlayer:
    case kOpcodeMovePet:
    case kOpcodeMovePlayer:
    case kOpcodeCloseRangeAttack:
    case kOpcodeRangedAttack:
    case kOpcodeMagicAttack:
    case kOpcodeDamagePlayer:
    case kOpcodeSpawnMonster:
    case kOpcodeKillMonster:
    case kOpcodeSpawnMonsterControl:
    case kOpcodeMoveMonster:
    case kOpcodeMoveMonsterResponse:
    case kOpcodeApplyMonsterStatus:
    case kOpcodeCancelMonsterStatus:
    case kOpcodeDamageMonster:
    case kOpcodeShowMobDamage:
        return true;
    default:
        return false;
    }
}
static void* FindMobByObjectId(int objectId) {
    DWORD mobPool = 0;
    if (!TryReadDword(kMobPoolPtr, mobPool) || mobPool == 0) {
        return nullptr;
    }
    return g_FindMob(reinterpret_cast<void*>(mobPool), objectId);
}
static void UpdateBossVenomVisualTarget(int objectId, bool active) {
    int emptyIndex = -1;
    for (int i = 0; i < kBossVenomTargetCount; ++i) {
        BossVenomVisualTarget& target = g_bossVenomTargets[i];
        if (target.objectId == objectId) {
            target = active
                ? BossVenomVisualTarget{objectId, FindMobByObjectId(objectId)}
                : BossVenomVisualTarget{};
            return;
        }
        if (emptyIndex < 0 && target.objectId == 0) {
            emptyIndex = i;
        }
    }

    if (!active) {
        return;
    }
    const int targetIndex = emptyIndex >= 0
        ? emptyIndex
        : static_cast<unsigned int>(objectId) % kBossVenomTargetCount;
    g_bossVenomTargets[targetIndex] = {objectId, FindMobByObjectId(objectId)};
}
static bool IsBossVenomSkill(int skillId) {
    return skillId == 4120005 || skillId == 4220005 || skillId == 14110004;
}
static void ObserveBossVenomStatusPacket(CInPacket* packet) {
    __try {
        if (packet == nullptr || packet->Data == nullptr || packet->DataLen < 26) {
            return;
        }
        const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
        const unsigned short opcode = ReadUInt16LE(data + 4);
        if (opcode != kOpcodeApplyMonsterStatus && opcode != kOpcodeCancelMonsterStatus) {
            return;
        }

        const unsigned int firstMask = static_cast<unsigned int>(ReadInt32LE(data + 18));
        const unsigned int secondMask = static_cast<unsigned int>(ReadInt32LE(data + 22));
        if (firstMask != 0 || secondMask != kBossVenomVisualMask) {
            return;
        }

        const int objectId = ReadInt32LE(data + 6);
        if (opcode == kOpcodeCancelMonsterStatus) {
            UpdateBossVenomVisualTarget(objectId, false);
            CrashReporter::RecordRecentEvent(
                "bossVenom.visual",
                "cancel objectId=%d",
                objectId);
            return;
        }
        if (packet->DataLen < 32) {
            return;
        }

        const int visualDamage = ReadUInt16LE(data + 26);
        const int skillId = ReadInt32LE(data + 28);
        if (visualDamage == kBossVenomVisualDamage && IsBossVenomSkill(skillId)) {
            UpdateBossVenomVisualTarget(objectId, true);
            CrashReporter::RecordRecentEvent(
                "bossVenom.visual",
                "apply objectId=%d visualDamage=%d skillId=%d",
                objectId,
                visualDamage,
                skillId);
        }
    } __except (CrashReporter::CaptureHandledException(
        "bossVenom.visual.exception",
        "observePacket",
        GetExceptionInformation())) {
        return;
    }
}
static bool ShouldSuppressBossVenomLocalDamage(void* mob, int damage) {
    if (mob == nullptr || damage != kBossVenomVisualDamage) {
        return false;
    }
    for (int i = 0; i < kBossVenomTargetCount; ++i) {
        BossVenomVisualTarget& target = g_bossVenomTargets[i];
        if (target.objectId == 0) {
            continue;
        }
        if (target.mob == nullptr) {
            target.mob = FindMobByObjectId(target.objectId);
        }
        if (target.mob == mob) {
            CrashReporter::RecordRecentEvent(
                "bossVenom.visual",
                "suppress objectId=%d mob=%p damage=%d",
                target.objectId,
                mob,
                damage);
            return true;
        }
    }
    return false;
}
static void ResetBossVenomVisualTargets() {
    for (int i = 0; i < kBossVenomTargetCount; ++i) {
        g_bossVenomTargets[i] = {};
    }
}
static void SendHpMpAlertFromStatusBar() {
    DWORD statusBar = 0;
    if (!TryReadDword(kUIStatusBarPtr, statusBar) || statusBar == 0) {
        return;
    }
    DWORD hpRaw = 0;
    DWORD mpRaw = 0;
    if (!TryReadDword(statusBar + kHpAlertOffset, hpRaw)) {
        return;
    }
    if (!TryReadDword(statusBar + kMpAlertOffset, mpRaw)) {
        return;
    }
    const unsigned char hpAlert = ClampAlert(static_cast<int>(hpRaw));
    const unsigned char mpAlert = ClampAlert(static_cast<int>(mpRaw));
    DWORD socketPtr = 0;
    if (!TryReadDword(kClientSocketPtr, socketPtr) || socketPtr == 0) {
        return;
    }
    unsigned char payload[4] = {
        static_cast<unsigned char>(kOpcodeSetHpMpAlert & 0xFF),
        static_cast<unsigned char>((kOpcodeSetHpMpAlert >> 8) & 0xFF),
        hpAlert,
        mpAlert
    };
    COutPacket packet{};
    packet.Loopback = 0;
    packet.Data = payload;
    packet.Size = sizeof(payload);
    packet.Offset = 0;
    packet.EncryptedByShanda = 0;
    g_SendPacket(reinterpret_cast<void*>(socketPtr), nullptr, &packet);
}
static void ApplyHpMpAlertToStatusBar(unsigned char hpAlert, unsigned char mpAlert) {
    DWORD statusBar = 0;
    if (!TryReadDword(kUIStatusBarPtr, statusBar) || statusBar == 0) {
        return;
    }
    Memory::WriteInt(statusBar + kHpAlertOffset, hpAlert);
    Memory::WriteInt(statusBar + kMpAlertOffset, mpAlert);
}
static void HandleHpMpAlertPacket(CInPacket* packet) {
    if (packet == nullptr) {
        return;
    }
    __try {
        if (packet->Data == nullptr || packet->DataLen < 8) {
            return;
        }
        const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
        const unsigned short opcode = *reinterpret_cast<const unsigned short*>(data + 4);
        if (opcode != kOpcodeSetHpMpAlert) {
            return;
        }
        // First two bytes are HP/MP alert thresholds; append more settings after if needed.
        const unsigned char hpAlert = ClampAlert(static_cast<int>(data[6]));
        const unsigned char mpAlert = ClampAlert(static_cast<int>(data[7]));
        ApplyHpMpAlertToStatusBar(hpAlert, mpAlert);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}
static bool HandleShowMobDamagePacket(CInPacket* packet) {
    if (packet == nullptr) {
        return false;
    }
    const char* stage = "parse";
    int objectId = 0;
    int damage = 0;
    bool critical = false;
    int lineIndex = 0;
    LONG sequence = 0;
    bool persistSample = false;
    __try {
        if (packet->Data == nullptr || packet->DataLen < 6) {
            return false;
        }
        const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
        const unsigned short opcode = ReadUInt16LE(data + 4);
        if (opcode != kOpcodeShowMobDamage) {
            return false;
        }
        if (packet->DataLen < 16) {
            CrashReporter::RecordEvent(
                "showMobDamage.packet",
                "drop reason=truncated size=%u offset=%u rawSeq=%u",
                packet->DataLen,
                packet->Offset,
                packet->RawSeq);
            return true;
        }

        objectId = ReadInt32LE(data + 6);
        damage = ReadInt32LE(data + 10);
        critical = data[14] != 0;
        lineIndex = data[15] & 0x0F;
        sequence = InterlockedIncrement(&g_showMobDamagePacketCount);
        persistSample = ShouldPersistMobDamageSample(sequence);
        if (persistSample) {
            CrashReporter::RecordEvent(
                "showMobDamage",
                "receive seq=%ld objectId=%d damage=%d critical=%d line=%d size=%lu",
                sequence,
                objectId,
                damage,
                critical ? 1 : 0,
                lineIndex,
                static_cast<unsigned long>(packet->DataLen));
        } else {
            CrashReporter::RecordRecentEvent(
                "showMobDamage",
                "receive seq=%ld objectId=%d damage=%d critical=%d line=%d size=%lu",
                sequence,
                objectId,
                damage,
                critical ? 1 : 0,
                lineIndex,
                static_cast<unsigned long>(packet->DataLen));
        }

        if (damage < 0) {
            CrashReporter::RecordRecentEvent("showMobDamage", "skip seq=%ld reason=negativeDamage", sequence);
            return true;
        }
        if (SnipeDamageSync::TrackServerDamage(objectId, damage, critical)) {
            return true;
        }

        stage = "queue";
        QueuedMobDamage queued{};
        queued.objectId = objectId;
        queued.damage = damage;
        queued.critical = critical;
        queued.lineIndex = lineIndex;
        queued.sequence = sequence;
        if (!QueueMobDamage(queued)) {
            CrashReporter::RecordRecentEvent(
                "showMobDamage.queue",
                "drop seq=%ld reason=fieldInactive objectId=%d",
                sequence,
                objectId);
        }
        return true;
    } __except (CrashReporter::CaptureHandledException(
        "showMobDamage.exception",
        stage,
        GetExceptionInformation())) {
        CrashReporter::RecordEvent(
            "showMobDamage.exception",
            "stage=%s code=0x%08lX seq=%ld objectId=%d damage=%d critical=%d line=%d",
            stage,
            GetExceptionCode(),
            sequence,
            objectId,
            damage,
            critical ? 1 : 0,
            lineIndex);
        return true;
    }
}
static void __fastcall ShowMobDamage_Hook(void* pThis, void* edx, int damage, int lineIndex, int extra, int compact) {
    if (!g_renderingServerMobDamage) {
        int synchronizedDamage = 0;
        bool synchronizedCritical = false;
        if (SnipeDamageSync::TryResolveLocalDamage(
                pThis,
                damage,
                synchronizedDamage,
                synchronizedCritical)) {
            g_ShowMobDamage(
                pThis,
                edx,
                synchronizedDamage,
                lineIndex,
                synchronizedCritical ? 1 : 0,
                compact);
            return;
        }
        if (ShouldSuppressBossVenomLocalDamage(pThis, damage)) {
            return;
        }
        if (AbsoluteDefenseSync::ShouldSuppressLocalDamage(pThis)) {
            return;
        }
    }
    g_ShowMobDamage(pThis, edx, damage, lineIndex, extra, compact);
    int additionalDamage = 0;
    if (!g_renderingServerMobDamage
        && IntegratedFinalAttack::TakeAdditionalDisplayedDamage(pThis, damage, lineIndex, additionalDamage)) {
        g_ShowMobDamage(pThis, edx, additionalDamage, lineIndex + 1, extra, compact);
    }
}
using SaveGlobal_t = void(__fastcall*)(void* pThis, void* edx);
static SaveGlobal_t s_SaveGlobal = reinterpret_cast<SaveGlobal_t>(kSaveGlobalAddr);
static void __fastcall SaveGlobal_Hook(void* pThis, void* edx) {
    s_SaveGlobal(pThis, edx);
    SendHpMpAlertFromStatusBar();
}
using ProcessPacket_t = void(__fastcall*)(void* pThis, void* edx, CInPacket* packet);
static ProcessPacket_t s_ProcessPacket = reinterpret_cast<ProcessPacket_t>(kProcessPacketAddr);
static void TraceIncomingPacket(CInPacket* packet) {
    __try {
        if (packet == nullptr || packet->Data == nullptr || packet->DataLen < 6) {
            return;
        }
        const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
        const unsigned short opcode = ReadUInt16LE(data + 4);
        const bool capturePayload = ShouldCapturePacketPayload(opcode);
        CrashReporter::RecordIncomingPacket(
            opcode,
            packet->DataLen,
            packet->Offset,
            capturePayload ? data + 4 : nullptr,
            capturePayload ? static_cast<size_t>(packet->DataLen - 4) : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CrashReporter::RecordEvent(
            "incomingPacket.exception",
            "code=0x%08lX packet=%p",
            GetExceptionCode(),
            packet);
    }
}
static void __fastcall ProcessPacket_Hook(void* pThis, void* edx, CInPacket* packet) {
    TraceIncomingPacket(packet);
    ObserveBossVenomStatusPacket(packet);
    if (packet != nullptr
        && AbsoluteDefenseSync::HandlePacket(
            reinterpret_cast<const unsigned char*>(packet->Data),
            packet->DataLen)) {
        return;
    }
    if (IntegratedFinalAttack::HandlePacket(packet)) {
        return;
    }
    if (StackedBuffIcons::HandlePacket(packet)) {
        return;
    }
    if (HandleShowMobDamagePacket(packet)) {
        return;
    }
    HandleHpMpAlertPacket(packet);
    if (packet != nullptr) {
        AbsoluteDefenseSync::BeginIncomingAttackPacket(
            reinterpret_cast<const unsigned char*>(packet->Data),
            packet->DataLen);
    }
    s_ProcessPacket(pThis, edx, packet);
    AbsoluteDefenseSync::EndIncomingAttackPacket();
}
} // namespace
void HookSaveGlobal(bool enable) {
    Memory::SetHook(enable, reinterpret_cast<void**>(&s_SaveGlobal), SaveGlobal_Hook);
}
void HookHpMpAlertRecv(bool enable) {
    if (enable) {
        AbsoluteDefenseSync::InstallImmunityBypassHooks();
    }
    if (enable && !g_mobDamageQueueLockInitialized) {
        InitializeCriticalSection(&g_mobDamageQueueLock);
        g_mobDamageQueue.reserve(kMaxQueuedMobDamage);
        g_mobDamageQueueLockInitialized = true;
    }

    if (!enable && g_mobDamageQueueLockInitialized) {
        EnterCriticalSection(&g_mobDamageQueueLock);
        g_mobDamageFieldActive = false;
        ++g_mobDamageFieldGeneration;
        g_mobDamageQueue.clear();
        LeaveCriticalSection(&g_mobDamageQueueLock);
    }
    if (!enable) {
        AbsoluteDefenseSync::Reset();
        ResetBossVenomVisualTargets();
    }

    Memory::SetHook(enable, reinterpret_cast<void**>(&g_ShowMobDamage), ShowMobDamage_Hook);
    Memory::SetHook(enable, reinterpret_cast<void**>(&s_ProcessPacket), ProcessPacket_Hook);
}

void UpdateQueuedMobDamageDisplay() {
    if (!g_mobDamageQueueLockInitialized) {
        return;
    }

    std::vector<QueuedMobDamage> ready;
    ready.reserve(kMaxMobDamagePerFrame);
    unsigned int expiredCount = 0;
    unsigned int staleCount = 0;
    const DWORD now = GetTickCount();

    EnterCriticalSection(&g_mobDamageQueueLock);
    const bool activatedFromUserUpdate = !g_mobDamageFieldActive;
    g_mobDamageFieldActive = true;
    ++g_mobDamageFrame;
    const DWORD currentGeneration = g_mobDamageFieldGeneration;
    const DWORD currentFrame = g_mobDamageFrame;
    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < g_mobDamageQueue.size(); ++readIndex) {
        const QueuedMobDamage queued = g_mobDamageQueue[readIndex];
        if (queued.fieldGeneration != g_mobDamageFieldGeneration) {
            ++staleCount;
            continue;
        }
        if (IsQueuedMobDamageExpired(queued, now)) {
            ++expiredCount;
            continue;
        }
        if (queued.queuedFrame < g_mobDamageFrame && ready.size() < kMaxMobDamagePerFrame) {
            ready.push_back(queued);
            continue;
        }
        g_mobDamageQueue[writeIndex++] = queued;
    }
    g_mobDamageQueue.resize(writeIndex);
    LeaveCriticalSection(&g_mobDamageQueueLock);

    if (activatedFromUserUpdate) {
        CrashReporter::RecordEvent(
            "showMobDamage.field",
            "activateFromUserUpdate generation=%lu frame=%lu queued=%u",
            currentGeneration,
            currentFrame,
            static_cast<unsigned int>(ready.size() + writeIndex));
    }

    if (staleCount != 0 || expiredCount != 0) {
        CrashReporter::RecordEvent(
            "showMobDamage.queue",
            "drop stale=%u expired=%u generation=%lu frame=%lu",
            staleCount,
            expiredCount,
            currentGeneration,
            currentFrame);
    }

    for (const QueuedMobDamage& queued : ready) {
        const MobDamageRenderResult result = RenderQueuedMobDamage(queued);
        if (result == MobDamageRenderResult::Retry && !RequeueMobDamage(queued)) {
            CrashReporter::RecordRecentEvent(
                "showMobDamage.queue",
                "drop seq=%ld reason=retryRejected generation=%lu",
                queued.sequence,
                queued.fieldGeneration);
        }
    }
}

void OnMobDamageFieldInit() {
    AbsoluteDefenseSync::Reset();
    ResetBossVenomVisualTargets();
    if (!g_mobDamageQueueLockInitialized) {
        return;
    }

    EnterCriticalSection(&g_mobDamageQueueLock);
    ++g_mobDamageFieldGeneration;
    g_mobDamageFrame = 0;
    g_mobDamageFieldActive = false;
    g_mobDamageQueue.clear();
    const DWORD generation = g_mobDamageFieldGeneration;
    LeaveCriticalSection(&g_mobDamageQueueLock);

    CrashReporter::RecordEvent("showMobDamage.field", "init generation=%lu", generation);
}

void OnMobDamageFieldDispose() {
    AbsoluteDefenseSync::Reset();
    ResetBossVenomVisualTargets();
    if (!g_mobDamageQueueLockInitialized) {
        return;
    }

    EnterCriticalSection(&g_mobDamageQueueLock);
    g_mobDamageFieldActive = false;
    ++g_mobDamageFieldGeneration;
    const size_t droppedCount = g_mobDamageQueue.size();
    g_mobDamageQueue.clear();
    const DWORD generation = g_mobDamageFieldGeneration;
    LeaveCriticalSection(&g_mobDamageQueueLock);

    CrashReporter::RecordEvent(
        "showMobDamage.field",
        "dispose generation=%lu dropped=%u",
        generation,
        static_cast<unsigned int>(droppedCount));
}
