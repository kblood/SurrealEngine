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

	float DirectReachAcceptanceRadius(const DirectReachGoal& goal)
	{
		// Reaching an actor means getting close enough to touch it. A goal that blocks
		// movement can never be stood on top of, and the sweep leaves it a further
		// margin of clearance. The slack on top absorbs the float error a simulated
		// walk of a few hundred units accumulates before it comes to rest there.
		const float arrivalSlack = 0.01f;
		const float touchRadius = goal.GoalCollides
			? std::max(goal.PawnRadius + goal.GoalRadius + goal.SweepMargin, 1.0f)
			: 1.0f;
		return touchRadius + arrivalSlack;
	}

	float DirectReachVerticalReach(const DirectReachGoal& goal)
	{
		return goal.GoalCollides
			? goal.PawnHeight + goal.GoalHeight + goal.MaxStepHeight
			: goal.PawnHeight;
	}
}
