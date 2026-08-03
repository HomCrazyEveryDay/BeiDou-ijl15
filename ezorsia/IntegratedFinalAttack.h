#pragma once

#include <vector>

namespace IntegratedFinalAttack {
bool HandlePacket(void* rawPacket);
bool BuildOutgoingAttackPacket(const unsigned char* data, unsigned long size, std::vector<unsigned char>& output);
bool TakeAdditionalDisplayedDamage(void* mob, int damage, int lineIndex, int& additionalDamage);
}
