#include "UObject/PawnPathCost.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static void TestAffordableSafeRouteBeatsShortPainRoute()
{
	// Both routes are expanded backward from the same goal. Entering the goal
	// costs nothing; entering the PainPath on the short branch invokes its
	// SpecialCost penalty.
	int32_t painRoute = PawnPath::AccumulateCost(0, 50, 0);
	painRoute = PawnPath::AccumulateCost(painRoute, 50, 1000000);

	int32_t safeRoute = PawnPath::AccumulateCost(0, 120, 0);
	safeRoute = PawnPath::AccumulateCost(safeRoute, 120, 0);

	Check(painRoute == 1000100, "short pain route includes the PainPath SpecialCost");
	Check(safeRoute == 240, "safe route retains its physical path cost");
	Check(PawnPath::PreferCost(safeRoute, 1, painRoute, 0), "affordable safe route is selected over shorter pain route");
	Check(!PawnPath::PreferCost(painRoute, 0, safeRoute, 1), "physical shortness cannot bypass the pain penalty");
}

static void TestCostAdditionSaturatesWithoutOverflow()
{
	const int32_t maximum = std::numeric_limits<int32_t>::max();
	Check(PawnPath::AccumulateCost(maximum - 5, 100, 1000000) == maximum, "large accumulated cost saturates");
	Check(PawnPath::AccumulateCost(maximum, maximum, maximum) == maximum, "three maximum terms cannot wrap negative");
	Check(!PawnPath::PreferCost(maximum, 0, 500, 1), "saturated route cannot look cheaper after overflow");
}

static void TestMalformedNegativeCostsDoNotCreateRewards()
{
	Check(PawnPath::AccumulateCost(-100, -20, -500) == 0, "negative route terms clamp to zero");
	Check(PawnPath::AccumulateCost(40, -20, -500) == 40, "negative map costs cannot reduce accumulated cost");
}

static void TestEqualCostsUseStableOrder()
{
	Check(PawnPath::PreferCost(100, 2, 100, 5), "lower stable order wins an equal-cost tie");
	Check(!PawnPath::PreferCost(100, 5, 100, 2), "equal-cost tie is deterministic");
}

int main()
{
	TestAffordableSafeRouteBeatsShortPainRoute();
	TestCostAdditionSaturatesWithoutOverflow();
	TestMalformedNegativeCostsDoNotCreateRewards();
	TestEqualCostsUseStableOrder();
	if (Failures == 0)
		std::cout << "Pawn path cost tests passed\n";
	return Failures == 0 ? 0 : 1;
}
