#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Automation
{
	static constexpr size_t MaximumCommandIdBytes = 64;
	static constexpr size_t MaximumTargetIdentityBytes = 128;
	static constexpr size_t MaximumClassNameBytes = 128;
	static constexpr size_t MaximumDetailBytes = 256;
	static constexpr size_t MaximumObservationTargets = 256;
	static constexpr uint32_t MaximumInventoryResourceValue = 1000000;
	static constexpr uint64_t MaximumCommandLifetimeTicks = 1000000;
	static constexpr double MaximumCoordinateMagnitude = 1000000000.0;
	static constexpr double MaximumArrivalRadius = 65536.0;

	enum class CommandKind
	{
		WalkToPoint,
		WalkToActor,
		AcquireItem,
		Interact,
		Wait,
		Abort
	};

	enum class CommandState
	{
		Rejected,
		Accepted,
		Running,
		Succeeded,
		Failed,
		TimedOut,
		Cancelled
	};

	enum class ValidationStatus
	{
		Valid,
		InvalidCommandKind,
		InvalidCommandState,
		InvalidCommandId,
		InvalidTickWindow,
		CommandNotYetIssued,
		CommandExpired,
		StaleObservation,
		InvalidTarget,
		InvalidPoint,
		InvalidArrivalRadius,
		InvalidWait,
		InvalidAbort,
		InvalidResult,
		InvalidTelemetry
	};

	struct WorldPoint
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
	};

	struct TargetSelector
	{
		uint64_t ObservationRevision = 0;
		std::string Identity;
		std::string ExpectedClass;
	};

	struct AutomationCommand
	{
		std::string Id;
		CommandKind Kind = CommandKind::Wait;
		uint64_t IssuedTick = 0;
		uint64_t DeadlineTick = 0;
		std::optional<TargetSelector> Target;
		std::optional<WorldPoint> Point;
		std::optional<double> ArrivalRadius;
		uint64_t WaitTicks = 0;
		std::string AbortCommandId;
	};

	struct ValidationResult
	{
		ValidationStatus Status = ValidationStatus::Valid;
		std::string Error;

		explicit operator bool() const { return Status == ValidationStatus::Valid; }
	};

	ValidationResult ValidateCommandStructure(const AutomationCommand& command);
	ValidationResult ValidateCommandForSnapshot(const AutomationCommand& command,
		uint64_t currentTick, uint64_t observationRevision);

	struct TargetSnapshot
	{
		enum class InventoryResourceKind
		{
			AmmoAmount,
			NumCopies
		};

		struct InventoryResourceSnapshot
		{
			InventoryResourceKind Kind = InventoryResourceKind::AmmoAmount;
			uint32_t Value = 0;
		};

		uint64_t ObservationRevision = 0;
		std::string Identity;
		std::string ClassName;
		std::string OwnerIdentity;
		std::string StateToken;
		std::string TagName;
		std::string EventName;
		std::optional<InventoryResourceSnapshot> InventoryResource;
		WorldPoint Location;
		bool Deleted = false;
		bool Reachable = false;
		bool Interactable = false;
		bool Acquirable = false;
	};

	struct ObservationSnapshot
	{
		uint64_t Revision = 0;
		uint64_t Tick = 0;
		std::string PlayerIdentity;
		WorldPoint PlayerPosition;
		std::vector<TargetSnapshot> Targets;
	};

	ValidationResult ValidateObservationSnapshot(const ObservationSnapshot& observation);
	std::string ObservationSnapshotJson(const ObservationSnapshot& observation);
	std::string ObservationSnapshotDigest(const ObservationSnapshot& observation);

	enum class TargetRequirement : uint32_t
	{
		None = 0,
		Reachable = 1,
		Interactable = 2,
		Acquirable = 4
	};

	enum class TargetResolutionStatus
	{
		Resolved,
		InvalidSelector,
		StaleSelector,
		SnapshotRevisionMismatch,
		Missing,
		Ambiguous,
		ClassMismatch,
		Unreachable,
		NotInteractable,
		NotAcquirable
	};

	struct TargetResolutionResult
	{
		TargetResolutionStatus Status = TargetResolutionStatus::Missing;
		std::optional<TargetSnapshot> Target;
		std::string Error;

		explicit operator bool() const { return Status == TargetResolutionStatus::Resolved && Target.has_value(); }
	};

	TargetResolutionResult ResolveTarget(const TargetSelector& selector,
		const std::vector<TargetSnapshot>& snapshots, uint64_t observationRevision,
		TargetRequirement requirements);
	TargetResolutionResult ResolveTargetForCommand(const AutomationCommand& command,
		const std::vector<TargetSnapshot>& snapshots, uint64_t observationRevision);

	struct CommandResult
	{
		std::string CommandId;
		CommandKind Kind = CommandKind::Wait;
		CommandState State = CommandState::Rejected;
		uint64_t Tick = 0;
		uint64_t ObservationRevision = 0;
		std::string TargetIdentity;
		std::string Reason;
	};

	bool IsTerminal(CommandState state);
	ValidationResult ValidateCommandResult(const CommandResult& result);

	enum class TelemetryEventType
	{
		CommandIssued,
		CommandAccepted,
		TargetResolved,
		CommandProgress,
		VisualCapturePublished,
		InteractionAttempt,
		CommandResult
	};

	struct TelemetryEvent
	{
		uint64_t Sequence = 0;
		uint64_t Tick = 0;
		uint64_t ObservationRevision = 0;
		std::string ConfigIdentity;
		TelemetryEventType Type = TelemetryEventType::CommandIssued;
		std::string CommandId;
		CommandKind Kind = CommandKind::Wait;
		std::optional<CommandState> State;
		std::string TargetIdentity;
		std::string Detail;
		std::optional<WorldPoint> Position;
		std::optional<double> Distance;
	};

	ValidationResult ValidateTelemetryEvent(const TelemetryEvent& event);
	std::string CommandJson(const AutomationCommand& command);
	std::string CommandDigest(const AutomationCommand& command);
	std::string CommandResultJson(const CommandResult& result);
	std::string TelemetryEventJson(const TelemetryEvent& event);

	const char* CommandKindName(CommandKind kind);
	const char* CommandStateName(CommandState state);
	const char* TelemetryEventTypeName(TelemetryEventType type);
}
