#pragma once

#include <cstddef>
#include <cstdint>

namespace PawnPath
{
	int32_t AccumulateCost(int32_t accumulatedCost, int32_t edgeDistance, int32_t nodeCost);
	bool PreferCost(int32_t leftCost, size_t leftStableOrder, int32_t rightCost, size_t rightStableOrder);
}
