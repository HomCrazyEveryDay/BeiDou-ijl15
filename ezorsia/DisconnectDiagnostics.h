#pragma once
#include <windows.h>

namespace DisconnectDiagnostics {
void ExpectChannelClose();
void Install(bool enabled);
void Packet(bool incoming, unsigned short opcode, unsigned long size);
LONG PacketException(EXCEPTION_POINTERS* info);
}
