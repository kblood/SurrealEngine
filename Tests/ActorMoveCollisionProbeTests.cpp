#include "UObject/ActorMoveCollisionProbe.h"
#include "Math/vec.h"

#include <array>
#include <iostream>

namespace
{
	struct FakeHit
	{
		float Fraction = 1.0f;
		vec3 Normal = vec3(0.0f);
		const void* Actor = nullptr;
		ActorMoveCollisionProbe::HitProperties Properties;
	};

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	ActorMoveCollisionProbe::BlockingRules DefaultRules()
	{
		ActorMoveCollisionProbe::BlockingRules rules;
		rules.ConsiderBlocking = true;
		rules.MovingActorBlocksActors = true;
		rules.MovingActorBlocksPlayers = true;
		return rules;
	}
}

int main()
{
	using ActorMoveCollisionProbe::SelectFirstBlockingHit;
	auto describe = [](const FakeHit& hit) { return hit.Properties; };

	std::array<FakeHit, 0> clearHits;
	auto clear = SelectFirstBlockingHit(
		clearHits.begin(), clearHits.end(), DefaultRules(), describe);
	if (clear != clearHits.end())
		return Fail("clear sweep selected a blocking hit");

	std::array<FakeHit, 1> worldHits = {{
		{ 0.25f, vec3(-1.0f, 0.0f, 0.0f), nullptr, { .IsWorld = true } }
	}};
	auto world = SelectFirstBlockingHit(
		worldHits.begin(), worldHits.end(), DefaultRules(), describe);
	if (world != worldHits.begin() || world->Fraction != 0.25f
		|| world->Normal != vec3(-1.0f, 0.0f, 0.0f) || world->Actor != nullptr)
	{
		return Fail("world sweep did not preserve the exact selected hit");
	}

	const int pawnIdentity = 1;
	const int moverIdentity = 2;
	const int otherIdentity = 3;
	std::array<FakeHit, 3> orderedHits = {{
		{ 0.10f, vec3(1.0f, 0.0f, 0.0f), &pawnIdentity,
			{ .HasActor = true, .HitActorUsesPlayerBlocking = true, .HitActorBlocksActors = true } },
		{ 0.20f, vec3(0.0f, 1.0f, 0.0f), &moverIdentity,
			{ .HasActor = true, .HitActorBlocksActors = true, .HitActorBlocksPlayers = true } },
		{ 0.30f, vec3(0.0f, 0.0f, 1.0f), &otherIdentity,
			{ .HasActor = true, .HitActorBlocksActors = true, .HitActorBlocksPlayers = true } }
	}};
	auto ordered = SelectFirstBlockingHit(
		orderedHits.begin(), orderedHits.end(), DefaultRules(), describe);
	if (ordered != orderedHits.begin() + 1 || ordered->Actor != &moverIdentity
		|| ordered->Fraction != 0.20f || ordered->Normal != vec3(0.0f, 1.0f, 0.0f))
	{
		return Fail("pawn/mover/other ordering did not preserve the first blocking hit");
	}

	auto playerRules = DefaultRules();
	playerRules.MovingActorUsesPlayerBlocking = true;
	orderedHits[0].Properties.HitActorBlocksPlayers = true;
	auto playerOrdered = SelectFirstBlockingHit(
		orderedHits.begin(), orderedHits.end(), playerRules, describe);
	if (playerOrdered != orderedHits.begin() || playerOrdered->Actor != &pawnIdentity)
		return Fail("player-blocking selection did not preserve pawn identity");

	orderedHits[0].Properties.HitActorIsBasedOnMovingActor = true;
	playerRules.OwnBaseIsBlocking = false;
	auto baseIgnored = SelectFirstBlockingHit(
		orderedHits.begin(), orderedHits.end(), playerRules, describe);
	if (baseIgnored != orderedHits.begin() + 1 || baseIgnored->Actor != &moverIdentity)
		return Fail("own-base exclusion changed candidate ordering");

	orderedHits[1].Properties.MovingActorIsBasedOnHitActor = true;
	auto basedMoverIgnored = SelectFirstBlockingHit(
		orderedHits.begin(), orderedHits.end(), playerRules, describe);
	if (basedMoverIgnored != orderedHits.begin() + 2 || basedMoverIgnored->Actor != &otherIdentity)
		return Fail("moving-base exclusion changed candidate ordering");

	auto disabledRules = DefaultRules();
	disabledRules.ConsiderBlocking = false;
	auto disabled = SelectFirstBlockingHit(
		worldHits.begin(), worldHits.end(), disabledRules, describe);
	if (disabled != worldHits.end())
		return Fail("disabled blocking selected a world hit");

	std::array<FakeHit, 1> unknownHits = {{
		{ 0.5f, vec3(0.0f, 0.0f, 1.0f), &otherIdentity, {} }
	}};
	auto unknown = SelectFirstBlockingHit(
		unknownHits.begin(), unknownHits.end(), DefaultRules(), describe);
	if (unknown != unknownHits.begin())
		return Fail("unknown hit classification did not fail closed");

	return 0;
}
