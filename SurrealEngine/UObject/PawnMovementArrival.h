#pragma once

namespace PawnMovement
{
	struct ArrivalQuery
	{
		float DistanceSquared = 0.0f;
		float SpeedSquared = 0.0f;
		float Elapsed = 0.0f;
		float AcceptanceRadius = 1.0f;
		bool VerticallyReachable = true;
	};

	// How close a simulated walk has to get before an actor counts as reached.
	struct DirectReachGoal
	{
		bool GoalCollides = false;
		float PawnRadius = 0.0f;
		float PawnHeight = 0.0f;
		float GoalRadius = 0.0f;
		float GoalHeight = 0.0f;
		float MaxStepHeight = 0.0f;
		float SweepMargin = 0.0f;
	};

	float ArrivalThreshold(const ArrivalQuery& query);
	bool HasArrived(const ArrivalQuery& query);
	float DirectReachAcceptanceRadius(const DirectReachGoal& goal);
	float DirectReachVerticalReach(const DirectReachGoal& goal);
}
