#pragma once
#include <windows.h>

namespace VisitorsTimerTooltip {
bool Install(HMODULE module);
// Observe existing SET_FIELD timestamps only; never consume or send packets.
void ObservePacket(const unsigned char* data, unsigned long size);
void Clear(void* owner);
}
