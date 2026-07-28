#pragma once

#include "Math/vec.h"

#include <algorithm>
#include <cmath>

constexpr bool HasHorizontalMovement(float x, float y)
{
	return x != 0.0f || y != 0.0f;
}

constexpr bool HasSpatialMovement(float x, float y, float z)
{
	return x != 0.0f || y != 0.0f || z != 0.0f;
}

namespace ActorMovement
{
	struct FallPredictionState
	{
		vec3 Location;
		vec3 Velocity;
		bool Valid = true;
	};

	struct FallPredictionStep
	{
		vec3 Acceleration;
		vec3 Gravity;
		vec3 ZoneVelocity;
		float GroundSpeed = 0.0f;
		float TerminalVelocity = 0.0f;
		float Elapsed = 0.0f;
	};

	inline bool IsFiniteVector(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	inline FallPredictionState PredictFallStep(
		const FallPredictionState& state, const FallPredictionStep& step)
	{
		FallPredictionState result = state;
		if (!state.Valid || !IsFiniteVector(state.Location) || !IsFiniteVector(state.Velocity)
			|| !IsFiniteVector(step.Acceleration) || !IsFiniteVector(step.Gravity)
			|| !IsFiniteVector(step.ZoneVelocity) || !std::isfinite(step.GroundSpeed)
			|| !std::isfinite(step.TerminalVelocity) || !std::isfinite(step.Elapsed)
			|| step.GroundSpeed < 0.0f || step.TerminalVelocity < 0.0f
			|| step.Elapsed <= 0.0f)
		{
			result.Valid = false;
			return result;
		}

		vec3 velocity = state.Velocity
			+ (step.Acceleration * 1.5f + step.Gravity * 2.0f) * (0.5f * step.Elapsed);
		const float oldSpeedSquared = dot(state.Velocity.xy(), state.Velocity.xy());
		const float newSpeedSquared = dot(velocity.xy(), velocity.xy());
		if (oldSpeedSquared >= step.GroundSpeed * step.GroundSpeed
			&& newSpeedSquared > oldSpeedSquared)
		{
			velocity = vec3(normalize(velocity.xy()) * std::sqrt(oldSpeedSquared), velocity.z);
		}
		if (dot(velocity, velocity) > step.TerminalVelocity * step.TerminalVelocity)
			velocity = normalize(velocity) * step.TerminalVelocity;

		result.Velocity = velocity;
		result.Location = state.Location
			+ (velocity + step.ZoneVelocity * step.Elapsed * 25.0f) * step.Elapsed;
		result.Valid = IsFiniteVector(result.Location) && IsFiniteVector(result.Velocity);
		return result;
	}

	inline int BoundedFallSegmentSampleCount(
		float segmentDistance, float maxSpacing, int maxSamples)
	{
		if (!std::isfinite(segmentDistance) || !std::isfinite(maxSpacing)
			|| segmentDistance <= 0.0f || maxSpacing <= 0.0f || maxSamples <= 0)
			return 0;
		return std::min(static_cast<int>(std::ceil(segmentDistance / maxSpacing)), maxSamples);
	}

	inline bool IsAIControlledPlayer(
		bool isPlayer, bool isPlayerPawn, bool hasPlayerController)
	{
		return isPlayer && (!isPlayerPawn || !hasPlayerController);
	}

	inline bool ShouldVetoPainZoneLedge(bool aiPlayerBot, bool normalDownwardGravity,
		bool currentlyInPainZone, bool predictedHarmfulPainZone)
	{
		return aiPlayerBot && normalDownwardGravity
			&& !currentlyInPainZone && predictedHarmfulPainZone;
	}
}
