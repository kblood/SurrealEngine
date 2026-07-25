#pragma once

#include "Math/vec.h"

namespace BotAI
{
	enum class HazardSwimEgressPhysics
	{
		Unknown,
		Swimming,
		Falling,
		Other,
	};

	enum class HazardSwimEgressProbe
	{
		NotRun,
		Clear,
		Blocked,
	};

	enum class HazardSwimEgressLiveSteerTransition
	{
		NoAction,
		SteerCandidate,
		Terminal,
	};

	enum class HazardSwimEgressLiveSteerTerminal
	{
		None,
		Falling,
		HazardCleared,
		ProbeBlocked,
	};

	enum class HazardSwimEgressLiveSteerReason
	{
		PolicyDisabled,
		IneligibleActor,
		NotAlive,
		NoActiveEpisode,
		Falling,
		NotSwimming,
		HazardCleared,
		NotAuthorized,
		MovementCommandActive,
		ProbePreviouslyRejected,
		MissingAnchor,
		AnchorNotFromFallingPreMove,
		AnchorDistanceUnknown,
		AnchorTooNear,
		AnchorTooFar,
		ProbeNotRun,
		ProbeBlocked,
		Ready,
	};

	enum class HazardSwimEgressLiveReplanDecision
	{
		NoAction,
		Replan,
	};

	struct HazardSwimEgressLiveSteerInput
	{
		bool PolicyEnabled = false;
		bool EligibleActor = false;
		bool Alive = false;
		HazardSwimEgressPhysics Physics = HazardSwimEgressPhysics::Unknown;
		bool HarmfulWaterEpisodeActive = false;
		bool LiveActionAuthorized = false;
		bool MovementCommandActive = false;
		bool ProbePreviouslyRejected = false;
		bool FiniteAnchorKnown = false;
		bool AnchorFromFallingPreMove = false;
		bool ExactHarmfulWater = false;
		bool AnchorDistanceKnown = false;
		double AnchorDistance = 0.0;
		HazardSwimEgressProbe Probe = HazardSwimEgressProbe::NotRun;
	};

	struct HazardSwimEgressLiveSteerDecision
	{
		HazardSwimEgressLiveSteerTransition Transition =
			HazardSwimEgressLiveSteerTransition::NoAction;
		HazardSwimEgressLiveSteerTerminal Terminal =
			HazardSwimEgressLiveSteerTerminal::None;
		HazardSwimEgressLiveSteerReason Reason =
			HazardSwimEgressLiveSteerReason::PolicyDisabled;
	};

	struct HazardSwimEgressAccelerationOverlayRestoreInput
	{
		bool OverlayActive = false;
		vec3 CurrentAcceleration = vec3(0.0f);
		vec3 OverlayDirection = vec3(0.0f);
		float ExpectedMaximumAcceleration = 0.0f;
	};

	struct HazardSwimEgressLiveReplanInput
	{
		bool PolicyEnabled = false;
		bool EligibleActor = false;
		bool Alive = false;
		bool Swimming = false;
		bool HarmfulWaterEpisodeActive = false;
		bool LiveActionAuthorized = false;
		bool MovementCommandActive = false;
		bool SafeNavigationCandidateKnown = false;
		bool ReplanAlreadyIssued = false;
	};

	HazardSwimEgressLiveSteerDecision EvaluateHazardSwimEgressLiveSteer(
		const HazardSwimEgressLiveSteerInput& input);
	bool ShouldRestoreHazardSwimEgressAccelerationOverlay(
		const HazardSwimEgressAccelerationOverlayRestoreInput& input);
	HazardSwimEgressLiveReplanDecision EvaluateHazardSwimEgressLiveReplan(
		const HazardSwimEgressLiveReplanInput& input);
}
