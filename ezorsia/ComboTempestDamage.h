#pragma once

namespace ComboTempestDamage {
bool HandlePacket(const unsigned char* data, unsigned long size);
void Update();
void Reset();
}
