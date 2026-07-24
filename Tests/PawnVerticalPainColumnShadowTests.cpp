#include "UObject/PawnVerticalPainColumnShadow.h"

#include <cmath>
#include <iostream>
#include <limits>
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

	PawnMovement::VerticalPainZoneObservation SafeZone(bool water = false)
	{
		return { .Known = true, .WaterZone = water };
	}

	PawnMovement::VerticalPainZoneObservation PainZone(
		bool harmful, bool water = true)
	{
		return {
			.Known = true,
			.PainZone = true,
			.HarmfulPain = harmful,
			.WaterZone = water
		};
	}

	PawnMovement::VerticalPainColumnInput StaticColumn(float drop = 50.0f)
	{
		using namespace PawnMovement;
		VerticalPainColumnInput input;
		input.UnsupportedEndpointKnown = true;
		input.UnsupportedEndpoint = true;
		input.VerticalCollision = VerticalPainColumnCollisionKind::StaticWorld;
		input.SupportCollision = VerticalPainColumnCollisionKind::StaticWorld;
		input.SupportNormalZ = 1.0f;
		input.DropDistance = drop;
		input.MaximumSampleSpacing = 25.0f;
		input.SampleCount = static_cast<size_t>(std::ceil(drop / 25.0f));
		for (size_t index = 0; index < input.SampleCount; index++)
		{
			auto& sample = input.Samples[index];
			sample.DropDistance = std::min(
				drop, static_cast<float>(index + 1) * 25.0f);
			sample.Center = SafeZone();
			sample.Foot = SafeZone();
			sample.Head = SafeZone();
		}
		return input;
	}

	void TestStaticColumnFindsFootPainWithRetailDepth()
	{
		using namespace PawnMovement;
		auto input = StaticColumn();
		input.Samples[1].Center = PainZone(true);
		input.Samples[1].Foot = PainZone(true);
		input.Samples[1].Head = PainZone(false);
		const auto result = ClassifyVerticalPainColumn(input);
		Check(result.Classification ==
			VerticalPainColumnClassification::HarmfulPainObserved,
			"a fully sampled static column reports harmful foot pain");
		Check(result.FirstHarmfulSample == 1
			&& result.FirstHarmfulDropDistance == 50.0f,
			"the first harmful sample and drop are retained for correlation");
		Check(std::abs(result.FirstHarmfulPainDepth - 1.0f) < 0.0001f,
			"foot, center, and head pain reproduce retail Pawn.PainTimer depth");
	}

	void TestRetailZoneHazardRequiresPositivePainDamage()
	{
		using namespace PawnMovement;
		const auto harmful = ObserveVerticalPainZone(true, true, 40, true);
		Check(harmful.Known && harmful.PainZone && harmful.HarmfulPain
			&& harmful.WaterZone,
			"stock positive-DamagePerSec liquid pain zones are potential hazards");
		Check(!ObserveVerticalPainZone(true, true, 0, false).HarmfulPain,
			"zero-damage pain zones are not labeled harmful");
		Check(!ObserveVerticalPainZone(true, true, -20, false).HarmfulPain,
			"retail healing pain zones are not labeled harmful");
		Check(!ObserveVerticalPainZone(false, true, 40, true).Known,
			"an unavailable zone never becomes known from supplied flag values");
	}

	void TestFootGatesPainDamage()
	{
		using namespace PawnMovement;
		auto input = StaticColumn();
		input.Samples[1].Center = PainZone(true, false);
		input.Samples[1].Head = PainZone(true, false);
		const auto result = ClassifyVerticalPainColumn(input);
		Check(result.Classification ==
			VerticalPainColumnClassification::NoHarmfulPainObserved,
			"center or head pain alone is not reported as retail Pawn pain damage");

		input.Samples[1].Foot = PainZone(false, false);
		Check(ClassifyVerticalPainColumn(input).Classification ==
			VerticalPainColumnClassification::NoHarmfulPainObserved,
			"immune or non-damaging foot pain is not labeled harmful");
	}

	void TestUnknownAndIncompleteSamplesFailClosed()
	{
		using namespace PawnMovement;
		auto input = StaticColumn();
		input.Samples[0].Head.Known = false;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::UnknownZoneSample,
			"unknown center/foot/head evidence produces unknown");

		input = StaticColumn();
		input.SampleCount = 1;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::IncompleteSampleCoverage,
			"a sample gap larger than the configured spacing produces unknown");
		input = StaticColumn();
		input.Samples[0].Foot = PainZone(true);
		input.Samples[1].DropDistance = 40.0f;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::IncompleteSampleCoverage,
			"harmful evidence does not bypass complete bounded-column validation");

		input = StaticColumn(100.0f);
		input.MaximumSamples = 3;
		input.SampleCapExhausted = true;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::SampleCapExceeded,
			"sample-cap exhaustion produces unknown instead of a partial result");
	}

	void TestOnlyWalkableStaticWorldSupportIsClassified()
	{
		using namespace PawnMovement;
		for (const auto collision : {
			VerticalPainColumnCollisionKind::Mover,
			VerticalPainColumnCollisionKind::DynamicActor })
		{
			auto input = StaticColumn();
			input.VerticalCollision = collision;
			Check(ClassifyVerticalPainColumn(input).Classification ==
				VerticalPainColumnClassification::Unknown,
				"mover and dynamic vertical contacts remain unknown");
			input = StaticColumn();
			input.SupportCollision = collision;
			Check(ClassifyVerticalPainColumn(input).Classification ==
				VerticalPainColumnClassification::Unknown,
				"mover and dynamic landing support remain unknown");
		}

		auto input = StaticColumn();
		input.SupportNormalZ = 0.5f;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::NonWalkableSupport,
			"steep static BSP is not treated as a known landing");
		input = StaticColumn();
		input.SupportNormalZ = FallingParityWalkableNormalZ;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::NonWalkableSupport,
			"retail falling does not land at exactly normal.z 0.7");
		input.SupportNormalZ = std::nextafter(
			FallingParityWalkableNormalZ, 1.0f);
		Check(ClassifyVerticalPainColumn(input).Classification ==
			VerticalPainColumnClassification::NoHarmfulPainObserved,
			"the first representable normal above 0.7 is walkable in retail falling");
		input = StaticColumn();
		input.VerticalCollision = VerticalPainColumnCollisionKind::Clear;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::VerticalHorizonExhausted,
			"a column with no bounded landing remains unknown");
		input = StaticColumn();
		input.UnmodeledCallbackRequired = true;
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::UnmodeledCallbackRequired,
			"unmodeled script callbacks remain unknown");
	}

	void TestDefaultHorizonHasCompleteBoundedCapacity()
	{
		using namespace PawnMovement;
		auto input = StaticColumn(VerticalPainColumnMaximumDropDistance);
		Check(input.SampleCount == VerticalPainColumnMaximumSamples,
			"the default 4096-unit horizon fits the bounded sample array");
		Check(ClassifyVerticalPainColumn(input).Classification ==
			VerticalPainColumnClassification::NoHarmfulPainObserved,
			"the full default horizon can be classified with complete evidence");
	}

	void TestWaterPhysicsFailsClosedBeforeLaterPain()
	{
		using namespace PawnMovement;
		auto input = StaticColumn(75.0f);
		input.Samples[0].Foot = SafeZone(true);
		input.Samples[1].Foot = PainZone(true);
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::WaterPhysicsUnknown,
			"water physics before a later pain sample makes the trajectory unknown");

		input = StaticColumn();
		input.Samples[0].Center = SafeZone(true);
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::WaterPhysicsUnknown,
			"center, foot, or head water evidence fails closed");

		input = StaticColumn();
		input.Samples[0].Foot = PainZone(true);
		Check(ClassifyVerticalPainColumn(input).Reason ==
			VerticalPainColumnReason::HarmfulFootPainObserved,
			"direct harmful foot evidence is retained at the first water sample");
	}

	void TestOutcomeCorrelationRetainsLandingProgressAndDeath()
	{
		using namespace PawnMovement;
		auto state = BeginVerticalPainColumnOutcome(
			VerticalPainColumnClassification::HarmfulPainObserved);
		state = ObserveVerticalPainColumnOutcome(state, {
			.HarmfulFootZoneKnown = true,
			.InHarmfulFootZone = true,
			.TargetProgressKnown = true,
			.TargetProgress = 12.0f
		});
		state = ObserveVerticalPainColumnOutcome(state, {
			.HarmfulFootZoneKnown = true,
			.InHarmfulFootZone = true,
			.Landed = true,
			.LandingCollision = VerticalPainColumnCollisionKind::StaticWorld,
			.TargetProgressKnown = true,
			.TargetProgress = 7.0f,
			.Died = true,
			.Suicide = true
		});
		Check(CorrelateVerticalPainColumnOutcome(state) ==
			VerticalPainColumnCorrelation::ConfirmedHarmfulForecast,
			"forecasted and actual harmful entry correlate after a bounded outcome");
		Check(state.Landed && state.LandingCollision ==
			VerticalPainColumnCollisionKind::StaticWorld,
			"actual static landing remains attached to the forecast episode");
		Check(state.TargetProgressKnown && state.MaximumTargetProgress == 12.0f
			&& state.FinalTargetProgress == 7.0f,
			"maximum and final semantic-target progress are retained");
		Check(state.Died && state.Suicide,
			"death and suicide remain attached to the same episode");
	}

	void TestCorrelationReportsForecastOnlyActualOnlyAndNoHazard()
	{
		using namespace PawnMovement;
		auto finish = [](VerticalPainColumnClassification forecast, bool actual)
		{
			auto state = BeginVerticalPainColumnOutcome(forecast);
			return ObserveVerticalPainColumnOutcome(state, {
				.HarmfulFootZoneKnown = true,
				.InHarmfulFootZone = actual,
				.WindowComplete = true
			});
		};
		Check(CorrelateVerticalPainColumnOutcome(finish(
			VerticalPainColumnClassification::HarmfulPainObserved, false)) ==
			VerticalPainColumnCorrelation::ForecastOnly,
			"unobserved predicted exposure is retained as forecast-only");
		Check(CorrelateVerticalPainColumnOutcome(finish(
			VerticalPainColumnClassification::NoHarmfulPainObserved, true)) ==
			VerticalPainColumnCorrelation::ActualOnly,
			"actual unforecast exposure is retained as actual-only");
		Check(CorrelateVerticalPainColumnOutcome(finish(
			VerticalPainColumnClassification::NoHarmfulPainObserved, false)) ==
			VerticalPainColumnCorrelation::ConfirmedNoHarmfulObservation,
			"bounded no-exposure observations correlate without calling them safe");
	}

	void TestUnstableActualTrajectoriesRemainUnknown()
	{
		using namespace PawnMovement;
		for (const auto collision : {
			VerticalPainColumnCollisionKind::Mover,
			VerticalPainColumnCollisionKind::DynamicActor,
			VerticalPainColumnCollisionKind::Unknown })
		{
			auto state = BeginVerticalPainColumnOutcome(
				VerticalPainColumnClassification::HarmfulPainObserved);
			state = ObserveVerticalPainColumnOutcome(state, {
				.HarmfulFootZoneKnown = true,
				.InHarmfulFootZone = true,
				.Collision = collision,
				.WindowComplete = true
			});
			Check(CorrelateVerticalPainColumnOutcome(state) ==
				VerticalPainColumnCorrelation::Unknown,
				"mover, dynamic, and unknown actual contacts do not validate forecasts");
		}

		auto state = BeginVerticalPainColumnOutcome(
			VerticalPainColumnClassification::HarmfulPainObserved);
		state = ObserveVerticalPainColumnOutcome(state, {
			.HarmfulFootZoneKnown = true,
			.InHarmfulFootZone = true,
			.UnmodeledCallbackObserved = true,
			.WindowComplete = true
		});
		Check(CorrelateVerticalPainColumnOutcome(state) ==
			VerticalPainColumnCorrelation::Unknown,
			"unmodeled actual callbacks make the correlation unknown");

		state = BeginVerticalPainColumnOutcome(
			VerticalPainColumnClassification::HarmfulPainObserved);
		state = ObserveVerticalPainColumnOutcome(state, {
			.SamePawnLife = false,
			.HarmfulFootZoneKnown = true,
			.InHarmfulFootZone = true,
			.WindowComplete = true
		});
		Check(CorrelateVerticalPainColumnOutcome(state) ==
			VerticalPainColumnCorrelation::Unknown,
			"pawn-life changes cannot be correlated across respawns");

		state = BeginVerticalPainColumnOutcome(
			VerticalPainColumnClassification::HarmfulPainObserved);
		state = ObserveVerticalPainColumnOutcome(state, {
			.HarmfulFootZoneKnown = true,
			.TargetProgressKnown = true,
			.TargetProgress = std::numeric_limits<float>::quiet_NaN(),
			.WindowComplete = true
		});
		Check(CorrelateVerticalPainColumnOutcome(state) ==
			VerticalPainColumnCorrelation::Unknown,
			"non-finite progress invalidates the outcome episode");

		state = BeginVerticalPainColumnOutcome(
			VerticalPainColumnClassification::Unknown);
		state = ObserveVerticalPainColumnOutcome(state, {
			.HarmfulFootZoneKnown = true,
			.WindowComplete = true
		});
		Check(CorrelateVerticalPainColumnOutcome(state) ==
			VerticalPainColumnCorrelation::Unknown,
			"an unknown forecast never produces a quality correlation");

		state = BeginVerticalPainColumnOutcome(
			VerticalPainColumnClassification::HarmfulPainObserved);
		state = ObserveVerticalPainColumnOutcome(state, {
			.HarmfulFootZoneKnown = false,
			.WindowComplete = true
		});
		Check(CorrelateVerticalPainColumnOutcome(state) ==
			VerticalPainColumnCorrelation::Unknown,
			"unknown actual foot-zone evidence cannot confirm or reject a forecast");

		state = BeginVerticalPainColumnOutcome(
			VerticalPainColumnClassification::HarmfulPainObserved);
		state = ObserveVerticalPainColumnOutcome(state, {
			.HarmfulFootZoneKnown = true,
			.InHarmfulFootZone = true
		});
		Check(CorrelateVerticalPainColumnOutcome(state) ==
			VerticalPainColumnCorrelation::Unknown,
			"an active episode is not correlated before its bounded window completes");
	}
}

int main()
{
	TestStaticColumnFindsFootPainWithRetailDepth();
	TestRetailZoneHazardRequiresPositivePainDamage();
	TestFootGatesPainDamage();
	TestUnknownAndIncompleteSamplesFailClosed();
	TestOnlyWalkableStaticWorldSupportIsClassified();
	TestDefaultHorizonHasCompleteBoundedCapacity();
	TestWaterPhysicsFailsClosedBeforeLaterPain();
	TestOutcomeCorrelationRetainsLandingProgressAndDeath();
	TestCorrelationReportsForecastOnlyActualOnlyAndNoHazard();
	TestUnstableActualTrajectoriesRemainUnknown();
	if (Failures == 0)
		std::cout << "Pawn vertical pain-column shadow tests passed\n";
	return Failures == 0 ? 0 : 1;
}
