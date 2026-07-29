#include "UObject/PawnDirectHarmfulWaterEntryCertificate.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}

	PawnMovement::FallingHazardForecastUpdate ValidForecast()
	{
		using namespace PawnMovement;
		FallingHazardForecastUpdate forecast;
		forecast.Complete = true;
		forecast.Result.Classification = FallingHazardForecast::HarmfulPainObserved;
		forecast.Result.Reason = FallingHazardForecastReason::HarmfulFootPainAtEndpoint;
		forecast.Result.ExpectedHarmfulWaterEntry = true;
		forecast.Result.ExpectedBotAvoidanceRelevant = true;
		forecast.Result.ExpectedHarmfulFootZone = { true, 2, 3 };
		forecast.Result.ExpectedHarmfulPhysicsZone = { true, 2, 3 };
		forecast.Result.Elapsed = 0.02f;
		forecast.Result.PathDistance = 4.1231055f;
		forecast.Result.SegmentCount = 1;
		forecast.State.Result = forecast.Result;
		forecast.State.Input.Phase = FallingHazardForecastPhase::FullStep;
		for (auto* zone : { &forecast.State.Input.StartingZones.Center,
			&forecast.State.Input.StartingZones.Foot, &forecast.State.Input.StartingZones.Head,
			&forecast.State.Input.StartingZones.Physics })
		{
			zone->Identity.Known = true;
			zone->Identity.ZoneActorId = 1;
		}
		forecast.State.ExpectedSegmentCount = 1;
		auto& segment = forecast.State.ExpectedSegments[0];
		segment.Leg = FallingHazardSweepLeg::Direct;
		segment.Collision = FallingHazardCollisionKind::Clear;
		segment.ElapsedContribution = 0.02f;
		segment.Origin = vec3(1.0f, 2.0f, 3.0f);
		segment.RequestedDelta = vec3(4.0f, 0.0f, -1.0f);
		segment.Endpoint = vec3(5.0f, 2.0f, 2.0f);
		return forecast;
	}
}

int main()
{
	using namespace PawnMovement;
	const auto valid = ValidForecast();
	Check(CertifyDirectHarmfulWaterEntry(valid).IsCertified(),
		"complete direct dry-to-harmful water forecast must certify");

	auto incomplete = valid;
	incomplete.Complete = false;
	Check(CertifyDirectHarmfulWaterEntry(incomplete).Result
		== DirectHarmfulWaterEntryCertificateResult::ForecastIncompleteOrInconsistent,
		"incomplete forecasts must fail closed");

	auto continuation = valid;
	continuation.State.Input.Phase = FallingHazardForecastPhase::AlignedContinuation;
	Check(CertifyDirectHarmfulWaterEntry(continuation).Result
		== DirectHarmfulWaterEntryCertificateResult::NotFullStep,
		"continuation forecasts must not be treated as direct pre-entry evidence");

	auto unsafe = valid;
	unsafe.State.Input.StartingZones.Foot.WaterZone = true;
	Check(CertifyDirectHarmfulWaterEntry(unsafe).Result
		== DirectHarmfulWaterEntryCertificateResult::UnsafeOrUnknownStart,
		"water or harmful start zones must fail closed");

	auto indirect = valid;
	indirect.State.ExpectedSegments[0].Leg = FallingHazardSweepLeg::Aligned;
	Check(CertifyDirectHarmfulWaterEntry(indirect).Result
		== DirectHarmfulWaterEntryCertificateResult::NoDirectClearPath,
		"non-direct or callback-sensitive path predictions must fail closed");

	auto immune = valid;
	immune.Result.ExpectedHarmfulDamageTypeMatchesReduced = true;
	immune.State.Result = immune.Result;
	Check(CertifyDirectHarmfulWaterEntry(immune).Result
		== DirectHarmfulWaterEntryCertificateResult::DamageNotAvoidanceRelevant,
		"damage-reduced forecasts must not be classified as avoidance candidates");
	return 0;
}
