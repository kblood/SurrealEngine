#include "UObject/PawnFallingHitWallCallbackWitness.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	PawnMovement::FallingHitWallCallbackState ValidState()
	{
		PawnMovement::FallingHitWallCallbackState state;
		state.Physics = PawnMovement::FallingHitWallCallbackWitnessFallingPhysics;
		state.HasStateFrame = true;
		state.Location = vec3(1.0f, 2.0f, 3.0f);
		state.Velocity = vec3(4.0f, 5.0f, 6.0f);
		state.Acceleration = vec3(7.0f, 8.0f, 9.0f);
		state.Destination = vec3(10.0f, 11.0f, 12.0f);
		state.Focus = vec3(13.0f, 14.0f, 15.0f);
		state.MoveTarget = reinterpret_cast<const void*>(0x1);
		state.MoveTimer = 1.0f;
		state.LatentState = 3;
		return state;
	}
}

int main()
{
	using namespace PawnMovement;
	const FallingHitWallCallbackState state = ValidState();
	Expect(EvaluateFallingHitWallCallbackWitness(true, state, state)
		== FallingHitWallCallbackWitnessDecision::ExactNoOp,
		"a finite falling static-world callback with no observed state changes is exact no-op evidence");
	Expect(EvaluateFallingHitWallCallbackWitness(false, state, state)
		== FallingHitWallCallbackWitnessDecision::Ineligible,
		"dynamic or mover contact cannot certify a no-op callback");
	auto invalid = state;
	invalid.Physics = 1;
	Expect(EvaluateFallingHitWallCallbackWitness(true, invalid, invalid)
		== FallingHitWallCallbackWitnessDecision::Ineligible,
		"non-falling callbacks cannot certify the falling handler");
	invalid = state;
	invalid.Velocity.x = std::numeric_limits<float>::quiet_NaN();
	Expect(EvaluateFallingHitWallCallbackWitness(true, invalid, invalid)
		== FallingHitWallCallbackWitnessDecision::Ineligible,
		"non-finite callback state fails closed");

	auto changed = state;
	changed.Acceleration.x += 1.0f;
	Expect(EvaluateFallingHitWallCallbackWitness(true, state, changed)
		== FallingHitWallCallbackWitnessDecision::MutationObserved,
		"acceleration changes are observable callback mutations");
	changed = state;
	changed.MoveTarget = nullptr;
	Expect(EvaluateFallingHitWallCallbackWitness(true, state, changed)
		== FallingHitWallCallbackWitnessDecision::MutationObserved,
		"movement-target changes are observable callback mutations");
	changed = state;
	changed.Location.x += FallingHitWallCallbackWitnessTolerance * 2.0f;
	Expect(EvaluateFallingHitWallCallbackWitness(true, state, changed)
		== FallingHitWallCallbackWitnessDecision::MutationObserved,
		"material location changes are observable callback mutations");
}
