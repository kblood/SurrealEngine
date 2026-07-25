#pragma once

#include "Math/vec.h"

namespace PawnMovement
{
	// Pure reconstruction of the two observable walking callback predicates.
	// The MinHitWall predicate intentionally does not normalize HitNormal: UE1
	// collision supplies that normal, and the Pawn contract is its dot product
	// with Velocity.Normal.
	struct WalkingHitWallDispatchDecision
	{
		bool Valid = false;
		float NormalVelocityDot = 0.0f;
		bool LegacyVerticalWallBand = false;
		bool MinHitWallDispatch = false;
	};

	WalkingHitWallDispatchDecision EvaluateWalkingHitWallDispatch(
		const vec3& hitNormal, const vec3& velocity, float minHitWall);
}
