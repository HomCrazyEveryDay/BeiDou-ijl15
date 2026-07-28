#pragma once

namespace SnipeDamageSync {
void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size);
bool ShouldSuppressLocalDamage(void* mob, int damage);
}
