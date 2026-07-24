#pragma once

#include "PawnFallingHazardTrajectoryModel.h"
#include "PawnFallingParityForecast.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace PawnMovement
{
	constexpr float FallingHazardForecastMaximumElapsed = 4.0f;
	constexpr float FallingHazardForecastMaximumPathDistance = 4096.0f;
	constexpr float FallingHazardForecastMaximumSampleSpacing = 25.0f;
	constexpr size_t FallingHazardForecastMaximumSegments = 256;
	constexpr size_t FallingHazardForecastMaximumSamples = 512;
	constexpr float FallingHazardForecastVectorTolerance = 0.001f;
	constexpr float FallingHazardForecastElapsedTolerance = 0.000001f;

	enum class FallingHazardForecastPhase
	{
		FullStep,
		AlignedContinuation,
		ThirdContinuation
	};

	enum class FallingHazardForecastReason
	{
		PendingProbe,
		HarmfulFootPainAtEndpoint,
		NoHarmfulPainAtStaticLanding,
		InvalidInput,
		InvalidProbe,
		UnknownZoneSample,
		AlreadyInHarmfulPain,
		InitialWaterPhysics,
		WaterBeforeHarm,
		TransientHarmfulPain,
		ZoneTransitionRequiresCallback,
		PhysicsZoneChanged,
		PhysicsEnvironmentChanged,
		HitWallCallbackRequired,
		MoverCollisionUnknown,
		DynamicCollisionUnknown,
		DitchSupportUnknown,
		NonStaticDitchSupport,
		NonWalkableDitchSupport,
		ElapsedHorizonExceeded,
		SegmentCapExceeded,
		PathDistanceCapExceeded,
		SampleCapExceeded,
		IncompleteSampleCoverage
	};

	struct FallingHazardForecastZoneObservation
	{
		FallingHazardZoneId Identity;
		bool PainZone = false;
		int DamagePerSecond = 0;
		bool WaterZone = false;
		bool DamageTypeMatchesReduced = false;
		vec3 Gravity = vec3(0.0f);
		vec3 ZoneVelocity = vec3(0.0f);
		float TerminalVelocity = 0.0f;
	};

	struct FallingHazardForecastPointObservation
	{
		FallingHazardForecastZoneObservation Center;
		FallingHazardForecastZoneObservation Foot;
		FallingHazardForecastZoneObservation Head;
		FallingHazardForecastZoneObservation Physics;
	};

	struct FallingHazardForecastPathSample
	{
		float DistanceAlongSegment = 0.0f;
		FallingHazardForecastPointObservation Zones;
	};

	struct FallingHazardForecastContinuationSeed
	{
		FallingHazardForecastPhase Phase = FallingHazardForecastPhase::FullStep;
		float PrechargedElapsed = 0.0f;
		vec3 IterationStartLocation = vec3(0.0f);
		vec3 ExpectedContinuationOrigin = vec3(0.0f);
		float IterationOldVelocityZ = 0.0f;
		vec3 PendingDelta = vec3(0.0f);
		vec3 DesiredDirection = vec3(0.0f);
		vec3 FirstHitNormal = vec3(0.0f);
		vec3 SecondHitNormal = vec3(0.0f);
		float SecondHitFraction = 1.0f;
	};

	struct FallingHazardForecastInput
	{
		FallingHazardForecastPhase Phase = FallingHazardForecastPhase::FullStep;
		FallingParityState State;
		vec3 Acceleration = vec3(0.0f);
		float GroundSpeed = 0.0f;
		float PhysicsSliceElapsed = 0.0f;
		bool Bounce = false;
		FallingHazardForecastPointObservation StartingZones;
		FallingHazardForecastContinuationSeed Continuation;
		float MaximumElapsed = FallingHazardForecastMaximumElapsed;
		float MaximumPathDistance = FallingHazardForecastMaximumPathDistance;
		float MaximumSampleSpacing = FallingHazardForecastMaximumSampleSpacing;
		size_t MaximumSegments = FallingHazardForecastMaximumSegments;
		size_t MaximumSamples = FallingHazardForecastMaximumSamples;
	};

	struct FallingHazardForecastProbeRequest
	{
		bool Valid = false;
		FallingHazardSweepLeg Leg = FallingHazardSweepLeg::Direct;
		size_t SegmentOrdinal = 0;
		vec3 Origin = vec3(0.0f);
		vec3 Delta = vec3(0.0f);
		float ElapsedContribution = 0.0f;
	};

	struct FallingHazardForecastSweepObservation
	{
		FallingHazardCollisionKind Collision = FallingHazardCollisionKind::Unknown;
		float Fraction = 1.0f;
		vec3 Normal = vec3(0.0f);
		std::span<const FallingHazardForecastPathSample> Samples;
		bool SampleCapExhausted = false;
		bool IndependentSupportKnown = false;
		FallingHazardCollisionKind IndependentSupportCollision =
			FallingHazardCollisionKind::Unknown;
		vec3 IndependentSupportNormal = vec3(0.0f);
	};

	struct FallingHazardForecastExpectedSegment
	{
		FallingHazardSweepLeg Leg = FallingHazardSweepLeg::Direct;
		size_t Ordinal = 0;
		vec3 Origin = vec3(0.0f);
		vec3 RequestedDelta = vec3(0.0f);
		FallingHazardCollisionKind Collision = FallingHazardCollisionKind::Unknown;
		float HitFraction = 1.0f;
		vec3 HitNormal = vec3(0.0f);
		vec3 Endpoint = vec3(0.0f);
		float ElapsedContribution = 0.0f;
		size_t FirstSample = 0;
		size_t SampleCount = 0;
	};

	struct FallingHazardForecastResult
	{
		FallingHazardForecast Classification = FallingHazardForecast::Unknown;
		FallingHazardForecastReason Reason = FallingHazardForecastReason::InvalidInput;
		float Elapsed = 0.0f;
		float PathDistance = 0.0f;
		size_t SegmentCount = 0;
		size_t SampleCount = 0;
		bool TransientHarmfulPainObserved = false;
		FallingHazardZoneId ExpectedHarmfulFootZone;
		FallingHazardZoneId ExpectedHarmfulPhysicsZone;
		bool ExpectedHarmfulWaterEntry = false;
		bool ExpectedHarmfulDamageTypeMatchesReduced = false;
		bool ExpectedBotAvoidanceRelevant = false;
		float FirstHarmfulPainDepth = 0.0f;
		FallingHazardForecastContinuationSeed Continuation;
	};

	struct FallingHazardForecastState
	{
		bool Active = false;
		FallingHazardForecastInput Input;
		FallingParityState PhysicsState;
		FallingParityTransition ActiveParityStep;
		FallingHazardForecastProbeRequest PendingProbe;
		FallingHazardForecastContinuationSeed Continuation;
		FallingTwoWallAdjustment ThirdAdjustment;
		float Elapsed = 0.0f;
		float PathDistance = 0.0f;
		size_t SampleCount = 0;
		std::array<FallingHazardForecastExpectedSegment,
			FallingHazardForecastMaximumSegments> ExpectedSegments;
		size_t ExpectedSegmentCount = 0;
		FallingHazardForecastResult Result;
	};

	struct FallingHazardForecastUpdate
	{
		FallingHazardForecastState State;
		FallingHazardForecastProbeRequest Probe;
		FallingHazardForecastResult Result;
		bool Complete = false;
	};

	FallingHazardForecastUpdate BeginFallingHazardForecast(
		const FallingHazardForecastInput& input);
	FallingHazardForecastUpdate ObserveFallingHazardForecastSweep(
		const FallingHazardForecastState& state,
		const FallingHazardForecastSweepObservation& observation);

	bool FallingHazardForecastExpectedSegmentMatches(
		const FallingHazardForecastExpectedSegment& expected,
		const FallingHazardForecastExpectedSegment& actual,
		float vectorTolerance = FallingHazardForecastVectorTolerance,
		float elapsedTolerance = FallingHazardForecastElapsedTolerance);
}
