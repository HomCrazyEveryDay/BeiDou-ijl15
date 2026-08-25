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
	using SecondPendantRules::ShouldIgnoreExpansionExpirationForTarget;

	Require(ShouldIgnoreExpansionExpirationForTarget(51));
	Require(!ShouldIgnoreExpansionExpirationForTarget(17));
	Require(!ShouldIgnoreExpansionExpirationForTarget(50));
	Require(!ShouldIgnoreExpansionExpirationForTarget(52));
	Require(!ShouldIgnoreExpansionExpirationForTarget(0));
	return 0;
}
