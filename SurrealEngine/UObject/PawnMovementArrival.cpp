#include "PawnMovementArrival.h"

#include <algorithm>
#include <cmath>

namespace PawnMovement
{
	float ArrivalThreshold(const ArrivalQuery& query)
	{
		const float stepDistance = std::sqrt(std::max(query.SpeedSquared, 0.0f)) * std::max(query.Elapsed, 0.0f);
		return std::max(std::max(query.AcceptanceRadius, 0.0f), stepDistance + 1.0f);
	}

	bool HasArrived(const ArrivalQuery& query)
	{
		if (!query.VerticallyReachable)
			return false;
		const float threshold = ArrivalThreshold(query);
		return query.DistanceSquared <= threshold * threshold;
	}
}
