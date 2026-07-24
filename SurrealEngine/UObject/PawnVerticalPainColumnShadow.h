#pragma once

#include <array>
#include <cstddef>

namespace PawnMovement
{
	constexpr size_t VerticalPainColumnMaximumSamples = 64;

	enum class VerticalPainColumnCollisionKind
	{
		Unknown,
		Clear,
		StaticWorld,
		Mover,
		DynamicActor
	};

	struct VerticalPainZoneObservation
	{
		bool Known = false;
		bool PainZone = false;
		bool HarmfulPain = false;
		bool WaterZone = false;
	};

	VerticalPainZoneObservation ObserveVerticalPainZone(
		bool zoneKnown, bool painZone, int damagePerSecond, bool waterZone);

	struct VerticalPainColumnSample
	{
		float DropDistance = 0.0f;
		VerticalPainZoneObservation Center;
		VerticalPainZoneObservation Foot;
		VerticalPainZoneObservation Head;
	};

	struct VerticalPainColumnInput
	{
		bool UnsupportedEndpointKnown = false;
		bool UnsupportedEndpoint = false;
		VerticalPainColumnCollisionKind VerticalCollision =
			VerticalPainColumnCollisionKind::Unknown;
		VerticalPainColumnCollisionKind SupportCollision =
			VerticalPainColumnCollisionKind::Unknown;
		float SupportNormalZ = 0.0f;
		bool UnmodeledCallbackRequired = false;
		float DropDistance = 0.0f;
		float MaximumDropDistance = 4096.0f;
		float MaximumSampleSpacing = 25.0f;
		size_t MaximumSamples = VerticalPainColumnMaximumSamples;
		bool SampleCapExhausted = false;
		std::array<VerticalPainColumnSample, VerticalPainColumnMaximumSamples> Samples;
		size_t SampleCount = 0;
		float WalkableNormalZ = 0.7071f;
	};

	enum class VerticalPainColumnClassification
	{
		Unknown,
		NoHarmfulPainObserved,
		HarmfulPainObserved
	};

	enum class VerticalPainColumnReason
	{
		InvalidInput,
		EndpointSupportUnknown,
		EndpointStillSupported,
		VerticalCollisionUnknown,
		VerticalHorizonExhausted,
		MoverCollisionUnknown,
		DynamicCollisionUnknown,
		SupportCollisionUnknown,
		NonStaticSupport,
		NonWalkableSupport,
		UnmodeledCallbackRequired,
		SampleCapExceeded,
		IncompleteSampleCoverage,
		UnknownZoneSample,
		NoHarmfulPainObserved,
		HarmfulFootPainObserved
	};

	struct VerticalPainColumnResult
	{
		VerticalPainColumnClassification Classification =
			VerticalPainColumnClassification::Unknown;
		VerticalPainColumnReason Reason = VerticalPainColumnReason::InvalidInput;
		size_t FirstHarmfulSample = VerticalPainColumnMaximumSamples;
		float FirstHarmfulDropDistance = 0.0f;
		float FirstHarmfulPainDepth = 0.0f;
	};

	VerticalPainColumnResult ClassifyVerticalPainColumn(
		const VerticalPainColumnInput& input);

	struct VerticalPainColumnOutcomeObservation
	{
		bool SamePawnLife = true;
		bool HarmfulFootZoneKnown = false;
		bool InHarmfulFootZone = false;
		VerticalPainColumnCollisionKind Collision =
			VerticalPainColumnCollisionKind::Clear;
		bool UnmodeledCallbackObserved = false;
		bool Landed = false;
		VerticalPainColumnCollisionKind LandingCollision =
			VerticalPainColumnCollisionKind::Unknown;
		bool TargetProgressKnown = false;
		float TargetProgress = 0.0f;
		bool Died = false;
		bool Suicide = false;
		bool WindowComplete = false;
	};

	struct VerticalPainColumnOutcomeState
	{
		bool Active = false;
		VerticalPainColumnClassification Forecast =
			VerticalPainColumnClassification::Unknown;
		bool Invalidated = false;
		bool ActualTrajectoryUnknown = false;
		bool HazardObservationsKnown = true;
		size_t HazardObservationCount = 0;
		bool EnteredHarmfulFootZone = false;
		bool Landed = false;
		VerticalPainColumnCollisionKind LandingCollision =
			VerticalPainColumnCollisionKind::Unknown;
		bool TargetProgressKnown = false;
		float MaximumTargetProgress = 0.0f;
		float FinalTargetProgress = 0.0f;
		bool Died = false;
		bool Suicide = false;
		bool Complete = false;
	};

	VerticalPainColumnOutcomeState BeginVerticalPainColumnOutcome(
		VerticalPainColumnClassification forecast);
	VerticalPainColumnOutcomeState ObserveVerticalPainColumnOutcome(
		const VerticalPainColumnOutcomeState& state,
		const VerticalPainColumnOutcomeObservation& observation);

	enum class VerticalPainColumnCorrelation
	{
		Unknown,
		ConfirmedHarmfulForecast,
		ForecastOnly,
		ActualOnly,
		ConfirmedNoHarmfulObservation
	};

	VerticalPainColumnCorrelation CorrelateVerticalPainColumnOutcome(
		const VerticalPainColumnOutcomeState& state);
}
