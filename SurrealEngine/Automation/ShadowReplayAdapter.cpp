#include "ShadowReplayAdapter.h"

#include <limits>
#include <utility>

namespace Automation
{
	namespace
	{
		ShadowReplayProjection Reject(ShadowReplayStatus status, std::string error)
		{
			ShadowReplayProjection result;
			result.Status = status;
			result.Error = std::move(error);
			return result;
		}

		bool IsActorAction(CommandKind kind)
		{
			return kind == CommandKind::WalkToActor || kind == CommandKind::AcquireItem ||
				kind == CommandKind::Interact;
		}
	}

	ShadowReplayProjection ProjectShadowReplayCandidate(
		const ShadowReplayCandidate& candidate,
		const ObservationSnapshot& observation,
		std::string commandId,
		uint64_t leaseTicks)
	{
		ValidationResult observationValidation = ValidateObservationSnapshot(observation);
		if (!observationValidation)
			return Reject(ShadowReplayStatus::InvalidObservation, observationValidation.Error);
		if (candidate.DispatchAuthorized)
			return Reject(ShadowReplayStatus::DispatchFlagSet,
				"shadow replay candidates must not authorize dispatch");
		if (candidate.Kind != CommandKind::Wait && !IsActorAction(candidate.Kind))
			return Reject(ShadowReplayStatus::UnsupportedKind,
				"shadow replay candidate kind is unsupported");
		if (candidate.ObservationRevision != observation.Revision)
			return Reject(ShadowReplayStatus::StaleObservation,
				"shadow replay candidate observation is stale");
		if (leaseTicks == 0 || leaseTicks > MaximumCommandLifetimeTicks ||
			observation.Tick > std::numeric_limits<uint64_t>::max() - leaseTicks)
			return Reject(ShadowReplayStatus::InvalidCommand,
				"shadow replay command lease is invalid or unbounded");

		AutomationCommand command;
		command.Id = std::move(commandId);
		command.Kind = candidate.Kind;
		command.IssuedTick = observation.Tick;
		command.DeadlineTick = observation.Tick + leaseTicks;

		if (candidate.Kind == CommandKind::Wait)
		{
			if (candidate.Target || candidate.ArrivalRadius || candidate.WaitTicks == 0 ||
				candidate.WaitTicks > MaximumShadowReplayWaitTicks || candidate.WaitTicks > leaseTicks)
				return Reject(ShadowReplayStatus::InvalidCandidate,
					"shadow replay wait candidate fields are invalid");
			command.WaitTicks = candidate.WaitTicks;
		}
		else
		{
			const double expectedRadius = candidate.Kind == CommandKind::WalkToActor ?
				ShadowWalkArrivalRadius : ShadowActorActionArrivalRadius;
			if (!candidate.Target || candidate.Target->Identity.empty() ||
				candidate.Target->ExpectedClass.empty() || !candidate.ArrivalRadius ||
				*candidate.ArrivalRadius != expectedRadius || candidate.WaitTicks != 0)
				return Reject(ShadowReplayStatus::InvalidCandidate,
					"shadow replay actor candidate fields are invalid");
			command.Target = TargetSelector{
				candidate.ObservationRevision,
				candidate.Target->Identity,
				candidate.Target->ExpectedClass
			};
			command.ArrivalRadius = candidate.ArrivalRadius;
		}

		ValidationResult commandValidation = ValidateCommandForSnapshot(
			command, observation.Tick, observation.Revision);
		if (!commandValidation)
			return Reject(ShadowReplayStatus::InvalidCommand, commandValidation.Error);

		ShadowReplayProjection result;
		if (IsActorAction(command.Kind))
		{
			TargetResolutionResult resolution = ResolveTargetForCommand(
				command, observation.Targets, observation.Revision);
			if (!resolution)
			{
				result.Status = ShadowReplayStatus::TargetRejected;
				result.TargetStatus = resolution.Status;
				result.Error = resolution.Error;
				return result;
			}
			result.TargetStatus = resolution.Status;
		}
		result.Status = ShadowReplayStatus::Ready;
		result.Command = std::move(command);
		return result;
	}
}
