#include "ShadowReplayFixture.h"

#include "Automation/InteractionActionController.h"
#include "Automation/PlayerMovementController.h"
#include "Input/InputComposition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Automation::TestFixtures
{
	namespace
	{
		static constexpr double TurnRateRadiansPerSecond = 3.015928947446201;

		class FixtureInputTarget final : public InputCommandTarget
		{
		public:
			void InputCommand(const std::string& command,
				InputControlId control, float delta) override
			{
				if (command.find("aBaseY") != std::string::npos)
					Composition.SetAxis("aBaseY", control, 7000.0f * delta);
				else if (command.find("aStrafe") != std::string::npos)
					Composition.SetAxis("aStrafe", control, 7000.0f * delta);
				else if (command.find("aTurn") != std::string::npos)
					Composition.SetAxis("aTurn", control, 4096.0f * delta);
			}

			void ReleaseInputControl(InputControlId control) override
			{
				Composition.ReleaseControl(control);
			}

			void ReleaseInputSource(InputSourceId source) override
			{
				Composition.ReleaseSource(source);
			}

			InputComposition Composition;
		};

		class TelemetryRecorder
		{
		public:
			explicit TelemetryRecorder(ShadowReplayFixtureRun& run) : Run(run) { }

			void Emit(uint64_t tick, uint64_t revision, TelemetryEventType type,
				const AutomationCommand& command, std::optional<CommandState> state,
				std::string targetIdentity, std::string detail,
				std::optional<WorldPoint> position = {},
				std::optional<double> distance = {})
			{
				TelemetryEvent event;
				event.Sequence = ++Sequence;
				event.Tick = tick;
				event.ObservationRevision = revision;
				event.ConfigIdentity = "shadow-replay-fixture-v1";
				event.Type = type;
				event.CommandId = command.Id;
				event.Kind = command.Kind;
				event.State = state;
				event.TargetIdentity = std::move(targetIdentity);
				event.Detail = std::move(detail);
				event.Position = position;
				event.Distance = distance;
				ValidationResult validation = ValidateTelemetryEvent(event);
				if (!validation)
					throw std::logic_error(validation.Error);
				Run.TelemetryJson += TelemetryEventJson(event);
				Run.Telemetry.push_back(std::move(event));
			}

		private:
			ShadowReplayFixtureRun& Run;
			uint64_t Sequence = 0;
		};

		std::string TargetIdentity(const AutomationCommand& command)
		{
			return command.Target ? command.Target->Identity : std::string();
		}

		bool HasSyntheticMovementInput(const PlayerMovementInputAdapter& adapter,
			const FixtureInputTarget& input)
		{
			return adapter.IsActive() ||
				std::abs(input.Composition.GetAxisValue("aBaseY")) > 0.0001f ||
				std::abs(input.Composition.GetAxisValue("aStrafe")) > 0.0001f ||
				std::abs(input.Composition.GetAxisValue("aTurn")) > 0.0001f;
		}

		TargetSnapshot* FindTarget(ObservationSnapshot& observation,
			const std::string& identity)
		{
			auto it = std::find_if(observation.Targets.begin(), observation.Targets.end(),
				[&](const TargetSnapshot& target) { return target.Identity == identity; });
			return it == observation.Targets.end() ? nullptr : &*it;
		}

		const TargetSnapshot* FindTarget(const ObservationSnapshot& observation,
			const std::string& identity)
		{
			auto it = std::find_if(observation.Targets.begin(), observation.Targets.end(),
				[&](const TargetSnapshot& target) { return target.Identity == identity; });
			return it == observation.Targets.end() ? nullptr : &*it;
		}

		std::string ValidateStockTransitionConfig(
			const ShadowReplayFixtureConfig& config,
			const AutomationCommand& command,
			const ObservationSnapshot& observation,
			const TargetSnapshot* target)
		{
			using Transition = ShadowReplayFixtureStockTransition;
			const bool supported =
				config.StockTransition == Transition::CommandDefault ||
				config.StockTransition == Transition::None ||
				config.StockTransition == Transition::AcquireTombstoneExactResourceDelta ||
				config.StockTransition == Transition::AcquireTombstoneNoResourceDelta ||
				config.StockTransition == Transition::InteractEventReceiverMovement ||
				config.StockTransition == Transition::InteractEventReceiverSubthresholdJitter ||
				config.StockTransition == Transition::InteractUnrelatedActorMovement;
			if (!supported)
				return "stock transition mode is unknown";
			if (config.StockTransition == Transition::CommandDefault ||
				config.StockTransition == Transition::None)
			{
				return config.AuxiliaryTargetIdentity.empty() ? std::string() :
					"default/none stock transition must not name an auxiliary actor";
			}
			if (!target || config.AuxiliaryTargetIdentity.empty())
				return "explicit stock transition requires a target and auxiliary actor";
			const TargetSnapshot* auxiliary = FindTarget(
				observation, config.AuxiliaryTargetIdentity);
			if (!auxiliary || auxiliary->Deleted || auxiliary->Identity == target->Identity)
				return "explicit stock transition auxiliary actor is missing or invalid";

			const bool resourceMode =
				config.StockTransition == Transition::AcquireTombstoneExactResourceDelta ||
				config.StockTransition == Transition::AcquireTombstoneNoResourceDelta;
			if (resourceMode)
			{
				if (command.Kind != CommandKind::AcquireItem || !target->InventoryResource ||
					target->InventoryResource->Value == 0)
					return "resource transition requires a nonzero acquire-item resource target";
				if (auxiliary->OwnerIdentity != observation.PlayerIdentity ||
					auxiliary->ClassName != target->ClassName || !auxiliary->InventoryResource ||
					auxiliary->InventoryResource->Kind != target->InventoryResource->Kind)
					return "resource transition auxiliary actor is not the matching owned stack";
				const size_t ownedClassCount = static_cast<size_t>(std::count_if(
					observation.Targets.begin(), observation.Targets.end(),
					[&](const TargetSnapshot& candidate)
					{
						return !candidate.Deleted &&
							candidate.OwnerIdentity == observation.PlayerIdentity &&
							candidate.ClassName == target->ClassName;
					}));
				if (ownedClassCount != 1)
					return "resource transition requires exactly one owned matching-class stack";
				const uint64_t merged = static_cast<uint64_t>(
					auxiliary->InventoryResource->Value) + target->InventoryResource->Value;
				if (merged > MaximumInventoryResourceValue)
					return "resource transition would overflow its bounded snapshot value";
				return {};
			}

			if (command.Kind != CommandKind::Interact || target->EventName.empty())
				return "receiver transition requires an interact target with an event binding";
			const size_t correlatedCount = static_cast<size_t>(std::count_if(
				observation.Targets.begin(), observation.Targets.end(),
				[&](const TargetSnapshot& candidate)
				{
					return candidate.Identity != target->Identity && !candidate.Deleted &&
						candidate.TagName == target->EventName;
				}));
			if (correlatedCount != 1)
				return "receiver transition requires exactly one correlated event receiver";
			const bool unrelated =
				config.StockTransition == Transition::InteractUnrelatedActorMovement;
			if ((auxiliary->TagName == target->EventName) == unrelated)
				return unrelated ? "unrelated transition auxiliary actor is correlated" :
					"receiver transition auxiliary actor is not correlated";
			return {};
		}

		CommandResult MakeResult(const AutomationCommand& command, CommandState state,
			uint64_t tick, uint64_t revision, std::string reason)
		{
			CommandResult result;
			result.CommandId = command.Id;
			result.Kind = command.Kind;
			result.State = state;
			result.Tick = tick;
			result.ObservationRevision = revision;
			result.TargetIdentity = TargetIdentity(command);
			result.Reason = std::move(reason);
			ValidationResult validation = ValidateCommandResult(result);
			if (!validation)
				throw std::logic_error(validation.Error);
			return result;
		}
	}

	ShadowReplayFixtureRun RunShadowReplayFixture(
		const ShadowReplayCandidate& candidate,
		const ObservationSnapshot& initialObservation,
		std::string commandId,
		uint64_t leaseTicks,
		ShadowReplayFixtureConfig config)
	{
		ShadowReplayFixtureRun run;
		run.FinalPosition = initialObservation.PlayerPosition;
		run.FinalObservation = initialObservation;
		run.Projection = ProjectShadowReplayCandidate(
			candidate, initialObservation, std::move(commandId), leaseTicks);
		if (!run.Projection)
			return run;
		if (leaseTicks > MaximumShadowReplayFixtureTicks ||
			run.Projection.Command->DeadlineTick == std::numeric_limits<uint64_t>::max() ||
			!std::isfinite(config.FixedDelta) || config.FixedDelta <= 0.0 ||
			config.FixedDelta > 1.0 || !std::isfinite(config.MovementSpeed) ||
			config.MovementSpeed <= 0.0 || config.MovementSpeed > 10000.0)
		{
			run.Projection.Status = ShadowReplayStatus::InvalidCommand;
			run.Projection.Error = "shadow replay fixture configuration is invalid or unbounded";
			run.Projection.Command.reset();
			return run;
		}

		const AutomationCommand command = *run.Projection.Command;
		if (config.AbortAtTick &&
			(*config.AbortAtTick <= command.IssuedTick ||
				*config.AbortAtTick > command.DeadlineTick))
		{
			run.Projection.Status = ShadowReplayStatus::InvalidCommand;
			run.Projection.Error = "shadow replay fixture abort tick is outside the command lease";
			run.Projection.Command.reset();
			return run;
		}

		ObservationSnapshot observation = initialObservation;
		auto captureFinalObservation = [&](uint64_t tick)
		{
			observation.Tick = tick;
			observation.PlayerPosition = run.FinalPosition;
			run.FinalObservation = observation;
		};
		PlayerMovementController movement;
		PlayerMovementInputAdapter movementInput;
		InteractionActionController action;
		FixtureInputTarget input;
		TelemetryRecorder telemetry(run);
		const std::string targetIdentity = TargetIdentity(command);
		const TargetSnapshot* resolvedTarget = targetIdentity.empty() ? nullptr :
			FindTarget(initialObservation, targetIdentity);
		const std::string transitionConfigError = ValidateStockTransitionConfig(
			config, command, initialObservation, resolvedTarget);
		if (!transitionConfigError.empty())
		{
			run.Projection.Status = ShadowReplayStatus::InvalidCommand;
			run.Projection.Error = transitionConfigError;
			run.Projection.Command.reset();
			return run;
		}

		telemetry.Emit(observation.Tick, observation.Revision,
			TelemetryEventType::CommandIssued, command, {}, targetIdentity,
			"curated shadow replay command issued to the data-free fixture");
		telemetry.Emit(observation.Tick, observation.Revision,
			TelemetryEventType::CommandAccepted, command, CommandState::Accepted,
			targetIdentity, "curated shadow replay command accepted by the fixture");
		if (resolvedTarget)
			telemetry.Emit(observation.Tick, observation.Revision,
				TelemetryEventType::TargetResolved, command, {}, targetIdentity,
				"exact shadow replay target resolved from the fixture observation");

		if (command.Kind != CommandKind::Wait)
		{
			ValidationResult start = movement.Start(command, *resolvedTarget,
				observation.Tick, observation.Revision);
			if (!start)
				throw std::logic_error(start.Error);
		}
		if (command.Kind != CommandKind::WalkToActor)
		{
			ValidationResult start = action.Start(command, observation);
			if (!start)
				throw std::logic_error(start.Error);
		}

		double yawRadians = 0.0;
		bool proofPending = false;
		bool proofApplied = false;
		for (uint64_t tick = command.IssuedTick + 1;
			tick <= command.DeadlineTick + 1; tick++)
		{
			if (config.AbortAtTick && tick == *config.AbortAtTick)
			{
				AutomationCommand abort;
				abort.Id = "shadow-replay-abort";
				abort.Kind = CommandKind::Abort;
				abort.IssuedTick = tick;
				abort.DeadlineTick = tick + 1;
				abort.AbortCommandId = command.Id;
				telemetry.Emit(tick, observation.Revision, TelemetryEventType::CommandIssued,
					abort, {}, {}, "bounded shadow replay abort issued");
				telemetry.Emit(tick, observation.Revision, TelemetryEventType::CommandAccepted,
					abort, CommandState::Accepted, {}, "bounded shadow replay abort accepted");
				bool cancelled = false;
				if (movement.IsActive())
				{
					ValidationResult result = movement.Abort(
						abort, tick, observation.Revision);
					if (!result)
						throw std::logic_error(result.Error);
					cancelled = true;
				}
				if (action.IsActive())
				{
					InteractionActionStep cancelledStep;
					ValidationResult result = action.Abort(
						abort, tick, observation.Revision, cancelledStep);
					if (!result)
						throw std::logic_error(result.Error);
					cancelled = true;
				}
				if (!cancelled)
					throw std::logic_error("shadow replay abort found no active controller");
				movementInput.Release(input);
				run.Result = MakeResult(command, CommandState::Cancelled, tick,
					observation.Revision, "active command cancelled by bounded fixture abort");
				telemetry.Emit(tick, observation.Revision, TelemetryEventType::CommandResult,
					command, CommandState::Cancelled, targetIdentity, run.Result->Reason);
				run.AbortResult = MakeResult(abort, CommandState::Succeeded, tick,
					observation.Revision, "bounded fixture abort completed");
				telemetry.Emit(tick, observation.Revision, TelemetryEventType::CommandResult,
					abort, CommandState::Succeeded, {}, run.AbortResult->Reason);
				run.SyntheticInputActive = HasSyntheticMovementInput(movementInput, input);
				captureFinalObservation(tick);
				return run;
			}

			if (movement.IsActive())
			{
				MovementStep step = movement.Update({ tick, run.FinalPosition, yawRadians });
				movementInput.Apply(step, input);
				if (step.Status == MovementStatus::Running)
				{
					telemetry.Emit(tick, observation.Revision,
						TelemetryEventType::CommandProgress, command, CommandState::Running,
						targetIdentity, "shadow replay movement in progress",
						run.FinalPosition, step.HorizontalDistance);
					if (!config.FreezeMovement)
					{
						const double forward = input.Composition.GetAxisValue("aBaseY") / 7000.0;
						const double strafe = input.Composition.GetAxisValue("aStrafe") / 7000.0;
						const double turn = input.Composition.GetAxisValue("aTurn") / 4096.0;
						yawRadians += turn * TurnRateRadiansPerSecond * config.FixedDelta;
						const double distance = config.MovementSpeed * config.FixedDelta;
						run.FinalPosition.X +=
							(std::cos(yawRadians) * forward - std::sin(yawRadians) * strafe) * distance;
						run.FinalPosition.Y +=
							(std::sin(yawRadians) * forward + std::cos(yawRadians) * strafe) * distance;
					}
				}
				else if (step.Status == MovementStatus::Arrived)
				{
					if (command.Kind == CommandKind::WalkToActor)
					{
						run.Result = MakeResult(command, CommandState::Succeeded,
							tick, observation.Revision, step.Reason);
						telemetry.Emit(tick, observation.Revision,
							TelemetryEventType::CommandResult, command, CommandState::Succeeded,
							targetIdentity, run.Result->Reason, run.FinalPosition,
							step.HorizontalDistance);
						run.SyntheticInputActive = HasSyntheticMovementInput(movementInput, input);
						captureFinalObservation(tick);
						return run;
					}
				}
				else
				{
					action.Reset();
					const CommandState state = step.Status == MovementStatus::TimedOut ?
						CommandState::TimedOut : CommandState::Failed;
					run.Result = MakeResult(command, state, tick,
						observation.Revision, step.Reason);
					telemetry.Emit(tick, observation.Revision,
						TelemetryEventType::CommandResult, command, state,
						targetIdentity, run.Result->Reason, run.FinalPosition,
						step.HorizontalDistance);
					run.SyntheticInputActive = HasSyntheticMovementInput(movementInput, input);
					captureFinalObservation(tick);
					return run;
				}
			}

			observation.Tick = tick;
			observation.Revision++;
			observation.PlayerPosition = run.FinalPosition;
			for (TargetSnapshot& target : observation.Targets)
				target.ObservationRevision = observation.Revision;
			TargetSnapshot* target = targetIdentity.empty() ? nullptr :
				FindTarget(observation, targetIdentity);
			if (target && command.ArrivalRadius && !proofApplied)
			{
				const double distance = std::hypot(target->Location.X - run.FinalPosition.X,
					target->Location.Y - run.FinalPosition.Y);
				target->Interactable = distance <= *command.ArrivalRadius;
			}
			if (target && proofPending)
			{
				using Transition = ShadowReplayFixtureStockTransition;
				if (config.StockTransition == Transition::CommandDefault &&
					command.Kind == CommandKind::AcquireItem)
				{
					target->OwnerIdentity = observation.PlayerIdentity;
					target->Acquirable = false;
					target->Interactable = false;
				}
				else if (config.StockTransition == Transition::CommandDefault &&
					command.Kind == CommandKind::Interact)
				{
					target->StateToken = "FixtureTransition";
					target->Interactable = false;
				}
				else if (config.StockTransition ==
					Transition::AcquireTombstoneExactResourceDelta ||
					config.StockTransition == Transition::AcquireTombstoneNoResourceDelta)
				{
					TargetSnapshot* owned = FindTarget(
						observation, config.AuxiliaryTargetIdentity);
					target->Deleted = true;
					target->Acquirable = false;
					target->Interactable = false;
					if (config.StockTransition ==
						Transition::AcquireTombstoneExactResourceDelta)
					{
						owned->InventoryResource->Value += target->InventoryResource->Value;
					}
				}
				else
				{
					TargetSnapshot* auxiliary = FindTarget(
						observation, config.AuxiliaryTargetIdentity);
					auxiliary->Location.Y += config.StockTransition ==
						Transition::InteractEventReceiverSubthresholdJitter ? 0.0005 : 64.0;
				}
				run.StockTransitionTick = tick;
				proofPending = false;
				proofApplied = true;
			}

			if (action.IsActive())
			{
				InteractionActionStep step = action.Update(observation);
				if (step.State == CommandState::Running &&
					(command.Kind == CommandKind::Wait || !movement.IsActive()))
				{
					telemetry.Emit(tick, observation.Revision,
						TelemetryEventType::CommandProgress, command, CommandState::Running,
						targetIdentity, step.Reason, run.FinalPosition);
				}
				if (step.RequestInteraction)
				{
					run.InteractionRequests++;
					telemetry.Emit(tick, observation.Revision,
						TelemetryEventType::InteractionAttempt, command, CommandState::Running,
						targetIdentity, step.Reason, run.FinalPosition);
					proofPending = config.StockTransition !=
						ShadowReplayFixtureStockTransition::None;
				}
				if (IsTerminal(step.State))
				{
					movement.Reset();
					movementInput.Release(input);
					run.Result = MakeResult(command, step.State, tick,
						observation.Revision, step.Reason);
					telemetry.Emit(tick, observation.Revision,
						TelemetryEventType::CommandResult, command, step.State,
						targetIdentity, run.Result->Reason, run.FinalPosition);
					run.SyntheticInputActive = HasSyntheticMovementInput(movementInput, input);
					captureFinalObservation(tick);
					return run;
				}
			}
		}

		movementInput.Release(input);
		run.Result = MakeResult(command, CommandState::Failed,
			observation.Tick, observation.Revision,
			"shadow replay fixture ended without a terminal controller result");
		telemetry.Emit(observation.Tick, observation.Revision,
			TelemetryEventType::CommandResult, command, CommandState::Failed,
			targetIdentity, run.Result->Reason, run.FinalPosition);
		run.SyntheticInputActive = HasSyntheticMovementInput(movementInput, input);
		captureFinalObservation(observation.Tick);
		return run;
	}
}
