#pragma once

#include "Math/vec.h"

namespace PawnMovement
{
	struct MoveTowardPoll
	{
		vec3 Destination;
		bool ApplyAdvancedTactics = false;
	};

	MoveTowardPoll PrepareMoveTowardPoll(const vec3& targetLocation, bool advancedTactics);
	bool AbortMoveTowardAfterAlterDestination(bool pawnDeleted, bool targetUsable);
	vec3 ReflectTacticalDestination(const vec3& unalteredDestination, const vec3& preferredDestination);
	vec3 SelectTacticalDestination(const vec3& unalteredDestination, const vec3& preferredDestination,
		bool preferredClear, bool oppositeClear);
}
