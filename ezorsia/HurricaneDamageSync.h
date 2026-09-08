#pragma once

namespace HurricaneDamageSync {
void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size);
bool ShouldSuppressLocalDamage(void* mob, int damage);
void BeginIncomingPacket();
void EndIncomingPacket();
void Reset();
}
