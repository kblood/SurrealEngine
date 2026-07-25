#pragma once

#include "PawnFallingHazardDiagnostics.h"
#include "PawnDirectHarmfulWaterEntryCertificate.h"
#include "PawnFallingHazardForecast.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <optional>
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
		uint64_t PersistentHarmfulFallCandidatesStarted = 0;
		uint64_t PersistentHarmfulFallPromotions = 0;
		uint64_t PersistentHarmfulFallResets = 0;
		uint64_t PersistentHarmfulFallConfirmedHarmfulEntries = 0;
		uint64_t PersistentHarmfulFallObservedLeadSamples = 0;
		uint64_t PersistentHarmfulFallObservedLeadMilliseconds = 0;
		uint64_t SingleHarmfulFallPrefixCandidatesStarted = 0;
		uint64_t SingleHarmfulFallPrefixPromotions = 0;
		uint64_t SingleHarmfulFallPrefixResets = 0;
		uint64_t SingleHarmfulFallPrefixConfirmedHarmfulEntries = 0;
		uint64_t SingleHarmfulFallPrefixObservedLeadSamples = 0;
		uint64_t SingleHarmfulFallPrefixObservedLeadMilliseconds = 0;
		uint64_t DirectHarmfulWaterEntryCandidates = 0;
		uint64_t DirectHarmfulWaterEntryConfirmed = 0;
		uint64_t DirectHarmfulWaterEntryConfirmedNoHarm = 0;
		uint64_t DirectHarmfulWaterEntryUnresolved = 0;
		uint64_t DirectHarmfulWaterEntryLeadSamples = 0;
		uint64_t DirectHarmfulWaterEntryLeadMilliseconds = 0;
		std::array<uint64_t, DirectHarmfulWaterEntryCertificateResultCount>
			DirectHarmfulWaterEntryCertificateResults = {};
	};

	struct FallingHazardRuntimeSweepObservation
	{
		FallingHazardForecastExpectedSegment Segment;
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
		bool HasPromotedSingleHarmfulFallPrefix() const
		{
			return SingleHarmfulFallPrefix.CandidateActive
				&& SingleHarmfulFallPrefix.Promoted
				&& SingleHarmfulFallPrefix.Life.Value == Life.Value
				&& SingleHarmfulFallPrefix.FallEpisode.Value == FallEpisode.Value;
		}
		FallingHazardLifeId CurrentLife() const { return Life; }
		FallingHazardFallEpisodeId CurrentFallEpisode() const { return FallEpisode; }
		FallingHazardGenerationId CurrentGeneration() const;
		std::optional<FallingHazardTerminal> LastCompletedTerminal() const
		{
			return HasLastCompletion
				? std::optional<FallingHazardTerminal>(LastCompletionTerminal)
				: std::nullopt;
		}
		bool GenerationCapacityExhaustedForLife() const
		{
			return TrajectoryModel.GenerationCapacityExceeded;
		}
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
		void ObservePersistentHarmfulFallForecast(
			const FallingHazardGenerationState& generation);
		void ObservePersistentHarmfulFallCompletion(
			const FallingHazardGenerationState& generation,
			FallingHazardCorrelation correlation);
		void ClearPersistentHarmfulFall(bool countReset);
		void ArmSingleHarmfulFallPrefix(
			FallingHazardForecastSource source,
			const FallingHazardForecastUpdate& forecast,
			const FallingHazardGenerationState& generation);
		void ObserveSingleHarmfulFallPrefix(
			const FallingHazardGenerationState& generation,
			const FallingHazardRuntimeSweepObservation& observation,
			bool expectedKnown, bool expectedMatched);
		void ObserveSingleHarmfulFallPrefixCompletion(
			const FallingHazardGenerationState& generation,
			FallingHazardCorrelation correlation);
		void ClearSingleHarmfulFallPrefix(bool countReset);
		void ArmDirectHarmfulWaterEntryPrediction(FallingHazardForecastSource source,
			const DirectHarmfulWaterEntryCertificate& certificate,
			const FallingHazardGenerationState& generation);
		void ObserveDirectHarmfulWaterEntryPrediction(
			const FallingHazardGenerationState& generation,
			const FallingHazardRuntimeSweepObservation& observation);
		void ResolveDirectHarmfulWaterEntryPrediction(
			const FallingHazardGenerationState& generation);

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
		bool HasLastCompletion = false;
		FallingHazardTerminal LastCompletionTerminal =
			FallingHazardTerminal::Active;
		struct PersistentHarmfulFallState
		{
			bool CandidateActive = false;
			bool Latched = false;
			uint32_t ConsecutiveForecasts = 0;
			FallingHazardLifeId Life;
			FallingHazardFallEpisodeId FallEpisode;
			FallingHazardZoneId ExpectedHarmfulFootZone;
			FallingHazardZoneId ExpectedHarmfulPhysicsZone;
			bool ExpectedHarmfulWaterEntry = false;
			float LatchedObservedSweepElapsed = 0.0f;
		};
		PersistentHarmfulFallState PersistentHarmfulFall;
		struct SingleHarmfulFallPrefixState
		{
			bool CandidateActive = false;
			bool Promoted = false;
			bool AlignedStartObserved = false;
			uint32_t MatchingSweeps = 0;
			FallingHazardLifeId Life;
			FallingHazardFallEpisodeId FallEpisode;
			FallingHazardGenerationId Generation;
			float PromotedObservedSweepElapsed = 0.0f;
		};
		SingleHarmfulFallPrefixState SingleHarmfulFallPrefix;
		struct DirectHarmfulWaterEntryPredictionState
		{
			bool Active = false;
			bool ObservedWaterEntry = false;
			FallingHazardLifeId Life;
			FallingHazardFallEpisodeId FallEpisode;
			FallingHazardGenerationId Generation;
			float StartObservedElapsed = 0.0f;
		};
		std::unique_ptr<DirectHarmfulWaterEntryPredictionState>
			DirectHarmfulWaterEntryPrediction;
		float FallEpisodeObservedSweepElapsed = 0.0f;
		FallingHazardRuntimeCounters CounterValues;
		std::vector<FallingHazardDiagnosticRecord> DiagnosticQueue;
	};
}
