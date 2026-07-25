#include "BotAI/HazardSwimEgressLiveSteer.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace BotAI;

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

	HazardSwimEgressLiveSteerInput ReadyInput()
	{
		HazardSwimEgressLiveSteerInput input;
		input.PolicyEnabled = true;
		input.EligibleActor = true;
		input.Alive = true;
		input.Physics = HazardSwimEgressPhysics::Swimming;
		input.HarmfulWaterEpisodeActive = true;
		input.LiveActionAuthorized = true;
		input.FiniteAnchorKnown = true;
		input.AnchorFromFallingPreMove = true;
		input.ExactHarmfulWater = true;
		input.AnchorDistanceKnown = true;
		input.AnchorDistance = 64.0;
		input.Probe = HazardSwimEgressProbe::Clear;
		return input;
	}

	void CheckDecision(const HazardSwimEgressLiveSteerDecision& decision,
		HazardSwimEgressLiveSteerTransition transition,
		HazardSwimEgressLiveSteerTerminal terminal,
		HazardSwimEgressLiveSteerReason reason, const std::string& message)
	{
		Check(decision.Transition == transition && decision.Terminal == terminal
			&& decision.Reason == reason, message);
	}

	void TestReadyCandidate()
	{
		CheckDecision(EvaluateHazardSwimEgressLiveSteer(ReadyInput()),
			HazardSwimEgressLiveSteerTransition::SteerCandidate,
			HazardSwimEgressLiveSteerTerminal::None,
			HazardSwimEgressLiveSteerReason::Ready,
			"all checked facts produce only a steer candidate");
	}

	void TestExplicitTerminals()
	{
		auto input = ReadyInput();
		input.Physics = HazardSwimEgressPhysics::Falling;
		CheckDecision(EvaluateHazardSwimEgressLiveSteer(input),
			HazardSwimEgressLiveSteerTransition::Terminal,
			HazardSwimEgressLiveSteerTerminal::Falling,
			HazardSwimEgressLiveSteerReason::Falling,
			"falling terminates an active episode");
		input = ReadyInput();
		input.ExactHarmfulWater = false;
		CheckDecision(EvaluateHazardSwimEgressLiveSteer(input),
			HazardSwimEgressLiveSteerTransition::Terminal,
			HazardSwimEgressLiveSteerTerminal::HazardCleared,
			HazardSwimEgressLiveSteerReason::HazardCleared,
			"cleared primary hazard terminates steering");
		input = ReadyInput();
		input.Probe = HazardSwimEgressProbe::Blocked;
		CheckDecision(EvaluateHazardSwimEgressLiveSteer(input),
			HazardSwimEgressLiveSteerTransition::Terminal,
			HazardSwimEgressLiveSteerTerminal::ProbeBlocked,
			HazardSwimEgressLiveSteerReason::ProbeBlocked,
			"blocked current probe terminates steering");
		input = ReadyInput();
		input.ProbePreviouslyRejected = true;
		CheckDecision(EvaluateHazardSwimEgressLiveSteer(input),
			HazardSwimEgressLiveSteerTransition::Terminal,
			HazardSwimEgressLiveSteerTerminal::ProbeBlocked,
			HazardSwimEgressLiveSteerReason::ProbePreviouslyRejected,
			"previously blocked probe remains terminal");
	}

	void TestFailsClosed()
	{
		auto checkNoAction = [](HazardSwimEgressLiveSteerInput input,
			HazardSwimEgressLiveSteerReason reason, const std::string& message)
		{
			CheckDecision(EvaluateHazardSwimEgressLiveSteer(input),
				HazardSwimEgressLiveSteerTransition::NoAction,
				HazardSwimEgressLiveSteerTerminal::None, reason, message);
		};
		auto input = ReadyInput();
		input.PolicyEnabled = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::PolicyDisabled,
			"disabled policy cannot steer");
		input = ReadyInput();
		input.EligibleActor = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::IneligibleActor,
			"non-stock actor cannot steer");
		input = ReadyInput();
		input.Alive = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::NotAlive,
			"dead actor cannot steer");
		input = ReadyInput();
		input.HarmfulWaterEpisodeActive = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::NoActiveEpisode,
			"no episode cannot steer");
		input = ReadyInput();
		input.Physics = HazardSwimEgressPhysics::Other;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::NotSwimming,
			"non-swimming actor cannot steer");
		input = ReadyInput();
		input.LiveActionAuthorized = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::NotAuthorized,
			"unauthorized episode cannot steer");
		input = ReadyInput();
		input.MovementCommandActive = true;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::MovementCommandActive,
			"an active bot movement command keeps ownership of acceleration");
		input = ReadyInput();
		input.FiniteAnchorKnown = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::MissingAnchor,
			"unknown anchor cannot steer");
		input = ReadyInput();
		input.AnchorFromFallingPreMove = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::AnchorNotFromFallingPreMove,
			"safe-swimming anchor cannot steer");
		input = ReadyInput();
		input.AnchorDistanceKnown = false;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::AnchorDistanceUnknown,
			"unknown distance cannot steer");
		input = ReadyInput();
		input.AnchorDistance = std::numeric_limits<double>::quiet_NaN();
		checkNoAction(input, HazardSwimEgressLiveSteerReason::AnchorDistanceUnknown,
			"non-finite distance cannot steer");
		input = ReadyInput();
		input.AnchorDistance = 7.99;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::AnchorTooNear,
			"near anchor cannot steer");
		input = ReadyInput();
		input.AnchorDistance = 512.01;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::AnchorTooFar,
			"distant anchor cannot steer");
		input = ReadyInput();
		input.Probe = HazardSwimEgressProbe::NotRun;
		checkNoAction(input, HazardSwimEgressLiveSteerReason::ProbeNotRun,
			"unrun probe cannot steer");
	}

	void TestAccelerationOverlayRestorationOwnership()
	{
		HazardSwimEgressAccelerationOverlayRestoreInput input;
		input.OverlayActive = true;
		input.CurrentAcceleration = vec3(30.0f, 0.0f, 0.0f);
		input.OverlayDirection = vec3(1.0f, 0.0f, 0.0f);
		input.ExpectedMaximumAcceleration = 30.0f;
		Check(ShouldRestoreHazardSwimEgressAccelerationOverlay(input),
			"the unchanged one-tick egress overlay restores the prior command");
		input.CurrentAcceleration = vec3(0.0f, 30.0f, 0.0f);
		Check(!ShouldRestoreHazardSwimEgressAccelerationOverlay(input),
			"a callback replacement command keeps ownership of acceleration");
		input.CurrentAcceleration = vec3(60.0f, 0.0f, 0.0f);
		Check(!ShouldRestoreHazardSwimEgressAccelerationOverlay(input),
			"an acceleration outside the swim overlay bound is not restored");
		input.OverlayActive = false;
		input.CurrentAcceleration = vec3(30.0f, 0.0f, 0.0f);
		Check(!ShouldRestoreHazardSwimEgressAccelerationOverlay(input),
			"an inactive overlay cannot restore acceleration");
	}

	void TestStockPlannerReplan()
	{
		HazardSwimEgressLiveReplanInput input;
		input.PolicyEnabled = true;
		input.EligibleActor = true;
		input.Alive = true;
		input.Swimming = true;
		input.HarmfulWaterEpisodeActive = true;
		input.LiveActionAuthorized = true;
		input.MovementCommandActive = true;
		input.SafeNavigationCandidateKnown = true;
		Check(EvaluateHazardSwimEgressLiveReplan(input)
			== HazardSwimEgressLiveReplanDecision::Replan,
			"an authorized water entry with a safe candidate yields one stock replan");
		input.ReplanAlreadyIssued = true;
		Check(EvaluateHazardSwimEgressLiveReplan(input)
			== HazardSwimEgressLiveReplanDecision::NoAction,
			"a water episode cannot force the stock planner twice");
		input.ReplanAlreadyIssued = false;
		input.SafeNavigationCandidateKnown = false;
		Check(EvaluateHazardSwimEgressLiveReplan(input)
			== HazardSwimEgressLiveReplanDecision::NoAction,
			"an unprobed escape candidate cannot force a stock replan");
		input.SafeNavigationCandidateKnown = true;
		input.MovementCommandActive = false;
		Check(EvaluateHazardSwimEgressLiveReplan(input)
			== HazardSwimEgressLiveReplanDecision::NoAction,
			"without a stock movement command there is nothing to hand back");
	}
}

int main()
{
	TestReadyCandidate();
	TestExplicitTerminals();
	TestFailsClosed();
	TestAccelerationOverlayRestorationOwnership();
	TestStockPlannerReplan();
	if (Failures == 0)
		std::cout << "Hazard swim egress live steer tests passed\n";
	return Failures == 0 ? 0 : 1;
}
