#include "stdafx.h"
#include "SelectCharMacFix.h"
#include "ClientDiagnostics.h"
#include "ClientLog.h"
#include "AbsoluteDefenseSync.h"
#include "IntegratedFinalAttack.h"
#include "SnipeDamageSync.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace {
constexpr WORD kOpcodeSelectChar = 0x0013;

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

using SendPacket_t = void(__fastcall*)(void* pThis, void* edx, COutPacket* packet);
static SendPacket_t g_SendPacket = reinterpret_cast<SendPacket_t>(0x0049637B);

static unsigned short ReadU16(const unsigned char* ptr) {
    return static_cast<unsigned short>(ptr[0] | (ptr[1] << 8));
}

static void WriteU16(unsigned char* ptr, unsigned short value) {
    ptr[0] = static_cast<unsigned char>(value & 0xFF);
    ptr[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
}

static bool TryBuildMacFromHostString(const unsigned char* hostString, std::size_t hostLength, char (&macText)[18]) {
    if (hostString == nullptr || hostLength < 12) {
        return false;
    }

    char hexDigits[12] = {};
    std::size_t hexCount = 0;
    for (std::size_t i = 0; i < hostLength; ++i) {
        const unsigned char ch = hostString[i];
        if (ch == '_') {
            break;
        }
        if (!std::isxdigit(ch) || hexCount >= sizeof(hexDigits)) {
            return false;
        }
        hexDigits[hexCount++] = static_cast<char>(std::toupper(ch));
    }

    if (hexCount != sizeof(hexDigits)) {
        return false;
    }

    sprintf_s(
        macText,
        sizeof(macText),
        "%.2s-%.2s-%.2s-%.2s-%.2s-%.2s",
        hexDigits,
        hexDigits + 2,
        hexDigits + 4,
        hexDigits + 6,
        hexDigits + 8,
        hexDigits + 10);
    return true;
}

static bool RewriteSelectCharMacList(COutPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size < 10) {
        return false;
    }

    unsigned char* data = packet->Data;
    const std::size_t packetSize = packet->Size;
    if (ReadU16(data) != kOpcodeSelectChar) {
        return false;
    }

    const std::size_t macLengthPos = 6;
    if (packetSize < macLengthPos + 2) {
        return false;
    }

    const unsigned short macLength = ReadU16(data + macLengthPos);
    const std::size_t macPos = macLengthPos + 2;
    if (packetSize < macPos + macLength + 2 || macLength < 17) {
        return false;
    }

    const std::size_t hostLengthPos = macPos + macLength;
    const unsigned short hostLength = ReadU16(data + hostLengthPos);
    const std::size_t hostPos = hostLengthPos + 2;
    if (packetSize < hostPos + hostLength) {
        return false;
    }

    char canonicalMac[18] = {};
    if (!TryBuildMacFromHostString(data + hostPos, hostLength, canonicalMac)) {
        return false;
    }

    if (macLength == 17 && std::memcmp(data + macPos, canonicalMac, 17) == 0) {
        return false;
    }

    const std::size_t bytesAfterMacs = packetSize - hostLengthPos;
    const std::size_t newMacLength = 17;
    const std::size_t shrinkBytes = macLength - newMacLength;
    unsigned char* newHostLengthPos = data + macPos + newMacLength;
    if (shrinkBytes > 0) {
        std::memmove(newHostLengthPos, data + hostLengthPos, bytesAfterMacs);
    }

    WriteU16(data + macLengthPos, static_cast<unsigned short>(newMacLength));
    std::memcpy(data + macPos, canonicalMac, newMacLength);
    packet->Size = static_cast<unsigned long>(packetSize - shrinkBytes);
    return true;
}

static void SendConnectionDiagnostic(void* pThis, void* edx, COutPacket* packet) {
    const DWORD savedError = GetLastError();
    __try {
        if (packet && packet->Data && packet->Size >= 2) {
            const unsigned short opcode = ReadU16(packet->Data);
            if (opcode == 0x0001 || opcode == 0x0014) {
                unsigned char data[ClientDiagnostics::kIdentifyPacketSize]{};
                if (ClientDiagnostics::BeginConnection(data, opcode)) {
                    COutPacket diagnostic{};
                    diagnostic.Data = data;
                    diagnostic.Size = sizeof(data);
                    g_SendPacket(pThis, edx, &diagnostic);
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ClientLog::Append(ClientLog::Component::Lifecycle, "diagnostic_send_failed code=0x%08lX", GetExceptionCode());
    }
    SetLastError(savedError);
}

static void __fastcall SendPacket_Hook(void* pThis, void* edx, COutPacket* packet) {
    SendConnectionDiagnostic(pThis, edx, packet);
    RewriteSelectCharMacList(packet);
    COutPacket integratedPacket{};
    COutPacket* outgoingPacket = packet;
    std::vector<unsigned char> integratedData;
    if (packet != nullptr
        && IntegratedFinalAttack::BuildOutgoingAttackPacket(packet->Data, packet->Size, integratedData)) {
        integratedPacket = *packet;
        integratedPacket.Data = integratedData.data();
        integratedPacket.Size = static_cast<unsigned long>(integratedData.size());
        outgoingPacket = &integratedPacket;
    }
    if (outgoingPacket != nullptr) {
        AbsoluteDefenseSync::TrackOutgoingAttackPacket(outgoingPacket->Data, outgoingPacket->Size);
        SnipeDamageSync::TrackOutgoingAttackPacket(outgoingPacket->Data, outgoingPacket->Size);
    }
    g_SendPacket(pThis, edx, outgoingPacket);
}
}

void HookSelectCharMacFix(bool enable) {
    Memory::SetHook(enable, reinterpret_cast<void**>(&g_SendPacket), SendPacket_Hook);
}
