#pragma once

namespace AbsoluteDefenseSync {
void InstallImmunityBypassHooks();
bool HandlePacket(const unsigned char* data, unsigned long size);
void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size);
void BeginIncomingAttackPacket(const unsigned char* data, unsigned long size);
void EndIncomingAttackPacket();
bool ShouldSuppressLocalDamage(void* mob);
void Reset();
}
