#pragma once

namespace ShadowPartnerDamageSync {
constexpr unsigned char kNativeImpactMarker = 1;
enum class LocalResult { Unchanged, Deferred, Resolved };

void TrackOutgoingAttackPacket(const unsigned char* data, unsigned long size);
void TrackIncomingAttackPacket(const unsigned char* data, unsigned long size);
// Replies only update authority. Rendering is driven by native impact and Update.
void TrackServerDamage(int objectId, int damage, bool critical, int lineIndex, int pursuitLine = 0);
LocalResult ResolveAtNativeImpact(void* mob, int damage, int lineIndex, int compact, int& displayedDamage, bool& critical);
void Update();
void BeginIncomingPacket();
void EndIncomingPacket();
void Reset();
}
