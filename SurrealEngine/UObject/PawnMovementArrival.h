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

	float ArrivalThreshold(const ArrivalQuery& query);
	bool HasArrived(const ArrivalQuery& query);
}
