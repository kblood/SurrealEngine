#include "PawnWalkingStepPreflight.h"

#include <cmath>

namespace PawnMovement
{
	namespace
	{
		WalkingStepPreflightResult NoDecision(WalkingStepPreflightReason reason)
		{
			return { WalkingStepPreflightDecision::NoDecision, reason };
		}

		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsUnitNormal(const vec3& value)
		{
			const float lengthSquared = dot(value, value);
			return IsFinite(value) && lengthSquared >= 0.99f && lengthSquared <= 1.01f;
		}

		bool IsBoundedDelta(const vec3& delta, float maximumLength)
		{
			const float lengthSquared = dot(delta, delta);
			return IsFinite(delta) && lengthSquared > 0.0001f
				&& lengthSquared <= maximumLength * maximumLength;
		}

		WalkingStepPreflightReason ClassifyStepCollision(WalkingStepCollisionKind collision)
		{
			switch (collision)
			{
			case WalkingStepCollisionKind::Unknown:
				return WalkingStepPreflightReason::UnknownStepEvidence;
			case WalkingStepCollisionKind::Mover:
				return WalkingStepPreflightReason::MoverStepEvidence;
			case WalkingStepCollisionKind::DynamicActor:
				return WalkingStepPreflightReason::DynamicStepEvidence;
			default:
				return WalkingStepPreflightReason::InvalidStepSequence;
			}
		}

		WalkingStepPreflightReason ClassifyFallCollision(WalkingStepCollisionKind collision)
		{
			switch (collision)
			{
			case WalkingStepCollisionKind::Unknown:
				return WalkingStepPreflightReason::UnknownFallEvidence;
			case WalkingStepCollisionKind::Mover:
				return WalkingStepPreflightReason::MoverFallEvidence;
			case WalkingStepCollisionKind::DynamicActor:
				return WalkingStepPreflightReason::DynamicFallEvidence;
			default:
				return WalkingStepPreflightReason::InvalidFallContinuation;
			}
		}
	}

	WalkingStepPreflightResult EvaluateWalkingStepPreflight(
		const WalkingStepPreflightInput& input)
	{
		switch (input.Actor)
		{
		case WalkingStepActorKind::HumanPlayer:
			return NoDecision(WalkingStepPreflightReason::IneligibleHumanPlayer);
		case WalkingStepActorKind::ScriptedPawn:
			return NoDecision(WalkingStepPreflightReason::IneligibleScriptedPawn);
		case WalkingStepActorKind::StockAutonomousPlayerBot:
			break;
		default:
			return NoDecision(WalkingStepPreflightReason::IneligibleUnknownActor);
		}

		if (!input.Walking)
			return NoDecision(WalkingStepPreflightReason::NotWalking);
		if (input.StartSupport.Collision == WalkingStepCollisionKind::Unknown)
			return NoDecision(WalkingStepPreflightReason::UnknownStartSupport);
		if (input.StartSupport.Collision != WalkingStepCollisionKind::StaticBsp)
			return NoDecision(WalkingStepPreflightReason::NonStaticStartSupport);
		if (!IsUnitNormal(input.StartSupport.Normal)
			|| !std::isfinite(input.WalkableNormalZ)
			|| input.StartSupport.Normal.z < input.WalkableNormalZ)
			return NoDecision(WalkingStepPreflightReason::NonWalkableStartSupport);
		if (input.StartSupport.Zone == WalkingStepZoneKind::Unknown)
			return NoDecision(WalkingStepPreflightReason::UnknownStartZone);
		if (input.StartSupport.Zone != WalkingStepZoneKind::Safe)
			return NoDecision(WalkingStepPreflightReason::UnsafeStartZone);
		if (!input.GravityKnown)
			return NoDecision(WalkingStepPreflightReason::UnknownGravity);
		if (!IsFinite(input.Gravity) || input.Gravity.x != 0.0f
			|| input.Gravity.y != 0.0f || input.Gravity.z >= 0.0f)
			return NoDecision(WalkingStepPreflightReason::NonAxialDownwardGravity);
		if (input.UpwardJumpRequested)
			return NoDecision(WalkingStepPreflightReason::UpwardJumpRequested);

		if (!std::isfinite(input.WalkableNormalZ) || input.WalkableNormalZ <= 0.0f
			|| input.WalkableNormalZ > 1.0f
			|| !std::isfinite(input.MaximumStepDelta) || input.MaximumStepDelta <= 0.0f
			|| !std::isfinite(input.MaximumFallSegmentDelta)
			|| input.MaximumFallSegmentDelta <= 0.0f
			|| !std::isfinite(input.MaximumForecastDrop) || input.MaximumForecastDrop <= 0.0f
			|| !std::isfinite(input.MaximumVerticalWallNormalZ)
			|| input.MaximumVerticalWallNormalZ < 0.0f
			|| input.MaximumVerticalWallNormalZ >= input.WalkableNormalZ)
			return NoDecision(WalkingStepPreflightReason::InvalidBounds);

		if (!IsBoundedDelta(input.StepUp.Delta, input.MaximumStepDelta)
			|| !IsBoundedDelta(input.Forward.Delta, input.MaximumStepDelta)
			|| !IsBoundedDelta(input.StepDown.Delta, input.MaximumStepDelta)
			|| input.StepUp.Delta.x != 0.0f || input.StepUp.Delta.y != 0.0f
			|| input.StepUp.Delta.z <= 0.0f
			|| input.Forward.Delta.z != 0.0f
			|| input.StepDown.Delta.x != 0.0f || input.StepDown.Delta.y != 0.0f
			|| input.StepDown.Delta.z >= 0.0f)
			return NoDecision(WalkingStepPreflightReason::InvalidStepDelta);
		if (input.SlideCount > input.Slides.size())
			return NoDecision(WalkingStepPreflightReason::InvalidStepSequence);
		for (size_t index = 0; index < input.SlideCount; index++)
		{
			if (!IsBoundedDelta(input.Slides[index].Delta, input.MaximumStepDelta)
				|| input.Slides[index].Delta.z != 0.0f)
				return NoDecision(WalkingStepPreflightReason::InvalidStepDelta);
		}

		const WalkingStepSweepObservation* observations[] = {
			&input.StepUp, &input.Forward, &input.StepDown
		};
		for (const WalkingStepSweepObservation* observation : observations)
		{
			if (observation->Collision == WalkingStepCollisionKind::Unknown
				|| observation->Collision == WalkingStepCollisionKind::Mover
				|| observation->Collision == WalkingStepCollisionKind::DynamicActor)
				return NoDecision(ClassifyStepCollision(observation->Collision));
		}
		for (size_t index = 0; index < input.SlideCount; index++)
		{
			const WalkingStepCollisionKind collision = input.Slides[index].Collision;
			if (collision == WalkingStepCollisionKind::Unknown
				|| collision == WalkingStepCollisionKind::Mover
				|| collision == WalkingStepCollisionKind::DynamicActor)
				return NoDecision(ClassifyStepCollision(collision));
		}

		if (input.StepUp.Collision != WalkingStepCollisionKind::Clear)
			return NoDecision(WalkingStepPreflightReason::InvalidStepSequence);
		if (input.Forward.Collision == WalkingStepCollisionKind::Clear)
		{
			if (input.SlideCount != 0)
				return NoDecision(WalkingStepPreflightReason::InvalidStepSequence);
		}
		else if (input.Forward.Collision == WalkingStepCollisionKind::StaticBsp)
		{
			if (!IsUnitNormal(input.Forward.HitNormal)
				|| input.Forward.HitNormal.z >= input.WalkableNormalZ
				|| input.SlideCount != 1
				|| input.Slides[0].Collision != WalkingStepCollisionKind::Clear)
				return NoDecision(WalkingStepPreflightReason::InvalidStepSequence);
		}
		else
			return NoDecision(WalkingStepPreflightReason::InvalidStepSequence);

		if (input.StepDown.Collision == WalkingStepCollisionKind::StaticBsp)
		{
			if (IsUnitNormal(input.StepDown.HitNormal)
				&& input.StepDown.HitNormal.z >= input.WalkableNormalZ)
				return NoDecision(WalkingStepPreflightReason::SupportedStepEndpoint);
			return NoDecision(WalkingStepPreflightReason::InvalidStepSequence);
		}
		if (input.StepDown.Collision != WalkingStepCollisionKind::Clear)
			return NoDecision(WalkingStepPreflightReason::InvalidStepSequence);

		const WalkingFallForecastObservation& forecast = input.FallForecast;
		if (!forecast.Complete)
			return NoDecision(WalkingStepPreflightReason::IncompleteFallForecast);
		if (forecast.ContinuationCount > forecast.Continuations.size())
			return NoDecision(WalkingStepPreflightReason::FallContinuationLimitExceeded);
		if (!std::isfinite(forecast.TotalDrop) || forecast.TotalDrop <= 0.0f
			|| forecast.TotalDrop > input.MaximumForecastDrop)
			return NoDecision(WalkingStepPreflightReason::InvalidFallDelta);

		for (size_t index = 0; index < forecast.ContinuationCount; index++)
		{
			const WalkingFallContinuationObservation& continuation = forecast.Continuations[index];
			if (!IsBoundedDelta(continuation.SegmentDelta, input.MaximumFallSegmentDelta))
				return NoDecision(WalkingStepPreflightReason::InvalidFallDelta);
			if (continuation.Collision != WalkingStepCollisionKind::StaticBsp)
				return NoDecision(ClassifyFallCollision(continuation.Collision));
			if (!IsUnitNormal(continuation.HitNormal)
				|| std::abs(continuation.HitNormal.z) > input.MaximumVerticalWallNormalZ)
				return NoDecision(WalkingStepPreflightReason::InvalidFallContinuation);
		}

		if (forecast.Landing.Collision != WalkingStepCollisionKind::StaticBsp)
		{
			const WalkingStepPreflightReason reason = ClassifyFallCollision(
				forecast.Landing.Collision);
			return NoDecision(reason == WalkingStepPreflightReason::InvalidFallContinuation
				? WalkingStepPreflightReason::InvalidFallLanding : reason);
		}
		if (!IsUnitNormal(forecast.Landing.Normal)
			|| forecast.Landing.Normal.z < input.WalkableNormalZ)
			return NoDecision(WalkingStepPreflightReason::InvalidFallLanding);
		if (forecast.Landing.Zone == WalkingStepZoneKind::Unknown)
			return NoDecision(WalkingStepPreflightReason::UnknownLandingZone);
		if (forecast.Landing.Zone != WalkingStepZoneKind::Pain)
			return NoDecision(WalkingStepPreflightReason::SafeFallLanding);
		if (!forecast.PainDamageImmunityKnown)
			return NoDecision(WalkingStepPreflightReason::UnknownPainDamageImmunity);
		if (forecast.PainDamageImmune)
			return NoDecision(WalkingStepPreflightReason::PainDamageImmune);

		return {
			WalkingStepPreflightDecision::AuthorizeUnsafeStepVeto,
			WalkingStepPreflightReason::HarmfulPainFall
		};
	}
}
