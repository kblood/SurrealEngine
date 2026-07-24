#include "PawnVerticalPainColumnShadow.h"

#include <algorithm>
#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(float value)
		{
			return std::isfinite(value);
		}

		bool ZoneObservationValid(const VerticalPainZoneObservation& observation)
		{
			return observation.Known
				&& (!observation.HarmfulPain || observation.PainZone);
		}

		VerticalPainColumnResult Unknown(VerticalPainColumnReason reason)
		{
			VerticalPainColumnResult result;
			result.Reason = reason;
			return result;
		}

		VerticalPainColumnReason CollisionUnknownReason(
			VerticalPainColumnCollisionKind collision,
			VerticalPainColumnReason unknownReason,
			VerticalPainColumnReason clearReason)
		{
			switch (collision)
			{
			case VerticalPainColumnCollisionKind::Unknown: return unknownReason;
			case VerticalPainColumnCollisionKind::Clear: return clearReason;
			case VerticalPainColumnCollisionKind::Mover:
				return VerticalPainColumnReason::MoverCollisionUnknown;
			case VerticalPainColumnCollisionKind::DynamicActor:
				return VerticalPainColumnReason::DynamicCollisionUnknown;
			case VerticalPainColumnCollisionKind::StaticWorld: break;
			}
			return VerticalPainColumnReason::InvalidInput;
		}
	}

	VerticalPainZoneObservation ObserveVerticalPainZone(
		bool zoneKnown, bool painZone, int damagePerSecond, bool waterZone)
	{
		if (!zoneKnown)
			return {};
		return {
			.Known = true,
			.PainZone = painZone,
			.HarmfulPain = painZone && damagePerSecond > 0,
			.WaterZone = waterZone
		};
	}

	VerticalPainColumnResult ClassifyVerticalPainColumn(
		const VerticalPainColumnInput& input)
	{
		if (!IsFinite(input.DropDistance) || !IsFinite(input.MaximumDropDistance)
			|| !IsFinite(input.MaximumSampleSpacing) || !IsFinite(input.SupportNormalZ)
			|| input.DropDistance <= 0.0f
			|| input.MaximumDropDistance <= 0.0f
			|| input.DropDistance > input.MaximumDropDistance
			|| input.MaximumSampleSpacing <= 0.0f
			|| input.MaximumSamples == 0
			|| input.MaximumSamples > VerticalPainColumnMaximumSamples
			|| input.SampleCount > VerticalPainColumnMaximumSamples)
			return Unknown(VerticalPainColumnReason::InvalidInput);
		if (!input.UnsupportedEndpointKnown)
			return Unknown(VerticalPainColumnReason::EndpointSupportUnknown);
		if (!input.UnsupportedEndpoint)
			return Unknown(VerticalPainColumnReason::EndpointStillSupported);
		if (input.VerticalCollision != VerticalPainColumnCollisionKind::StaticWorld)
		{
			return Unknown(CollisionUnknownReason(input.VerticalCollision,
				VerticalPainColumnReason::VerticalCollisionUnknown,
				VerticalPainColumnReason::VerticalHorizonExhausted));
		}
		if (input.SupportCollision != VerticalPainColumnCollisionKind::StaticWorld)
		{
			return Unknown(CollisionUnknownReason(input.SupportCollision,
				VerticalPainColumnReason::SupportCollisionUnknown,
				VerticalPainColumnReason::NonStaticSupport));
		}
		if (input.SupportNormalZ <= FallingParityWalkableNormalZ)
			return Unknown(VerticalPainColumnReason::NonWalkableSupport);
		if (input.UnmodeledCallbackRequired)
			return Unknown(VerticalPainColumnReason::UnmodeledCallbackRequired);

		const size_t requiredSamples = static_cast<size_t>(
			std::ceil(input.DropDistance / input.MaximumSampleSpacing));
		if (input.SampleCapExhausted || requiredSamples > input.MaximumSamples
			|| input.SampleCount > input.MaximumSamples)
			return Unknown(VerticalPainColumnReason::SampleCapExceeded);
		if (input.SampleCount < requiredSamples || input.SampleCount == 0)
			return Unknown(VerticalPainColumnReason::IncompleteSampleCoverage);

		float previousDrop = 0.0f;
		for (size_t index = 0; index < input.SampleCount; index++)
		{
			const VerticalPainColumnSample& sample = input.Samples[index];
			if (!IsFinite(sample.DropDistance) || sample.DropDistance <= previousDrop
				|| sample.DropDistance - previousDrop > input.MaximumSampleSpacing + 0.001f
				|| sample.DropDistance > input.DropDistance + 0.001f)
				return Unknown(VerticalPainColumnReason::IncompleteSampleCoverage);
			if (!ZoneObservationValid(sample.Center)
				|| !ZoneObservationValid(sample.Foot)
				|| !ZoneObservationValid(sample.Head))
				return Unknown(VerticalPainColumnReason::UnknownZoneSample);
			previousDrop = sample.DropDistance;
		}

		if (input.DropDistance - previousDrop > 0.001f)
			return Unknown(VerticalPainColumnReason::IncompleteSampleCoverage);

		for (size_t index = 0; index < input.SampleCount; index++)
		{
			const VerticalPainColumnSample& sample = input.Samples[index];
			if (sample.Foot.HarmfulPain)
			{
				VerticalPainColumnResult result;
				result.Classification =
					VerticalPainColumnClassification::HarmfulPainObserved;
				result.Reason = VerticalPainColumnReason::HarmfulFootPainObserved;
				result.FirstHarmfulSample = index;
				result.FirstHarmfulDropDistance = sample.DropDistance;
				result.FirstHarmfulPainDepth = 0.4f
					+ (sample.Center.PainZone ? 0.4f : 0.0f)
					+ (sample.Head.PainZone ? 0.2f : 0.0f);
				return result;
			}
			if (sample.Center.WaterZone || sample.Foot.WaterZone
				|| sample.Head.WaterZone)
				return Unknown(VerticalPainColumnReason::WaterPhysicsUnknown);
		}

		VerticalPainColumnResult result;
		result.Classification =
			VerticalPainColumnClassification::NoHarmfulPainObserved;
		result.Reason = VerticalPainColumnReason::NoHarmfulPainObserved;
		return result;
	}

	VerticalPainColumnOutcomeState BeginVerticalPainColumnOutcome(
		VerticalPainColumnClassification forecast)
	{
		VerticalPainColumnOutcomeState state;
		state.Active = true;
		state.Forecast = forecast;
		return state;
	}

	VerticalPainColumnOutcomeState ObserveVerticalPainColumnOutcome(
		const VerticalPainColumnOutcomeState& state,
		const VerticalPainColumnOutcomeObservation& observation)
	{
		VerticalPainColumnOutcomeState result = state;
		if (!state.Active || state.Complete || state.Invalidated)
			return result;
		if (!observation.SamePawnLife
			|| (observation.TargetProgressKnown
				&& !IsFinite(observation.TargetProgress)))
		{
			result.Invalidated = true;
			result.Complete = true;
			return result;
		}

		result.HazardObservationCount++;
		result.HazardObservationsKnown = result.HazardObservationsKnown
			&& observation.HarmfulFootZoneKnown;
		result.EnteredHarmfulFootZone = result.EnteredHarmfulFootZone
			|| (observation.HarmfulFootZoneKnown && observation.InHarmfulFootZone);
		if (observation.Collision == VerticalPainColumnCollisionKind::Mover
			|| observation.Collision == VerticalPainColumnCollisionKind::DynamicActor
			|| observation.Collision == VerticalPainColumnCollisionKind::Unknown
			|| observation.UnmodeledCallbackObserved)
			result.ActualTrajectoryUnknown = true;
		if (observation.Landed)
		{
			result.Landed = true;
			result.LandingCollision = observation.LandingCollision;
			if (observation.LandingCollision != VerticalPainColumnCollisionKind::StaticWorld)
				result.ActualTrajectoryUnknown = true;
		}
		if (observation.TargetProgressKnown)
		{
			if (!result.TargetProgressKnown)
				result.MaximumTargetProgress = observation.TargetProgress;
			else
				result.MaximumTargetProgress = std::max(
					result.MaximumTargetProgress, observation.TargetProgress);
			result.TargetProgressKnown = true;
			result.FinalTargetProgress = observation.TargetProgress;
		}
		result.Died = result.Died || observation.Died;
		result.Suicide = result.Suicide || observation.Suicide;
		result.Complete = observation.WindowComplete || observation.Died;
		return result;
	}

	VerticalPainColumnCorrelation CorrelateVerticalPainColumnOutcome(
		const VerticalPainColumnOutcomeState& state)
	{
		if (!state.Active || !state.Complete || state.Invalidated
			|| state.ActualTrajectoryUnknown || !state.HazardObservationsKnown
			|| state.HazardObservationCount == 0
			|| state.Forecast == VerticalPainColumnClassification::Unknown)
			return VerticalPainColumnCorrelation::Unknown;

		const bool forecastHarmful = state.Forecast
			== VerticalPainColumnClassification::HarmfulPainObserved;
		if (forecastHarmful && state.EnteredHarmfulFootZone)
			return VerticalPainColumnCorrelation::ConfirmedHarmfulForecast;
		if (forecastHarmful)
			return VerticalPainColumnCorrelation::ForecastOnly;
		if (state.EnteredHarmfulFootZone)
			return VerticalPainColumnCorrelation::ActualOnly;
		return VerticalPainColumnCorrelation::ConfirmedNoHarmfulObservation;
	}
}
