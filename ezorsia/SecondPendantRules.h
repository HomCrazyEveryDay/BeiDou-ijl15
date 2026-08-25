#pragma once

namespace SecondPendantRules
{
constexpr int kSecondPendantSlot = 51;

constexpr bool ShouldIgnoreExpansionExpirationForTarget(int targetSlot)
{
	return targetSlot == kSecondPendantSlot;
}
}
