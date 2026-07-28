#pragma once

namespace SnipeDamageSync {
void TrackOutgoingAttackPacket(unsigned char* data, unsigned long size);
bool ShouldSuppressLocalDamage(void* mob, int damage);
}
