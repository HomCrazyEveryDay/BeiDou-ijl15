#include "stdafx.h"
#include "HpMpAlert.h"
#include "SnipeDamageSync.h"
#include "StackedBuffIcons.h"
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
constexpr WORD kOpcodeSetHpMpAlert = 0x1000;
constexpr WORD kOpcodeShowMobDamage = 0x1001;
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
    bool Loopback;
    int State;
    void* Data;
    unsigned long Size;
    unsigned short RawSeq;
    unsigned short DataLen;
    unsigned short Unknown;
    unsigned int Offset;
    void* Unk;
};
using SendPacket_t = void(__fastcall*)(void* pThis, void* edx, COutPacket* packet);
static SendPacket_t g_SendPacket = reinterpret_cast<SendPacket_t>(0x0049637B);
using FindMob_t = void* (__thiscall*)(void* pThis, int objectId);
static FindMob_t g_FindMob = reinterpret_cast<FindMob_t>(kFindMobAddr);
using ShowMobDamage_t = void(__fastcall*)(void* pThis, void* edx, int damage, int lineIndex, int extra, int compact);
static ShowMobDamage_t g_ShowMobDamage = reinterpret_cast<ShowMobDamage_t>(kShowMobDamageAddr);
static bool g_renderingServerMobDamage = false;
static bool TryReadDword(DWORD address, DWORD& out) {
    __try {
        out = *reinterpret_cast<DWORD*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
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
        if (packet->Data == nullptr || packet->Size < 8) {
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
    __try {
        if (packet->Data == nullptr || packet->Size < 16) {
            return false;
        }
        const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
        const unsigned short opcode = ReadUInt16LE(data + 4);
        if (opcode != kOpcodeShowMobDamage) {
            return false;
        }

        const int objectId = ReadInt32LE(data + 6);
        const int damage = ReadInt32LE(data + 10);
        const bool critical = data[14] != 0;
        const int lineIndex = data[15] & 0x0F;

        if (damage <= 0) {
            return true;
        }

        DWORD mobPool = 0;
        if (!TryReadDword(kMobPoolPtr, mobPool) || mobPool == 0) {
            return true;
        }

        void* mob = g_FindMob(reinterpret_cast<void*>(mobPool), objectId);
        if (mob == nullptr) {
            return true;
        }

        // 006691D3's third argument selects the alternate damage number resource used by critical hits.
        g_renderingServerMobDamage = true;
        g_ShowMobDamage(mob, nullptr, damage, lineIndex, critical ? 1 : 0, 0);
        g_renderingServerMobDamage = false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_renderingServerMobDamage = false;
        return true;
    }
}
static void __fastcall ShowMobDamage_Hook(void* pThis, void* edx, int damage, int lineIndex, int extra, int compact) {
    if (!g_renderingServerMobDamage && SnipeDamageSync::ShouldSuppressLocalDamage(pThis, damage)) {
        return;
    }
    g_ShowMobDamage(pThis, edx, damage, lineIndex, extra, compact);
}
using SaveGlobal_t = void(__fastcall*)(void* pThis, void* edx);
static SaveGlobal_t s_SaveGlobal = reinterpret_cast<SaveGlobal_t>(kSaveGlobalAddr);
static void __fastcall SaveGlobal_Hook(void* pThis, void* edx) {
    s_SaveGlobal(pThis, edx);
    SendHpMpAlertFromStatusBar();
}
using ProcessPacket_t = void(__fastcall*)(void* pThis, void* edx, CInPacket* packet);
static ProcessPacket_t s_ProcessPacket = reinterpret_cast<ProcessPacket_t>(kProcessPacketAddr);
static void __fastcall ProcessPacket_Hook(void* pThis, void* edx, CInPacket* packet) {
    if (StackedBuffIcons::HandlePacket(packet)) {
        return;
    }
    if (HandleShowMobDamagePacket(packet)) {
        return;
    }
    HandleHpMpAlertPacket(packet);
    s_ProcessPacket(pThis, edx, packet);
}
} // namespace
void HookSaveGlobal(bool enable) {
    Memory::SetHook(enable, reinterpret_cast<void**>(&s_SaveGlobal), SaveGlobal_Hook);
}
void HookHpMpAlertRecv(bool enable) {
    Memory::SetHook(enable, reinterpret_cast<void**>(&g_ShowMobDamage), ShowMobDamage_Hook);
    Memory::SetHook(enable, reinterpret_cast<void**>(&s_ProcessPacket), ProcessPacket_Hook);
}
