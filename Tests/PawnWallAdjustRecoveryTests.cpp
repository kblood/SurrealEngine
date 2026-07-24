#include "UObject/PawnWallAdjustRecovery.h"

#include <cmath>
#include <iostream>

namespace
{
	int Failures = 0;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			Failures++;
		}

	}

	bool Near(float left, float right)
	{
		return std::abs(left - right) < 0.0001f;
	}
}

int main()
{
	using namespace PawnMovement;

	const WallAdjustObservation first = ObserveWallAdjust({}, vec3(10.0f, 20.0f, 30.0f),
		1.0f, 48.0f, 0.25f, 2);
	Check(first.State.Active && !first.Escalate && first.State.RepeatOrdinal == 0,
		"first wall adjustment retains stock behavior");
	const WallAdjustObservation immediate = ObserveWallAdjust(first.State,
		vec3(20.0f, 20.0f, 30.0f), 1.0f, 48.0f, 0.25f, 2);
	Check(immediate.Repeated && !immediate.Escalate,
		"same-tick duplicate does not escalate an ordinary wall contact");

	WallAdjustRecoveryAdvance advance = AdvanceWallAdjustRecovery(immediate.State,
		immediate.State.Origin, 0.2f, 48.0f, 96.0f, true);
	Check(advance.State.Active && Near(advance.State.AgeSeconds, 0.2f)
		&& Near(advance.State.RemainingSeconds, 0.8f),
		"repeat window age advances only with simulation time");
	const WallAdjustObservation early = ObserveWallAdjust(advance.State,
		vec3(21.0f, 20.0f, 30.0f), 1.0f, 48.0f, 0.25f, 2);
	Check(early.Repeated && !early.Escalate,
		"multiple observations before sustained threshold remain stock");
	advance = AdvanceWallAdjustRecovery(early.State, early.State.Origin,
		0.1f, 48.0f, 96.0f, true);
	const WallAdjustObservation sustained = ObserveWallAdjust(advance.State,
		vec3(21.0f, 20.0f, 30.0f), 1.0f, 48.0f, 0.25f, 2);
	Check(sustained.Repeated && sustained.Escalate && sustained.State.RepeatOrdinal == 1,
		"sustained cross-tick wall contact escalates after 0.25 seconds");
	Check(Near(sustained.State.RemainingSeconds, 0.7f),
		"wall callbacks do not reset the one-second detection window");

	const WallAdjustObservation distant = ObserveWallAdjust(sustained.State,
		vec3(100.0f, 20.0f, 30.0f), 1.0f, 48.0f, 0.25f, 2);
	Check(!distant.Escalate && distant.State.RepeatOrdinal == 0,
		"leaving the locality starts a new stock adjustment sequence");

	const WallAdjustObservation tightFirst = ObserveWallAdjust({}, vec3(10.0f, 20.0f, 30.0f),
		1.0f, 4.0f, 0.25f, 2);
	WallAdjustRecoveryAdvance tightAdvance = AdvanceWallAdjustRecovery(tightFirst.State,
		tightFirst.State.Origin, 0.3f, 4.0f, 96.0f, true);
	const WallAdjustObservation exactTightRepeat = ObserveWallAdjust(tightAdvance.State,
		tightFirst.State.Origin, 1.0f, 4.0f, 0.25f, 2);
	Check(exactTightRepeat.Repeated && exactTightRepeat.Escalate,
		"exact fixed-place contact sustains tight-locality detection");
	tightAdvance = AdvanceWallAdjustRecovery(tightFirst.State,
		tightFirst.State.Origin, 0.3f, 4.0f, 96.0f, true);
	const WallAdjustObservation subFourJitter = ObserveWallAdjust(tightAdvance.State,
		vec3(13.9f, 20.0f, 30.0f), 1.0f, 4.0f, 0.25f, 2);
	Check(subFourJitter.Repeated && subFourJitter.Escalate,
		"sub-four-unit collision jitter sustains fixed-place detection");
	tightAdvance = AdvanceWallAdjustRecovery(tightFirst.State,
		tightFirst.State.Origin, 0.3f, 4.0f, 96.0f, true);
	const WallAdjustObservation meaningfulProgress = ObserveWallAdjust(tightAdvance.State,
		vec3(14.1f, 20.0f, 30.0f), 1.0f, 4.0f, 0.25f, 2);
	Check(!meaningfulProgress.Repeated && !meaningfulProgress.Escalate
		&& meaningfulProgress.State.ObservationCount == 1,
		"more than four units of progress resets fixed-place detection");

	WallAdjustRecoveryState steering = BeginWallAdjustSteering(sustained.State,
		sustained.State.Origin, vec2(1.0f, 0.0f), vec2(-1.0f, 0.0f), 0.5f);
	Check(steering.SteeringActive && steering.RecoveryAttempted
		&& Near(steering.SteeringRemainingSeconds, 0.5f),
		"selected candidate records a bounded steering attempt");
	const WallAdjustObservation whileSteering = ObserveWallAdjust(steering,
		steering.Origin + vec3(60.0f, 0.0f, 0.0f), 1.0f, 48.0f, 0.25f, 2);
	Check(whileSteering.Repeated && !whileSteering.Escalate
		&& whileSteering.State.EscapeDirection == steering.EscapeDirection,
		"wall callbacks during steering retain the chosen direction");
	Check(EvaluateWallAdjustSteeringRequest(steering, vec2(1.0f, 0.0f), 0.5f)
		== WallAdjustSteeringRequest::Apply, "aligned unsafe request keeps steering override active");
	Check(EvaluateWallAdjustSteeringRequest(steering, vec2(0.0f, 1.0f), 0.5f)
		== WallAdjustSteeringRequest::Clear, "nonaligned safe request clears steering override");
	Check(EvaluateWallAdjustSteeringRequest(steering, vec2(0.0f), 0.5f)
		== WallAdjustSteeringRequest::Clear,
		"steering requires a latent movement request rather than callback-time acceleration");

	advance = AdvanceWallAdjustRecovery(steering, steering.SteeringOrigin,
		0.2f, 48.0f, 96.0f, true);
	Check(advance.State.SteeringActive && Near(advance.State.SteeringRemainingSeconds, 0.3f),
		"steering window decays in simulation time");
	advance = AdvanceWallAdjustRecovery(advance.State,
		advance.State.SteeringOrigin + vec3(97.0f, 0.0f, 0.0f),
		0.01f, 48.0f, 96.0f, true);
	Check(!advance.State.Active && advance.Escaped,
		"recovery succeeds only after actual 96-unit escape");
	advance = AdvanceWallAdjustRecovery(steering, steering.SteeringOrigin,
		0.5f, 48.0f, 96.0f, true);
	Check(advance.State.Active && !advance.State.SteeringActive
		&& advance.State.ReplanPending && advance.TimedOut && !advance.Escaped,
		"steering timeout requests a one-shot replan without reporting success");
	const WallAdjustObservation pendingObservation = ObserveWallAdjust(advance.State,
		advance.State.SteeringOrigin, 1.0f, 48.0f, 0.25f, 2);
	Check(pendingObservation.State.ReplanPending && !pendingObservation.Repeated
		&& !pendingObservation.Escalate,
		"pending replan cannot silently restart wall detection");
	const WallAdjustRecoveryAdvance pendingAdvance = AdvanceWallAdjustRecovery(advance.State,
		advance.State.SteeringOrigin, 0.1f, 48.0f, 96.0f, true);
	Check(pendingAdvance.State.ReplanPending && !pendingAdvance.TimedOut,
		"pending replan remains latched until the wall callback consumes it");
	Check(!AdvanceWallAdjustRecovery(first.State, first.State.Origin,
		0.1f, 48.0f, 96.0f, false).State.Active,
		"invalid movement context clears the repeat sequence");

	const auto directions = WallAdjustEscapeDirections(vec2(1.0f, 0.0f));
	Check(Near(directions[0].x, -1.0f) && Near(directions[0].y, 0.0f),
		"reverse is the first wall escape direction");
	Check(Near(directions[1].x, 0.0f) && Near(directions[1].y, 1.0f),
		"left is the second wall escape direction");
	Check(Near(directions[2].x, 0.0f) && Near(directions[2].y, -1.0f),
		"right is the third wall escape direction");

	Check(WallAdjustCandidateOrder(1) == std::array<int, 3>{ 0, 1, 2 },
		"first escalation prefers reverse");
	Check(WallAdjustCandidateOrder(2) == std::array<int, 3>{ 1, 2, 0 },
		"second escalation rotates preference to left");
	Check(WallAdjustCandidateOrder(3) == std::array<int, 3>{ 2, 0, 1 },
		"third escalation rotates preference to right");

	std::array<WallAdjustCandidateProbe, 3> probes = {{
		{ true, true, true },
		{ true, true, true },
		{ true, true, true }
	}};
	Check(SelectWallAdjustCandidate(probes, WallAdjustCandidateOrder(2)) == 1,
		"selection honors the alternating preferred candidate");
	probes[1].NonPainFootRegion = false;
	Check(SelectWallAdjustCandidate(probes, WallAdjustCandidateOrder(2)) == 2,
		"selection rejects a pain-zone candidate and advances deterministically");
	probes[2].WalkableSupport = false;
	probes[0].SweepClear = false;
	Check(SelectWallAdjustCandidate(probes, WallAdjustCandidateOrder(2)) == -1,
		"selection fails closed when no candidate passes every probe");

	if (Failures == 0)
		std::cout << "All pawn wall-adjust recovery tests passed.\n";
	return Failures == 0 ? 0 : 1;
}
