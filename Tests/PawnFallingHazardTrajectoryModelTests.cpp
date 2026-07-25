#include "UObject/PawnFallingHazardTrajectoryModel.h"

#include <iostream>
#include <string>

namespace
{
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	PawnMovement::FallingHazardZoneId Zone(
		uint32_t actorId, uint32_t zoneNumber = 0)
	{
		return {
			.Known = true,
			.ZoneActorId = actorId,
			.ZoneNumber = zoneNumber
		};
	}

	PawnMovement::FallingHazardArmUpdate Arm(
		const PawnMovement::FallingHazardTrajectoryModel& model,
		PawnMovement::FallingHazardForecast forecast,
		PawnMovement::FallingHazardForecastSource source =
			PawnMovement::FallingHazardForecastSource::ExistingFallingCommit,
		uint64_t life = 1, uint64_t fall = 10,
		size_t segmentBudget = PawnMovement::FallingHazardMaximumSweptSegments,
		float horizon = 4.0f, bool expectedWater = false,
		float prechargedElapsed = 0.0f)
	{
		return PawnMovement::ArmFallingHazardForecast(model, {
			.Life = { life },
			.FallEpisode = { fall },
			.Source = source,
			.Forecast = forecast,
			.StartingPhysicsZone = Zone(100),
			.ExpectedHarmfulFootZone =
				forecast == PawnMovement::FallingHazardForecast::HarmfulPainObserved
					? Zone(42) : PawnMovement::FallingHazardZoneId{},
			.ExpectedHarmfulPhysicsZone =
				forecast == PawnMovement::FallingHazardForecast::HarmfulPainObserved
					? Zone(42) : PawnMovement::FallingHazardZoneId{},
			.ExpectedHarmfulWaterEntry = expectedWater,
			.SweptSegmentBudget = segmentBudget,
			.ElapsedHorizon = horizon,
			.PrechargedElapsed = prechargedElapsed
		});
	}

	PawnMovement::FallingHazardSweptSegment SafeSegment(
		PawnMovement::FallingHazardGenerationId generation,
		size_t ordinal = 0, float elapsed = 0.02f)
	{
		return {
			.Life = { 1 },
			.FallEpisode = { 10 },
			.Generation = generation,
			.Ordinal = ordinal,
			.Collision = PawnMovement::FallingHazardCollisionKind::Clear,
			.Elapsed = elapsed,
			.HarmfulCenterZoneKnown = true,
			.CenterZone = Zone(100),
			.HarmfulFootZoneKnown = true,
			.FootZone = Zone(100),
			.PhysicsZone = Zone(100),
			.RegionWaterKnown = true,
			.FootWaterKnown = true,
			.HeadWaterKnown = true,
			.ForecastEndpointKnown = true,
			.ForecastEndpointMatched = true,
			.CallbackMaskKnown = true
		};
	}

	PawnMovement::FallingHazardSweptSegment HarmfulSegment(
		PawnMovement::FallingHazardGenerationId generation,
		uint64_t zone = 42, bool water = true)
	{
		auto segment = SafeSegment(generation);
		segment.Collision = PawnMovement::FallingHazardCollisionKind::StaticWorld;
		segment.InHarmfulFootZone = true;
		segment.FootZone = Zone(static_cast<uint32_t>(zone));
		segment.PhysicsZone = Zone(static_cast<uint32_t>(zone));
		segment.InRegionWater = water;
		segment.InFootWater = water;
		segment.InHeadWater = water;
		segment.CallbackMask = PawnMovement::FallingHazardRegionCallback
			| PawnMovement::FallingHazardFootZoneCallback
			| PawnMovement::FallingHazardHeadZoneCallback;
		return segment;
	}

	PawnMovement::FallingHazardSweptSegment HarmfulCenterSegment(
		PawnMovement::FallingHazardGenerationId generation,
		uint32_t zone = 42)
	{
		auto segment = SafeSegment(generation);
		segment.Collision = PawnMovement::FallingHazardCollisionKind::StaticWorld;
		segment.InHarmfulCenterZone = true;
		segment.CenterZone = Zone(zone);
		segment.PhysicsZone = Zone(zone);
		segment.CallbackMask = PawnMovement::FallingHazardRegionCallback;
		return segment;
	}

	PawnMovement::FallingHazardTrajectoryUpdate Finish(
		const PawnMovement::FallingHazardTrajectoryModel& model,
		PawnMovement::FallingHazardGenerationId generation,
		PawnMovement::FallingHazardTerminal terminal,
		PawnMovement::FallingHazardCollisionKind landing =
			PawnMovement::FallingHazardCollisionKind::Unknown,
		uint64_t life = 1, uint64_t fall = 10)
	{
		return PawnMovement::FinishFallingHazardGeneration(model, {
			.Life = { life },
			.FallEpisode = { fall },
			.Generation = generation,
			.Terminal = terminal,
			.LandingCollision = landing
		});
	}

	void TestHarmfulEntryCompletesImmediately()
	{
		using namespace PawnMovement;
		auto armed = Arm({}, FallingHazardForecast::HarmfulPainObserved,
			FallingHazardForecastSource::PostWallDeflectionCommit,
			1, 10, FallingHazardMaximumSweptSegments, 4.0f, true);
		const auto observed = ObserveFallingHazardSweptSegment(
			armed.Model, HarmfulSegment(armed.ArmedGeneration));
		Check(observed.HasCompletedGeneration
			&& !observed.Model.HasActiveGeneration
			&& observed.CompletedGeneration.Terminal
				== FallingHazardTerminal::HarmfulPainEntered,
			"known harmful foot entry terminally completes before Swimming");
		Check(CorrelateFallingHazardGeneration(observed.CompletedGeneration)
			== FallingHazardCorrelation::ConfirmedHarmfulForecast,
			"Deck-like exact post-wall harmful entry confirms immediately");
	}

	void TestNegativeForecastHarmfulWaterIsActualOnly()
	{
		using namespace PawnMovement;
		auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		const auto observed = ObserveFallingHazardSweptSegment(
			armed.Model, HarmfulSegment(armed.ArmedGeneration));
		Check(observed.HasCompletedGeneration
			&& observed.CompletedGeneration.Terminal
				== FallingHazardTerminal::HarmfulPainEntered,
			"known harmful water wins over the subsequent water boundary");
		Check(CorrelateFallingHazardGeneration(observed.CompletedGeneration)
			== FallingHazardCorrelation::ActualOnly,
			"known harmful water after a negative forecast is retained as a false negative");
	}

	void TestCenterOnlyHarmfulCorrelationAndInvalidIdentity()
	{
		using namespace PawnMovement;
		auto armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		const auto predicted = ObserveFallingHazardSweptSegment(
			armed.Model, HarmfulCenterSegment(armed.ArmedGeneration));
		Check(predicted.HasCompletedGeneration
			&& predicted.CompletedGeneration.EnteredHarmfulCenterZone
			&& !predicted.CompletedGeneration.EnteredHarmfulFootZone
			&& predicted.CompletedGeneration.ObservedHarmfulCenterZone.Known
			&& predicted.CompletedGeneration.ObservedHarmfulCenterZone.ZoneActorId
				== 42
			&& CorrelateFallingHazardGeneration(predicted.CompletedGeneration)
				== FallingHazardCorrelation::ConfirmedHarmfulForecast,
			"a center-only harmful entry confirms a matching harmful forecast");

		armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		const auto missed = ObserveFallingHazardSweptSegment(
			armed.Model, HarmfulCenterSegment(armed.ArmedGeneration));
		Check(missed.HasCompletedGeneration
			&& missed.CompletedGeneration.EnteredHarmfulCenterZone
			&& !missed.CompletedGeneration.EnteredHarmfulFootZone
			&& CorrelateFallingHazardGeneration(missed.CompletedGeneration)
				== FallingHazardCorrelation::ActualOnly,
			"a center-only harmful entry is a false negative for a safe forecast");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto unknownFoot = HarmfulCenterSegment(armed.ArmedGeneration);
		unknownFoot.HarmfulFootZoneKnown = false;
		const auto centerWithUnknownFoot = ObserveFallingHazardSweptSegment(
			armed.Model, unknownFoot);
		Check(centerWithUnknownFoot.HasCompletedGeneration
			&& centerWithUnknownFoot.CompletedGeneration.Terminal
				== FallingHazardTerminal::HarmfulPainEntered
			&& centerWithUnknownFoot.CompletedGeneration.EnteredHarmfulCenterZone
			&& !centerWithUnknownFoot.CompletedGeneration.EnteredHarmfulFootZone
			&& centerWithUnknownFoot.CompletedGeneration.HarmfulCenterEvidenceKnown
			&& !centerWithUnknownFoot.CompletedGeneration.HarmfulFootEvidenceKnown
			&& CorrelateFallingHazardGeneration(
				centerWithUnknownFoot.CompletedGeneration)
				== FallingHazardCorrelation::Unknown,
			"a valid center entry with unclaimed unknown foot evidence remains unknown");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto invalid = HarmfulCenterSegment(armed.ArmedGeneration);
		invalid.CenterZone = Zone(0);
		const auto unknown = ObserveFallingHazardSweptSegment(
			armed.Model, invalid);
		Check(unknown.HasCompletedGeneration
			&& unknown.CompletedGeneration.Terminal
				== FallingHazardTerminal::ContinuityLost
			&& !unknown.CompletedGeneration.HarmfulCenterEvidenceKnown
			&& !unknown.CompletedGeneration.EnteredHarmfulCenterZone
			&& !unknown.CompletedGeneration.EnteredHarmfulFootZone
			&& !unknown.CompletedGeneration.ObservedHarmfulCenterZone.Known
			&& unknown.CompletedGeneration.ObservedHarmfulCenterZone.ZoneActorId == 0
			&& unknown.CompletedGeneration.ObservedHarmfulCenterZone.ZoneNumber == 0
			&& CorrelateFallingHazardGeneration(unknown.CompletedGeneration)
				== FallingHazardCorrelation::Unknown,
			"an invalid-only harmful center fails closed without a harmful terminal");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto partial = HarmfulSegment(armed.ArmedGeneration, 42, false);
		partial.InHarmfulCenterZone = true;
		partial.CenterZone = Zone(0);
		const auto partiallyKnown = ObserveFallingHazardSweptSegment(
			armed.Model, partial);
		Check(partiallyKnown.HasCompletedGeneration
			&& partiallyKnown.CompletedGeneration.Terminal
				== FallingHazardTerminal::HarmfulPainEntered
			&& !partiallyKnown.CompletedGeneration.EnteredHarmfulCenterZone
			&& partiallyKnown.CompletedGeneration.EnteredHarmfulFootZone
			&& partiallyKnown.CompletedGeneration.ObservedHarmfulFootZone.ZoneActorId
				== 42
			&& CorrelateFallingHazardGeneration(
				partiallyKnown.CompletedGeneration)
				== FallingHazardCorrelation::Unknown,
			"a valid foot entry preserves the harmful terminal while invalid center evidence makes correlation unknown");
	}

	void TestCausalUncertaintyIsAmbiguous()
	{
		using namespace PawnMovement;
		auto armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto observed = ObserveFallingHazardSweptSegment(
			armed.Model, HarmfulSegment(armed.ArmedGeneration, 99, false));
		Check(CorrelateFallingHazardGeneration(observed.CompletedGeneration)
			== FallingHazardCorrelation::Ambiguous,
			"a different known harmful zone is ambiguous rather than unknown");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto physicsMismatch = HarmfulSegment(armed.ArmedGeneration, 42, false);
		physicsMismatch.PhysicsZone = Zone(99);
		observed = ObserveFallingHazardSweptSegment(
			armed.Model, physicsMismatch);
		Check(CorrelateFallingHazardGeneration(observed.CompletedGeneration)
			== FallingHazardCorrelation::Ambiguous,
			"exact foot pain in a different physics zone is causally ambiguous");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto segment = HarmfulSegment(armed.ArmedGeneration, 42, false);
		segment.CallbackMask = FallingHazardTouchCallback;
		observed = ObserveFallingHazardSweptSegment(armed.Model, segment);
		Check(CorrelateFallingHazardGeneration(observed.CompletedGeneration)
			== FallingHazardCorrelation::Ambiguous,
			"simultaneous non-zone callback and pain entry has a distinct ambiguous class");
	}

	void TestDeathIsCensored()
	{
		using namespace PawnMovement;
		for (const auto forecast : {
			FallingHazardForecast::HarmfulPainObserved,
			FallingHazardForecast::NoHarmfulPainObserved })
		{
			auto armed = Arm({}, forecast);
			auto observed = ObserveFallingHazardSweptSegment(
				armed.Model, SafeSegment(armed.ArmedGeneration));
			const auto died = Finish(observed.Model, armed.ArmedGeneration,
				FallingHazardTerminal::Died);
			Check(CorrelateFallingHazardGeneration(died.CompletedGeneration)
				== FallingHazardCorrelation::Unknown,
				"combat death before pain or landing is censored, never FP or TN");
		}
	}

	void TestZeroElapsedSecondaryLegsAndSafeLanding()
	{
		using namespace PawnMovement;
		auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		auto first = ObserveFallingHazardSweptSegment(
			armed.Model, SafeSegment(armed.ArmedGeneration));
		auto alignedSegment = SafeSegment(armed.ArmedGeneration, 1, 0.0f);
		alignedSegment.Leg = FallingHazardSweepLeg::Aligned;
		alignedSegment.Collision = FallingHazardCollisionKind::StaticWorld;
		auto aligned = ObserveFallingHazardSweptSegment(first.Model, alignedSegment);
		auto adjustedSegment = SafeSegment(armed.ArmedGeneration, 2, 0.0f);
		adjustedSegment.Leg = FallingHazardSweepLeg::TwoWallAdjusted;
		auto adjusted = ObserveFallingHazardSweptSegment(
			aligned.Model, adjustedSegment);
		const auto landed = Finish(adjusted.Model, armed.ArmedGeneration,
			FallingHazardTerminal::Landed,
			FallingHazardCollisionKind::StaticWorld);
		Check(landed.CompletedGeneration.SweptSegmentCount == 3
			&& landed.CompletedGeneration.ObservedElapsed == 0.02f,
			"aligned/TwoWallAdjust legs may share the already charged time slice");
		Check(CorrelateFallingHazardGeneration(landed.CompletedGeneration)
			== FallingHazardCorrelation::ConfirmedNoHarmfulObservation,
			"a known static landing after charged and zero-time legs is comparable");

		armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		const auto zeroFirst = ObserveFallingHazardSweptSegment(
			armed.Model, SafeSegment(armed.ArmedGeneration, 0, 0.0f));
		Check(zeroFirst.HasCompletedGeneration
			&& zeroFirst.CompletedGeneration.Terminal
				== FallingHazardTerminal::InvalidObservation,
			"a generation cannot become comparable from zero-time legs alone");

		armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		first = ObserveFallingHazardSweptSegment(
			armed.Model, SafeSegment(armed.ArmedGeneration));
		const auto zeroDirect = ObserveFallingHazardSweptSegment(
			first.Model, SafeSegment(armed.ArmedGeneration, 1, 0.0f));
		Check(zeroDirect.CompletedGeneration.Terminal
			== FallingHazardTerminal::InvalidObservation,
			"only aligned and TwoWallAdjust legs may reuse charged elapsed time");

		armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved,
			FallingHazardForecastSource::AlignedContinuationCommit,
			1, 10, FallingHazardMaximumSweptSegments, 4.0f, false, 0.02f);
		auto firstAligned = SafeSegment(armed.ArmedGeneration, 0, 0.0f);
		firstAligned.Leg = FallingHazardSweepLeg::Aligned;
		aligned = ObserveFallingHazardSweptSegment(armed.Model, firstAligned);
		const auto continuationLanding = Finish(aligned.Model,
			armed.ArmedGeneration, FallingHazardTerminal::Landed,
			FallingHazardCollisionKind::StaticWorld);
		Check(CorrelateFallingHazardGeneration(
			continuationLanding.CompletedGeneration) ==
			FallingHazardCorrelation::ConfirmedNoHarmfulObservation,
			"typed aligned continuation carries its explicitly precharged time slice");
	}

	void TestPhysicsZoneAndFallContinuity()
	{
		using namespace PawnMovement;
		auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		auto safe = ObserveFallingHazardSweptSegment(
			armed.Model, SafeSegment(armed.ArmedGeneration));
		Check(!safe.HasCompletedGeneration && safe.Model.HasActiveGeneration
			&& safe.Model.ActiveGeneration.PhysicsZoneEvidenceKnown
			&& safe.Model.ActiveGeneration.LastObservedPhysicsZone.ZoneActorId == 100
			&& safe.Model.ActiveGeneration.LastObservedPhysicsZone.ZoneNumber == 0,
			"unchanged known physics-zone identity preserves continuity");

		for (int mode = 0; mode < 3; mode++)
		{
			armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
			auto segment = SafeSegment(armed.ArmedGeneration);
			if (mode == 0)
				segment.PhysicsZone = {};
			else if (mode == 1)
				segment.PhysicsZone = Zone(101);
			else
				segment.PhysicsZone = Zone(100, 1);
			const auto lost = ObserveFallingHazardSweptSegment(armed.Model, segment);
			Check(lost.HasCompletedGeneration
				&& lost.CompletedGeneration.Terminal
					== FallingHazardTerminal::ContinuityLost
				&& lost.CompletedGeneration.PhysicsZoneEvidenceKnown == (mode != 0)
				&& (mode == 0
					|| (lost.CompletedGeneration.LastObservedPhysicsZone.ZoneActorId
						== (mode == 1 ? 101u : 100u)
						&& lost.CompletedGeneration.LastObservedPhysicsZone.ZoneNumber
							== (mode == 2 ? 1u : 0u))),
				"unknown or changed physics-zone identity fails closed");
		}

		armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		auto wrongFall = SafeSegment(armed.ArmedGeneration);
		wrongFall.FallEpisode = { 11 };
		const auto lost = ObserveFallingHazardSweptSegment(armed.Model, wrongFall);
		Check(lost.CompletedGeneration.Terminal
			== FallingHazardTerminal::ContinuityLost,
			"generation evidence cannot cross fall episodes within one pawn life");
	}

	void TestInvalidKnownZoneIdentitiesFailClosed()
	{
		using namespace PawnMovement;
		FallingHazardForecastArm arm = {
			.Life = { 1 },
			.FallEpisode = { 10 },
			.Source = FallingHazardForecastSource::ExistingFallingCommit,
			.Forecast = FallingHazardForecast::Unknown,
			.StartingPhysicsZone = Zone(0)
		};
		Check(!ArmFallingHazardForecast({}, arm).Armed,
			"a known starting zone requires a positive actor identity");

		arm.StartingPhysicsZone = Zone(100);
		arm.Forecast = FallingHazardForecast::HarmfulPainObserved;
		arm.ExpectedHarmfulFootZone = Zone(0);
		arm.ExpectedHarmfulPhysicsZone = Zone(42);
		Check(!ArmFallingHazardForecast({}, arm).Armed,
			"a known harmful foot zone requires a positive actor identity");
		arm.ExpectedHarmfulFootZone = Zone(42);
		arm.ExpectedHarmfulPhysicsZone = Zone(0);
		Check(!ArmFallingHazardForecast({}, arm).Armed,
			"a known harmful physics zone requires a positive actor identity");
		arm.Forecast = FallingHazardForecast::NoHarmfulPainObserved;
		arm.ExpectedHarmfulFootZone = {
			.Known = false, .ZoneActorId = 42, .ZoneNumber = 0
		};
		arm.ExpectedHarmfulPhysicsZone = {};
		Check(!ArmFallingHazardForecast({}, arm).Armed,
			"an unknown expected zone cannot carry a stale actor identity");

		auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		auto invalidPhysics = SafeSegment(armed.ArmedGeneration);
		invalidPhysics.PhysicsZone = Zone(0);
		const auto lost = ObserveFallingHazardSweptSegment(
			armed.Model, invalidPhysics);
		Check(lost.HasCompletedGeneration
			&& CorrelateFallingHazardGeneration(lost.CompletedGeneration)
				== FallingHazardCorrelation::Unknown
			&& !lost.CompletedGeneration.PhysicsZoneEvidenceKnown
			&& !lost.CompletedGeneration.LastObservedPhysicsZone.Known
			&& lost.CompletedGeneration.LastObservedPhysicsZone.ZoneActorId == 0
			&& lost.CompletedGeneration.LastObservedPhysicsZone.ZoneNumber == 0,
			"a known-zero observed physics zone becomes sanitized unknown evidence");

		armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		auto safeWithoutFootIdentity = SafeSegment(armed.ArmedGeneration);
		safeWithoutFootIdentity.FootZone = {};
		const auto safe = ObserveFallingHazardSweptSegment(
			armed.Model, safeWithoutFootIdentity);
		Check(!safe.HasCompletedGeneration
			&& safe.Model.ActiveGeneration.HarmfulFootEvidenceKnown,
			"known non-harmful truth does not require an unused foot-zone identity");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto invalidFoot = HarmfulSegment(armed.ArmedGeneration, 42, false);
		invalidFoot.FootZone = Zone(0);
		const auto unknownFoot = ObserveFallingHazardSweptSegment(
			armed.Model, invalidFoot);
		Check(unknownFoot.HasCompletedGeneration
			&& CorrelateFallingHazardGeneration(unknownFoot.CompletedGeneration)
				== FallingHazardCorrelation::Unknown
			&& !unknownFoot.CompletedGeneration.HarmfulFootEvidenceKnown
			&& !unknownFoot.CompletedGeneration.ObservedHarmfulFootZone.Known
			&& unknownFoot.CompletedGeneration.ObservedHarmfulFootZone.ZoneActorId == 0
			&& unknownFoot.CompletedGeneration.ObservedHarmfulFootZone.ZoneNumber == 0,
			"a known-zero observed harmful foot zone is never copied to completion");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto invalidHarmfulPhysics = HarmfulSegment(
			armed.ArmedGeneration, 42, false);
		invalidHarmfulPhysics.PhysicsZone = Zone(0);
		const auto unknownPhysics = ObserveFallingHazardSweptSegment(
			armed.Model, invalidHarmfulPhysics);
		Check(unknownPhysics.HasCompletedGeneration
			&& CorrelateFallingHazardGeneration(unknownPhysics.CompletedGeneration)
				== FallingHazardCorrelation::Unknown
			&& !unknownPhysics.CompletedGeneration.PhysicsZoneEvidenceKnown
			&& !unknownPhysics.CompletedGeneration.LastObservedPhysicsZone.Known
			&& unknownPhysics.CompletedGeneration.LastObservedPhysicsZone.ZoneActorId == 0,
			"known-zero harmful physics evidence is sanitized before completion");
	}

	void TestUnexpectedRegionFootAndHeadWaterFailClosed()
	{
		using namespace PawnMovement;
		for (int waterPart = 0; waterPart < 4; waterPart++)
		{
			auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
			auto segment = SafeSegment(armed.ArmedGeneration);
			if (waterPart == 0) segment.InRegionWater = true;
			if (waterPart == 1) segment.InFootWater = true;
			if (waterPart == 2) segment.InHeadWater = true;
			if (waterPart == 3) segment.HeadWaterKnown = false;
			const auto observed = ObserveFallingHazardSweptSegment(
				armed.Model, segment);
			Check(observed.CompletedGeneration.Terminal
				== FallingHazardTerminal::WaterPhysicsBoundary
				&& CorrelateFallingHazardGeneration(observed.CompletedGeneration)
					== FallingHazardCorrelation::Unknown,
				"unexpected region, foot, and head water are competing unknown boundaries");
		}

		auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
		auto zoneCallback = SafeSegment(armed.ArmedGeneration);
		zoneCallback.CallbackMask = FallingHazardRegionCallback;
		const auto callback = ObserveFallingHazardSweptSegment(
			armed.Model, zoneCallback);
		Check(callback.CompletedGeneration.Terminal
			== FallingHazardTerminal::CallbackBoundary
			&& CorrelateFallingHazardGeneration(callback.CompletedGeneration)
				== FallingHazardCorrelation::Unknown,
			"even a zone-only callback without harmful entry requires rearm");
	}

	void TestNonharmfulUnstableContactsCompleteUnknown()
	{
		using namespace PawnMovement;
		for (const auto collision : {
			FallingHazardCollisionKind::Mover,
			FallingHazardCollisionKind::DynamicActor,
			FallingHazardCollisionKind::Unknown })
		{
			auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
			auto segment = SafeSegment(armed.ArmedGeneration);
			segment.Collision = collision;
			const auto observed = ObserveFallingHazardSweptSegment(
				armed.Model, segment);
			Check(observed.HasCompletedGeneration
				&& !observed.Model.HasActiveGeneration
				&& observed.CompletedGeneration.Terminal
					== FallingHazardTerminal::ContinuityLost
				&& CorrelateFallingHazardGeneration(
					observed.CompletedGeneration)
					== FallingHazardCorrelation::Unknown,
				"nonharmful mover, dynamic, and unknown contacts terminate fail-closed");
		}

		for (int mode = 0; mode < 3; mode++)
		{
			auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved);
			auto segment = SafeSegment(armed.ArmedGeneration);
			if (mode == 0)
				segment.ForecastEndpointMatched = false;
			else if (mode == 1)
				segment.HarmfulFootZoneKnown = false;
			else
				segment.HarmfulCenterZoneKnown = false;
			const auto observed = ObserveFallingHazardSweptSegment(
				armed.Model, segment);
			Check(observed.HasCompletedGeneration
				&& observed.CompletedGeneration.Terminal
					== FallingHazardTerminal::ContinuityLost
				&& CorrelateFallingHazardGeneration(
					observed.CompletedGeneration)
					== FallingHazardCorrelation::Unknown,
				"endpoint mismatch and unknown center or foot pain evidence terminate immediately");
		}
	}

	void TestRearmStreamsCompletedGenerations()
	{
		using namespace PawnMovement;
		auto armed = Arm({}, FallingHazardForecast::Unknown);
		FallingHazardTrajectoryModel model = armed.Model;
		uint32_t completed = 0;
		for (int index = 0; index < 24; index++)
		{
			const auto next = Arm(model, FallingHazardForecast::Unknown,
				FallingHazardForecastSource::CallbackReturnCommit);
			Check(next.Armed && next.HasCompletedGeneration
				&& next.CompletedGeneration.Terminal
					== FallingHazardTerminal::CallbackBoundary,
				"callback rearm drains one completed generation without a history array");
			completed += next.HasCompletedGeneration ? 1u : 0u;
			model = next.Model;
		}
		Check(completed == 24 && model.GenerationCountForLife == 25,
			"callback chains longer than eight generations remain bounded and drainable");

		const auto horizon = Arm(model, FallingHazardForecast::Unknown,
			FallingHazardForecastSource::HorizonContinuationCommit);
		Check(horizon.CompletedGeneration.Terminal
			== FallingHazardTerminal::ObservationHorizonExhausted,
			"typed horizon continuation closes Unknown and rearms from current state");
		const auto impulse = Arm(horizon.Model, FallingHazardForecast::Unknown,
			FallingHazardForecastSource::ExternalImpulseCommit);
		Check(impulse.CompletedGeneration.Terminal
			== FallingHazardTerminal::ExternalImpulseBoundary,
			"external impulse commit drains and rearms a distinct generation");
	}

	void TestEveryCommittedFallingSourceArms()
	{
		using namespace PawnMovement;
		for (const auto source : {
			FallingHazardForecastSource::UnsupportedWalkCommit,
			FallingHazardForecastSource::ExistingFallingCommit,
			FallingHazardForecastSource::PostWallDeflectionCommit,
			FallingHazardForecastSource::AlignedContinuationCommit,
			FallingHazardForecastSource::ThirdMoveContinuationCommit,
			FallingHazardForecastSource::CallbackReturnCommit,
			FallingHazardForecastSource::ScriptTickTransitionCommit,
			FallingHazardForecastSource::ExternalImpulseCommit,
			FallingHazardForecastSource::HorizonContinuationCommit })
		{
			const bool precharged =
				source == FallingHazardForecastSource::AlignedContinuationCommit
				|| source
					== FallingHazardForecastSource::ThirdMoveContinuationCommit;
			const auto armed = Arm({}, FallingHazardForecast::Unknown, source,
				1, 10, FallingHazardMaximumSweptSegments, 4.0f, false,
				precharged ? 0.02f : 0.0f);
			Check(armed.Armed && armed.ArmedGeneration.Value == 1
				&& armed.Model.ActiveGeneration.Source == source,
				"every committed PHYS_Falling source arms a typed generation");
		}
	}

	void TestArmAndObservationBounds()
	{
		using namespace PawnMovement;
		FallingHazardForecastArm invalid = {
			.Life = { 1 }, .FallEpisode = { 10 },
			.Source = FallingHazardForecastSource::ExistingFallingCommit,
			.Forecast = FallingHazardForecast::HarmfulPainObserved,
			.StartingPhysicsZone = Zone(100)
		};
		Check(!ArmFallingHazardForecast({}, invalid).Armed,
			"a harmful forecast without exact expected zone identity is rejected");
		invalid.ExpectedHarmfulFootZone = Zone(42);
		Check(!ArmFallingHazardForecast({}, invalid).Armed,
			"a harmful forecast also requires exact endpoint physics-zone identity");
		invalid.Forecast = FallingHazardForecast::NoHarmfulPainObserved;
		Check(!ArmFallingHazardForecast({}, invalid).Armed,
			"a negative forecast cannot carry contradictory harmful-zone state");

		auto armed = Arm({}, FallingHazardForecast::NoHarmfulPainObserved,
			FallingHazardForecastSource::ExistingFallingCommit, 1, 10, 1, 0.01f);
		Check(armed.Model.ActiveGeneration.SweptSegmentBudget == 1
			&& armed.Model.ActiveGeneration.ElapsedHorizon == 0.01f,
			"smaller focused-test bounds remain configurable");
		const auto defaults = Arm({}, FallingHazardForecast::Unknown);
		Check(defaults.Model.ActiveGeneration.SweptSegmentBudget == 256
			&& defaults.Model.ActiveGeneration.ElapsedHorizon == 4.0f
			&& defaults.Model.ActiveGeneration.StartingPhysicsZone.Known
			&& defaults.Model.ActiveGeneration.StartingPhysicsZone.ZoneNumber == 0,
			"audited default bounds cover 256 subsegments and four seconds");
		const auto exhausted = ObserveFallingHazardSweptSegment(
			armed.Model, SafeSegment(armed.ArmedGeneration, 0, 0.02f));
		Check(exhausted.CompletedGeneration.Terminal
			== FallingHazardTerminal::ObservationHorizonExhausted,
			"elapsed overflow terminates distinctly for horizon reforecasting");

		FallingHazardTrajectoryModel atLimit;
		atLimit.CountedLife = { 1 };
		atLimit.GenerationCountForLife = FallingHazardMaximumGenerationsPerLife;
		const auto overflow = Arm(atLimit, FallingHazardForecast::Unknown);
		Check(!overflow.Armed && overflow.Model.GenerationCapacityExceeded,
			"the high per-life generation count still has an explicit hard bound");
		FallingHazardTrajectoryModel rejectedModel = atLimit;
		uint32_t capacityTransitions = 0;
		for (int attempt = 0; attempt < 3; attempt++)
		{
			const bool wasExceeded = rejectedModel.GenerationCapacityExceeded;
			const auto rejected = Arm(rejectedModel, FallingHazardForecast::Unknown);
			capacityTransitions += !wasExceeded
				&& rejected.Model.GenerationCapacityExceeded ? 1u : 0u;
			Check(!rejected.Armed,
				"every arm beyond the per-life generation bound is rejected");
			rejectedModel = rejected.Model;
		}
		Check(capacityTransitions == 1,
			"capacity exhaustion has one latched transition per pawn life");
		const auto nextLife = Arm(rejectedModel, FallingHazardForecast::Unknown,
			FallingHazardForecastSource::ExistingFallingCommit, 2, 20);
		Check(nextLife.Armed && !nextLife.Model.GenerationCapacityExceeded
			&& nextLife.Model.GenerationCountForLife == 1,
			"a new pawn life resets the one-shot capacity transition latch");

		auto activeAtLimit = defaults.Model;
		activeAtLimit.GenerationCountForLife =
			FallingHazardMaximumGenerationsPerLife;
		const auto rejectedRearm = Arm(activeAtLimit,
			FallingHazardForecast::Unknown,
			FallingHazardForecastSource::CallbackReturnCommit);
		Check(!rejectedRearm.Armed && rejectedRearm.HasCompletedGeneration
			&& rejectedRearm.CompletedGeneration.Terminal
				== FallingHazardTerminal::CallbackBoundary
			&& rejectedRearm.Model.GenerationCapacityExceeded,
			"capacity rejection does not overwrite the actual completed boundary");

		armed = Arm({}, FallingHazardForecast::HarmfulPainObserved);
		auto mover = HarmfulSegment(armed.ArmedGeneration, 42, false);
		mover.Collision = FallingHazardCollisionKind::Mover;
		const auto unstable = ObserveFallingHazardSweptSegment(armed.Model, mover);
		Check(CorrelateFallingHazardGeneration(unstable.CompletedGeneration)
			== FallingHazardCorrelation::Unknown,
			"mover, dynamic, or unknown contact cannot validate harmful attribution");
	}
}

int main()
{
	TestHarmfulEntryCompletesImmediately();
	TestNegativeForecastHarmfulWaterIsActualOnly();
	TestCenterOnlyHarmfulCorrelationAndInvalidIdentity();
	TestCausalUncertaintyIsAmbiguous();
	TestDeathIsCensored();
	TestZeroElapsedSecondaryLegsAndSafeLanding();
	TestPhysicsZoneAndFallContinuity();
	TestInvalidKnownZoneIdentitiesFailClosed();
	TestUnexpectedRegionFootAndHeadWaterFailClosed();
	TestNonharmfulUnstableContactsCompleteUnknown();
	TestRearmStreamsCompletedGenerations();
	TestEveryCommittedFallingSourceArms();
	TestArmAndObservationBounds();
	if (Failures == 0)
		std::cout << "Pawn falling hazard trajectory model tests passed\n";
	return Failures == 0 ? 0 : 1;
}
