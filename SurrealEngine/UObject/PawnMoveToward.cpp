#include "PawnMoveToward.h"

namespace PawnMovement
{
	MoveTowardPoll PrepareMoveTowardPoll(const vec3& targetLocation, bool advancedTactics)
	{
		return { targetLocation, advancedTactics };
	}

	bool AbortMoveTowardAfterAlterDestination(bool pawnDeleted, bool targetUsable)
	{
		return pawnDeleted || !targetUsable;
	}

	vec3 ReflectTacticalDestination(const vec3& unalteredDestination, const vec3& preferredDestination)
	{
		return unalteredDestination * 2.0f - preferredDestination;
	}

	vec3 SelectTacticalDestination(const vec3& unalteredDestination, const vec3& preferredDestination,
		bool preferredClear, bool oppositeClear)
	{
		if (preferredClear)
			return preferredDestination;
		const vec3 oppositeDestination = ReflectTacticalDestination(unalteredDestination, preferredDestination);
		return oppositeClear ? oppositeDestination : unalteredDestination;
	}
}
