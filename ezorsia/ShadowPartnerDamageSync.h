#pragma once

namespace ShadowPartnerDamageSync {
constexpr unsigned char kNativeImpactMarker = 1;
enum class LocalResult { Unchanged, WaitForServer, Resolved, RemotePursuit };

void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size);
void TrackIncomingAttackPacket(const unsigned char* data, unsigned long size);
// True only when native impact already happened, so a late reply may be rendered now.
bool TrackServerDamage(int objectId, int damage, bool critical, int lineIndex, int pursuitLine = 0);
LocalResult ResolveAtNativeImpact(void* mob, int damage, int lineIndex, int& displayedDamage, bool& critical, int& pursuitLine);
void BeginIncomingPacket();
void EndIncomingPacket();
void Reset();
}
