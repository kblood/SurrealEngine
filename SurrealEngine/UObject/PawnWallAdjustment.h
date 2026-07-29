#pragma once

#include "Math/vec.h"

namespace PawnMovement
{
	struct EAdjustJumpVelocityInput
	{
		vec3 Location;
		vec3 Focus;
		vec3 Destination;
		float GroundSpeed = 0.0f;
		float JumpZ = 0.0f;
	};

	struct WallAdjustJumpDecisionInput
	{
		vec3 ProposedVelocity;
		bool AIPlayerBot = false;
		bool UnrealTournament = false;
		bool StockBot = false;
		bool WanderingState = false;
		bool KneeSweepClear = false;
		bool NormalDownwardGravity = false;
		bool CurrentlyInPainZone = false;
		bool PredictedHarmfulPainZone = false;
	};

	vec3 CalculateEAdjustJumpVelocity(const EAdjustJumpVelocityInput& input);
	bool ShouldAttemptWallAdjustJump(const WallAdjustJumpDecisionInput& input);
	float WallAdjustmentDistance(float collisionRadius);
	vec3 WallAdjustmentDelta(const vec3& lateralDirection, float collisionRadius);
}
