#pragma once

#include "PawnFallingHazardDiagnostics.h"
#include "PawnFallingHazardForecast.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PawnMovement
{
	constexpr size_t FallingHazardRuntimeMaximumQueuedDiagnostics = 1024;

	struct FallingHazardRuntimeCounters
	{
		uint64_t EpisodesStarted = 0;
		uint64_t EpisodesCompleted = 0;
		uint64_t TruePositiveOutcomes = 0;
		uint64_t FalsePositiveOutcomes = 0;
		uint64_t FalseNegativeOutcomes = 0;
		uint64_t TrueNegativeOutcomes = 0;
		uint64_t AmbiguousOutcomes = 0;
		uint64_t UnknownOutcomes = 0;
		uint64_t DiagnosticOverflows = 0;
		uint64_t GenerationCapacityExhaustions = 0;
	};

	struct FallingHazardRuntimeSweepObservation
	{
		FallingHazardForecastExpectedSegment Segment;
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
		bool CallbackMaskKnown = false;
		uint32_t CallbackMask = 0;
	};

	class FallingHazardRuntimeObserver
	{
	public:
		explicit FallingHazardRuntimeObserver(std::string sourcePawnActor = {});

		void SetSourcePawnActor(std::string sourcePawnActor);
		bool BeginFallEpisode();
		void AbandonFallEpisode();

		bool ArmGeneration(FallingHazardForecastSource source,
			const FallingHazardForecastUpdate& forecast);
		bool ObserveSweep(const FallingHazardRuntimeSweepObservation& observation);
		bool FinishGeneration(FallingHazardTerminal terminal,
			FallingHazardCollisionKind landingCollision =
				FallingHazardCollisionKind::Unknown);
		bool FinishCallbackBoundary();
		bool FinishExternalImpulseBoundary();
		bool FinishObservationHorizon();
		bool FinishLanding(FallingHazardCollisionKind collision);
		bool FinishDeath();
		void EndLife();

		bool HasActiveFallEpisode() const { return FallEpisodeActive; }
		bool HasActiveGeneration() const
		{
			return TrajectoryModel.HasActiveGeneration;
		}
		FallingHazardLifeId CurrentLife() const { return Life; }
		FallingHazardFallEpisodeId CurrentFallEpisode() const { return FallEpisode; }
		FallingHazardGenerationId CurrentGeneration() const;
		const FallingHazardRuntimeCounters& Counters() const { return CounterValues; }
		const FallingHazardTrajectoryModel& Model() const { return TrajectoryModel; }
		const FallingHazardForecastState* ActiveForecast() const;
		const std::vector<FallingHazardDiagnosticRecord>& Diagnostics() const
		{
			return DiagnosticQueue;
		}
		std::vector<FallingHazardDiagnosticRecord> DrainDiagnostics();

	private:
		void ProcessCompletion(const FallingHazardGenerationState& generation);
		void QueueStart(float prechargedElapsed);
		void QueueCapacity(FallingHazardForecastSource attemptedSource);
		void QueueDiagnostic(FallingHazardDiagnosticRecord record);
		void CountCorrelation(FallingHazardCorrelation correlation);
		void ClearGenerationForecast();

		std::string SourcePawnActor;
		FallingHazardLifeId Life = { 1 };
		FallingHazardFallEpisodeId FallEpisode;
		uint64_t NextFallEpisodeValue = 1;
		bool FallEpisodeActive = false;
		FallingHazardTrajectoryModel TrajectoryModel;
		FallingHazardForecastState ForecastState;
		bool HasForecastState = false;
		size_t ActualSampleCount = 0;
		uint64_t NextDiagnosticSequence = 1;
		bool CapacityDiagnosticEmittedForLife = false;
		FallingHazardRuntimeCounters CounterValues;
		std::vector<FallingHazardDiagnosticRecord> DiagnosticQueue;
	};
}
