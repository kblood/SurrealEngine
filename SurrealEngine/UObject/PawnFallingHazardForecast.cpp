#include "PawnFallingHazardForecast.h"

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

		float Length(const vec3& value)
		{
			return std::sqrt(dot(value, value));
		}

		bool Near(float left, float right, float tolerance)
		{
			return std::isfinite(left) && std::isfinite(right)
				&& std::abs(left - right) <= tolerance;
		}

		bool Near(const vec3& left, const vec3& right, float tolerance)
		{
			return Near(left.x, right.x, tolerance)
				&& Near(left.y, right.y, tolerance)
				&& Near(left.z, right.z, tolerance);
		}

		bool SameZone(const FallingHazardZoneId& left,
			const FallingHazardZoneId& right)
		{
			return left.Known && right.Known
				&& left.ZoneActorId == right.ZoneActorId
				&& left.ZoneNumber == right.ZoneNumber;
		}

		bool ValidZone(const FallingHazardZoneId& zone)
		{
			return zone.Known && zone.ZoneActorId > 0;
		}

		bool KnownPoint(const FallingHazardForecastPointObservation& point)
		{
			return ValidZone(point.Center.Identity) && ValidZone(point.Foot.Identity)
				&& ValidZone(point.Head.Identity) && ValidZone(point.Physics.Identity);
		}

		bool FinitePhysics(const FallingHazardForecastZoneObservation& zone)
		{
			return IsFinite(zone.Gravity) && IsFinite(zone.ZoneVelocity)
				&& std::isfinite(zone.TerminalVelocity)
				&& zone.TerminalVelocity >= 0.0f;
		}

		bool ValidPhysicsInput(const FallingHazardForecastInput& input)
		{
			return input.State.Valid && IsFinite(input.State.Location)
				&& IsFinite(input.State.Velocity)
				&& input.State.NonWalkableStaticContacts >= 0
				&& IsFinite(input.Acceleration)
				&& std::isfinite(input.GroundSpeed) && input.GroundSpeed >= 0.0f
				&& std::isfinite(input.PhysicsSliceElapsed)
				&& input.PhysicsSliceElapsed > 0.0f
				&& input.PhysicsSliceElapsed <= FallingParityMaximumPhysicsSubstep
				&& !input.Bounce;
		}

		bool DamageBearing(const FallingHazardForecastZoneObservation& zone)
		{
			return zone.PainZone && zone.DamagePerSecond > 0;
		}

		bool AnyWater(const FallingHazardForecastPointObservation& point)
		{
			return point.Center.WaterZone || point.Foot.WaterZone
				|| point.Head.WaterZone || point.Physics.WaterZone;
		}

		FallingParityCollisionKind ToParityCollision(
			FallingHazardCollisionKind collision)
		{
			switch (collision)
			{
			case FallingHazardCollisionKind::Clear:
				return FallingParityCollisionKind::Clear;
			case FallingHazardCollisionKind::StaticWorld:
				return FallingParityCollisionKind::StaticWorld;
			case FallingHazardCollisionKind::Mover:
				return FallingParityCollisionKind::Mover;
			case FallingHazardCollisionKind::DynamicActor:
				return FallingParityCollisionKind::DynamicActor;
			default:
				return FallingParityCollisionKind::Unknown;
			}
		}

		FallingHazardForecastUpdate MakeUpdate(
			const FallingHazardForecastState& state, bool complete)
		{
			FallingHazardForecastUpdate update;
			update.State = state;
			update.Probe = state.PendingProbe;
			update.Result = state.Result;
			update.Complete = complete;
			return update;
		}

		FallingHazardForecastUpdate Complete(FallingHazardForecastState state,
			FallingHazardForecast classification, FallingHazardForecastReason reason)
		{
			state.Active = false;
			state.PendingProbe.Valid = false;
			state.Result.Classification = classification;
			state.Result.Reason = reason;
			state.Result.Elapsed = state.Elapsed;
			state.Result.PathDistance = state.PathDistance;
			state.Result.SegmentCount = state.ExpectedSegmentCount;
			state.Result.SampleCount = state.SampleCount;
			return MakeUpdate(state, true);
		}

		FallingHazardForecastUpdate Pending(FallingHazardForecastState state)
		{
			state.Active = true;
			state.Result.Classification = FallingHazardForecast::Unknown;
			state.Result.Reason = FallingHazardForecastReason::PendingProbe;
			state.Result.Elapsed = state.Elapsed;
			state.Result.PathDistance = state.PathDistance;
			state.Result.SegmentCount = state.ExpectedSegmentCount;
			state.Result.SampleCount = state.SampleCount;
			return MakeUpdate(state, false);
		}

		bool ValidLimits(const FallingHazardForecastInput& input)
		{
			return std::isfinite(input.MaximumElapsed)
				&& input.MaximumElapsed > 0.0f
				&& input.MaximumElapsed <= FallingHazardForecastMaximumElapsed
				&& std::isfinite(input.MaximumPathDistance)
				&& input.MaximumPathDistance > 0.0f
				&& input.MaximumPathDistance
					<= FallingHazardForecastMaximumPathDistance
				&& std::isfinite(input.MaximumSampleSpacing)
				&& input.MaximumSampleSpacing > 0.0f
				&& input.MaximumSampleSpacing
					<= FallingHazardForecastMaximumSampleSpacing
				&& input.MaximumSegments > 0
				&& input.MaximumSegments <= FallingHazardForecastMaximumSegments
				&& input.MaximumSamples > 0
				&& input.MaximumSamples <= FallingHazardForecastMaximumSamples;
		}

		bool ValidContinuation(const FallingHazardForecastInput& input)
		{
			if (input.Phase == FallingHazardForecastPhase::FullStep)
				return true;
			const FallingHazardForecastContinuationSeed& seed = input.Continuation;
			if (seed.Phase != input.Phase || !std::isfinite(seed.PrechargedElapsed)
				|| seed.PrechargedElapsed <= 0.0f
				|| seed.PrechargedElapsed > input.MaximumElapsed
					+ FallingHazardForecastElapsedTolerance
				|| !IsFinite(seed.IterationStartLocation)
				|| !IsFinite(seed.ExpectedContinuationOrigin)
				|| !Near(input.State.Location, seed.ExpectedContinuationOrigin,
					FallingHazardForecastVectorTolerance)
				|| !std::isfinite(seed.IterationOldVelocityZ)
				|| !IsFinite(seed.PendingDelta)
				|| dot(seed.PendingDelta, seed.PendingDelta) <= 0.0001f
				|| !IsUnitNormal(seed.FirstHitNormal)
				|| !IsFinite(seed.DesiredDirection)
				|| dot(seed.DesiredDirection, seed.DesiredDirection) < 0.99f
				|| dot(seed.DesiredDirection, seed.DesiredDirection) > 1.01f)
				return false;
			if (input.Phase == FallingHazardForecastPhase::ThirdContinuation)
			{
				return IsUnitNormal(seed.SecondHitNormal)
					&& std::isfinite(seed.SecondHitFraction)
					&& seed.SecondHitFraction >= 0.0f
					&& seed.SecondHitFraction < 1.0f;
			}
			return true;
		}

		bool ScheduleProbe(FallingHazardForecastState& state,
			FallingHazardSweepLeg leg, const vec3& origin, const vec3& delta,
			float elapsedContribution)
		{
			if (state.ExpectedSegmentCount >= state.Input.MaximumSegments)
				return false;
			state.PendingProbe.Valid = true;
			state.PendingProbe.Leg = leg;
			state.PendingProbe.SegmentOrdinal = state.ExpectedSegmentCount;
			state.PendingProbe.Origin = origin;
			state.PendingProbe.Delta = delta;
			state.PendingProbe.ElapsedContribution = elapsedContribution;
			return IsFinite(origin) && IsFinite(delta)
				&& std::isfinite(elapsedContribution)
				&& elapsedContribution >= 0.0f;
		}

		FallingHazardForecastUpdate ScheduleDirect(FallingHazardForecastState state)
		{
			if (state.Elapsed + state.Input.PhysicsSliceElapsed
				> state.Input.MaximumElapsed + FallingHazardForecastElapsedTolerance)
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::ElapsedHorizonExceeded);
			}
			if (state.ExpectedSegmentCount >= state.Input.MaximumSegments)
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::SegmentCapExceeded);
			}

			FallingParityEnvironment environment;
			environment.Acceleration = state.Input.Acceleration;
			environment.Gravity = state.Input.StartingZones.Physics.Gravity;
			environment.ZoneVelocity = state.Input.StartingZones.Physics.ZoneVelocity;
			environment.GroundSpeed = state.Input.GroundSpeed;
			environment.TerminalVelocity =
				state.Input.StartingZones.Physics.TerminalVelocity;
			environment.Elapsed = state.Input.PhysicsSliceElapsed;
			environment.WaterPhysics = false;
			environment.Bounce = state.Input.Bounce;
			state.ActiveParityStep = BeginFallingParityStep(
				state.PhysicsState, environment);
			if (state.ActiveParityStep.Kind
				!= FallingParityTransitionKind::ProbeDirectSweep)
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::InvalidInput);
			}
			if (!ScheduleProbe(state, FallingHazardSweepLeg::Direct,
				state.ActiveParityStep.StepStart.Location,
				state.ActiveParityStep.DirectDelta,
				state.Input.PhysicsSliceElapsed))
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::InvalidInput);
			}
			return Pending(state);
		}

		FallingHazardForecastReason ValidateSamples(
			const FallingHazardForecastState& state,
			const FallingHazardForecastSweepObservation& observation,
			float traveledDistance)
		{
			if (observation.SampleCapExhausted)
				return FallingHazardForecastReason::SampleCapExceeded;
			if (observation.Samples.size() > FallingHazardForecastMaximumSamples
				|| state.SampleCount > state.Input.MaximumSamples
				|| observation.Samples.size()
					> state.Input.MaximumSamples - state.SampleCount)
			{
				return FallingHazardForecastReason::SampleCapExceeded;
			}
			size_t required = 1;
			if (traveledDistance > FallingHazardForecastVectorTolerance)
			{
				const float sampleCount = std::ceil(
					traveledDistance / state.Input.MaximumSampleSpacing);
				if (!std::isfinite(sampleCount)
					|| sampleCount > static_cast<float>(state.Input.MaximumSamples))
				{
					return FallingHazardForecastReason::SampleCapExceeded;
				}
				required = static_cast<size_t>(sampleCount);
			}
			if (observation.Samples.size() != required)
				return FallingHazardForecastReason::IncompleteSampleCoverage;
			float previous = 0.0f;
			for (size_t index = 0; index < observation.Samples.size(); index++)
			{
				const float distance = observation.Samples[index].DistanceAlongSegment;
				if (!std::isfinite(distance) || distance < 0.0f
					|| (index > 0 && distance <= previous)
					|| distance - previous > state.Input.MaximumSampleSpacing
						+ FallingHazardForecastVectorTolerance)
				{
					return FallingHazardForecastReason::IncompleteSampleCoverage;
				}
				previous = distance;
			}
			if (!Near(previous, traveledDistance,
				FallingHazardForecastVectorTolerance))
				return FallingHazardForecastReason::IncompleteSampleCoverage;
			return FallingHazardForecastReason::PendingProbe;
		}

		FallingHazardForecastReason EvaluateSamples(
			FallingHazardForecastState& state,
			const FallingHazardForecastSweepObservation& observation)
		{
			bool transient = false;
			for (size_t index = 0; index < observation.Samples.size(); index++)
			{
				const FallingHazardForecastPointObservation& point =
					observation.Samples[index].Zones;
				if (!KnownPoint(point) || !FinitePhysics(point.Physics))
					return FallingHazardForecastReason::UnknownZoneSample;
				const bool last = index + 1 == observation.Samples.size();
				const bool harmful = DamageBearing(point.Foot);
				if (harmful)
				{
					if (!last)
						transient = true;
					else
					{
						state.Result.TransientHarmfulPainObserved = transient;
						state.Result.ExpectedHarmfulFootZone = point.Foot.Identity;
						state.Result.ExpectedHarmfulPhysicsZone = point.Physics.Identity;
						state.Result.ExpectedHarmfulWaterEntry = point.Foot.WaterZone;
						state.Result.ExpectedHarmfulDamageTypeMatchesReduced =
							point.Foot.DamageTypeMatchesReduced;
						state.Result.ExpectedBotAvoidanceRelevant =
							!point.Foot.DamageTypeMatchesReduced;
						state.Result.FirstHarmfulPainDepth = 0.4f
							+ (DamageBearing(point.Center) ? 0.4f : 0.0f)
							+ (DamageBearing(point.Head) ? 0.2f : 0.0f);
						return FallingHazardForecastReason::HarmfulFootPainAtEndpoint;
					}
				}
				if (AnyWater(point))
					return FallingHazardForecastReason::WaterBeforeHarm;
				if (!SameZone(point.Physics.Identity,
					state.Input.StartingZones.Physics.Identity))
					return FallingHazardForecastReason::PhysicsZoneChanged;
				if (!Near(point.Physics.Gravity,
					state.Input.StartingZones.Physics.Gravity,
					FallingHazardForecastVectorTolerance)
					|| !Near(point.Physics.ZoneVelocity,
						state.Input.StartingZones.Physics.ZoneVelocity,
						FallingHazardForecastVectorTolerance)
					|| !Near(point.Physics.TerminalVelocity,
						state.Input.StartingZones.Physics.TerminalVelocity,
						FallingHazardForecastVectorTolerance))
				{
					return FallingHazardForecastReason::PhysicsEnvironmentChanged;
				}
				if (!harmful
					&& (!SameZone(point.Center.Identity,
						state.Input.StartingZones.Center.Identity)
						|| !SameZone(point.Foot.Identity,
							state.Input.StartingZones.Foot.Identity)
						|| !SameZone(point.Head.Identity,
							state.Input.StartingZones.Head.Identity)))
				{
					return FallingHazardForecastReason::ZoneTransitionRequiresCallback;
				}
			}
			if (transient)
			{
				state.Result.TransientHarmfulPainObserved = true;
				return FallingHazardForecastReason::TransientHarmfulPain;
			}
			return FallingHazardForecastReason::PendingProbe;
		}

		FallingHazardForecastReason CollisionReason(
			FallingHazardCollisionKind collision)
		{
			switch (collision)
			{
			case FallingHazardCollisionKind::Mover:
				return FallingHazardForecastReason::MoverCollisionUnknown;
			case FallingHazardCollisionKind::DynamicActor:
				return FallingHazardForecastReason::DynamicCollisionUnknown;
			case FallingHazardCollisionKind::Unknown:
				return FallingHazardForecastReason::InvalidProbe;
			default:
				return FallingHazardForecastReason::PendingProbe;
			}
		}
	}

	FallingHazardForecastUpdate BeginFallingHazardForecast(
		const FallingHazardForecastInput& input)
	{
		FallingHazardForecastState state;
		state.Input = input;
		state.PhysicsState = input.State;
		state.Continuation = input.Continuation;
		state.Elapsed = input.Phase == FallingHazardForecastPhase::FullStep
			? 0.0f : input.Continuation.PrechargedElapsed;

		if (!ValidLimits(input) || !ValidPhysicsInput(input)
			|| !ValidContinuation(input)
			|| !KnownPoint(input.StartingZones)
			|| !FinitePhysics(input.StartingZones.Physics))
		{
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::InvalidInput);
		}
		if (DamageBearing(input.StartingZones.Center)
			|| DamageBearing(input.StartingZones.Foot))
		{
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::AlreadyInHarmfulPain);
		}
		if (AnyWater(input.StartingZones))
		{
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::InitialWaterPhysics);
		}
		if (input.Phase == FallingHazardForecastPhase::FullStep)
			return ScheduleDirect(state);

		if (input.Phase == FallingHazardForecastPhase::AlignedContinuation)
		{
			if (!ScheduleProbe(state, FallingHazardSweepLeg::Aligned,
				input.State.Location, input.Continuation.PendingDelta, 0.0f))
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::InvalidInput);
			}
			return Pending(state);
		}

		state.ThirdAdjustment = BuildFallingTwoWallAdjustment(
			input.Continuation.DesiredDirection,
			input.Continuation.PendingDelta,
			input.Continuation.SecondHitNormal,
			input.Continuation.FirstHitNormal,
			input.Continuation.SecondHitFraction);
		if (!ScheduleProbe(state, FallingHazardSweepLeg::TwoWallAdjusted,
			input.State.Location, state.ThirdAdjustment.Delta, 0.0f))
		{
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::InvalidInput);
		}
		return Pending(state);
	}

	FallingHazardForecastUpdate ObserveFallingHazardForecastSweep(
		const FallingHazardForecastState& source,
		const FallingHazardForecastSweepObservation& observation)
	{
		FallingHazardForecastState state = source;
		if (!state.Active || !state.PendingProbe.Valid
			|| !std::isfinite(observation.Fraction)
			|| observation.Fraction < 0.0f || observation.Fraction > 1.0f
			|| (observation.Collision == FallingHazardCollisionKind::Clear
				&& observation.Fraction != 1.0f)
			|| (observation.Collision != FallingHazardCollisionKind::Clear
				&& (observation.Collision == FallingHazardCollisionKind::Unknown
					|| observation.Fraction >= 1.0f
					|| !IsUnitNormal(observation.Normal))))
		{
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::InvalidProbe);
		}

		const float traveledDistance = Length(
			state.PendingProbe.Delta * observation.Fraction);
		if (!std::isfinite(traveledDistance))
		{
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::InvalidProbe);
		}
		if (state.PathDistance + traveledDistance
			> state.Input.MaximumPathDistance + FallingHazardForecastVectorTolerance)
		{
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::PathDistanceCapExceeded);
		}
		const FallingHazardForecastReason sampleValidity = ValidateSamples(
			state, observation, traveledDistance);
		if (sampleValidity != FallingHazardForecastReason::PendingProbe)
		{
			return Complete(state, FallingHazardForecast::Unknown, sampleValidity);
		}

		FallingHazardForecastExpectedSegment& expected =
			state.ExpectedSegments[state.ExpectedSegmentCount++];
		expected.Leg = state.PendingProbe.Leg;
		expected.Ordinal = state.PendingProbe.SegmentOrdinal;
		expected.Origin = state.PendingProbe.Origin;
		expected.RequestedDelta = state.PendingProbe.Delta;
		expected.Collision = observation.Collision;
		expected.HitFraction = observation.Fraction;
		expected.HitNormal = observation.Normal;
		expected.Endpoint = state.PendingProbe.Origin
			+ state.PendingProbe.Delta * observation.Fraction;
		expected.ElapsedContribution = state.PendingProbe.ElapsedContribution;
		expected.FirstSample = state.SampleCount;
		expected.SampleCount = observation.Samples.size();
		state.PathDistance += traveledDistance;
		state.SampleCount += observation.Samples.size();
		state.Elapsed += state.PendingProbe.ElapsedContribution;

		const FallingHazardForecastReason collisionResult =
			CollisionReason(observation.Collision);
		if (collisionResult != FallingHazardForecastReason::PendingProbe)
			return Complete(state, FallingHazardForecast::Unknown, collisionResult);
		if (state.PendingProbe.Leg == FallingHazardSweepLeg::TwoWallAdjusted
			&& state.ThirdAdjustment.Ditch)
		{
			if (!observation.IndependentSupportKnown)
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::DitchSupportUnknown);
			}
			if (observation.IndependentSupportCollision
				!= FallingHazardCollisionKind::StaticWorld)
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::NonStaticDitchSupport);
			}
			if (!IsUnitNormal(observation.IndependentSupportNormal)
				|| observation.IndependentSupportNormal.z
					<= FallingParityWalkableNormalZ)
			{
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::NonWalkableDitchSupport);
			}
		}

		const FallingHazardForecastReason sampleResult = EvaluateSamples(
			state, observation);
		if (sampleResult == FallingHazardForecastReason::HarmfulFootPainAtEndpoint)
		{
			return Complete(state, FallingHazardForecast::HarmfulPainObserved,
				sampleResult);
		}
		if (sampleResult != FallingHazardForecastReason::PendingProbe)
			return Complete(state, FallingHazardForecast::Unknown, sampleResult);

		if (state.PendingProbe.Leg == FallingHazardSweepLeg::Direct)
		{
			FallingParitySweepObservation parityObservation;
			parityObservation.Collision = ToParityCollision(observation.Collision);
			parityObservation.Fraction = observation.Fraction;
			parityObservation.Normal = observation.Normal;
			const FallingParityTransition resolved = ResolveFallingParityDirectSweep(
				state.ActiveParityStep, parityObservation);
			if (resolved.Kind == FallingParityTransitionKind::Landed)
			{
				return Complete(state, FallingHazardForecast::NoHarmfulPainObserved,
					FallingHazardForecastReason::NoHarmfulPainAtStaticLanding);
			}
			if (resolved.Kind == FallingParityTransitionKind::Continue)
			{
				state.PhysicsState = resolved.State;
				state.PhysicsState.NonWalkableStaticContacts = 0;
				return ScheduleDirect(state);
			}
			if (resolved.Kind == FallingParityTransitionKind::ProbeAlignedSweep)
			{
				state.Result.Continuation.Phase =
					FallingHazardForecastPhase::AlignedContinuation;
				state.Result.Continuation.PrechargedElapsed =
					state.Input.PhysicsSliceElapsed;
				state.Result.Continuation.IterationStartLocation =
					state.ActiveParityStep.StepStart.Location;
				state.Result.Continuation.ExpectedContinuationOrigin =
					expected.Endpoint;
				state.Result.Continuation.IterationOldVelocityZ =
					state.ActiveParityStep.StepStart.Velocity.z;
				state.Result.Continuation.PendingDelta = resolved.AlignedDelta;
				state.Result.Continuation.DesiredDirection =
					normalize(state.ActiveParityStep.DirectDelta);
				state.Result.Continuation.FirstHitNormal = observation.Normal;
				if (state.Input.CertifiedStaticHitWallCallbackNoOp)
				{
					state.Continuation = state.Result.Continuation;
					if (!ScheduleProbe(state, FallingHazardSweepLeg::Aligned,
						expected.Endpoint, resolved.AlignedDelta, 0.0f))
					{
						return Complete(state, FallingHazardForecast::Unknown,
							FallingHazardForecastReason::InvalidProbe);
					}
					return Pending(state);
				}
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::HitWallCallbackRequired);
			}
			return Complete(state, FallingHazardForecast::Unknown,
				FallingHazardForecastReason::InvalidProbe);
		}

		if (state.PendingProbe.Leg == FallingHazardSweepLeg::Aligned)
		{
			if (observation.Collision == FallingHazardCollisionKind::StaticWorld
				&& observation.Normal.z > FallingParityWalkableNormalZ)
			{
				return Complete(state, FallingHazardForecast::NoHarmfulPainObserved,
					FallingHazardForecastReason::NoHarmfulPainAtStaticLanding);
			}
			if (observation.Collision == FallingHazardCollisionKind::StaticWorld)
			{
				state.Result.Continuation = state.Continuation;
				state.Result.Continuation.Phase =
					FallingHazardForecastPhase::ThirdContinuation;
				state.Result.Continuation.PendingDelta = state.PendingProbe.Delta;
				state.Result.Continuation.ExpectedContinuationOrigin =
					expected.Endpoint;
				state.Result.Continuation.SecondHitNormal = observation.Normal;
				state.Result.Continuation.SecondHitFraction = observation.Fraction;
				if (state.Input.CertifiedStaticHitWallCallbackNoOp)
				{
					state.Continuation = state.Result.Continuation;
					state.ThirdAdjustment = BuildFallingTwoWallAdjustment(
						state.Continuation.DesiredDirection,
						state.Continuation.PendingDelta,
						state.Continuation.SecondHitNormal,
						state.Continuation.FirstHitNormal,
						state.Continuation.SecondHitFraction);
					if (!ScheduleProbe(state, FallingHazardSweepLeg::TwoWallAdjusted,
						expected.Endpoint, state.ThirdAdjustment.Delta, 0.0f))
					{
						return Complete(state, FallingHazardForecast::Unknown,
							FallingHazardForecastReason::InvalidProbe);
					}
					return Pending(state);
				}
				return Complete(state, FallingHazardForecast::Unknown,
					FallingHazardForecastReason::HitWallCallbackRequired);
			}
			state.PhysicsState.Location = expected.Endpoint;
			state.PhysicsState.Velocity = ReconstructFallingCollisionVelocity(
				state.Continuation.IterationStartLocation, expected.Endpoint,
				state.Input.PhysicsSliceElapsed,
				state.Continuation.IterationOldVelocityZ);
			state.PhysicsState.NonWalkableStaticContacts = 0;
			return ScheduleDirect(state);
		}

		if (state.ThirdAdjustment.Ditch)
		{
			return Complete(state, FallingHazardForecast::NoHarmfulPainObserved,
				FallingHazardForecastReason::NoHarmfulPainAtStaticLanding);
		}
		if (observation.Collision == FallingHazardCollisionKind::StaticWorld
			&& observation.Normal.z > FallingParityWalkableNormalZ)
		{
			return Complete(state, FallingHazardForecast::NoHarmfulPainObserved,
				FallingHazardForecastReason::NoHarmfulPainAtStaticLanding);
		}
		state.PhysicsState.Location = expected.Endpoint;
		state.PhysicsState.Velocity = ReconstructFallingCollisionVelocity(
			state.Continuation.IterationStartLocation, expected.Endpoint,
			state.Input.PhysicsSliceElapsed,
			state.Continuation.IterationOldVelocityZ);
		state.PhysicsState.NonWalkableStaticContacts = 0;
		return ScheduleDirect(state);
	}

	bool FallingHazardForecastExpectedSegmentMatches(
		const FallingHazardForecastExpectedSegment& expected,
		const FallingHazardForecastExpectedSegment& actual,
		float vectorTolerance, float elapsedTolerance)
	{
		return expected.Leg == actual.Leg && expected.Ordinal == actual.Ordinal
			&& Near(expected.Origin, actual.Origin, vectorTolerance)
			&& Near(expected.RequestedDelta, actual.RequestedDelta, vectorTolerance)
			&& expected.Collision == actual.Collision
			&& Near(expected.HitFraction, actual.HitFraction, vectorTolerance)
			&& Near(expected.HitNormal, actual.HitNormal, vectorTolerance)
			&& Near(expected.Endpoint, actual.Endpoint, vectorTolerance)
			&& Near(expected.ElapsedContribution, actual.ElapsedContribution,
				elapsedTolerance)
			&& expected.FirstSample == actual.FirstSample
			&& expected.SampleCount == actual.SampleCount;
	}
}
