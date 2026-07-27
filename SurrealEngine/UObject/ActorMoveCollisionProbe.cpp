#include "ActorMoveCollisionProbe.h"

namespace ActorMoveCollisionProbe
{
	bool IsBlockingHit(const BlockingRules& rules, const HitProperties& hit)
	{
		if (!rules.ConsiderBlocking)
			return false;
		if (hit.IsWorld)
			return true;
		if (!hit.HasActor)
			return true;
		if (hit.IsPenetratingSeparation)
			return false;

		bool isBlocking;
		if (hit.HitActorIsBrush)
		{
			isBlocking = rules.MovingActorBlocksWorld
				&& (rules.MovingActorUsesPlayerBlocking ? hit.HitActorBlocksPlayers : hit.HitActorBlocksActors);
		}
		else
		{
			const bool usePlayerBlocking = rules.MovingActorUsesPlayerBlocking
				|| hit.HitActorUsesPlayerBlocking;
			isBlocking = usePlayerBlocking
				? hit.HitActorBlocksPlayers && rules.MovingActorBlocksPlayers
				: hit.HitActorBlocksActors && rules.MovingActorBlocksActors;
		}

		return isBlocking
			&& (rules.OwnBaseIsBlocking || !hit.HitActorIsBasedOnMovingActor)
			&& !hit.MovingActorIsBasedOnHitActor;
	}
}
