#include "InteractionActionController.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Automation
{
	namespace
	{
		const TargetSnapshot* FindTarget(const ObservationSnapshot& observation,
			const std::string& identity)
		{
			auto it = std::find_if(observation.Targets.begin(), observation.Targets.end(),
				[&](const TargetSnapshot& target) { return target.Identity == identity; });
			return it == observation.Targets.end() ? nullptr : &*it;
		}

		TargetRequirement InteractionRequirements(CommandKind kind)
		{
			if (kind == CommandKind::AcquireItem)
				return TargetRequirement::Acquirable;
			return TargetRequirement::None;
		}

		bool HasTransitioned(const TargetSnapshot& baseline, const TargetSnapshot& current)
		{
			static constexpr double MinimumObservableMovement = 0.001;
			return current.Deleted != baseline.Deleted ||
				current.OwnerIdentity != baseline.OwnerIdentity ||
				current.StateToken != baseline.StateToken ||
				std::abs(current.Location.X - baseline.Location.X) > MinimumObservableMovement ||
				std::abs(current.Location.Y - baseline.Location.Y) > MinimumObservableMovement ||
				std::abs(current.Location.Z - baseline.Location.Z) > MinimumObservableMovement;
		}

		const char* InventoryResourceKindName(
			TargetSnapshot::InventoryResourceKind kind)
		{
			return kind == TargetSnapshot::InventoryResourceKind::AmmoAmount ?
				"ammo_amount" : "num_copies";
		}
	}

	ValidationResult InteractionActionController::Start(const AutomationCommand& command,
		const ObservationSnapshot& observation)
	{
		Reset();
		ValidationResult observationValidation = ValidateObservationSnapshot(observation);
		if (!observationValidation)
			return observationValidation;
		ValidationResult commandValidation = ValidateCommandForSnapshot(
			command, observation.Tick, observation.Revision);
		if (!commandValidation)
			return commandValidation;
		if (command.Kind != CommandKind::Wait && command.Kind != CommandKind::AcquireItem &&
			command.Kind != CommandKind::Interact)
			return { ValidationStatus::InvalidCommandKind,
				"interaction controller supports only wait, acquire-item, and interact" };

		Command = command;
		PlayerIdentity = observation.PlayerIdentity;
		LastTick = observation.Tick;
		LastRevision = observation.Revision;
		if (command.Kind == CommandKind::AcquireItem || command.Kind == CommandKind::Interact)
		{
			TargetResolutionResult resolved = ResolveTarget(*command.Target, observation.Targets,
				observation.Revision, InteractionRequirements(command.Kind));
			if (!resolved)
				return { ValidationStatus::InvalidTarget, resolved.Error };
			BaselineTarget = *resolved.Target;
			BaselineOwnedClassCount = OwnedClassCount(observation);
			if (BaselineTarget.InventoryResource)
			{
				BaselineResourceKind = BaselineTarget.InventoryResource->Kind;
				BaselineOwnedResourceTotal = OwnedClassResourceTotal(
					observation, *BaselineResourceKind);
			}
		}
		Active = true;
		return {};
	}

	InteractionActionStep InteractionActionController::Update(const ObservationSnapshot& observation)
	{
		if (!Active)
			return { CommandState::Failed, observation.Tick, false, "interaction controller is inactive" };
		ValidationResult validation = ValidateObservationSnapshot(observation);
		if (!validation)
			return Finish(CommandState::Failed, observation.Tick,
				"interaction observation is invalid: " + validation.Error);
		if (observation.Tick < LastTick)
			return Finish(CommandState::Failed, observation.Tick,
				"interaction observation tick is out of order");
		if (observation.Revision <= LastRevision)
			return Finish(CommandState::Failed, observation.Tick,
				"interaction observation revision is stale");
		if (observation.PlayerIdentity != PlayerIdentity)
			return Finish(CommandState::Failed, observation.Tick,
				"interaction observation belongs to another player");

		LastTick = observation.Tick;
		LastRevision = observation.Revision;
		if (observation.Tick > Command.DeadlineTick)
			return Finish(CommandState::TimedOut, observation.Tick, "interaction command deadline passed");

		if (Command.Kind == CommandKind::Wait)
		{
			if (observation.Tick >= Command.IssuedTick + Command.WaitTicks)
				return Finish(CommandState::Succeeded, observation.Tick, "bounded wait completed");
			return { CommandState::Running, observation.Tick, false, "bounded wait in progress" };
		}

		const TargetSnapshot* target = FindTarget(observation, BaselineTarget.Identity);
		if (!InteractionRequested)
		{
			if (!target || target->Deleted || target->ClassName != BaselineTarget.ClassName)
				return Finish(CommandState::Failed, observation.Tick,
					"exact target is no longer available for stock interaction");
			if (Command.Kind == CommandKind::AcquireItem && !target->Acquirable)
				return Finish(CommandState::Failed, observation.Tick,
					"exact target is no longer eligible for acquisition");
			if (!target->Interactable)
				return { CommandState::Running, observation.Tick, false,
					"waiting for the exact target to enter stock interaction range" };
			BaselineEventReceivers.clear();
			if (!target->EventName.empty())
			{
				for (const TargetSnapshot& candidate : observation.Targets)
				{
					if (candidate.Identity != target->Identity &&
						candidate.TagName == target->EventName)
						BaselineEventReceivers.push_back(candidate);
				}
			}
			InteractionRequested = true;
			return { CommandState::Running, observation.Tick, true,
				"stock interaction input requested" };
		}

		if (!target)
			return Finish(CommandState::Failed, observation.Tick,
				"exact target disappeared without an observable tombstone");
		if (Command.Kind == CommandKind::AcquireItem)
		{
			if (!target->Deleted && target->OwnerIdentity == PlayerIdentity)
				return Finish(CommandState::Succeeded, observation.Tick,
					"exact target ownership transferred to the player");
			if (target->Deleted && BaselineTarget.InventoryResource)
			{
				// A resource-bearing target must prove its exact same-kind aggregate
				// delta. Falling back to actor count would accept a replacement with
				// the wrong amount, resource kind, or no resource at all.
				if (BaselineResourceKind && BaselineOwnedResourceTotal)
				{
					const std::optional<uint64_t> currentResourceTotal =
						OwnedClassResourceTotal(observation, *BaselineResourceKind);
					const uint64_t expectedResourceTotal = *BaselineOwnedResourceTotal +
						BaselineTarget.InventoryResource->Value;
					if (currentResourceTotal &&
						*currentResourceTotal == expectedResourceTotal)
					{
						return Finish(CommandState::Succeeded, observation.Tick,
							std::string("target tombstone and matching ") +
							InventoryResourceKindName(*BaselineResourceKind) +
							" increase observed from " +
							std::to_string(*BaselineOwnedResourceTotal) + " to " +
							std::to_string(*currentResourceTotal));
					}
				}
			}
			else if (target->Deleted &&
				OwnedClassCount(observation) > BaselineOwnedClassCount)
			{
				return Finish(CommandState::Succeeded, observation.Tick,
					"target tombstone and matching inventory increase observed");
			}
			return { CommandState::Running, observation.Tick, false,
				"waiting for an inventory or ownership transition" };
		}

		if (target->Deleted || target->OwnerIdentity != BaselineTarget.OwnerIdentity ||
			target->StateToken != BaselineTarget.StateToken)
			return Finish(CommandState::Succeeded, observation.Tick,
				"target state transition observed after stock interaction");
		for (const TargetSnapshot& baseline : BaselineEventReceivers)
		{
			const TargetSnapshot* receiver = FindTarget(observation, baseline.Identity);
			if (receiver && HasTransitioned(baseline, *receiver))
				return Finish(CommandState::Succeeded, observation.Tick,
					"target event receiver transition observed after stock interaction");
		}
		return { CommandState::Running, observation.Tick, false,
			"waiting for a target or correlated event receiver transition" };
	}

	ValidationResult InteractionActionController::Abort(const AutomationCommand& abortCommand,
		uint64_t currentTick, uint64_t observationRevision,
		InteractionActionStep& cancelledStep)
	{
		if (!Active)
			return { ValidationStatus::InvalidAbort, "no active command can be aborted" };
		ValidationResult validation = ValidateCommandForSnapshot(
			abortCommand, currentTick, observationRevision);
		if (!validation)
			return validation;
		if (abortCommand.Kind != CommandKind::Abort || abortCommand.AbortCommandId != Command.Id)
			return { ValidationStatus::InvalidAbort, "abort command does not name the active command" };
		cancelledStep = Finish(CommandState::Cancelled, currentTick,
			"active command cancelled by bounded abort");
		return {};
	}

	void InteractionActionController::Reset()
	{
		Active = false;
		InteractionRequested = false;
		Command = {};
		BaselineTarget = {};
		PlayerIdentity.clear();
		BaselineOwnedClassCount = 0;
		BaselineResourceKind.reset();
		BaselineOwnedResourceTotal.reset();
		BaselineEventReceivers.clear();
		LastTick = 0;
		LastRevision = 0;
	}

	InteractionActionStep InteractionActionController::Finish(CommandState state,
		uint64_t tick, std::string reason)
	{
		InteractionActionStep step{ state, tick, false, std::move(reason) };
		Reset();
		return step;
	}

	size_t InteractionActionController::OwnedClassCount(
		const ObservationSnapshot& observation) const
	{
		return static_cast<size_t>(std::count_if(observation.Targets.begin(),
			observation.Targets.end(), [&](const TargetSnapshot& target)
			{
				return !target.Deleted && target.OwnerIdentity == PlayerIdentity &&
					target.ClassName == BaselineTarget.ClassName;
			}));
	}

	std::optional<uint64_t> InteractionActionController::OwnedClassResourceTotal(
		const ObservationSnapshot& observation,
		TargetSnapshot::InventoryResourceKind kind) const
	{
		uint64_t total = 0;
		for (const TargetSnapshot& target : observation.Targets)
		{
			if (target.Deleted || target.OwnerIdentity != PlayerIdentity ||
				target.ClassName != BaselineTarget.ClassName)
				continue;
			if (!target.InventoryResource || target.InventoryResource->Kind != kind)
				return std::nullopt;
			total += target.InventoryResource->Value;
		}
		return total;
	}
}
