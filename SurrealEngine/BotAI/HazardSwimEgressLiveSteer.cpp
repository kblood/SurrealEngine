#include "HazardSwimEgressLiveSteer.h"

#include <cmath>

namespace
{
	constexpr double MinimumAnchorDistance = 8.0;
	constexpr double MaximumAnchorDistance = 512.0;
}

namespace BotAI
{
	HazardSwimEgressLiveSteerDecision EvaluateHazardSwimEgressLiveSteer(
		const HazardSwimEgressLiveSteerInput& input)
	{
		using Decision = HazardSwimEgressLiveSteerDecision;
		using Reason = HazardSwimEgressLiveSteerReason;
		using Terminal = HazardSwimEgressLiveSteerTerminal;
		using Transition = HazardSwimEgressLiveSteerTransition;
		auto noAction = [](Reason reason)
		{
			return Decision{ Transition::NoAction, Terminal::None, reason };
		};
		auto terminal = [](Terminal value, Reason reason)
		{
			return Decision{ Transition::Terminal, value, reason };
		};

		if (!input.PolicyEnabled)
			return noAction(Reason::PolicyDisabled);
		if (!input.EligibleActor)
			return noAction(Reason::IneligibleActor);
		if (!input.Alive)
			return noAction(Reason::NotAlive);
		if (!input.HarmfulWaterEpisodeActive)
			return noAction(Reason::NoActiveEpisode);
		if (input.Physics == HazardSwimEgressPhysics::Falling)
			return terminal(Terminal::Falling, Reason::Falling);
		if (input.Physics != HazardSwimEgressPhysics::Swimming)
			return noAction(Reason::NotSwimming);
		if (!input.ExactHarmfulWater)
			return terminal(Terminal::HazardCleared, Reason::HazardCleared);
		if (!input.LiveActionAuthorized)
			return noAction(Reason::NotAuthorized);
		if (input.MovementCommandActive)
			return noAction(Reason::MovementCommandActive);
		if (input.ProbePreviouslyRejected)
			return terminal(Terminal::ProbeBlocked, Reason::ProbePreviouslyRejected);
		if (!input.FiniteAnchorKnown)
			return noAction(Reason::MissingAnchor);
		if (!input.AnchorFromFallingPreMove)
			return noAction(Reason::AnchorNotFromFallingPreMove);
		if (!input.AnchorDistanceKnown || !std::isfinite(input.AnchorDistance))
			return noAction(Reason::AnchorDistanceUnknown);
		if (input.AnchorDistance < MinimumAnchorDistance)
			return noAction(Reason::AnchorTooNear);
		if (input.AnchorDistance > MaximumAnchorDistance)
			return noAction(Reason::AnchorTooFar);
		if (input.Probe == HazardSwimEgressProbe::Blocked)
			return terminal(Terminal::ProbeBlocked, Reason::ProbeBlocked);
		if (input.Probe != HazardSwimEgressProbe::Clear)
			return noAction(Reason::ProbeNotRun);
		return { Transition::SteerCandidate, Terminal::None, Reason::Ready };
	}

	bool ShouldRestoreHazardSwimEgressAccelerationOverlay(
		const HazardSwimEgressAccelerationOverlayRestoreInput& input)
	{
		const vec3 acceleration = input.CurrentAcceleration;
		const float accelerationLength = length(acceleration);
		return input.OverlayActive
			&& std::isfinite(acceleration.x) && std::isfinite(acceleration.y)
			&& std::isfinite(acceleration.z)
			&& std::isfinite(input.OverlayDirection.x)
			&& std::isfinite(input.OverlayDirection.y)
			&& std::isfinite(input.OverlayDirection.z)
			&& std::isfinite(accelerationLength)
			&& std::isfinite(input.ExpectedMaximumAcceleration)
			&& input.ExpectedMaximumAcceleration > 0.0f
			&& accelerationLength > 0.0001f
			&& accelerationLength <= input.ExpectedMaximumAcceleration * 1.001f
			&& dot(normalize(acceleration), input.OverlayDirection) > 0.999f;
	}

	HazardSwimEgressLiveReplanDecision EvaluateHazardSwimEgressLiveReplan(
		const HazardSwimEgressLiveReplanInput& input)
	{
		return input.PolicyEnabled && input.EligibleActor && input.Alive
			&& input.Swimming && input.HarmfulWaterEpisodeActive
			&& input.LiveActionAuthorized && input.MovementCommandActive
			&& input.SafeNavigationCandidateKnown && !input.ReplanAlreadyIssued
			? HazardSwimEgressLiveReplanDecision::Replan
			: HazardSwimEgressLiveReplanDecision::NoAction;
	}
}
