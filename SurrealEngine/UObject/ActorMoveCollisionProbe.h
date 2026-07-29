#pragma once

namespace ActorMoveCollisionProbe
{
	struct BlockingRules
	{
		bool ConsiderBlocking = false;
		bool MovingActorUsesPlayerBlocking = false;
		bool MovingActorBlocksActors = false;
		bool MovingActorBlocksPlayers = false;
		bool MovingActorBlocksWorld = false;
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
		// A mover counts as level geometry: whether it stops the moving actor is its own
		// decision (MovingActorBlocksWorld), not a symmetric actor-vs-actor block flag check.
		// Requiring both sides made movers invisible to projectiles, whose class defaults are
		// bBlockActors=bBlockPlayers=false, so no door or lift could ever stop a rocket.
		bool HitActorIsBrush = false;
		// True if the moving actor started this sweep already inside HitActor's cylinder and is
		// moving apart from it. A sweep that begins in contact can otherwise report a blocking
		// hit in every direction, including straight back out, leaving a pawn that starts inside
		// a decoration or another pawn with no way to ever free itself.
		bool IsPenetratingSeparation = false;
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
