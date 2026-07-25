#pragma once

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

	// Observer-only disposition of a one-shot handoff to the stock planner.
	// It never authorizes or changes movement.
	enum class HazardSwimEgressPlannerHandoffOutcome
	{
		None,
		SameCommandReissued,
		DifferentCommandIssued,
		HazardClearedBeforeCommand,
		FellBeforeCommand,
		DiedBeforeCommand,
		LifeBoundaryCensored,
		RunEndCensored,
		EpisodeAbandoned,
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

	struct HazardSwimEgressPlannerHandoffInput
	{
		bool Pending = false;
		bool NextMovementCommandIssued = false;
		bool SameMoveTarget = false;
		bool SameDestination = false;
		bool HazardCleared = false;
		bool Fell = false;
		bool Died = false;
		bool LifeBoundary = false;
		bool RunEnd = false;
		bool EpisodeAbandoned = false;
	};

	HazardSwimEgressLiveSteerDecision EvaluateHazardSwimEgressLiveSteer(
		const HazardSwimEgressLiveSteerInput& input);
	HazardSwimEgressLiveReplanDecision EvaluateHazardSwimEgressLiveReplan(
		const HazardSwimEgressLiveReplanInput& input);
	HazardSwimEgressPlannerHandoffOutcome ClassifyHazardSwimEgressPlannerHandoff(
		const HazardSwimEgressPlannerHandoffInput& input);
}
