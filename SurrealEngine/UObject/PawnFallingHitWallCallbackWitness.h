#pragma once

#include "Math/vec.h"

#include <cstdint>

namespace PawnMovement
{
	constexpr float FallingHitWallCallbackWitnessTolerance = 0.001f;
	constexpr uint8_t FallingHitWallCallbackWitnessFallingPhysics = 2;

	struct FallingHitWallCallbackState
	{
		uint8_t Physics = 0;
		bool HasStateFrame = false;
		bool JustTeleported = false;
		vec3 Location = vec3(0.0f);
		vec3 Velocity = vec3(0.0f);
		vec3 Acceleration = vec3(0.0f);
		vec3 Destination = vec3(0.0f);
		vec3 Focus = vec3(0.0f);
		const void* MoveTarget = nullptr;
		float MoveTimer = 0.0f;
		uint8_t LatentState = 0;
	};

	enum class FallingHitWallCallbackWitnessDecision
	{
		Ineligible,
		ExactNoOp,
		MutationObserved
	};

	struct FallingHitWallCallbackWitness
	{
		bool StaticWorldCollision = false;
		FallingHitWallCallbackState Before;
		FallingHitWallCallbackState After;
		FallingHitWallCallbackWitnessDecision Decision =
			FallingHitWallCallbackWitnessDecision::Ineligible;
	};

	FallingHitWallCallbackWitnessDecision EvaluateFallingHitWallCallbackWitness(
		bool staticWorldCollision, const FallingHitWallCallbackState& before,
		const FallingHitWallCallbackState& after);
}
