#include "Automation/AutomationProtocol.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Automation;

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

	void Check(const ValidationResult& result, const std::string& message)
	{
		Check(static_cast<bool>(result), message + (result.Error.empty() ? std::string() : ": " + result.Error));
	}

	AutomationCommand WalkPoint()
	{
		AutomationCommand command;
		command.Id = "walk.training.reception";
		command.Kind = CommandKind::WalkToPoint;
		command.IssuedTick = 100;
		command.DeadlineTick = 700;
		command.Point = WorldPoint{ 352.0, 576.0, 40.0 };
		command.ArrivalRadius = 32.0;
		return command;
	}

	AutomationCommand ActorCommand(CommandKind kind)
	{
		AutomationCommand command;
		command.Id = "target.nano-key";
		command.Kind = kind;
		command.IssuedTick = 200;
		command.DeadlineTick = 800;
		command.Target = TargetSelector{ 17, "actor:NanoKey0", "DeusEx.NanoKey" };
		command.ArrivalRadius = 64.0;
		return command;
	}

	TargetSnapshot NanoKey()
	{
		TargetSnapshot target;
		target.ObservationRevision = 17;
		target.Identity = "actor:NanoKey0";
		target.ClassName = "DeusEx.NanoKey";
		target.Location = { 415.0, 1072.0, 32.0 };
		target.Reachable = true;
		target.Interactable = true;
		target.Acquirable = true;
		return target;
	}

	void TestCommandValidation()
	{
		AutomationCommand point = WalkPoint();
		Check(ValidateCommandStructure(point), "valid walk-to-point structure");
		Check(ValidateCommandForSnapshot(point, 100, 1), "valid walk-to-point snapshot validation");

		point.Id = "bad command";
		Check(ValidateCommandStructure(point).Status == ValidationStatus::InvalidCommandId,
			"command IDs reject whitespace");
		point = WalkPoint();
		point.DeadlineTick = point.IssuedTick;
		Check(ValidateCommandStructure(point).Status == ValidationStatus::InvalidTickWindow,
			"command requires a positive tick window");
		point = WalkPoint();
		point.DeadlineTick = point.IssuedTick + MaximumCommandLifetimeTicks + 1;
		Check(ValidateCommandStructure(point).Status == ValidationStatus::InvalidTickWindow,
			"command lifetime is bounded");
		point = WalkPoint();
		point.Point->X = std::numeric_limits<double>::quiet_NaN();
		Check(ValidateCommandStructure(point).Status == ValidationStatus::InvalidPoint,
			"walk-to-point rejects non-finite coordinates");
		point = WalkPoint();
		point.ArrivalRadius = MaximumArrivalRadius + 1.0;
		Check(ValidateCommandStructure(point).Status == ValidationStatus::InvalidArrivalRadius,
			"arrival radius is bounded");

		AutomationCommand actor = ActorCommand(CommandKind::WalkToActor);
		Check(ValidateCommandForSnapshot(actor, 200, 17), "valid actor command");
		Check(ValidateCommandForSnapshot(actor, 199, 17).Status == ValidationStatus::CommandNotYetIssued,
			"future command is rejected");
		Check(ValidateCommandForSnapshot(actor, 801, 17).Status == ValidationStatus::CommandExpired,
			"expired command is rejected");
		Check(ValidateCommandForSnapshot(actor, 200, 18).Status == ValidationStatus::StaleObservation,
			"stale actor observation is rejected");
		actor.Point = WorldPoint{};
		Check(ValidateCommandStructure(actor).Status == ValidationStatus::InvalidTarget,
			"actor command rejects mixed target forms");

		AutomationCommand wait;
		wait.Id = "wait.datalink";
		wait.Kind = CommandKind::Wait;
		wait.IssuedTick = 10;
		wait.DeadlineTick = 20;
		wait.WaitTicks = 10;
		Check(ValidateCommandStructure(wait), "valid wait command");
		wait.WaitTicks = 11;
		Check(ValidateCommandStructure(wait).Status == ValidationStatus::InvalidWait,
			"wait cannot outlive command deadline");

		AutomationCommand abort;
		abort.Id = "abort.current";
		abort.Kind = CommandKind::Abort;
		abort.IssuedTick = 10;
		abort.DeadlineTick = 11;
		abort.AbortCommandId = "walk.training.reception";
		Check(ValidateCommandStructure(abort), "valid abort command");
		abort.AbortCommandId = abort.Id;
		Check(ValidateCommandStructure(abort).Status == ValidationStatus::InvalidAbort,
			"abort cannot target itself");
	}

	void TestTargetResolution()
	{
		TargetSnapshot key = NanoKey();
		std::vector<TargetSnapshot> snapshots{ key };
		AutomationCommand acquire = ActorCommand(CommandKind::AcquireItem);
		TargetResolutionResult resolved = ResolveTargetForCommand(acquire, snapshots, 17);
		Check(resolved && resolved.Target->Identity == key.Identity, "acquirable target resolves exactly");

		Check(ResolveTarget(*acquire.Target, snapshots, 18, TargetRequirement::None).Status ==
			TargetResolutionStatus::StaleSelector, "stale selector is rejected");
		snapshots.front().ObservationRevision = 16;
		Check(ResolveTarget(*acquire.Target, snapshots, 17, TargetRequirement::None).Status ==
			TargetResolutionStatus::SnapshotRevisionMismatch, "wrong snapshot revision is distinguished");
		snapshots.clear();
		Check(ResolveTarget(*acquire.Target, snapshots, 17, TargetRequirement::None).Status ==
			TargetResolutionStatus::Missing, "missing target is rejected");

		snapshots = { key, key };
		Check(ResolveTarget(*acquire.Target, snapshots, 17, TargetRequirement::None).Status ==
			TargetResolutionStatus::Ambiguous, "duplicate identity is rejected");
		snapshots = { key };
		acquire.Target->ExpectedClass = "DeusEx.Lockpick";
		Check(ResolveTargetForCommand(acquire, snapshots, 17).Status == TargetResolutionStatus::ClassMismatch,
			"class mismatch is rejected");

		acquire = ActorCommand(CommandKind::AcquireItem);
		snapshots.front().Reachable = false;
		Check(ResolveTargetForCommand(acquire, snapshots, 17).Status == TargetResolutionStatus::Unreachable,
			"acquire requires reachability");
		snapshots.front().Reachable = true;
		snapshots.front().Acquirable = false;
		Check(ResolveTargetForCommand(acquire, snapshots, 17).Status == TargetResolutionStatus::NotAcquirable,
			"acquire requires acquisition capability");

		AutomationCommand interact = ActorCommand(CommandKind::Interact);
		snapshots.front() = key;
		snapshots.front().Interactable = false;
		Check(ResolveTargetForCommand(interact, snapshots, 17).Status == TargetResolutionStatus::NotInteractable,
			"interact requires interaction capability");
		snapshots.front().Deleted = true;
		Check(ResolveTargetForCommand(interact, snapshots, 17).Status == TargetResolutionStatus::Missing,
			"deleted target is absent");
	}

	void TestResultsAndSerialization()
	{
		CommandResult running;
		running.CommandId = "walk.training.reception";
		running.Kind = CommandKind::WalkToPoint;
		running.State = CommandState::Running;
		running.Tick = 150;
		running.ObservationRevision = 17;
		Check(ValidateCommandResult(running), "running result is valid");
		Check(!IsTerminal(running.State), "running state is non-terminal");

		CommandResult failed = running;
		failed.State = CommandState::Failed;
		Check(ValidateCommandResult(failed).Status == ValidationStatus::InvalidResult,
			"failed result requires a reason");
		failed.Reason = "no route to target";
		Check(ValidateCommandResult(failed) && IsTerminal(failed.State), "reasoned failure is terminal and valid");

		AutomationCommand command = WalkPoint();
		const std::string json = CommandJson(command);
		Check(json.find("\"schema\":\"surreal-automation-command-v1\"") != std::string::npos,
			"command schema is explicit");
		Check(json.find("\"kind\":\"walk_to_point\"") != std::string::npos,
			"command kind is canonical");
		Check(CommandDigest(command) == CommandDigest(command), "command digest is stable");
		AutomationCommand changed = command;
		changed.Point->X += 1.0;
		Check(CommandDigest(command) != CommandDigest(changed), "command digest changes with content");

		const std::string resultJson = CommandResultJson(failed);
		Check(resultJson.find("\"state\":\"failed\"") != std::string::npos,
			"result state is canonical");
		bool threw = false;
		try
		{
			CommandResult invalid = failed;
			invalid.Reason.assign(MaximumDetailBytes + 1, 'x');
			(void)CommandResultJson(invalid);
		}
		catch (const std::invalid_argument&)
		{
			threw = true;
		}
		Check(threw, "invalid result is not serialized");
	}

	void TestObservationSnapshots()
	{
		ObservationSnapshot observation;
		observation.Revision = 17;
		observation.Tick = 200;
		observation.PlayerIdentity = "actor:JCDentonMale0";
		observation.PlayerPosition = { 100.0, 200.0, 30.0 };
		TargetSnapshot key = NanoKey();
		key.TagName = "TrainingKey";
		key.EventName = "TrainingDoor";
		TargetSnapshot lockpick = key;
		lockpick.Identity = "actor:Lockpick0";
		lockpick.ClassName = "DeusEx.Lockpick";
		lockpick.OwnerIdentity = observation.PlayerIdentity;
		lockpick.InventoryResource = TargetSnapshot::InventoryResourceSnapshot{
			TargetSnapshot::InventoryResourceKind::NumCopies, 2 };
		lockpick.Location = observation.PlayerPosition;
		observation.Targets = { key, lockpick };
		Check(ValidateObservationSnapshot(observation), "bounded observation snapshot is valid");

		const std::string json = ObservationSnapshotJson(observation);
		Check(json.find("\"schema\":\"surreal-automation-observation-v2\"") != std::string::npos &&
			json.find("\"owner_identity\":\"actor:JCDentonMale0\"") != std::string::npos &&
			json.find("\"inventory_resource\":{\"kind\":\"num_copies\",\"value\":\"2\"}") !=
				std::string::npos &&
			json.find("\"tag\":\"TrainingKey\"") != std::string::npos &&
			json.find("\"event\":\"TrainingDoor\"") != std::string::npos,
			"observation serialization includes schema, ownership, tag, and event");
		ObservationSnapshot reordered = observation;
		std::swap(reordered.Targets[0], reordered.Targets[1]);
		Check(ObservationSnapshotDigest(observation) == ObservationSnapshotDigest(reordered),
			"observation digest is independent of engine actor iteration order");

		ObservationSnapshot invalid = observation;
		invalid.Targets[1].Identity = invalid.Targets[0].Identity;
		Check(ValidateObservationSnapshot(invalid).Status == ValidationStatus::InvalidTarget,
			"observation rejects duplicate actor identity");
		invalid = observation;
		invalid.Targets[0].ObservationRevision--;
		Check(ValidateObservationSnapshot(invalid).Status == ValidationStatus::InvalidTarget,
			"observation rejects mixed revisions");
		invalid = observation;
		invalid.Targets[0].OwnerIdentity = "owner with spaces";
		Check(ValidateObservationSnapshot(invalid).Status == ValidationStatus::InvalidTarget,
			"observation rejects invalid owner identity");
		invalid = observation;
		invalid.Targets[1].InventoryResource->Value = MaximumInventoryResourceValue + 1;
		Check(ValidateObservationSnapshot(invalid).Status == ValidationStatus::InvalidTarget,
			"observation rejects an unbounded inventory resource");
		invalid = observation;
		invalid.Targets[1].InventoryResource->Kind =
			static_cast<TargetSnapshot::InventoryResourceKind>(99);
		Check(ValidateObservationSnapshot(invalid).Status == ValidationStatus::InvalidTarget,
			"observation rejects an unknown inventory resource kind");
		invalid = observation;
		invalid.Targets[0].EventName = "event with spaces";
		Check(ValidateObservationSnapshot(invalid).Status == ValidationStatus::InvalidTarget,
			"observation rejects invalid event names");
		invalid = observation;
		invalid.Targets.resize(MaximumObservationTargets + 1, key);
		Check(ValidateObservationSnapshot(invalid).Status == ValidationStatus::InvalidTarget,
			"observation target count is bounded");
	}

	void TestTelemetry()
	{
		TelemetryEvent issued;
		issued.Sequence = 1;
		issued.Tick = 100;
		issued.ObservationRevision = 17;
		issued.ConfigIdentity = "fnv1a64:0123456789abcdef";
		issued.Type = TelemetryEventType::CommandIssued;
		issued.CommandId = "walk.training.reception";
		issued.Kind = CommandKind::WalkToPoint;
		issued.Detail = "validated input";
		Check(ValidateTelemetryEvent(issued), "command-issued telemetry is valid");
		const std::string issuedJson = TelemetryEventJson(issued);
		Check(issuedJson.find("\"event\":\"command_issued\"") != std::string::npos,
			"telemetry event name is canonical");
		Check(issuedJson.find("\"state\":null") != std::string::npos,
			"telemetry omits inapplicable state explicitly");

		TelemetryEvent accepted = issued;
		accepted.Sequence = 2;
		accepted.Type = TelemetryEventType::CommandAccepted;
		accepted.State = CommandState::Accepted;
		Check(ValidateTelemetryEvent(accepted), "accepted telemetry is valid");
		accepted.State = CommandState::Running;
		Check(ValidateTelemetryEvent(accepted).Status == ValidationStatus::InvalidTelemetry,
			"accepted event rejects inconsistent state");

		TelemetryEvent progress = issued;
		progress.Sequence = 3;
		progress.Type = TelemetryEventType::CommandProgress;
		progress.State = CommandState::Running;
		progress.Position = WorldPoint{ 100.0, 200.0, 30.0 };
		progress.Distance = 42.5;
		Check(ValidateTelemetryEvent(progress), "bounded progress telemetry is valid");
		progress.Distance = std::numeric_limits<double>::infinity();
		Check(ValidateTelemetryEvent(progress).Status == ValidationStatus::InvalidTelemetry,
			"telemetry rejects non-finite distance");

		TelemetryEvent capture = issued;
		capture.Sequence = 4;
		capture.Type = TelemetryEventType::VisualCapturePublished;
		capture.State = CommandState::Running;
		capture.TargetIdentity = "actor:DeusEx.Switch1:Switch1#0";
		Check(ValidateTelemetryEvent(capture),
			"visual capture publication telemetry is valid");
		Check(TelemetryEventJson(capture).find(
			"\"event\":\"visual_capture_published\"") != std::string::npos,
			"visual capture publication event name is canonical");
		capture.TargetIdentity.clear();
		Check(ValidateTelemetryEvent(capture).Status == ValidationStatus::InvalidTelemetry,
			"visual capture publication requires its exact target");

		TelemetryEvent result = issued;
		result.Sequence = 5;
		result.Type = TelemetryEventType::CommandResult;
		result.State = CommandState::Succeeded;
		Check(ValidateTelemetryEvent(result), "terminal result telemetry is valid");
		result.State = CommandState::Running;
		Check(ValidateTelemetryEvent(result).Status == ValidationStatus::InvalidTelemetry,
			"result telemetry rejects non-terminal state");

		TelemetryEvent target = issued;
		target.Sequence = 5;
		target.Type = TelemetryEventType::TargetResolved;
		Check(ValidateTelemetryEvent(target).Status == ValidationStatus::InvalidTelemetry,
			"target resolution requires stable target identity");
		target.TargetIdentity = "actor:NanoKey0";
		Check(ValidateTelemetryEvent(target), "target resolution telemetry is valid");
	}
}

int main()
{
	TestCommandValidation();
	TestTargetResolution();
	TestResultsAndSerialization();
	TestObservationSnapshots();
	TestTelemetry();
	if (Failures == 0)
		std::cout << "All automation protocol tests passed.\n";
	return Failures == 0 ? 0 : 1;
}
