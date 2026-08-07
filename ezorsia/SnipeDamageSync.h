#pragma once

namespace SnipeDamageSync {
void TrackOutgoingAttackPacket(unsigned char* data, unsigned long size);
bool TrackServerDamage(int objectId, int damage, bool critical);
bool TryResolveLocalDamage(void* mob, int localDamage, int& displayedDamage, bool& critical);
}
