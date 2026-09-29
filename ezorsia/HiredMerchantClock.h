#pragma once

namespace HiredMerchantClock {
bool Install();
bool HandlePacket(const unsigned char* data, unsigned long size);
}
