#include "PawnFallingParityForecast.h"

#include <algorithm>
#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z);
		}

		bool IsUnitNormal(const vec3& normal)
		{
			if (!IsFinite(normal))
				return false;
			const float lengthSquared = dot(normal, normal);
			return lengthSquared >= 0.99f && lengthSquared <= 1.01f;
		}

		FallingParityTransition Unknown(const FallingParityTransition& source,
			FallingParityReason reason)
		{
			FallingParityTransition result = source;
			result.Kind = FallingParityTransitionKind::Unknown;
			result.Reason = reason;
			result.State.Valid = false;
			return result;
		}

		FallingParityReason CollisionFailureReason(FallingParityCollisionKind collision)
		{
			switch (collision)
			{
			case FallingParityCollisionKind::Mover:
				return FallingParityReason::MoverCollisionUnknown;
			case FallingParityCollisionKind::DynamicActor:
				return FallingParityReason::DynamicActorCollisionUnknown;
			default:
				return FallingParityReason::InvalidSweepEvidence;
			}
		}

		bool ValidCollisionObservation(const FallingParitySweepObservation& observation)
		{
			if (!std::isfinite(observation.Fraction)
				|| observation.Fraction < 0.0f || observation.Fraction > 1.0f)
				return false;
			if (observation.Collision == FallingParityCollisionKind::Clear)
				return observation.Fraction == 1.0f;
			if (observation.Collision == FallingParityCollisionKind::Unknown
				|| observation.Fraction >= 1.0f)
				return false;
			return IsUnitNormal(observation.Normal);
		}
	}

	FallingParitySubstepSchedule BuildFallingParitySubstepSchedule(
		float elapsed, size_t maximumSubsteps)
	{
		FallingParitySubstepSchedule result;
		if (!std::isfinite(elapsed) || elapsed <= 0.0f || maximumSubsteps == 0
			|| maximumSubsteps > result.Elapsed.size())
			return result;

		for (float timeLeft = elapsed; timeLeft > 0.0f;
			timeLeft -= FallingParityMaximumPhysicsSubstep)
		{
			if (result.Count >= maximumSubsteps)
			{
				result.Exhausted = true;
				return result;
			}
			result.Elapsed[result.Count++] = std::min(
				timeLeft, FallingParityMaximumPhysicsSubstep);
		}
		result.Valid = result.Count > 0;
		return result;
	}

	FallingParityTransition BeginFallingParityStep(
		const FallingParityState& state,
		const FallingParityEnvironment& environment)
	{
		FallingParityTransition result;
		result.StepStart = state;
		result.State = state;
		result.Elapsed = environment.Elapsed;
		if (!state.Valid || !IsFinite(state.Location) || !IsFinite(state.Velocity)
			|| state.NonWalkableStaticContacts < 0)
			return Unknown(result, FallingParityReason::InvalidState);
		if (environment.WaterPhysics)
			return Unknown(result, FallingParityReason::WaterPhysicsUnknown);
		if (environment.Bounce)
			return Unknown(result, FallingParityReason::BouncePhysicsUnknown);
		if (!IsFinite(environment.Acceleration) || !IsFinite(environment.Gravity)
			|| !IsFinite(environment.ZoneVelocity)
			|| !std::isfinite(environment.GroundSpeed)
			|| !std::isfinite(environment.TerminalVelocity)
			|| !std::isfinite(environment.Elapsed)
			|| environment.GroundSpeed < 0.0f
			|| environment.TerminalVelocity < 0.0f
			|| environment.Elapsed <= 0.0f
			|| environment.Elapsed > FallingParityMaximumPhysicsSubstep)
			return Unknown(result, FallingParityReason::InvalidEnvironment);

		vec3 velocity = state.Velocity
			+ (environment.Acceleration * 1.5f + environment.Gravity * 2.0f)
			* (0.5f * environment.Elapsed);
		const float oldSpeedSquared = dot(state.Velocity.xy(), state.Velocity.xy());
		const float newSpeedSquared = dot(velocity.xy(), velocity.xy());
		if (oldSpeedSquared >= environment.GroundSpeed * environment.GroundSpeed
			&& newSpeedSquared > oldSpeedSquared)
		{
			const float oldSpeed = std::sqrt(oldSpeedSquared);
			velocity = vec3(normalize(velocity.xy()) * oldSpeed, velocity.z);
		}

		const float speedSquared = dot(velocity, velocity);
		if (speedSquared > environment.TerminalVelocity * environment.TerminalVelocity)
			velocity = normalize(velocity) * environment.TerminalVelocity;
		result.DirectDelta = (velocity
			+ environment.ZoneVelocity * environment.Elapsed * 25.0f)
			* environment.Elapsed;
		result.State.Location = state.Location + result.DirectDelta;
		result.State.Velocity = velocity;
		result.State.Valid = IsFinite(result.State.Location)
			&& IsFinite(result.State.Velocity) && IsFinite(result.DirectDelta)
			&& dot(result.DirectDelta, result.DirectDelta) > 0.0001f;
		if (!result.State.Valid)
			return Unknown(result, FallingParityReason::InvalidEnvironment);
		result.Kind = FallingParityTransitionKind::ProbeDirectSweep;
		result.Reason = FallingParityReason::None;
		return result;
	}

	FallingParityTransition ResolveFallingParityDirectSweep(
		const FallingParityTransition& step,
		const FallingParitySweepObservation& observation)
	{
		if (step.Kind != FallingParityTransitionKind::ProbeDirectSweep
			|| !step.StepStart.Valid || !step.State.Valid
			|| !ValidCollisionObservation(observation))
			return Unknown(step, FallingParityReason::InvalidSweepEvidence);
		if (observation.Collision == FallingParityCollisionKind::Clear)
		{
			FallingParityTransition result = step;
			result.Kind = FallingParityTransitionKind::Continue;
			return result;
		}
		if (observation.Collision != FallingParityCollisionKind::StaticWorld)
			return Unknown(step, CollisionFailureReason(observation.Collision));

		FallingParityTransition result = step;
		result.State.Location = step.StepStart.Location
			+ step.DirectDelta * observation.Fraction;
		if (observation.Normal.z >= FallingParityWalkableNormalZ)
		{
			result.Kind = FallingParityTransitionKind::Landed;
			return result;
		}
		if (step.StepStart.NonWalkableStaticContacts
			>= FallingParityMaximumNonWalkableStaticContacts)
		{
			return Unknown(step,
				FallingParityReason::NonWalkableStaticContactLimitExceeded);
		}

		result.State.NonWalkableStaticContacts =
			step.StepStart.NonWalkableStaticContacts + 1;
		result.AlignedDelta = (step.DirectDelta
			- observation.Normal * dot(step.DirectDelta, observation.Normal))
			* (1.0f - observation.Fraction);
		if (!IsFinite(result.AlignedDelta)
			|| dot(step.DirectDelta, result.AlignedDelta) < 0.0f)
			return Unknown(step, FallingParityReason::InvalidSweepEvidence);
		result.Kind = FallingParityTransitionKind::ProbeAlignedSweep;
		return result;
	}

	FallingParityTransition ResolveFallingParityAlignedSweep(
		const FallingParityTransition& alignedSweep,
		const FallingParitySweepObservation& observation)
	{
		if (alignedSweep.Kind != FallingParityTransitionKind::ProbeAlignedSweep
			|| !alignedSweep.StepStart.Valid || !alignedSweep.State.Valid
			|| !std::isfinite(alignedSweep.Elapsed) || alignedSweep.Elapsed <= 0.0f
			|| !ValidCollisionObservation(observation))
			return Unknown(alignedSweep, FallingParityReason::InvalidSweepEvidence);
		if (observation.Collision != FallingParityCollisionKind::Clear
			&& observation.Collision != FallingParityCollisionKind::StaticWorld)
			return Unknown(alignedSweep, CollisionFailureReason(observation.Collision));

		FallingParityTransition result = alignedSweep;
		const float fraction = observation.Collision == FallingParityCollisionKind::Clear
			? 1.0f : observation.Fraction;
		result.State.Location = alignedSweep.State.Location
			+ alignedSweep.AlignedDelta * fraction;
		if (!IsFinite(result.State.Location))
			return Unknown(alignedSweep, FallingParityReason::InvalidSweepEvidence);
		if (observation.Collision == FallingParityCollisionKind::StaticWorld
			&& observation.Normal.z > FallingParityWalkableNormalZ)
		{
			result.Kind = FallingParityTransitionKind::Landed;
			return result;
		}
		if (observation.Collision == FallingParityCollisionKind::StaticWorld)
		{
			if (result.State.NonWalkableStaticContacts
				>= FallingParityMaximumNonWalkableStaticContacts)
			{
				return Unknown(alignedSweep,
					FallingParityReason::NonWalkableStaticContactLimitExceeded);
			}
			result.State.NonWalkableStaticContacts++;
		}
		result.State.Velocity = (result.State.Location
			- result.StepStart.Location) / result.Elapsed;
		result.State.Valid = IsFinite(result.State.Velocity);
		if (!result.State.Valid)
			return Unknown(result, FallingParityReason::InvalidSweepEvidence);
		result.Kind = FallingParityTransitionKind::Continue;
		return result;
	}

	FallingParityTransition EndFallingParityForecastHorizon(
		const FallingParityState& state)
	{
		FallingParityTransition result;
		result.StepStart = state;
		result.State = state;
		return Unknown(result, FallingParityReason::ForecastHorizonExhausted);
	}
}
