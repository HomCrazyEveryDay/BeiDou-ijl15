#pragma once

namespace EvanPursuit {
// Server-authoritative, caster-only target. Incoming buffers include the transport prefix.
bool HandlePacket(const unsigned char* data, unsigned long size);
void Reset();
}
