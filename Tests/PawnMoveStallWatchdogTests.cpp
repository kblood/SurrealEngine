#include "UObject/PawnMoveStallWatchdog.h"

#include <cmath>
#include <iostream>
#include <limits>

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
	int navigationTargetA = 0;
	int navigationTargetB = 0;
	const MoveStallCommandKey moveToA = {
		MoveStallLatentMode::MoveTo, nullptr, vec3(100.0f, 0.0f, 0.0f)
	};
	const MoveStallCommandKey moveToB = {
		MoveStallLatentMode::MoveTo, nullptr, vec3(200.0f, 0.0f, 0.0f)
	};
	const MoveStallCommandKey moveTowardA = {
		MoveStallLatentMode::MoveToward, &navigationTargetA, vec3(100.0f, 0.0f, 0.0f)
	};
	const MoveStallCommandKey moveTowardAAfterTargetMoved = {
		MoveStallLatentMode::MoveToward, &navigationTargetA, vec3(200.0f, 0.0f, 0.0f)
	};
	const MoveStallCommandKey moveTowardB = {
		MoveStallLatentMode::MoveToward, &navigationTargetB, vec3(100.0f, 0.0f, 0.0f)
	};
	const MoveStallCommandKey strafeFacingA = {
		MoveStallLatentMode::StrafeFacing, &navigationTargetA, vec3(100.0f, 0.0f, 0.0f)
	};
	MoveStallRecoveryContext targetlessMoveTo = {
		.Detected = true,
		.LatentMode = MoveStallLatentMode::MoveTo,
		.Targetless = true,
		.MoveTimer = 1.0f,
		.Location = vec3(0.0f),
		.Destination = vec3(10.0f, 0.0f, 0.0f),
		.AcceptanceRadius = 1.0f
	};
	Check(SelectMoveStallRecovery(targetlessMoveTo)
		== MoveStallRecoveryDecision::None,
		"a detected targetless MoveTo remains inert until its timeout recovery is enabled");
	targetlessMoveTo.TargetlessMoveToTimeoutEnabled = true;
	Check(SelectMoveStallRecovery(targetlessMoveTo)
		== MoveStallRecoveryDecision::TargetlessTimeout,
		"an enabled detected targetless MoveTo beyond its arrival radius requests a timeout");
	MoveStallRecoveryContext targetlessStrafeTo = targetlessMoveTo;
	targetlessStrafeTo.LatentMode = MoveStallLatentMode::StrafeTo;
	Check(SelectMoveStallRecovery(targetlessStrafeTo)
		== MoveStallRecoveryDecision::None,
		"targetless StrafeTo remains shadow-only");

	MoveStallRecoveryContext navigationMove = targetlessMoveTo;
	navigationMove.LatentMode = MoveStallLatentMode::MoveToward;
	navigationMove.LiveNavigationMoveToward = true;
	navigationMove.Targetless = false;
	Check(SelectMoveStallRecovery(navigationMove)
		== MoveStallRecoveryDecision::None,
		"a detected navigation MoveToward remains inert until replan recovery is enabled");
	navigationMove.NavigationReplanEnabled = true;
	Check(SelectMoveStallRecovery(navigationMove)
		== MoveStallRecoveryDecision::NavigationReplan,
		"a detected MoveToward with a live navigation target requests a navigation replan");
	Check(SelectMoveStallRecovery(navigationMove)
		!= MoveStallRecoveryDecision::TargetlessTimeout,
		"a navigation replan is distinct from a targetless timeout");

	MoveStallRecoveryContext ineligible = targetlessMoveTo;
	ineligible.Detected = false;
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"an undetected positional move does not request recovery");
	ineligible = targetlessMoveTo;
	ineligible.Targetless = false;
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"a MoveTo with an actor target does not request a targetless timeout");
	ineligible = targetlessMoveTo;
	ineligible.MoveTimer = 0.0f;
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"a positional move whose timer has expired finishes normally");
	ineligible = targetlessMoveTo;
	ineligible.MoveTimer = std::numeric_limits<float>::infinity();
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"a positional move with a non-finite timer cannot request a timeout action");
	ineligible = targetlessMoveTo;
	ineligible.Destination.x = std::nanf("");
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"a positional move with a non-finite destination remains unrecovered");
	ineligible = targetlessMoveTo;
	ineligible.Destination = vec3(1.0f, 0.0f, 50.0f);
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"a positional move inside its XY acceptance radius finishes normally");
	ineligible = targetlessMoveTo;
	ineligible.LatentMode = MoveStallLatentMode::StrafeFacing;
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"StrafeFacing remains shadow-only");
	ineligible = targetlessMoveTo;
	ineligible.LatentMode = MoveStallLatentMode::MoveToward;
	ineligible.Targetless = false;
	Check(SelectMoveStallRecovery(ineligible) == MoveStallRecoveryDecision::None,
		"MoveToward without a live navigation target remains shadow-only");

	Check(ShouldForceMoveStallReplan(true, true),
		"a new detection during MoveToward with a live navigation target requests a forced replan");
	Check(!ShouldForceMoveStallReplan(false, true),
		"an already-reported episode does not request another forced replan");
	Check(!ShouldForceMoveStallReplan(true, false),
		"MoveTo, strafes, and unusable MoveToward targets remain shadow-only");
	Check(SameMoveStallCommand(moveTowardA, moveTowardAAfterTargetMoved),
		"a MoveToward command key follows its live target rather than its destination");
	Check(!SameMoveStallCommand(moveTowardA, moveTowardB),
		"a different MoveToward target is a semantic command change");
	Check(!SameMoveStallCommand(moveToA, moveToB),
		"a changed positional destination is a semantic command change");

	MoveStallRecoveryEpisodeUpdate recovery = StartMoveStallRecoveryEpisode();
	Check(recovery.Started && recovery.State.Active
		&& Near(recovery.State.SecondsSinceDetection, 0.0f),
		"recovery timing starts exactly at the one-shot stall detection");
	recovery = AdvanceMoveStallRecoveryEpisode(recovery.State, 2.0f,
		MoveStallRecoveryEpisodeEvent::Cleared);
	Check(recovery.Terminal && recovery.Outcome
		== MoveStallRecoveryEpisodeOutcome::ClearedWithin2Seconds,
		"clearance at the two-second boundary earns the strict recovery outcome");

	recovery = StartMoveStallRecoveryEpisode();
	recovery = AdvanceMoveStallRecoveryEpisode(recovery.State, 2.01f,
		MoveStallRecoveryEpisodeEvent::Cleared);
	Check(recovery.Terminal && recovery.Outcome
		== MoveStallRecoveryEpisodeOutcome::ClearedAfter2SecondsWithin5Seconds,
		"a late but timely clearance is distinct from the two-second outcome");

	recovery = StartMoveStallRecoveryEpisode();
	recovery = AdvanceMoveStallRecoveryEpisode(recovery.State, 5.0f,
		MoveStallRecoveryEpisodeEvent::QualifiedNavigationReplan);
	Check(recovery.Terminal && recovery.Outcome
		== MoveStallRecoveryEpisodeOutcome::ReplannedWithin5Seconds,
		"only an explicit qualified replan earns the five-second outcome");

	recovery = StartMoveStallRecoveryEpisode();
	recovery = AdvanceMoveStallRecoveryEpisode(recovery.State, 5.01f,
		MoveStallRecoveryEpisodeEvent::Cleared);
	Check(recovery.Terminal && recovery.Outcome
		== MoveStallRecoveryEpisodeOutcome::Missed5SecondDeadline,
		"a recovery after the five-second deadline remains a miss");

	recovery = StartMoveStallRecoveryEpisode();
	recovery = AdvanceMoveStallRecoveryEpisode(recovery.State, 0.0f,
		MoveStallRecoveryEpisodeEvent::IntentionalStop);
	Check(recovery.Terminal && recovery.Outcome
		== MoveStallRecoveryEpisodeOutcome::ExcludedIntentionalStop,
		"an evidenced intentional stop is explicitly excluded");

	recovery = StartMoveStallRecoveryEpisode();
	recovery = AdvanceMoveStallRecoveryEpisode(recovery.State, 0.0f,
		MoveStallRecoveryEpisodeEvent::LifeBoundary);
	Check(recovery.Terminal && recovery.Outcome
		== MoveStallRecoveryEpisodeOutcome::CensoredLifeBoundary,
		"death or a life boundary is censored rather than counted as a recovery");

	recovery = StartMoveStallRecoveryEpisode();
	recovery = AdvanceMoveStallRecoveryEpisode(recovery.State, std::nanf(""),
		MoveStallRecoveryEpisodeEvent::None);
	Check(recovery.Terminal && recovery.Outcome
		== MoveStallRecoveryEpisodeOutcome::Unknown,
		"invalid timing fails closed as an unknown recovery episode");

	MoveStallWatchdogState progressing = RecordMoveStallCommand({}, moveToA);
	MoveStallWatchdogObservation progressObservation = ObserveMoveStall(progressing,
		vec3(0.0f), 0.25f, true, false, 4.0f, 2.0f);
	progressObservation = ObserveMoveStall(progressObservation.State,
		vec3(4.1f, 0.0f, 0.0f), 0.25f, true, true, 4.0f, 2.0f);
	Check(!progressObservation.EpisodeReset && progressObservation.State.Active,
		"ordinary progress starts a fresh anchor without counting a detected-episode reset");
	progressing = RecordMoveStallCommand(progressObservation.State, moveToA);
	progressObservation = ObserveMoveStall(progressing,
		vec3(4.1f, 0.0f, 0.0f), 1.75f, true, false, 4.0f, 2.0f);
	Check(progressObservation.Detected,
		"a progress reset preserves the active command key across a same-key reissue");

	MoveStallWatchdogState commandState = RecordMoveStallCommand({}, moveTowardA);
	MoveStallWatchdogObservation commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 1.75f, true, false, 4.0f, 2.0f);
	Check(!commandObservation.Detected
		&& Near(commandObservation.State.NoProgressSeconds, 1.75f),
		"a single latent command retains its own no-progress time");
	commandState = RecordMoveStallCommand(
		commandObservation.State, moveTowardAAfterTargetMoved);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 0.25f, true, false, 4.0f, 2.0f);
	Check(commandObservation.Detected
		&& Near(commandObservation.State.NoProgressSeconds, 2.0f),
		"reissuing MoveToward for the same target preserves the stall episode");

	commandState = RecordMoveStallCommand({}, moveTowardA);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 1.75f, true, false, 4.0f, 2.0f);
	commandState = RecordMoveStallCommand(commandObservation.State, moveTowardB);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 0.25f, true, false, 4.0f, 2.0f);
	Check(!commandObservation.Detected
		&& Near(commandObservation.State.NoProgressSeconds, 0.25f),
		"selecting a different MoveToward target starts a fresh stall episode");

	commandState = RecordMoveStallCommand({}, moveTowardA);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 1.75f, true, false, 4.0f, 2.0f);
	commandState = RecordMoveStallCommand(commandObservation.State, strafeFacingA);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 0.25f, true, false, 4.0f, 2.0f);
	Check(!commandObservation.Detected
		&& Near(commandObservation.State.NoProgressSeconds, 0.25f),
		"changing latent mode resets even when the actor target is unchanged");

	commandState = RecordMoveStallCommand({}, moveToA);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 1.75f, true, false, 4.0f, 2.0f);
	commandState = RecordMoveStallCommand(commandObservation.State, moveToA);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 0.25f, true, false, 4.0f, 2.0f);
	Check(commandObservation.Detected
		&& Near(commandObservation.State.NoProgressSeconds, 2.0f),
		"reissuing targetless MoveTo with the same destination preserves the episode");

	commandState = RecordMoveStallCommand({}, moveToA);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 1.75f, true, false, 4.0f, 2.0f);
	commandState = RecordMoveStallCommand(commandObservation.State, moveToB);
	commandObservation = ObserveMoveStall(commandState,
		vec3(5.0f, 6.0f, 7.0f), 0.25f, true, false, 4.0f, 2.0f);
	Check(!commandObservation.Detected
		&& Near(commandObservation.State.NoProgressSeconds, 0.25f),
		"changing a targetless MoveTo destination starts a fresh episode");

	MoveStallWatchdogState state = RecordMoveStallCommand({}, moveToA);
	MoveStallWatchdogObservation observation = ObserveMoveStall(state,
		vec3(10.0f, 20.0f, 30.0f), 0.5f, true, false, 4.0f, 2.0f);
	Check(observation.State.Active && !observation.State.CommandSeenSinceObservation
		&& Near(observation.State.NoProgressSeconds, 0.5f),
		"a command issued after the prior pawn tick supplies movement intent once");
	Check(Near(observation.EligibleSeconds, 0.5f) && !observation.Detected,
		"eligible time is reported without an early detection");

	state = observation.State;
	observation = ObserveMoveStall(state, vec3(13.9f, 20.0f, 30.0f),
		1.0f, true, true, 4.0f, 2.0f);
	Check(observation.State.Active && Near(observation.State.NoProgressSeconds, 1.5f)
		&& !observation.EpisodeReset,
		"sub-four-unit displacement remains in the same no-progress episode");
	state = observation.State;
	observation = ObserveMoveStall(state, vec3(13.9f, 20.0f, 30.0f),
		0.5f, true, true, 4.0f, 2.0f);
	Check(observation.Detected && observation.State.DetectionReported,
		"two eligible seconds inside four units reports one shadow detection");
	state = observation.State;
	observation = ObserveMoveStall(state, vec3(13.9f, 20.0f, 30.0f),
		1.0f, true, true, 4.0f, 2.0f);
	Check(!observation.Detected && observation.State.DetectionReported,
		"a detected episode does not report repeatedly");

	MoveStallWatchdogState rearmed = RecordMoveStallCommand({}, moveToA);
	MoveStallWatchdogObservation rearmedObservation = ObserveMoveStall(rearmed,
		vec3(13.9f, 20.0f, 30.0f), 1.0f, true, false, 4.0f, 2.0f);
	rearmedObservation = ObserveMoveStall(rearmedObservation.State,
		vec3(13.9f, 20.0f, 30.0f), 1.0f, true, true, 4.0f, 2.0f);
	Check(rearmedObservation.Detected,
		"explicitly resetting after a forced replan re-arms a later stuck episode");

	state = observation.State;
	observation = ObserveMoveStall(state, vec3(14.1f, 20.0f, 30.0f),
		0.25f, true, true, 4.0f, 2.0f);
	Check(observation.EpisodeReset && observation.State.Active
		&& !observation.State.DetectionReported
		&& Near(observation.State.NoProgressSeconds, 0.25f),
		"more than four units of progress resets and starts a fresh episode");

	state = observation.State;
	observation = ObserveMoveStall(state, vec3(14.1f, 20.0f, 30.0f),
		0.5f, true, false, 4.0f, 2.0f);
	Check(observation.State.Active && observation.State.MissingIntentObservations == 1
		&& Near(observation.State.NoProgressSeconds, 0.25f)
		&& Near(observation.EligibleSeconds, 0.0f),
		"one missing-intent pawn tick preserves the episode without accruing time");
	state = observation.State;
	observation = ObserveMoveStall(state, vec3(14.1f, 20.0f, 30.0f),
		0.5f, true, true, 4.0f, 2.0f);
	Check(observation.State.Active && observation.State.MissingIntentObservations == 0
		&& Near(observation.State.NoProgressSeconds, 0.75f),
		"renewed latent intent continues after the one-tick scheduling gap");
	state = observation.State;
	observation = ObserveMoveStall(state, vec3(14.1f, 20.0f, 30.0f),
		0.5f, true, false, 4.0f, 2.0f);
	state = observation.State;
	observation = ObserveMoveStall(state, vec3(14.1f, 20.0f, 30.0f),
		0.5f, true, false, 4.0f, 2.0f);
	Check(!observation.EpisodeReset && !observation.State.Active,
		"ending an undetected episode clears it without reporting a detected-episode reset");

	state = RecordMoveStallCommand({}, moveToA);
	observation = ObserveMoveStall(state, vec3(0.0f), 1.0f,
		false, true, 4.0f, 2.0f);
	Check(!observation.State.Active && Near(observation.EligibleSeconds, 0.0f),
		"an ineligible pawn cannot start an episode");

	state = RecordMoveStallCommand({}, moveToA);
	state = RecordMoveStallCommand(state, moveToA);
	observation = ObserveMoveStall(state, vec3(0.0f), 0.25f,
		true, false, 4.0f, 2.0f);
	Check(Near(observation.State.NoProgressSeconds, 0.25f),
		"multiple movement commands between pawn ticks still produce one observation");

	if (Failures == 0)
		std::cout << "All pawn move-stall watchdog tests passed.\n";
	return Failures == 0 ? 0 : 1;
}
