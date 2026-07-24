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

		const bool usePlayerBlocking = rules.MovingActorUsesPlayerBlocking
			|| hit.HitActorUsesPlayerBlocking;
		const bool isBlocking = usePlayerBlocking
			? hit.HitActorBlocksPlayers && rules.MovingActorBlocksPlayers
			: hit.HitActorBlocksActors && rules.MovingActorBlocksActors;

		return isBlocking
			&& (rules.OwnBaseIsBlocking || !hit.HitActorIsBasedOnMovingActor)
			&& !hit.MovingActorIsBasedOnHitActor;
	}
}
