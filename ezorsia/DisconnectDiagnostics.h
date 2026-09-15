#pragma once
#include <windows.h>

namespace DisconnectDiagnostics {
void ExpectChannelClose();
void Install(bool enabled);
void Packet(bool incoming, unsigned short opcode, unsigned long size);
void SkillUse(unsigned long skillId, unsigned char level);
LONG PacketException(EXCEPTION_POINTERS* info);
}
