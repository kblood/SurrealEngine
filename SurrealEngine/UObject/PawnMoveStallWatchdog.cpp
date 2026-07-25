#include "PawnMoveStallWatchdog.h"

#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool SameDestination(const vec3& left, const vec3& right)
		{
			return left.x == right.x && left.y == right.y && left.z == right.z;
		}

		MoveStallWatchdogState BeginEpisode(const vec3& location, float elapsed,
			const MoveStallCommandKey& command)
		{
			MoveStallWatchdogState state;
			state.Active = true;
			state.Anchor = location;
			state.NoProgressSeconds = elapsed;
			state.Command = command;
			return state;
		}
	}

	MoveStallWatchdogState RecordMoveStallCommand(const MoveStallWatchdogState& state,
		const MoveStallCommandKey& command)
	{
		MoveStallWatchdogState result = SameMoveStallCommand(state.Command, command)
			? state : MoveStallWatchdogState{};
		result.Command = command;
		result.CommandSeenSinceObservation = true;
		return result;
	}

	bool SameMoveStallCommand(const MoveStallCommandKey& left,
		const MoveStallCommandKey& right)
	{
		if (left.LatentMode != right.LatentMode)
			return false;
		switch (left.LatentMode)
		{
		case MoveStallLatentMode::MoveToward:
			return left.Target && left.Target == right.Target;
		case MoveStallLatentMode::MoveTo:
		case MoveStallLatentMode::StrafeTo:
			return SameDestination(left.Destination, right.Destination);
		case MoveStallLatentMode::StrafeFacing:
			return left.Target == right.Target
				&& SameDestination(left.Destination, right.Destination);
		default:
			return false;
		}
	}

	bool ShouldForceMoveStallReplan(bool detected, bool liveNavigationMoveToward)
	{
		return detected && liveNavigationMoveToward;
	}

	MoveStallRecoveryDecision SelectMoveStallRecovery(const MoveStallRecoveryContext& context)
	{
		if (!context.Detected)
			return MoveStallRecoveryDecision::None;
		if (context.LatentMode == MoveStallLatentMode::MoveToward
			&& context.LiveNavigationMoveToward)
			return MoveStallRecoveryDecision::NavigationReplan;

		const bool targetlessPositionalMove = context.TargetlessMoveToTimeoutEnabled
			&& context.Targetless
			&& context.LatentMode == MoveStallLatentMode::MoveTo;
		const bool finitePosition = IsFinite(context.Location) && IsFinite(context.Destination);
		const bool validTimer = std::isfinite(context.MoveTimer) && context.MoveTimer > 0.0f;
		const bool validAcceptanceRadius = std::isfinite(context.AcceptanceRadius)
			&& context.AcceptanceRadius >= 0.0f;
		if (!targetlessPositionalMove || !finitePosition || !validTimer || !validAcceptanceRadius)
			return MoveStallRecoveryDecision::None;

		const vec2 displacement = context.Destination.xy() - context.Location.xy();
		if (dot(displacement, displacement)
			<= context.AcceptanceRadius * context.AcceptanceRadius)
			return MoveStallRecoveryDecision::None;
		return MoveStallRecoveryDecision::TargetlessTimeout;
	}

	MoveStallRecoveryEpisodeUpdate StartMoveStallRecoveryEpisode()
	{
		MoveStallRecoveryEpisodeUpdate update;
		update.State.Active = true;
		update.Started = true;
		return update;
	}

	MoveStallRecoveryEpisodeUpdate AdvanceMoveStallRecoveryEpisode(
		const MoveStallRecoveryEpisodeState& state, float elapsed,
		MoveStallRecoveryEpisodeEvent event)
	{
		MoveStallRecoveryEpisodeUpdate update;
		if (!state.Active)
			return update;
		if (!std::isfinite(elapsed) || elapsed < 0.0f)
		{
			update.Terminal = true;
			update.Outcome = MoveStallRecoveryEpisodeOutcome::Unknown;
			return update;
		}

		const float seconds = state.SecondsSinceDetection + elapsed;
		if (!std::isfinite(seconds))
		{
			update.Terminal = true;
			update.Outcome = MoveStallRecoveryEpisodeOutcome::Unknown;
			return update;
		}
		update.State = state;
		update.State.SecondsSinceDetection = seconds;
		auto terminal = [&update](MoveStallRecoveryEpisodeOutcome outcome)
		{
			update.State = {};
			update.Terminal = true;
			update.Outcome = outcome;
		};

		switch (event)
		{
		case MoveStallRecoveryEpisodeEvent::Cleared:
			if (seconds <= 2.0f)
				terminal(MoveStallRecoveryEpisodeOutcome::ClearedWithin2Seconds);
			else if (seconds <= 5.0f)
				terminal(MoveStallRecoveryEpisodeOutcome::ClearedAfter2SecondsWithin5Seconds);
			else
				terminal(MoveStallRecoveryEpisodeOutcome::Missed5SecondDeadline);
			return update;
		case MoveStallRecoveryEpisodeEvent::QualifiedNavigationReplan:
			terminal(seconds <= 5.0f
				? MoveStallRecoveryEpisodeOutcome::ReplannedWithin5Seconds
				: MoveStallRecoveryEpisodeOutcome::Missed5SecondDeadline);
			return update;
		case MoveStallRecoveryEpisodeEvent::IntentionalStop:
			terminal(MoveStallRecoveryEpisodeOutcome::ExcludedIntentionalStop);
			return update;
		case MoveStallRecoveryEpisodeEvent::LifeBoundary:
			terminal(MoveStallRecoveryEpisodeOutcome::CensoredLifeBoundary);
			return update;
		case MoveStallRecoveryEpisodeEvent::RunEnd:
			terminal(MoveStallRecoveryEpisodeOutcome::CensoredRunEnd);
			return update;
		case MoveStallRecoveryEpisodeEvent::Unknown:
			terminal(MoveStallRecoveryEpisodeOutcome::Unknown);
			return update;
		case MoveStallRecoveryEpisodeEvent::None:
			break;
		}
		if (seconds > 5.0f)
			terminal(MoveStallRecoveryEpisodeOutcome::Missed5SecondDeadline);
		return update;
	}

	MoveStallWatchdogObservation ObserveMoveStall(const MoveStallWatchdogState& state,
		const vec3& location, float elapsed, bool eligibleContext, bool latentMovementIntent,
		float progressRadius, float detectionSeconds)
	{
		MoveStallWatchdogObservation result;
		const bool valid = eligibleContext && IsFinite(location) && std::isfinite(elapsed)
			&& elapsed >= 0.0f && std::isfinite(progressRadius) && progressRadius >= 0.0f
			&& std::isfinite(detectionSeconds) && detectionSeconds > 0.0f;
		if (!valid)
		{
			result.EpisodeReset = state.Active && state.DetectionReported;
			return result;
		}

		result.State = state;
		const bool movementIntent = latentMovementIntent || state.CommandSeenSinceObservation;
		result.State.CommandSeenSinceObservation = false;
		if (!movementIntent)
		{
			if (state.Active && state.MissingIntentObservations == 0)
			{
				result.State.MissingIntentObservations = 1;
				return result;
			}
			result.EpisodeReset = state.Active && state.DetectionReported;
			result.State = {};
			return result;
		}

		result.EligibleSeconds = elapsed;
		if (!state.Active)
		{
			result.State = BeginEpisode(location, elapsed, state.Command);
		}
		else
		{
			result.State.MissingIntentObservations = 0;
			const vec2 displacement = location.xy() - state.Anchor.xy();
			if (dot(displacement, displacement) > progressRadius * progressRadius)
			{
				result.EpisodeReset = state.DetectionReported;
				result.State = BeginEpisode(location, elapsed, state.Command);
			}
			else
			{
				result.State.NoProgressSeconds += elapsed;
			}
		}

		if (!result.State.DetectionReported
			&& result.State.NoProgressSeconds >= detectionSeconds)
		{
			result.State.DetectionReported = true;
			result.Detected = true;
		}
		return result;
	}
}
