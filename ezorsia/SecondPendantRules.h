#pragma once

#include <cstring>

namespace SecondPendantRules
{
constexpr int kSecondPendantSlot = 51;
constexpr int kEquipmentSnapshotEntryCount = 52;
constexpr int kEquipmentSnapshotEntrySize = 8;
constexpr int kEquipmentSnapshotItemPointerOffsetInEntry = 4;
constexpr unsigned long long kExpiredExpansionTime = 94354848000000000ULL;
constexpr unsigned long long kPermanentExpansionTime = 150841440000000000ULL;

constexpr bool ShouldIgnoreExpansionExpirationForTarget(int targetSlot)
{
	return targetSlot == kSecondPendantSlot;
}

constexpr bool ShouldMaskDerivedEquipmentSnapshot(bool enabled, int slot)
{
	return !enabled && slot == kSecondPendantSlot;
}

constexpr int EquipmentSnapshotEntryOffset(int slot)
{
	return slot * kEquipmentSnapshotEntrySize;
}

constexpr int EquipmentSnapshotItemPointerOffset(int slot)
{
	return EquipmentSnapshotEntryOffset(slot) + kEquipmentSnapshotItemPointerOffsetInEntry;
}

constexpr int kEquipmentSnapshotSize =
	kEquipmentSnapshotEntryCount * kEquipmentSnapshotEntrySize;

constexpr unsigned long long ExpansionTimeForAccess(bool enabled)
{
	return enabled ? kPermanentExpansionTime : kExpiredExpansionTime;
}

constexpr unsigned long long ExpansionTimeForAccess(bool enabled,
	bool hasServerExpiration, unsigned long long serverExpiration)
{
	return hasServerExpiration ? serverExpiration : ExpansionTimeForAccess(enabled);
}

inline bool IsLockedTooltipTemplate(const char* text)
{
	return text != nullptr
		&& std::strstr(text, "will be equipped") != nullptr
		&& std::strstr(text, "slot extender") != nullptr;
}

inline bool IsPurchaseConfirmationTemplate(const char* text)
{
	return text != nullptr
		&& std::strstr(text, "additional days") != nullptr
		&& std::strstr(text, "Refunds are not available") != nullptr;
}
}
