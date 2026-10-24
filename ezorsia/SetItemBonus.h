#pragma once
#include <windows.h>

namespace SetItemBonus {
bool HandlePacket(const unsigned char* data, unsigned long size);
bool Install(HMODULE module);
}
