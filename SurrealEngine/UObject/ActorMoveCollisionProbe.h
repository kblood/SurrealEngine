#pragma once

namespace ActorMoveCollisionProbe
{
	struct BlockingRules
	{
		bool ConsiderBlocking = false;
		bool MovingActorUsesPlayerBlocking = false;
		bool MovingActorBlocksActors = false;
		bool MovingActorBlocksPlayers = false;
		bool OwnBaseIsBlocking = true;
	};

	struct HitProperties
	{
		bool IsWorld = false;
		bool HasActor = false;
		bool HitActorUsesPlayerBlocking = false;
		bool HitActorBlocksActors = false;
		bool HitActorBlocksPlayers = false;
		bool HitActorIsBasedOnMovingActor = false;
		bool MovingActorIsBasedOnHitActor = false;
	};

	bool IsBlockingHit(const BlockingRules& rules, const HitProperties& hit);

	template<typename Iterator, typename DescribeHit>
	Iterator SelectFirstBlockingHit(
		Iterator begin, Iterator end, const BlockingRules& rules, DescribeHit describeHit)
	{
		for (Iterator current = begin; current != end; ++current)
		{
			if (IsBlockingHit(rules, describeHit(*current)))
				return current;
		}
		return end;
	}
}
