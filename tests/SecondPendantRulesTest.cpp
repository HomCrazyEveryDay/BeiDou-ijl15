#include "../ezorsia/SecondPendantRules.h"

#include <cstdlib>

namespace
{
void Require(bool condition)
{
	if (!condition)
	{
		std::abort();
	}
}
}

int main()
{
	using SecondPendantRules::EquipmentSnapshotEntryOffset;
	using SecondPendantRules::EquipmentSnapshotItemPointerOffset;
	using SecondPendantRules::ExpansionTimeForAccess;
	using SecondPendantRules::IsLockedTooltipTemplate;
	using SecondPendantRules::IsPurchaseConfirmationTemplate;
	using SecondPendantRules::ShouldMaskDerivedEquipmentSnapshot;
	using SecondPendantRules::ShouldIgnoreExpansionExpirationForTarget;

	Require(ShouldIgnoreExpansionExpirationForTarget(51));
	Require(!ShouldIgnoreExpansionExpirationForTarget(17));
	Require(!ShouldIgnoreExpansionExpirationForTarget(50));
	Require(!ShouldIgnoreExpansionExpirationForTarget(52));
	Require(!ShouldIgnoreExpansionExpirationForTarget(0));
	Require(ShouldMaskDerivedEquipmentSnapshot(false, 51));
	Require(!ShouldMaskDerivedEquipmentSnapshot(true, 51));
	Require(!ShouldMaskDerivedEquipmentSnapshot(false, 17));
	Require(SecondPendantRules::kEquipmentSnapshotEntryCount == 52);
	Require(SecondPendantRules::kEquipmentSnapshotSize == 0x1A0);
	Require(EquipmentSnapshotEntryOffset(51) == 0x198);
	Require(EquipmentSnapshotItemPointerOffset(51) == 0x19C);
	Require(ExpansionTimeForAccess(false) == SecondPendantRules::kExpiredExpansionTime);
	Require(ExpansionTimeForAccess(true) == SecondPendantRules::kPermanentExpansionTime);
	Require(ExpansionTimeForAccess(false) != ExpansionTimeForAccess(true));
	Require(ExpansionTimeForAccess(true, true, 133700000000000000ULL)
		== 133700000000000000ULL);
	Require(ExpansionTimeForAccess(true, false, 133700000000000000ULL)
		== SecondPendantRules::kPermanentExpansionTime);
	Require(IsLockedTooltipTemplate(
		"The item will be equipped if you purchase a %s slot extender at the Cash Shop."));
	Require(IsLockedTooltipTemplate(
		"The item will be equipped if you purchase a Pendant slot extender at the Cash Shop."));
	Require(!IsLockedTooltipTemplate(nullptr));
	Require(!IsLockedTooltipTemplate("The item will be equipped normally."));
	Require(!IsLockedTooltipTemplate("Purchase a slot extender."));
	Require(IsPurchaseConfirmationTemplate(
		"If you purchase a %s(%dcash), you can equip this item for %d additional days. "
		"Refunds are not available after the purchase."));
	Require(!IsPurchaseConfirmationTemplate(nullptr));
	Require(!IsPurchaseConfirmationTemplate("Refunds are not available."));
	return 0;
}
