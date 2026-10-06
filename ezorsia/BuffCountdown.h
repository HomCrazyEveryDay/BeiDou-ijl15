#pragma once
#include <windows.h>

namespace BuffCountdown {
void Install(volatile LONG* focusStacks, bool enableLog);
void Reset();
bool UpdateCoupons(const unsigned char* data, unsigned size);
bool ShouldRemoveCouponIcon(int nativeType, int iconId);
}
