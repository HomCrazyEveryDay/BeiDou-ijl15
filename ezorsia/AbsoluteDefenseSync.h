#pragma once

namespace AbsoluteDefenseSync {
bool HandlePacket(const unsigned char* data, unsigned long size);
bool TryScaleDisplayedDamage(void* mob, int damage, int& scaledDamage);
void Reset();
}
