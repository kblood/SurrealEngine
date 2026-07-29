#include "PawnPainZoneFallPrediction.h"

#include <algorithm>
#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	FallPredictionState PredictFallStep(const FallPredictionState& state, const FallPredictionStep& step)
	{
		FallPredictionState result = state;
		if (!state.Valid || !IsFinite(state.Location) || !IsFinite(state.Velocity)
			|| !IsFinite(step.Acceleration) || !IsFinite(step.Gravity) || !IsFinite(step.ZoneVelocity)
			|| !std::isfinite(step.GroundSpeed) || !std::isfinite(step.TerminalVelocity)
			|| !std::isfinite(step.Elapsed) || step.GroundSpeed < 0.0f
			|| step.TerminalVelocity < 0.0f || step.Elapsed <= 0.0f)
		{
			result.Valid = false;
			return result;
		}

		vec3 velocity = state.Velocity + (step.Acceleration * 1.5f + step.Gravity * 2.0f) * (0.5f * step.Elapsed);
		const float oldSpeedSquared = dot(state.Velocity.xy(), state.Velocity.xy());
		const float newSpeedSquared = dot(velocity.xy(), velocity.xy());
		if (oldSpeedSquared >= step.GroundSpeed * step.GroundSpeed && newSpeedSquared > oldSpeedSquared)
		{
			const float oldSpeed = std::sqrt(oldSpeedSquared);
			velocity = vec3(normalize(velocity.xy()) * oldSpeed, velocity.z);
		}

		const float speedSquared = dot(velocity, velocity);
		if (speedSquared > step.TerminalVelocity * step.TerminalVelocity)
			velocity = normalize(velocity) * step.TerminalVelocity;

		result.Velocity = velocity;
		result.Location = state.Location + (velocity + step.ZoneVelocity * step.Elapsed * 25.0f) * step.Elapsed;
		result.Valid = IsFinite(result.Location) && IsFinite(result.Velocity);
		return result;
	}

	FirstWallContinuationResult EvaluateFirstWallContinuation(const FirstWallContinuationInput& input)
	{
		FirstWallContinuationResult result;
		if (!input.StepStart.Valid || !input.ProposedEnd.Valid
			|| !IsFinite(input.StepStart.Location) || !IsFinite(input.StepStart.Velocity)
			|| !IsFinite(input.ProposedEnd.Location) || !IsFinite(input.ProposedEnd.Velocity)
			|| !IsFinite(input.HitNormal) || !std::isfinite(input.HitFraction)
			|| !std::isfinite(input.Elapsed) || input.HitFraction < 0.0f
			|| input.HitFraction >= 1.0f || input.Elapsed <= 0.0f)
			return result;

		const float normalLengthSquared = dot(input.HitNormal, input.HitNormal);
		if (normalLengthSquared < 0.99f || normalLengthSquared > 1.01f)
			return result;
		if (input.CollisionKind != FallCollisionKind::StaticWorld)
			return result;
		if (input.HitNormal.z >= 0.7071f)
		{
			result.Decision = FirstWallContinuationDecision::SafeLanding;
			return result;
		}
		if (std::abs(input.HitNormal.z) >= 0.2f
			|| input.PriorWallContactCount < 0 || input.MaxWallContactCount <= 0
			|| input.PriorWallContactCount >= input.MaxWallContactCount)
			return result;
		if (input.PriorWallContactCount > 0)
		{
			const float firstNormalLengthSquared = dot(input.FirstWallNormal, input.FirstWallNormal);
			if (!input.SameStaticBspSurfaceAsFirst || !IsFinite(input.FirstWallNormal)
				|| firstNormalLengthSquared < 0.99f || firstNormalLengthSquared > 1.01f
				|| dot(input.HitNormal, input.FirstWallNormal) < 0.9999f)
				return result;
		}

		const vec3 fullDelta = input.ProposedEnd.Location - input.StepStart.Location;
		if (!IsFinite(fullDelta) || dot(fullDelta, fullDelta) <= 0.0001f)
			return result;

		result.HitSegmentDelta = fullDelta * input.HitFraction;
		result.AlignedSlideDelta = (fullDelta
			- input.HitNormal * dot(fullDelta, input.HitNormal))
			* (1.0f - input.HitFraction);
		result.Next.Location = input.StepStart.Location
			+ result.HitSegmentDelta + result.AlignedSlideDelta;
		result.Next.Velocity = (result.Next.Location - input.StepStart.Location) / input.Elapsed;
		result.Next.Valid = IsFinite(result.Next.Location) && IsFinite(result.Next.Velocity);
		if (!result.Next.Valid)
			return FirstWallContinuationResult();

		result.Decision = FirstWallContinuationDecision::ProbeAlignedSweep;
		return result;
	}

	FirstWallContinuationResult CompleteFirstWallContinuation(
		const FirstWallContinuationResult& candidate, bool alignedSweepClear)
	{
		if (candidate.Decision != FirstWallContinuationDecision::ProbeAlignedSweep
			|| !candidate.Next.Valid || !alignedSweepClear)
			return FirstWallContinuationResult();

		FirstWallContinuationResult result = candidate;
		result.Decision = FirstWallContinuationDecision::Continue;
		return result;
	}

	int BoundedFallSegmentSampleCount(float segmentDistance, float maxSpacing, int maxSamples)
	{
		if (!std::isfinite(segmentDistance) || !std::isfinite(maxSpacing)
			|| segmentDistance <= 0.0f || maxSpacing <= 0.0f || maxSamples <= 0)
			return 0;
		return std::min(static_cast<int>(std::ceil(segmentDistance / maxSpacing)), maxSamples);
	}

	bool IsAIControlledPlayer(bool isPlayer, bool isPlayerPawn, bool hasPlayerController)
	{
		return isPlayer && (!isPlayerPawn || !hasPlayerController);
	}

	bool ShouldVetoPainZoneLedge(bool aiPlayerBot, bool normalDownwardGravity,
		bool currentlyInPainZone, bool predictedHarmfulPainZone)
	{
		return aiPlayerBot && normalDownwardGravity && !currentlyInPainZone && predictedHarmfulPainZone;
	}
}
