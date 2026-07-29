#include "PawnFallingHazardRuntimeObserver.h"

#include <cmath>
#include <limits>
#include <utility>

namespace PawnMovement
{
	FallingHazardRuntimeObserver::FallingHazardRuntimeObserver(
		std::string sourcePawnActor)
		: SourcePawnActor(std::move(sourcePawnActor))
	{
	}

	void FallingHazardRuntimeObserver::SetSourcePawnActor(
		std::string sourcePawnActor)
	{
		if (!sourcePawnActor.empty() && !TrajectoryModel.HasActiveGeneration)
			SourcePawnActor = std::move(sourcePawnActor);
	}

	bool FallingHazardRuntimeObserver::BeginFallEpisode()
	{
		if (FallEpisodeActive || NextFallEpisodeValue == 0)
			return false;
		FallEpisode.Value = NextFallEpisodeValue++;
		FallEpisodeActive = true;
		FallEpisodeObservedSweepElapsed = 0.0f;
		ClearPersistentHarmfulFall(false);
		ClearSingleHarmfulFallPrefix(false);
		return true;
	}

	void FallingHazardRuntimeObserver::AbandonFallEpisode()
	{
		if (TrajectoryModel.HasActiveGeneration)
			FinishGeneration(FallingHazardTerminal::ContinuityLost);
		FallEpisodeActive = false;
		FallEpisode = {};
		FallEpisodeObservedSweepElapsed = 0.0f;
		ClearPersistentHarmfulFall(true);
		ClearSingleHarmfulFallPrefix(true);
		ClearGenerationForecast();
	}

	bool FallingHazardRuntimeObserver::ArmGeneration(
		FallingHazardForecastSource source,
		const FallingHazardForecastUpdate& forecast,
		FallingHazardAlignedCommandProvenance alignedCommandProvenance)
	{
		if (SourcePawnActor.empty() || !FallEpisodeActive || !forecast.Complete
			|| forecast.State.Active || forecast.State.PendingProbe.Valid
			|| forecast.State.Result.Classification
				!= forecast.Result.Classification
			|| forecast.State.Result.Reason != forecast.Result.Reason
			|| forecast.State.ExpectedSegmentCount != forecast.Result.SegmentCount
			|| forecast.State.ExpectedSegmentCount
				> forecast.State.Input.MaximumSegments
			|| source == FallingHazardForecastSource::Unknown)
			return false;

		const bool alignedContinuation = source
			== FallingHazardForecastSource::AlignedContinuationCommit;
		const bool thirdContinuation = source
			== FallingHazardForecastSource::ThirdMoveContinuationCommit;
		const bool prechargedContinuation = alignedContinuation || thirdContinuation;
		if ((alignedContinuation && forecast.State.Input.Phase
				!= FallingHazardForecastPhase::AlignedContinuation)
			|| (thirdContinuation && forecast.State.Input.Phase
				!= FallingHazardForecastPhase::ThirdContinuation)
			|| (!prechargedContinuation && forecast.State.Input.Phase
				!= FallingHazardForecastPhase::FullStep))
		{
			return false;
		}
		const float prechargedElapsed = prechargedContinuation
			? forecast.State.Input.Continuation.PrechargedElapsed : 0.0f;
		FallingHazardForecastArm arm;
		arm.Life = Life;
		arm.FallEpisode = FallEpisode;
		arm.Source = source;
		arm.Forecast = forecast.Result.Classification;
		arm.StartingPhysicsZone =
			forecast.State.Input.StartingZones.Physics.Identity;
		arm.ExpectedHarmfulFootZone = forecast.Result.ExpectedHarmfulFootZone;
		arm.ExpectedHarmfulPhysicsZone =
			forecast.Result.ExpectedHarmfulPhysicsZone;
		arm.ExpectedHarmfulWaterEntry =
			forecast.Result.ExpectedHarmfulWaterEntry;
		arm.SweptSegmentBudget = forecast.State.Input.MaximumSegments;
		arm.ElapsedHorizon = forecast.State.Input.MaximumElapsed;
		arm.PrechargedElapsed = prechargedElapsed;

		const bool capacityWasExceeded =
			TrajectoryModel.GenerationCapacityExceeded;
		const FallingHazardArmUpdate update =
			ArmFallingHazardForecast(TrajectoryModel, arm);
		TrajectoryModel = update.Model;
		if (update.HasCompletedGeneration)
			ProcessCompletion(update.CompletedGeneration);

		if (TrajectoryModel.GenerationCapacityExceeded)
		{
			ClearGenerationForecast();
			if (!capacityWasExceeded && !CapacityDiagnosticEmittedForLife)
				QueueCapacity(source);
			return false;
		}
		if (!update.Armed)
			return false;

		ForecastState = forecast.State;
		HasForecastState = true;
		ActiveAlignedCommandProvenance = source
			== FallingHazardForecastSource::AlignedContinuationCommit
			? alignedCommandProvenance
			: FallingHazardAlignedCommandProvenance::NotAlignedContinuation;
		ActualSampleCount = 0;
		CounterValues.EpisodesStarted++;
		ObservePersistentHarmfulFallForecast(TrajectoryModel.ActiveGeneration);
		ArmSingleHarmfulFallPrefix(source, forecast,
			TrajectoryModel.ActiveGeneration);
		DirectHarmfulWaterEntryCertificate certificate;
		if (source == FallingHazardForecastSource::ExistingFallingCommit)
		{
			certificate = CertifyDirectHarmfulWaterEntry(forecast);
		}
		else
		{
			certificate.Result =
				DirectHarmfulWaterEntryCertificateResult::SourceNotEligible;
		}
		const size_t resultIndex = static_cast<size_t>(certificate.Result);
		if (resultIndex < CounterValues.DirectHarmfulWaterEntryCertificateResults.size())
		{
			CounterValues.DirectHarmfulWaterEntryCertificateResults[resultIndex]++;
			ArmDirectHarmfulWaterEntryPrediction(source, certificate,
				TrajectoryModel.ActiveGeneration);
		}
		HasLastCompletion = false;
		LastCompletionTerminal = FallingHazardTerminal::Active;
		QueueStart(prechargedElapsed);
		return true;
	}

	bool FallingHazardRuntimeObserver::ObserveSweep(
		const FallingHazardRuntimeSweepObservation& observation)
	{
		if (!TrajectoryModel.HasActiveGeneration || !HasForecastState)
			return false;

		const FallingHazardGenerationState active =
			TrajectoryModel.ActiveGeneration;
		const size_t ordinal = active.SweptSegmentCount;
		FallingHazardForecastExpectedSegment actual = observation.Segment;
		actual.Ordinal = ordinal;
		const bool sampleCountValid = observation.Segment.SampleCount
			<= FallingHazardForecastMaximumSamples - std::min(
				ActualSampleCount, FallingHazardForecastMaximumSamples);
		actual.FirstSample = sampleCountValid ? ActualSampleCount
			: FallingHazardForecastMaximumSamples;
		bool expectedKnown = sampleCountValid
			&& ordinal < ForecastState.ExpectedSegmentCount;
		bool expectedMatched = false;
		if (expectedKnown)
		{
			expectedMatched = FallingHazardForecastExpectedSegmentMatches(
				ForecastState.ExpectedSegments[ordinal], actual);
		}

		FallingHazardSweptSegment segment;
		segment.Life = active.Life;
		segment.FallEpisode = active.FallEpisode;
		segment.Generation = active.Generation;
		segment.Ordinal = ordinal;
		segment.Leg = actual.Leg;
		segment.Collision = actual.Collision;
		segment.Elapsed = actual.ElapsedContribution;
		segment.HarmfulCenterZoneKnown = observation.HarmfulCenterZoneKnown;
		segment.InHarmfulCenterZone = observation.InHarmfulCenterZone;
		segment.CenterZone = observation.CenterZone;
		segment.HarmfulFootZoneKnown = observation.HarmfulFootZoneKnown;
		segment.InHarmfulFootZone = observation.InHarmfulFootZone;
		segment.FootZone = observation.FootZone;
		segment.PhysicsZone = observation.PhysicsZone;
		segment.RegionWaterKnown = observation.RegionWaterKnown;
		segment.InRegionWater = observation.InRegionWater;
		segment.FootWaterKnown = observation.FootWaterKnown;
		segment.InFootWater = observation.InFootWater;
		segment.HeadWaterKnown = observation.HeadWaterKnown;
		segment.InHeadWater = observation.InHeadWater;
		segment.ForecastEndpointKnown = expectedKnown;
		segment.ForecastEndpointMatched = expectedMatched;
		segment.CallbackMaskKnown = observation.CallbackMaskKnown;
		segment.CallbackMask = observation.CallbackMask;

		const FallingHazardTrajectoryUpdate update =
			ObserveFallingHazardSweptSegment(TrajectoryModel, segment);
		TrajectoryModel = update.Model;
		if (std::isfinite(observation.Segment.ElapsedContribution)
			&& observation.Segment.ElapsedContribution >= 0.0f
			&& std::isfinite(FallEpisodeObservedSweepElapsed))
		{
			FallEpisodeObservedSweepElapsed += observation.Segment.ElapsedContribution;
		}
		if (!update.HasCompletedGeneration)
		{
			ObserveSingleHarmfulFallPrefix(active, observation, expectedKnown,
				expectedMatched);
		}
		ObserveDirectHarmfulWaterEntryPrediction(active, observation);
		if (sampleCountValid)
			ActualSampleCount += observation.Segment.SampleCount;
		if (update.HasCompletedGeneration)
		{
			ProcessCompletion(update.CompletedGeneration);
			ClearGenerationForecast();
		}
		return true;
	}

	bool FallingHazardRuntimeObserver::FinishGeneration(
		FallingHazardTerminal terminal,
		FallingHazardCollisionKind landingCollision)
	{
		if (!TrajectoryModel.HasActiveGeneration)
			return false;
		const FallingHazardGenerationState& active =
			TrajectoryModel.ActiveGeneration;
		FallingHazardTerminalObservation observation;
		observation.Life = active.Life;
		observation.FallEpisode = active.FallEpisode;
		observation.Generation = active.Generation;
		observation.Terminal = terminal;
		observation.LandingCollision = landingCollision;
		const FallingHazardTrajectoryUpdate update =
			FinishFallingHazardGeneration(TrajectoryModel, observation);
		TrajectoryModel = update.Model;
		if (!update.HasCompletedGeneration)
			return false;
		ProcessCompletion(update.CompletedGeneration);
		ClearGenerationForecast();
		return true;
	}

	bool FallingHazardRuntimeObserver::FinishCallbackBoundary()
	{
		return FinishGeneration(FallingHazardTerminal::CallbackBoundary);
	}

	bool FallingHazardRuntimeObserver::FinishExternalImpulseBoundary()
	{
		return FinishGeneration(FallingHazardTerminal::ExternalImpulseBoundary);
	}

	bool FallingHazardRuntimeObserver::FinishObservationHorizon()
	{
		return FinishGeneration(
			FallingHazardTerminal::ObservationHorizonExhausted);
	}

	bool FallingHazardRuntimeObserver::FinishLanding(
		FallingHazardCollisionKind collision)
	{
		const bool completed = FinishGeneration(
			FallingHazardTerminal::Landed, collision);
		FallEpisodeActive = false;
		FallEpisode = {};
		FallEpisodeObservedSweepElapsed = 0.0f;
		ClearPersistentHarmfulFall(true);
		ClearSingleHarmfulFallPrefix(true);
		return completed;
	}

	bool FallingHazardRuntimeObserver::FinishDeath()
	{
		const bool completed = FinishGeneration(FallingHazardTerminal::Died);
		FallEpisodeActive = false;
		FallEpisode = {};
		FallEpisodeObservedSweepElapsed = 0.0f;
		ClearPersistentHarmfulFall(true);
		ClearSingleHarmfulFallPrefix(true);
		return completed;
	}

	void FallingHazardRuntimeObserver::EndLife()
	{
		if (TrajectoryModel.HasActiveGeneration)
			FinishDeath();
		FallEpisodeActive = false;
		FallEpisode = {};
		NextFallEpisodeValue = 1;
		TrajectoryModel = {};
		FallEpisodeObservedSweepElapsed = 0.0f;
		ClearPersistentHarmfulFall(true);
		ClearSingleHarmfulFallPrefix(true);
		ClearGenerationForecast();
		CapacityDiagnosticEmittedForLife = false;
		HasLastCompletion = false;
		LastCompletionTerminal = FallingHazardTerminal::Active;
		if (Life.Value < std::numeric_limits<uint64_t>::max())
			Life.Value++;
	}

	FallingHazardGenerationId
		FallingHazardRuntimeObserver::CurrentGeneration() const
	{
		return TrajectoryModel.HasActiveGeneration
			? TrajectoryModel.ActiveGeneration.Generation
			: FallingHazardGenerationId{};
	}

	const FallingHazardForecastState*
		FallingHazardRuntimeObserver::ActiveForecast() const
	{
		return HasForecastState ? &ForecastState : nullptr;
	}

	std::vector<FallingHazardDiagnosticRecord>
		FallingHazardRuntimeObserver::DrainDiagnostics()
	{
		std::vector<FallingHazardDiagnosticRecord> diagnostics;
		diagnostics.swap(DiagnosticQueue);
		return diagnostics;
	}

	void FallingHazardRuntimeObserver::ProcessCompletion(
		const FallingHazardGenerationState& generation)
	{
		HasLastCompletion = true;
		LastCompletionTerminal = generation.Terminal;
		const FallingHazardCorrelation correlation =
			CorrelateFallingHazardGeneration(generation);
		CounterValues.EpisodesCompleted++;
		CountCorrelation(correlation);
		ObservePersistentHarmfulFallCompletion(generation, correlation);
		ObserveSingleHarmfulFallPrefixCompletion(generation, correlation);
		ResolveDirectHarmfulWaterEntryPrediction(generation);
		FallingHazardDiagnosticRecord record;
		record.Kind = FallingHazardDiagnosticKind::Terminal;
		record.Generation = generation;
		record.Correlation = correlation;
		record.AlignedCommandProvenance = ActiveAlignedCommandProvenance;
		QueueDiagnostic(std::move(record));
	}

	void FallingHazardRuntimeObserver::QueueStart(float prechargedElapsed)
	{
		FallingHazardDiagnosticRecord record;
		record.Kind = FallingHazardDiagnosticKind::Start;
		record.PrechargedElapsed = prechargedElapsed;
		record.Generation = TrajectoryModel.ActiveGeneration;
		record.Correlation = FallingHazardCorrelation::Pending;
		record.AlignedCommandProvenance = ActiveAlignedCommandProvenance;
		QueueDiagnostic(std::move(record));
	}

	void FallingHazardRuntimeObserver::QueueCapacity(
		FallingHazardForecastSource attemptedSource)
	{
		CapacityDiagnosticEmittedForLife = true;
		CounterValues.GenerationCapacityExhaustions++;
		FallingHazardDiagnosticRecord record;
		record.Kind = FallingHazardDiagnosticKind::GenerationCapacityExceeded;
		record.Generation.Life = Life;
		record.Generation.FallEpisode = FallEpisode;
		record.Generation.Source = attemptedSource;
		record.Correlation = FallingHazardCorrelation::Unknown;
		QueueDiagnostic(std::move(record));
	}

	void FallingHazardRuntimeObserver::QueueDiagnostic(
		FallingHazardDiagnosticRecord record)
	{
		record.SourcePawnActor = SourcePawnActor;
		record.Sequence = NextDiagnosticSequence++;
		if (DiagnosticQueue.size()
			< FallingHazardRuntimeMaximumQueuedDiagnostics)
		{
			DiagnosticQueue.push_back(std::move(record));
		}
		else
		{
			CounterValues.DiagnosticOverflows++;
		}
	}

	void FallingHazardRuntimeObserver::CountCorrelation(
		FallingHazardCorrelation correlation)
	{
		switch (correlation)
		{
		case FallingHazardCorrelation::ConfirmedHarmfulForecast:
			CounterValues.TruePositiveOutcomes++;
			break;
		case FallingHazardCorrelation::ForecastOnly:
			CounterValues.FalsePositiveOutcomes++;
			break;
		case FallingHazardCorrelation::ActualOnly:
			CounterValues.FalseNegativeOutcomes++;
			break;
		case FallingHazardCorrelation::ConfirmedNoHarmfulObservation:
			CounterValues.TrueNegativeOutcomes++;
			break;
		case FallingHazardCorrelation::Ambiguous:
			CounterValues.AmbiguousOutcomes++;
			break;
		case FallingHazardCorrelation::Pending:
		case FallingHazardCorrelation::Unknown:
			CounterValues.UnknownOutcomes++;
			break;
		}
	}

	void FallingHazardRuntimeObserver::ClearGenerationForecast()
	{
		ForecastState = {};
		HasForecastState = false;
		ActualSampleCount = 0;
	}

	void FallingHazardRuntimeObserver::ObservePersistentHarmfulFallForecast(
		const FallingHazardGenerationState& generation)
	{
		const auto validZone = [](const FallingHazardZoneId& zone)
		{
			return zone.Known && zone.ZoneActorId != 0;
		};
		const bool harmful = generation.Forecast
			== FallingHazardForecast::HarmfulPainObserved
			&& validZone(generation.ExpectedHarmfulFootZone)
			&& validZone(generation.ExpectedHarmfulPhysicsZone);
		if (!harmful)
		{
			ClearPersistentHarmfulFall(true);
			return;
		}

		const bool sameKey = PersistentHarmfulFall.CandidateActive
			&& PersistentHarmfulFall.Life.Value == generation.Life.Value
			&& PersistentHarmfulFall.FallEpisode.Value == generation.FallEpisode.Value
			&& PersistentHarmfulFall.ExpectedHarmfulFootZone.Known
				== generation.ExpectedHarmfulFootZone.Known
			&& PersistentHarmfulFall.ExpectedHarmfulFootZone.ZoneActorId
				== generation.ExpectedHarmfulFootZone.ZoneActorId
			&& PersistentHarmfulFall.ExpectedHarmfulFootZone.ZoneNumber
				== generation.ExpectedHarmfulFootZone.ZoneNumber
			&& PersistentHarmfulFall.ExpectedHarmfulPhysicsZone.Known
				== generation.ExpectedHarmfulPhysicsZone.Known
			&& PersistentHarmfulFall.ExpectedHarmfulPhysicsZone.ZoneActorId
				== generation.ExpectedHarmfulPhysicsZone.ZoneActorId
			&& PersistentHarmfulFall.ExpectedHarmfulPhysicsZone.ZoneNumber
				== generation.ExpectedHarmfulPhysicsZone.ZoneNumber
			&& PersistentHarmfulFall.ExpectedHarmfulWaterEntry
				== generation.ExpectedHarmfulWaterEntry;
		const bool continuationBoundary = HasLastCompletion
			&& (LastCompletionTerminal == FallingHazardTerminal::CallbackBoundary
				|| LastCompletionTerminal
					== FallingHazardTerminal::ExternalImpulseBoundary);
		if (!sameKey || !continuationBoundary)
		{
			ClearPersistentHarmfulFall(true);
			PersistentHarmfulFall.CandidateActive = true;
			PersistentHarmfulFall.ConsecutiveForecasts = 1;
			PersistentHarmfulFall.Life = generation.Life;
			PersistentHarmfulFall.FallEpisode = generation.FallEpisode;
			PersistentHarmfulFall.ExpectedHarmfulFootZone =
				generation.ExpectedHarmfulFootZone;
			PersistentHarmfulFall.ExpectedHarmfulPhysicsZone =
				generation.ExpectedHarmfulPhysicsZone;
			PersistentHarmfulFall.ExpectedHarmfulWaterEntry =
				generation.ExpectedHarmfulWaterEntry;
			CounterValues.PersistentHarmfulFallCandidatesStarted++;
			return;
		}

		if (PersistentHarmfulFall.ConsecutiveForecasts
			< std::numeric_limits<uint32_t>::max())
		{
			PersistentHarmfulFall.ConsecutiveForecasts++;
		}
		if (!PersistentHarmfulFall.Latched
			&& PersistentHarmfulFall.ConsecutiveForecasts >= 2)
		{
			PersistentHarmfulFall.Latched = true;
			PersistentHarmfulFall.LatchedObservedSweepElapsed =
				FallEpisodeObservedSweepElapsed;
			CounterValues.PersistentHarmfulFallPromotions++;
		}
	}

	void FallingHazardRuntimeObserver::ObservePersistentHarmfulFallCompletion(
		const FallingHazardGenerationState& generation,
		FallingHazardCorrelation correlation)
	{
		const bool sameKey = PersistentHarmfulFall.CandidateActive
			&& PersistentHarmfulFall.Life.Value == generation.Life.Value
			&& PersistentHarmfulFall.FallEpisode.Value == generation.FallEpisode.Value
			&& PersistentHarmfulFall.ExpectedHarmfulFootZone.ZoneActorId
				== generation.ExpectedHarmfulFootZone.ZoneActorId
			&& PersistentHarmfulFall.ExpectedHarmfulFootZone.ZoneNumber
				== generation.ExpectedHarmfulFootZone.ZoneNumber
			&& PersistentHarmfulFall.ExpectedHarmfulPhysicsZone.ZoneActorId
				== generation.ExpectedHarmfulPhysicsZone.ZoneActorId
			&& PersistentHarmfulFall.ExpectedHarmfulPhysicsZone.ZoneNumber
				== generation.ExpectedHarmfulPhysicsZone.ZoneNumber
			&& PersistentHarmfulFall.ExpectedHarmfulWaterEntry
				== generation.ExpectedHarmfulWaterEntry;
		if (generation.Terminal == FallingHazardTerminal::HarmfulPainEntered
			&& correlation == FallingHazardCorrelation::ConfirmedHarmfulForecast
			&& PersistentHarmfulFall.Latched && sameKey)
		{
			CounterValues.PersistentHarmfulFallConfirmedHarmfulEntries++;
			const float lead = FallEpisodeObservedSweepElapsed
				- PersistentHarmfulFall.LatchedObservedSweepElapsed;
			if (std::isfinite(lead) && lead >= 0.0f)
			{
				const double milliseconds = std::round(static_cast<double>(lead) * 1000.0);
				if (milliseconds <= static_cast<double>(std::numeric_limits<uint64_t>::max()
					- CounterValues.PersistentHarmfulFallObservedLeadMilliseconds))
				{
					CounterValues.PersistentHarmfulFallObservedLeadSamples++;
					CounterValues.PersistentHarmfulFallObservedLeadMilliseconds +=
						static_cast<uint64_t>(milliseconds);
				}
			}
			ClearPersistentHarmfulFall(false);
			return;
		}

		if (generation.Terminal != FallingHazardTerminal::CallbackBoundary
			&& generation.Terminal != FallingHazardTerminal::ExternalImpulseBoundary)
		{
			ClearPersistentHarmfulFall(true);
		}
	}

	void FallingHazardRuntimeObserver::ClearPersistentHarmfulFall(bool countReset)
	{
		if (countReset && PersistentHarmfulFall.CandidateActive)
			CounterValues.PersistentHarmfulFallResets++;
		PersistentHarmfulFall = {};
	}

	void FallingHazardRuntimeObserver::ArmSingleHarmfulFallPrefix(
		FallingHazardForecastSource source,
		const FallingHazardForecastUpdate& forecast,
		const FallingHazardGenerationState& generation)
	{
		const auto validZone = [](const FallingHazardZoneId& zone)
		{
			return zone.Known && zone.ZoneActorId != 0;
		};
		bool expectedSegmentsClear = forecast.State.ExpectedSegmentCount > 0;
		for (size_t index = 0; index < forecast.State.ExpectedSegmentCount; index++)
		{
			const FallingHazardForecastExpectedSegment& segment =
				forecast.State.ExpectedSegments[index];
			const bool alignedStart = index == 0
				&& segment.Leg == FallingHazardSweepLeg::Aligned
				&& segment.ElapsedContribution == 0.0f;
			expectedSegmentsClear = expectedSegmentsClear
				&& (alignedStart || segment.Leg == FallingHazardSweepLeg::Direct)
				&& segment.Collision == FallingHazardCollisionKind::Clear;
		}
		const bool eligible = source
			== FallingHazardForecastSource::AlignedContinuationCommit
			&& forecast.Result.Classification
				== FallingHazardForecast::HarmfulPainObserved
			&& forecast.Result.Reason
				== FallingHazardForecastReason::HarmfulFootPainAtEndpoint
			&& forecast.Result.ExpectedHarmfulWaterEntry
			&& forecast.Result.ExpectedBotAvoidanceRelevant
			&& validZone(generation.ExpectedHarmfulFootZone)
			&& validZone(generation.ExpectedHarmfulPhysicsZone)
			&& expectedSegmentsClear;
		ClearSingleHarmfulFallPrefix(true);
		if (!eligible)
			return;

		SingleHarmfulFallPrefix.CandidateActive = true;
		SingleHarmfulFallPrefix.Life = generation.Life;
		SingleHarmfulFallPrefix.FallEpisode = generation.FallEpisode;
		SingleHarmfulFallPrefix.Generation = generation.Generation;
		CounterValues.SingleHarmfulFallPrefixCandidatesStarted++;
	}

	void FallingHazardRuntimeObserver::ObserveSingleHarmfulFallPrefix(
		const FallingHazardGenerationState& generation,
		const FallingHazardRuntimeSweepObservation& observation,
		bool expectedKnown, bool expectedMatched)
	{
		if (!SingleHarmfulFallPrefix.CandidateActive)
			return;

		const bool sameGeneration = SingleHarmfulFallPrefix.Life.Value
			== generation.Life.Value
			&& SingleHarmfulFallPrefix.FallEpisode.Value
				== generation.FallEpisode.Value
			&& SingleHarmfulFallPrefix.Generation.Value
				== generation.Generation.Value;
		const bool stablePhysicsZone = observation.PhysicsZone.Known
			== generation.StartingPhysicsZone.Known
			&& observation.PhysicsZone.ZoneActorId
				== generation.StartingPhysicsZone.ZoneActorId
			&& observation.PhysicsZone.ZoneNumber
				== generation.StartingPhysicsZone.ZoneNumber;
		const bool drySafeZones = observation.HarmfulCenterZoneKnown
			&& !observation.InHarmfulCenterZone
			&& observation.HarmfulFootZoneKnown
			&& !observation.InHarmfulFootZone
			&& observation.RegionWaterKnown && !observation.InRegionWater
			&& observation.FootWaterKnown && !observation.InFootWater
			&& observation.HeadWaterKnown && !observation.InHeadWater;
		const bool clearMatchedEvidence = sameGeneration && expectedKnown
			&& expectedMatched
			&& observation.Segment.Collision == FallingHazardCollisionKind::Clear
			&& observation.CallbackMaskKnown && observation.CallbackMask == 0
			&& stablePhysicsZone && drySafeZones;
		const bool alignedStart = clearMatchedEvidence
			&& !SingleHarmfulFallPrefix.AlignedStartObserved
			&& SingleHarmfulFallPrefix.MatchingSweeps == 0
			&& observation.Segment.Leg == FallingHazardSweepLeg::Aligned
			&& observation.Segment.ElapsedContribution == 0.0f;
		const bool directSweep = clearMatchedEvidence
			&& observation.Segment.Leg == FallingHazardSweepLeg::Direct
			&& std::isfinite(observation.Segment.ElapsedContribution)
			&& observation.Segment.ElapsedContribution > 0.0f;
		if (!alignedStart && !directSweep)
		{
			ClearSingleHarmfulFallPrefix(true);
			return;
		}
		if (alignedStart)
		{
			SingleHarmfulFallPrefix.AlignedStartObserved = true;
			return;
		}

		if (SingleHarmfulFallPrefix.MatchingSweeps
			< std::numeric_limits<uint32_t>::max())
		{
			SingleHarmfulFallPrefix.MatchingSweeps++;
		}
		if (!SingleHarmfulFallPrefix.Promoted
			&& SingleHarmfulFallPrefix.MatchingSweeps >= 3)
		{
			SingleHarmfulFallPrefix.Promoted = true;
			SingleHarmfulFallPrefix.PromotedObservedSweepElapsed =
				FallEpisodeObservedSweepElapsed;
			CounterValues.SingleHarmfulFallPrefixPromotions++;
		}
	}

	void FallingHazardRuntimeObserver::ObserveSingleHarmfulFallPrefixCompletion(
		const FallingHazardGenerationState& generation,
		FallingHazardCorrelation correlation)
	{
		if (!SingleHarmfulFallPrefix.CandidateActive)
			return;

		const bool sameGeneration = SingleHarmfulFallPrefix.Life.Value
			== generation.Life.Value
			&& SingleHarmfulFallPrefix.FallEpisode.Value
				== generation.FallEpisode.Value
			&& SingleHarmfulFallPrefix.Generation.Value
				== generation.Generation.Value;
		if (sameGeneration
			&& generation.Terminal == FallingHazardTerminal::HarmfulPainEntered
			&& correlation == FallingHazardCorrelation::ConfirmedHarmfulForecast
			&& SingleHarmfulFallPrefix.Promoted)
		{
			CounterValues.SingleHarmfulFallPrefixConfirmedHarmfulEntries++;
			const float lead = FallEpisodeObservedSweepElapsed
				- SingleHarmfulFallPrefix.PromotedObservedSweepElapsed;
			if (std::isfinite(lead) && lead >= 0.0f)
			{
				const double milliseconds = std::round(static_cast<double>(lead) * 1000.0);
				if (milliseconds <= static_cast<double>(std::numeric_limits<uint64_t>::max()
					- CounterValues.SingleHarmfulFallPrefixObservedLeadMilliseconds))
				{
					CounterValues.SingleHarmfulFallPrefixObservedLeadSamples++;
					CounterValues.SingleHarmfulFallPrefixObservedLeadMilliseconds +=
						static_cast<uint64_t>(milliseconds);
				}
			}
			ClearSingleHarmfulFallPrefix(false);
			return;
		}

		ClearSingleHarmfulFallPrefix(true);
	}

	void FallingHazardRuntimeObserver::ClearSingleHarmfulFallPrefix(
		bool countReset)
	{
		if (countReset && SingleHarmfulFallPrefix.CandidateActive)
			CounterValues.SingleHarmfulFallPrefixResets++;
		SingleHarmfulFallPrefix = {};
	}

	void FallingHazardRuntimeObserver::ArmDirectHarmfulWaterEntryPrediction(
		FallingHazardForecastSource source,
		const DirectHarmfulWaterEntryCertificate& certificate,
		const FallingHazardGenerationState& generation)
	{
		if (source != FallingHazardForecastSource::ExistingFallingCommit)
			return;
		if (!certificate.IsCertified())
			return;
		DirectHarmfulWaterEntryPrediction =
			std::make_unique<DirectHarmfulWaterEntryPredictionState>();
		DirectHarmfulWaterEntryPrediction->Active = true;
		DirectHarmfulWaterEntryPrediction->Life = generation.Life;
		DirectHarmfulWaterEntryPrediction->FallEpisode = generation.FallEpisode;
		DirectHarmfulWaterEntryPrediction->Generation = generation.Generation;
		DirectHarmfulWaterEntryPrediction->StartObservedElapsed =
			FallEpisodeObservedSweepElapsed;
		CounterValues.DirectHarmfulWaterEntryCandidates++;
	}

	void FallingHazardRuntimeObserver::ObserveDirectHarmfulWaterEntryPrediction(
		const FallingHazardGenerationState& generation,
		const FallingHazardRuntimeSweepObservation& observation)
	{
		if (!DirectHarmfulWaterEntryPrediction
			|| !DirectHarmfulWaterEntryPrediction->Active
			|| DirectHarmfulWaterEntryPrediction->Life.Value != generation.Life.Value
			|| DirectHarmfulWaterEntryPrediction->FallEpisode.Value != generation.FallEpisode.Value
			|| DirectHarmfulWaterEntryPrediction->Generation.Value != generation.Generation.Value)
		{
			return;
		}
		DirectHarmfulWaterEntryPrediction->ObservedWaterEntry =
			DirectHarmfulWaterEntryPrediction->ObservedWaterEntry
			|| (observation.RegionWaterKnown && observation.InRegionWater
				&& observation.FootWaterKnown && observation.InFootWater);
	}

	void FallingHazardRuntimeObserver::ResolveDirectHarmfulWaterEntryPrediction(
		const FallingHazardGenerationState& generation)
	{
		if (!DirectHarmfulWaterEntryPrediction
			|| !DirectHarmfulWaterEntryPrediction->Active
			|| DirectHarmfulWaterEntryPrediction->Life.Value != generation.Life.Value
			|| DirectHarmfulWaterEntryPrediction->FallEpisode.Value != generation.FallEpisode.Value
			|| DirectHarmfulWaterEntryPrediction->Generation.Value != generation.Generation.Value)
		{
			return;
		}
		if (generation.Terminal == FallingHazardTerminal::HarmfulPainEntered
			&& DirectHarmfulWaterEntryPrediction->ObservedWaterEntry)
		{
			CounterValues.DirectHarmfulWaterEntryConfirmed++;
			const float lead = FallEpisodeObservedSweepElapsed
				- DirectHarmfulWaterEntryPrediction->StartObservedElapsed;
			if (std::isfinite(lead) && lead >= 0.0f)
			{
				const double milliseconds = std::round(static_cast<double>(lead) * 1000.0);
				if (milliseconds <= static_cast<double>(std::numeric_limits<uint64_t>::max()
					- CounterValues.DirectHarmfulWaterEntryLeadMilliseconds))
				{
					CounterValues.DirectHarmfulWaterEntryLeadSamples++;
					CounterValues.DirectHarmfulWaterEntryLeadMilliseconds +=
						static_cast<uint64_t>(milliseconds);
				}
			}
		}
		else if (generation.Terminal == FallingHazardTerminal::Landed)
		{
			CounterValues.DirectHarmfulWaterEntryConfirmedNoHarm++;
		}
		else
		{
			CounterValues.DirectHarmfulWaterEntryUnresolved++;
		}
		DirectHarmfulWaterEntryPrediction.reset();
	}
}
