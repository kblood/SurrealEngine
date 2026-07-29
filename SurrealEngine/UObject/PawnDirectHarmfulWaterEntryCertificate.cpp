#include "PawnDirectHarmfulWaterEntryCertificate.h"

#include <cmath>
#include <limits>

namespace PawnMovement
{
	namespace
	{
		bool IsKnownDrySafe(const FallingHazardForecastZoneObservation& zone)
		{
			return zone.Identity.Known && zone.Identity.ZoneActorId != 0
				&& !zone.PainZone && !zone.WaterZone && zone.DamagePerSecond <= 0;
		}

		bool IsFiniteVector(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z);
		}
	}

	DirectHarmfulWaterEntryCertificate
		CertifyDirectHarmfulWaterEntry(const FallingHazardForecastUpdate& forecast)
	{
		DirectHarmfulWaterEntryCertificate certificate;
		const FallingHazardForecastState& state = forecast.State;
		const FallingHazardForecastResult& result = forecast.Result;
		if (!forecast.Complete || state.Active || state.PendingProbe.Valid
			|| state.Result.Classification != result.Classification
			|| state.Result.Reason != result.Reason
			|| state.ExpectedSegmentCount != result.SegmentCount
			|| !std::isfinite(state.Input.MaximumElapsed)
			|| !std::isfinite(state.Input.MaximumPathDistance)
			|| state.Input.MaximumElapsed <= 0.0f
			|| state.Input.MaximumPathDistance <= 0.0f)
		{
			return certificate;
		}
		if (state.Input.Phase != FallingHazardForecastPhase::FullStep
			|| state.Input.Continuation.PrechargedElapsed != 0.0f)
		{
			certificate.Result = DirectHarmfulWaterEntryCertificateResult::NotFullStep;
			return certificate;
		}
		if (result.Classification != FallingHazardForecast::HarmfulPainObserved
			|| result.Reason != FallingHazardForecastReason::HarmfulFootPainAtEndpoint
			|| !result.ExpectedHarmfulWaterEntry)
		{
			certificate.Result = DirectHarmfulWaterEntryCertificateResult::NotHarmfulWaterEndpoint;
			return certificate;
		}
		if (result.ExpectedHarmfulDamageTypeMatchesReduced
			|| !result.ExpectedBotAvoidanceRelevant)
		{
			certificate.Result =
				DirectHarmfulWaterEntryCertificateResult::DamageNotAvoidanceRelevant;
			return certificate;
		}
		if (!result.ExpectedHarmfulFootZone.Known
			|| !result.ExpectedHarmfulPhysicsZone.Known
			|| result.ExpectedHarmfulFootZone.ZoneActorId == 0
			|| result.ExpectedHarmfulPhysicsZone.ZoneActorId == 0)
		{
			certificate.Result =
				DirectHarmfulWaterEntryCertificateResult::InvalidExpectedHarmfulZones;
			return certificate;
		}
		const FallingHazardForecastPointObservation& zones = state.Input.StartingZones;
		if (!IsKnownDrySafe(zones.Center) || !IsKnownDrySafe(zones.Foot)
			|| !IsKnownDrySafe(zones.Head) || !IsKnownDrySafe(zones.Physics))
		{
			certificate.Result = DirectHarmfulWaterEntryCertificateResult::UnsafeOrUnknownStart;
			return certificate;
		}
		if (result.TransientHarmfulPainObserved)
		{
			certificate.Result =
				DirectHarmfulWaterEntryCertificateResult::IntermediateHazardObserved;
			return certificate;
		}
		if (!std::isfinite(result.Elapsed) || result.Elapsed <= 0.0f
			|| !std::isfinite(result.PathDistance) || result.PathDistance <= 0.0f
			|| result.Elapsed > state.Input.MaximumElapsed
			|| result.PathDistance > state.Input.MaximumPathDistance
			|| state.ExpectedSegmentCount == 0)
		{
			certificate.Result =
				DirectHarmfulWaterEntryCertificateResult::InvalidPredictionAccounting;
			return certificate;
		}
		float elapsed = 0.0f;
		float pathDistance = 0.0f;
		size_t samples = 0;
		for (size_t index = 0; index < state.ExpectedSegmentCount; index++)
		{
			const FallingHazardForecastExpectedSegment& segment = state.ExpectedSegments[index];
			if (segment.Leg != FallingHazardSweepLeg::Direct
				|| segment.Collision != FallingHazardCollisionKind::Clear
				|| !std::isfinite(segment.ElapsedContribution)
				|| segment.ElapsedContribution <= 0.0f
				|| segment.Ordinal != index || segment.HitFraction != 1.0f
				|| !IsFiniteVector(segment.Origin) || !IsFiniteVector(segment.RequestedDelta)
				|| !IsFiniteVector(segment.Endpoint)
				|| segment.FirstSample != samples
				|| segment.SampleCount > state.Input.MaximumSamples - samples)
			{
				certificate.Result = DirectHarmfulWaterEntryCertificateResult::NoDirectClearPath;
				return certificate;
			}
			elapsed += segment.ElapsedContribution;
			pathDistance += length(segment.RequestedDelta);
			samples += segment.SampleCount;
		}
		if (!std::isfinite(elapsed) || !std::isfinite(pathDistance)
			|| std::abs(elapsed - result.Elapsed) > FallingHazardForecastElapsedTolerance
			|| std::abs(pathDistance - result.PathDistance)
				> FallingHazardForecastVectorTolerance
			|| samples != result.SampleCount)
		{
			certificate.Result =
				DirectHarmfulWaterEntryCertificateResult::InvalidPredictionAccounting;
			return certificate;
		}
		certificate.Result = DirectHarmfulWaterEntryCertificateResult::Certified;
		certificate.PredictedDirectSegmentCount = state.ExpectedSegmentCount;
		certificate.PredictedElapsed = result.Elapsed;
		certificate.PredictedPathDistance = result.PathDistance;
		certificate.ExpectedHarmfulFootZone = result.ExpectedHarmfulFootZone;
		certificate.ExpectedHarmfulPhysicsZone = result.ExpectedHarmfulPhysicsZone;
		certificate.FirstHarmfulPainDepth = result.FirstHarmfulPainDepth;
		return certificate;
	}

	const char* DirectHarmfulWaterEntryCertificateResultName(
		DirectHarmfulWaterEntryCertificateResult result)
	{
		switch (result)
		{
		case DirectHarmfulWaterEntryCertificateResult::SourceNotEligible:
			return "source_not_eligible";
		case DirectHarmfulWaterEntryCertificateResult::Certified:
			return "certified";
		case DirectHarmfulWaterEntryCertificateResult::ForecastIncompleteOrInconsistent:
			return "forecast_incomplete_or_inconsistent";
		case DirectHarmfulWaterEntryCertificateResult::NotFullStep:
			return "not_full_step";
		case DirectHarmfulWaterEntryCertificateResult::NotHarmfulWaterEndpoint:
			return "not_harmful_water_endpoint";
		case DirectHarmfulWaterEntryCertificateResult::DamageNotAvoidanceRelevant:
			return "damage_not_avoidance_relevant";
		case DirectHarmfulWaterEntryCertificateResult::InvalidExpectedHarmfulZones:
			return "invalid_expected_harmful_zones";
		case DirectHarmfulWaterEntryCertificateResult::UnsafeOrUnknownStart:
			return "unsafe_or_unknown_start";
		case DirectHarmfulWaterEntryCertificateResult::NoDirectClearPath:
			return "no_direct_clear_path";
		case DirectHarmfulWaterEntryCertificateResult::InvalidPredictionAccounting:
			return "invalid_prediction_accounting";
		default:
			return "invalid_certificate_result";
		}
	}
}
