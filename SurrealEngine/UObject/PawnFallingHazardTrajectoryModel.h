#pragma once

#include <cstddef>
#include <cstdint>

namespace PawnMovement
{
	constexpr size_t FallingHazardMaximumSweptSegments = 256;
	constexpr uint32_t FallingHazardMaximumGenerationsPerLife = 4096;
	constexpr uint32_t FallingHazardRegionCallback = 1u << 0;
	constexpr uint32_t FallingHazardFootZoneCallback = 1u << 1;
	constexpr uint32_t FallingHazardHeadZoneCallback = 1u << 2;
	constexpr uint32_t FallingHazardBumpCallback = 1u << 3;
	constexpr uint32_t FallingHazardTouchCallback = 1u << 4;
	constexpr uint32_t FallingHazardEncroachmentCallback = 1u << 5;
	constexpr uint32_t FallingHazardHitWallCallback = 1u << 6;
	constexpr uint32_t FallingHazardZoneCallbackMask =
		FallingHazardRegionCallback | FallingHazardFootZoneCallback
		| FallingHazardHeadZoneCallback;

	struct FallingHazardLifeId { uint64_t Value = 0; };
	struct FallingHazardFallEpisodeId { uint64_t Value = 0; };
	struct FallingHazardGenerationId { uint32_t Value = 0; };
	struct FallingHazardZoneId
	{
		bool Known = false;
		uint32_t ZoneActorId = 0;
		uint32_t ZoneNumber = 0;
	};

	enum class FallingHazardForecastSource
	{
		Unknown,
		UnsupportedWalkCommit,
		ExistingFallingCommit,
		PostWallDeflectionCommit,
		AlignedContinuationCommit,
		ThirdMoveContinuationCommit,
		CallbackReturnCommit,
		ExternalImpulseCommit,
		HorizonContinuationCommit
	};

	enum class FallingHazardForecast
	{
		Unknown,
		NoHarmfulPainObserved,
		HarmfulPainObserved
	};

	enum class FallingHazardCollisionKind
	{
		Unknown,
		Clear,
		StaticWorld,
		Mover,
		DynamicActor
	};

	enum class FallingHazardSweepLeg
	{
		Direct,
		Aligned,
		TwoWallAdjusted
	};

	enum class FallingHazardTerminal
	{
		Active,
		HarmfulPainEntered,
		Landed,
		Died,
		CallbackBoundary,
		ExternalImpulseBoundary,
		ContinuityLost,
		WaterPhysicsBoundary,
		ObservationHorizonExhausted,
		SupersededByCommittedSource,
		SweptSegmentBudgetExceeded,
		InvalidObservation
	};

	enum class FallingHazardCorrelation
	{
		Pending,
		Unknown,
		Ambiguous,
		ConfirmedHarmfulForecast,
		ForecastOnly,
		ActualOnly,
		ConfirmedNoHarmfulObservation
	};

	struct FallingHazardForecastArm
	{
		FallingHazardLifeId Life;
		FallingHazardFallEpisodeId FallEpisode;
		FallingHazardForecastSource Source = FallingHazardForecastSource::Unknown;
		FallingHazardForecast Forecast = FallingHazardForecast::Unknown;
		FallingHazardZoneId StartingPhysicsZone;
		FallingHazardZoneId ExpectedHarmfulFootZone;
		FallingHazardZoneId ExpectedHarmfulPhysicsZone;
		bool ExpectedHarmfulWaterEntry = false;
		size_t SweptSegmentBudget = FallingHazardMaximumSweptSegments;
		float ElapsedHorizon = 4.0f;
		float PrechargedElapsed = 0.0f;
	};

	struct FallingHazardSweptSegment
	{
		FallingHazardLifeId Life;
		FallingHazardFallEpisodeId FallEpisode;
		FallingHazardGenerationId Generation;
		size_t Ordinal = 0;
		FallingHazardSweepLeg Leg = FallingHazardSweepLeg::Direct;
		FallingHazardCollisionKind Collision = FallingHazardCollisionKind::Unknown;
		float Elapsed = 0.0f;
		bool HarmfulCenterZoneKnown = false;
		bool InHarmfulCenterZone = false;
		FallingHazardZoneId CenterZone;
		bool HarmfulFootZoneKnown = false;
		bool InHarmfulFootZone = false;
		FallingHazardZoneId FootZone;
		FallingHazardZoneId PhysicsZone;
		bool RegionWaterKnown = false;
		bool InRegionWater = false;
		bool FootWaterKnown = false;
		bool InFootWater = false;
		bool HeadWaterKnown = false;
		bool InHeadWater = false;
		bool ForecastEndpointKnown = false;
		bool ForecastEndpointMatched = false;
		bool CallbackMaskKnown = false;
		uint32_t CallbackMask = 0;
	};

	struct FallingHazardTerminalObservation
	{
		FallingHazardLifeId Life;
		FallingHazardFallEpisodeId FallEpisode;
		FallingHazardGenerationId Generation;
		FallingHazardTerminal Terminal = FallingHazardTerminal::Active;
		FallingHazardCollisionKind LandingCollision =
			FallingHazardCollisionKind::Unknown;
	};

	struct FallingHazardGenerationState
	{
		FallingHazardGenerationId Generation;
		FallingHazardLifeId Life;
		FallingHazardFallEpisodeId FallEpisode;
		FallingHazardForecastSource Source = FallingHazardForecastSource::Unknown;
		FallingHazardForecast Forecast = FallingHazardForecast::Unknown;
		FallingHazardTerminal Terminal = FallingHazardTerminal::Active;
		FallingHazardZoneId StartingPhysicsZone;
		FallingHazardZoneId ExpectedHarmfulFootZone;
		FallingHazardZoneId ExpectedHarmfulPhysicsZone;
		FallingHazardZoneId LastObservedPhysicsZone;
		bool PhysicsZoneEvidenceKnown = true;
		bool ExpectedHarmfulWaterEntry = false;
		size_t SweptSegmentBudget = 0;
		size_t SweptSegmentCount = 0;
		float ElapsedHorizon = 0.0f;
		float ObservedElapsed = 0.0f;
		bool HasPositiveElapsed = false;
		bool HarmfulCenterEvidenceKnown = true;
		bool HarmfulFootEvidenceKnown = true;
		bool WaterEvidenceKnown = true;
		bool EnteredHarmfulCenterZone = false;
		bool EnteredHarmfulFootZone = false;
		FallingHazardZoneId ObservedHarmfulCenterZone;
		FallingHazardZoneId ObservedHarmfulFootZone;
		bool ExpectedHarmfulPathMatched = false;
		bool CausalAmbiguity = false;
		bool ActualTrajectoryUnknown = false;
		FallingHazardCollisionKind LandingCollision =
			FallingHazardCollisionKind::Unknown;
	};

	struct FallingHazardTrajectoryModel
	{
		bool HasActiveGeneration = false;
		FallingHazardGenerationState ActiveGeneration;
		FallingHazardLifeId CountedLife;
		uint32_t GenerationCountForLife = 0;
		uint32_t NextGenerationValue = 1;
		bool GenerationCapacityExceeded = false;
	};

	struct FallingHazardTrajectoryUpdate
	{
		FallingHazardTrajectoryModel Model;
		FallingHazardGenerationState CompletedGeneration;
		bool HasCompletedGeneration = false;
	};

	struct FallingHazardArmUpdate : FallingHazardTrajectoryUpdate
	{
		FallingHazardGenerationId ArmedGeneration;
		bool Armed = false;
	};

	FallingHazardArmUpdate ArmFallingHazardForecast(
		const FallingHazardTrajectoryModel& model,
		const FallingHazardForecastArm& arm);
	FallingHazardTrajectoryUpdate ObserveFallingHazardSweptSegment(
		const FallingHazardTrajectoryModel& model,
		const FallingHazardSweptSegment& observation);
	FallingHazardTrajectoryUpdate FinishFallingHazardGeneration(
		const FallingHazardTrajectoryModel& model,
		const FallingHazardTerminalObservation& observation);
	FallingHazardCorrelation CorrelateFallingHazardGeneration(
		const FallingHazardGenerationState& generation);
}
