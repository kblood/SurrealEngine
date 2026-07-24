#include "PawnFallingHazardRuntimeObserver.h"

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
		return true;
	}

	void FallingHazardRuntimeObserver::AbandonFallEpisode()
	{
		if (TrajectoryModel.HasActiveGeneration)
			FinishGeneration(FallingHazardTerminal::ContinuityLost);
		FallEpisodeActive = false;
		FallEpisode = {};
		ClearGenerationForecast();
	}

	bool FallingHazardRuntimeObserver::ArmGeneration(
		FallingHazardForecastSource source,
		const FallingHazardForecastUpdate& forecast)
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
		ActualSampleCount = 0;
		CounterValues.EpisodesStarted++;
		QueueStart(prechargedElapsed);
		return true;
	}

	bool FallingHazardRuntimeObserver::ObserveSweep(
		const FallingHazardRuntimeSweepObservation& observation)
	{
		if (!TrajectoryModel.HasActiveGeneration || !HasForecastState)
			return false;

		const FallingHazardGenerationState& active =
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
		return completed;
	}

	bool FallingHazardRuntimeObserver::FinishDeath()
	{
		const bool completed = FinishGeneration(FallingHazardTerminal::Died);
		FallEpisodeActive = false;
		FallEpisode = {};
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
		ClearGenerationForecast();
		CapacityDiagnosticEmittedForLife = false;
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
		const FallingHazardCorrelation correlation =
			CorrelateFallingHazardGeneration(generation);
		CounterValues.EpisodesCompleted++;
		CountCorrelation(correlation);
		FallingHazardDiagnosticRecord record;
		record.Kind = FallingHazardDiagnosticKind::Terminal;
		record.Generation = generation;
		record.Correlation = correlation;
		QueueDiagnostic(std::move(record));
	}

	void FallingHazardRuntimeObserver::QueueStart(float prechargedElapsed)
	{
		FallingHazardDiagnosticRecord record;
		record.Kind = FallingHazardDiagnosticKind::Start;
		record.PrechargedElapsed = prechargedElapsed;
		record.Generation = TrajectoryModel.ActiveGeneration;
		record.Correlation = FallingHazardCorrelation::Pending;
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
}
