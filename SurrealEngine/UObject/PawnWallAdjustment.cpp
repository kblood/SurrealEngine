#include "PawnWallAdjustment.h"
#include "PawnPainZoneFallPrediction.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace PawnMovement
{
	vec3 CalculateEAdjustJumpVelocity(const EAdjustJumpVelocityInput& input)
	{
		vec3 target = input.Focus;
		if (dot(target - input.Location, target - input.Location) < 0.001f)
			target = input.Destination;

		vec3 horizontalDirection = normalize(target - input.Location);
		horizontalDirection.z = 0.0f;
		vec3 horizontalVelocity = horizontalDirection
			* (length(target - input.Location) / 0.001f);
		const float horizontalSpeed = length(horizontalVelocity);
		if (horizontalSpeed > input.GroundSpeed)
			horizontalVelocity *= input.GroundSpeed / horizontalSpeed;

		return horizontalVelocity + vec3(0.0f, 0.0f, input.JumpZ);
	}

	bool ShouldAttemptWallAdjustJump(const WallAdjustJumpDecisionInput& input)
	{
		const bool finiteProposedJump = std::isfinite(input.ProposedVelocity.x)
			&& std::isfinite(input.ProposedVelocity.y)
			&& std::isfinite(input.ProposedVelocity.z);
		if (!input.KneeSweepClear || !finiteProposedJump)
			return false;

		const bool guardedScriptContext = input.UnrealTournament
			&& input.StockBot && input.WanderingState;
		return !(guardedScriptContext
			&& ShouldVetoPainZoneLedge(input.AIPlayerBot, input.NormalDownwardGravity,
				input.CurrentlyInPainZone, input.PredictedHarmfulPainZone));
	}

	float WallAdjustmentDistance(float collisionRadius)
	{
		if (!std::isfinite(collisionRadius))
			return 0.0f;
		const float boundedRadius = std::min(std::max(collisionRadius, 0.0f), std::numeric_limits<float>::max() * 0.5f);
		return 2.0f * boundedRadius;
	}

	vec3 WallAdjustmentDelta(const vec3& lateralDirection, float collisionRadius)
	{
		return lateralDirection * WallAdjustmentDistance(collisionRadius);
	}
}
