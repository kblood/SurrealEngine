#include "UObject/PawnFallingHazardRuntimeObserver.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace PawnMovement;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}

	FallingHazardZoneId Zone(uint32_t actor, uint32_t number = 0)
	{
		return { .Known = true, .ZoneActorId = actor, .ZoneNumber = number };
	}

	FallingHazardForecastPointObservation SafePoint()
	{
		FallingHazardForecastPointObservation point;
		point.Center.Identity = Zone(100);
		point.Foot.Identity = Zone(100);
		point.Head.Identity = Zone(100);
		point.Physics.Identity = Zone(100);
		point.Physics.Gravity = vec3(0.0f, 0.0f, -950.0f);
		point.Physics.TerminalVelocity = 2500.0f;
		return point;
	}

	FallingHazardForecastExpectedSegment DirectSegment(size_t ordinal = 0,
		float elapsed = 0.02f)
	{
		FallingHazardForecastExpectedSegment segment;
		segment.Leg = FallingHazardSweepLeg::Direct;
		segment.Ordinal = ordinal;
		segment.Origin = vec3(0.0f, 0.0f, 100.0f - ordinal * 10.0f);
		segment.RequestedDelta = vec3(10.0f, 0.0f, -10.0f);
		segment.Collision = FallingHazardCollisionKind::Clear;
		segment.HitFraction = 1.0f;
		segment.Endpoint = segment.Origin + segment.RequestedDelta;
		segment.ElapsedContribution = elapsed;
		segment.FirstSample = ordinal;
		segment.SampleCount = 1;
		return segment;
	}

	FallingHazardForecastUpdate Forecast(
		FallingHazardForecast classification,
		FallingHazardForecastPhase phase = FallingHazardForecastPhase::FullStep,
		float prechargedElapsed = 0.0f)
	{
		FallingHazardForecastUpdate update;
		update.Complete = true;
		update.State.Input.Phase = phase;
		update.State.Input.StartingZones = SafePoint();
		update.State.Input.MaximumSegments = FallingHazardMaximumSweptSegments;
		update.State.Input.MaximumElapsed = 4.0f;
		update.State.Input.Continuation.Phase = phase;
		update.State.Input.Continuation.PrechargedElapsed = prechargedElapsed;
		update.State.ExpectedSegments[0] = DirectSegment();
		update.State.ExpectedSegmentCount = 1;
		update.Result.Classification = classification;
		update.Result.SegmentCount = 1;
		if (classification == FallingHazardForecast::HarmfulPainObserved)
		{
			update.Result.ExpectedHarmfulFootZone = Zone(42);
			update.Result.ExpectedHarmfulPhysicsZone = Zone(100);
		}
		update.State.Result = update.Result;
		return update;
	}

	FallingHazardRuntimeSweepObservation SafeSweep(
		const FallingHazardForecastExpectedSegment& segment = DirectSegment())
	{
		FallingHazardRuntimeSweepObservation observation;
		observation.Segment = segment;
		observation.HarmfulFootZoneKnown = true;
		observation.FootZone = Zone(100);
		observation.PhysicsZone = Zone(100);
		observation.RegionWaterKnown = true;
		observation.FootWaterKnown = true;
		observation.HeadWaterKnown = true;
		observation.CallbackMaskKnown = true;
		return observation;
	}

	FallingHazardRuntimeSweepObservation HarmfulSweep(uint32_t callbackMask = 0)
	{
		auto observation = SafeSweep();
		observation.InHarmfulFootZone = true;
		observation.FootZone = Zone(42);
		observation.CallbackMask = callbackMask;
		return observation;
	}

	void CheckPartition(const FallingHazardRuntimeCounters& counters)
	{
		Check(counters.EpisodesCompleted == counters.TruePositiveOutcomes
			+ counters.FalsePositiveOutcomes + counters.FalseNegativeOutcomes
			+ counters.TrueNegativeOutcomes + counters.AmbiguousOutcomes
			+ counters.UnknownOutcomes,
			"every completion belongs to exactly one correlation partition");
	}

	void TestSafeLandingAndStableDiagnostics()
	{
		FallingHazardRuntimeObserver observer("Bot17");
		Check(observer.CurrentLife().Value == 1, "the first life ID is one");
		Check(observer.BeginFallEpisode(), "a fall episode begins");
		Check(observer.CurrentFallEpisode().Value == 1,
			"the first fall episode ID is one");
		Check(observer.ArmGeneration(FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"a valid safe forecast arms");
		Check(observer.CurrentGeneration().Value == 1,
			"the first generation ID is one");
		Check(observer.ObserveSweep(SafeSweep()), "the matching sweep is observed");
		observer.SetSourcePawnActor("ChangedDuringGeneration");
		Check(observer.FinishLanding(FallingHazardCollisionKind::StaticWorld),
			"a static landing completes the generation");
		Check(!observer.HasActiveFallEpisode() && !observer.HasActiveGeneration(),
			"landing closes both generation and fall episode");

		const auto& counters = observer.Counters();
		Check(counters.EpisodesStarted == 1 && counters.EpisodesCompleted == 1
			&& counters.TrueNegativeOutcomes == 1,
			"a matched safe landing is one true negative");
		CheckPartition(counters);
		const auto& diagnostics = observer.Diagnostics();
		Check(diagnostics.size() == 2
			&& diagnostics[0].Kind == FallingHazardDiagnosticKind::Start
			&& diagnostics[1].Kind == FallingHazardDiagnosticKind::Terminal,
			"start precedes terminal");
		Check(diagnostics[0].Sequence == 1 && diagnostics[1].Sequence == 2
			&& diagnostics[0].SourcePawnActor == "Bot17"
			&& diagnostics[1].SourcePawnActor == "Bot17",
			"diagnostic source and sequence stay stable across one generation");
		Check(diagnostics[1].Generation.Terminal == FallingHazardTerminal::Landed
			&& diagnostics[1].Generation.LandingCollision
				== FallingHazardCollisionKind::StaticWorld
			&& diagnostics[1].Correlation
				== FallingHazardCorrelation::ConfirmedNoHarmfulObservation,
			"landing terminal preserves the model correlation");
	}

	void TestAcceptsPureForecastOutput()
	{
		FallingHazardForecastInput input;
		input.State.Location = vec3(0.0f, 0.0f, 100.0f);
		input.State.Velocity = vec3(0.0f, 0.0f, -100.0f);
		input.GroundSpeed = 400.0f;
		input.PhysicsSliceElapsed = 0.02f;
		input.StartingZones = SafePoint();
		const auto begun = BeginFallingHazardForecast(input);
		Check(!begun.Complete && begun.Probe.Valid,
			"the pure forecast requests its first sweep");

		const float distance = length(begun.Probe.Delta * 0.5f);
		std::vector<FallingHazardForecastPathSample> samples(1);
		samples[0].DistanceAlongSegment = distance;
		samples[0].Zones = SafePoint();
		FallingHazardForecastSweepObservation landing;
		landing.Collision = FallingHazardCollisionKind::StaticWorld;
		landing.Fraction = 0.5f;
		landing.Normal = vec3(0.0f, 0.0f, 1.0f);
		landing.Samples = samples;
		const auto completed = ObserveFallingHazardForecastSweep(
			begun.State, landing);
		Check(completed.Complete && completed.Result.Classification
			== FallingHazardForecast::NoHarmfulPainObserved,
			"the pure forecast proves a static safe landing");

		FallingHazardRuntimeObserver observer("PureForecastBot");
		Check(observer.BeginFallEpisode()
			&& observer.ArmGeneration(
				FallingHazardForecastSource::ExistingFallingCommit, completed),
			"the coordinator accepts an unmodified pure forecast update");
		auto actual = SafeSweep(completed.State.ExpectedSegments[0]);
		Check(observer.ObserveSweep(actual)
			&& observer.FinishLanding(FallingHazardCollisionKind::StaticWorld)
			&& observer.Counters().TrueNegativeOutcomes == 1,
			"pure forecast and realized lifecycle correlate end to end");
	}

	void TestCorrelationPartition()
	{
		FallingHazardRuntimeObserver observer("PartitionBot");

		auto run = [&](FallingHazardForecast forecast,
			const FallingHazardRuntimeSweepObservation& sweep, bool land)
		{
			Check(observer.BeginFallEpisode(), "partition fall begins");
			Check(observer.ArmGeneration(
				FallingHazardForecastSource::ExistingFallingCommit,
				Forecast(forecast)), "partition forecast arms");
			Check(observer.ObserveSweep(sweep), "partition sweep is observed");
			if (land)
				Check(observer.FinishLanding(FallingHazardCollisionKind::StaticWorld),
					"partition landing completes");
			else
				observer.AbandonFallEpisode();
		};

		run(FallingHazardForecast::HarmfulPainObserved, HarmfulSweep(), false);
		run(FallingHazardForecast::HarmfulPainObserved, SafeSweep(), true);
		run(FallingHazardForecast::NoHarmfulPainObserved, HarmfulSweep(), false);
		run(FallingHazardForecast::NoHarmfulPainObserved, SafeSweep(), true);
		run(FallingHazardForecast::HarmfulPainObserved,
			HarmfulSweep(FallingHazardHitWallCallback), false);

		Check(observer.BeginFallEpisode(), "unknown death fall begins");
		Check(observer.ArmGeneration(FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"unknown death forecast arms");
		Check(observer.FinishDeath(), "death completes the active generation");

		const auto& counters = observer.Counters();
		Check(counters.TruePositiveOutcomes == 1
			&& counters.FalsePositiveOutcomes == 1
			&& counters.FalseNegativeOutcomes == 1
			&& counters.TrueNegativeOutcomes == 1
			&& counters.AmbiguousOutcomes == 1
			&& counters.UnknownOutcomes == 1,
			"all six terminal correlation classes are counted exactly once");
		CheckPartition(counters);
	}

	void TestSupersedeOrderingAndSources()
	{
		FallingHazardRuntimeObserver observer("SupersedeBot");
		Check(observer.BeginFallEpisode(), "supersede fall begins");
		Check(observer.ArmGeneration(FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"first supersede generation arms");
		Check(observer.ArmGeneration(FallingHazardForecastSource::CallbackReturnCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"replacement generation arms");
		const auto& diagnostics = observer.Diagnostics();
		Check(diagnostics.size() == 3
			&& diagnostics[0].Kind == FallingHazardDiagnosticKind::Start
			&& diagnostics[1].Kind == FallingHazardDiagnosticKind::Terminal
			&& diagnostics[2].Kind == FallingHazardDiagnosticKind::Start,
			"supersede emits terminal before replacement start");
		Check(diagnostics[1].Generation.Terminal
			== FallingHazardTerminal::CallbackBoundary
			&& diagnostics[2].Generation.Generation.Value == 2
			&& diagnostics[2].Generation.Source
				== FallingHazardForecastSource::CallbackReturnCommit,
			"callback replacement has typed terminal and increasing generation");
	}

	void TestZeroElapsedContinuation()
	{
		FallingHazardRuntimeObserver observer("ContinuationBot");
		Check(observer.BeginFallEpisode(), "continuation fall begins");
		auto forecast = Forecast(FallingHazardForecast::NoHarmfulPainObserved,
			FallingHazardForecastPhase::AlignedContinuation, 0.02f);
		forecast.State.ExpectedSegments[0] = DirectSegment(0, 0.0f);
		forecast.State.ExpectedSegments[0].Leg = FallingHazardSweepLeg::Aligned;
		Check(observer.ArmGeneration(
			FallingHazardForecastSource::AlignedContinuationCommit, forecast),
			"a precharged aligned continuation arms");
		Check(observer.Diagnostics().front().PrechargedElapsed == 0.02f,
			"the start diagnostic preserves precharged elapsed");
		auto sweep = SafeSweep(forecast.State.ExpectedSegments[0]);
		Check(observer.ObserveSweep(sweep),
			"a zero-additional-time aligned sweep is valid after precharge");
		Check(observer.FinishLanding(FallingHazardCollisionKind::StaticWorld),
			"the continuation lands");
		Check(observer.Counters().TrueNegativeOutcomes == 1,
			"precharged zero-time continuation remains comparable");

		Check(observer.BeginFallEpisode(), "invalid continuation fall begins");
		auto uncharged = Forecast(FallingHazardForecast::NoHarmfulPainObserved,
			FallingHazardForecastPhase::ThirdContinuation, 0.0f);
		Check(!observer.ArmGeneration(
			FallingHazardForecastSource::ThirdMoveContinuationCommit, uncharged),
			"an uncharged zero-time continuation is rejected");
		Check(observer.Counters().EpisodesStarted == 1,
			"a rejected continuation creates no episode counter or diagnostic");

		Check(!observer.ArmGeneration(
			FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved,
				FallingHazardForecastPhase::AlignedContinuation, 0.02f)),
			"a continuation forecast cannot masquerade as a full-step source");
		Check(!observer.ArmGeneration(
			FallingHazardForecastSource::AlignedContinuationCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"an aligned source requires an aligned forecast phase");
	}

	void TestTypedBoundaryCompletions()
	{
		struct Boundary
		{
			FallingHazardTerminal Terminal;
			bool (FallingHazardRuntimeObserver::*Finish)();
		};
		const Boundary boundaries[] = {
			{ FallingHazardTerminal::CallbackBoundary,
				&FallingHazardRuntimeObserver::FinishCallbackBoundary },
			{ FallingHazardTerminal::ExternalImpulseBoundary,
				&FallingHazardRuntimeObserver::FinishExternalImpulseBoundary },
			{ FallingHazardTerminal::ObservationHorizonExhausted,
				&FallingHazardRuntimeObserver::FinishObservationHorizon },
		};
		FallingHazardRuntimeObserver observer("BoundaryBot");
		for (const Boundary& boundary : boundaries)
		{
			if (!observer.HasActiveFallEpisode())
				Check(observer.BeginFallEpisode(), "boundary fall begins");
			Check(observer.ArmGeneration(FallingHazardForecastSource::ExistingFallingCommit,
				Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
				"boundary generation arms");
			Check((observer.*boundary.Finish)(), "typed boundary completes");
			Check(observer.Diagnostics().back().Generation.Terminal == boundary.Terminal,
				"typed boundary reaches its exact model terminal");
		}
		Check(observer.Counters().UnknownOutcomes == 3,
			"all non-observable boundary completions are unknown");
		CheckPartition(observer.Counters());
	}

	void TestLifeAndFallIdentifiers()
	{
		FallingHazardRuntimeObserver observer("LifecycleBot");
		Check(observer.BeginFallEpisode(), "life one fall one begins");
		Check(observer.ArmGeneration(FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"life one generation arms");
		observer.FinishLanding(FallingHazardCollisionKind::StaticWorld);
		Check(observer.BeginFallEpisode() && observer.CurrentFallEpisode().Value == 2,
			"fall IDs increase within a life");
		observer.AbandonFallEpisode();
		observer.EndLife();
		Check(observer.CurrentLife().Value == 2,
			"ending a life increments the life ID");
		Check(observer.BeginFallEpisode() && observer.CurrentFallEpisode().Value == 1,
			"fall IDs restart under a new life ID");
		Check(observer.ArmGeneration(FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"new-life generation arms");
		Check(observer.CurrentGeneration().Value == 1,
			"generation storage resets safely under the increasing life ID");
	}

	void TestCapacityAndBoundedDiagnostics()
	{
		FallingHazardRuntimeObserver capacity("CapacityBot");
		Check(capacity.BeginFallEpisode(), "capacity fall begins");
		std::vector<FallingHazardDiagnosticRecord> last;
		for (uint32_t index = 0; index < FallingHazardMaximumGenerationsPerLife;
			index++)
		{
			Check(capacity.ArmGeneration(
				FallingHazardForecastSource::ExistingFallingCommit,
				Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
				"every in-capacity generation arms");
			last = capacity.DrainDiagnostics();
		}
		Check(!capacity.ArmGeneration(
			FallingHazardForecastSource::HorizonContinuationCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"the first over-capacity generation is rejected");
		last = capacity.DrainDiagnostics();
		Check(last.size() == 2
			&& last[0].Kind == FallingHazardDiagnosticKind::Terminal
			&& last[1].Kind
				== FallingHazardDiagnosticKind::GenerationCapacityExceeded,
			"capacity closes the prior generation before its capacity record");
		Check(last[1].Generation.Generation.Value == 0
			&& last[1].Generation.Source
				== FallingHazardForecastSource::HorizonContinuationCommit,
			"capacity diagnostic has generation zero and attempted source");
		Check(capacity.Counters().EpisodesStarted
			== FallingHazardMaximumGenerationsPerLife
			&& capacity.Counters().EpisodesCompleted
				== FallingHazardMaximumGenerationsPerLife
			&& capacity.Counters().GenerationCapacityExhaustions == 1
			&& capacity.Counters().DiagnosticOverflows == 0,
			"capacity counters reconcile when the stream is drained");
		Check(!capacity.ArmGeneration(
			FallingHazardForecastSource::HorizonContinuationCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"repeated capacity attempts remain rejected");
		Check(capacity.DrainDiagnostics().empty()
			&& capacity.Counters().GenerationCapacityExhaustions == 1,
			"capacity exhaustion emits only once per life");

		FallingHazardRuntimeObserver overflow("OverflowBot");
		for (size_t index = 0; index < 600; index++)
		{
			Check(overflow.BeginFallEpisode(), "overflow fall begins");
			Check(overflow.ArmGeneration(
				FallingHazardForecastSource::ExistingFallingCommit,
				Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
				"overflow generation arms");
			Check(overflow.ObserveSweep(SafeSweep()), "overflow sweep observes");
			Check(overflow.FinishLanding(FallingHazardCollisionKind::StaticWorld),
				"overflow generation lands");
		}
		Check(overflow.Diagnostics().size()
			== FallingHazardRuntimeMaximumQueuedDiagnostics,
			"diagnostic storage never exceeds its fixed queue bound");
		Check(overflow.Counters().DiagnosticOverflows
			== 1200 - FallingHazardRuntimeMaximumQueuedDiagnostics,
			"every dropped diagnostic is counted exactly");
		CheckPartition(overflow.Counters());
	}

	void TestDrainAndSourceUpdate()
	{
		FallingHazardRuntimeObserver observer;
		observer.SetSourcePawnActor("RenamedBot");
		Check(observer.BeginFallEpisode(), "drain fall begins");
		Check(observer.ArmGeneration(FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"drain generation arms");
		auto first = observer.DrainDiagnostics();
		Check(first.size() == 1 && first[0].SourcePawnActor == "RenamedBot",
			"updated source is copied into diagnostics");
		Check(observer.Diagnostics().empty(), "drain empties the bounded queue");
		observer.FinishDeath();
		auto second = observer.DrainDiagnostics();
		Check(second.size() == 1 && second[0].Sequence == 2,
			"draining does not reset the diagnostic sequence");

		FallingHazardRuntimeObserver unnamed;
		Check(unnamed.BeginFallEpisode(), "an unnamed observer may track a fall");
		Check(!unnamed.ArmGeneration(
			FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"an empty source actor cannot create analyzer-invalid diagnostics");

		FallingHazardRuntimeObserver oversized("OversizedBot");
		Check(oversized.BeginFallEpisode(), "oversized-sample fall begins");
		Check(oversized.ArmGeneration(
			FallingHazardForecastSource::ExistingFallingCommit,
			Forecast(FallingHazardForecast::NoHarmfulPainObserved)),
			"oversized-sample generation arms");
		auto oversizedSweep = SafeSweep();
		oversizedSweep.Segment.SampleCount =
			FallingHazardForecastMaximumSamples + 1;
		Check(oversized.ObserveSweep(oversizedSweep)
			&& !oversized.HasActiveGeneration()
			&& oversized.Counters().UnknownOutcomes == 1,
			"oversized actual sample counts complete unknown without overflow");
	}
}

int main()
{
	TestSafeLandingAndStableDiagnostics();
	TestAcceptsPureForecastOutput();
	TestCorrelationPartition();
	TestSupersedeOrderingAndSources();
	TestZeroElapsedContinuation();
	TestTypedBoundaryCompletions();
	TestLifeAndFallIdentifiers();
	TestCapacityAndBoundedDiagnostics();
	TestDrainAndSourceUpdate();
	std::cout << "Pawn falling hazard runtime observer tests passed\n";
	return 0;
}
