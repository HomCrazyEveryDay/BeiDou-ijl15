#pragma once
namespace SkillPointSync {
bool Install();
bool HandlePacket(const unsigned char* data, unsigned long size);
void Reset();
}
