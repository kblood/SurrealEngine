#include "PawnPathCost.h"

#include <algorithm>
#include <limits>

namespace PawnPath
{
	int32_t AccumulateCost(int32_t accumulatedCost, int32_t edgeDistance, int32_t nodeCost)
	{
		const int64_t safeAccumulated = std::max<int64_t>(accumulatedCost, 0);
		const int64_t safeDistance = std::max<int64_t>(edgeDistance, 0);
		const int64_t safeNodeCost = std::max<int64_t>(nodeCost, 0);
		const int64_t total = safeAccumulated + safeDistance + safeNodeCost;
		return static_cast<int32_t>(std::min<int64_t>(total, std::numeric_limits<int32_t>::max()));
	}

	bool PreferCost(int32_t leftCost, size_t leftStableOrder, int32_t rightCost, size_t rightStableOrder)
	{
		if (leftCost != rightCost)
			return leftCost < rightCost;
		return leftStableOrder < rightStableOrder;
	}
}
