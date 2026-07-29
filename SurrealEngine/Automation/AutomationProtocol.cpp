#include "AutomationProtocol.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Automation
{
	namespace
	{
		ValidationResult Reject(ValidationStatus status, std::string error)
		{
			return { status, std::move(error) };
		}

		bool IsCommandId(const std::string& value)
		{
			if (value.empty() || value.size() > MaximumCommandIdBytes)
				return false;
			for (unsigned char character : value)
			{
				if ((character >= 'a' && character <= 'z') ||
					(character >= 'A' && character <= 'Z') ||
					(character >= '0' && character <= '9') ||
					character == '-' || character == '_' || character == '.')
					continue;
				return false;
			}
			return true;
		}

		bool IsBoundedToken(const std::string& value, size_t maximumBytes, bool allowEmpty)
		{
			if ((!allowEmpty && value.empty()) || value.size() > maximumBytes)
				return false;
			for (unsigned char character : value)
			{
				if (character < 0x21 || character > 0x7e)
					return false;
			}
			return true;
		}

		bool IsBoundedDetail(const std::string& value)
		{
			if (value.size() > MaximumDetailBytes)
				return false;
			for (unsigned char character : value)
			{
				if (character < 0x20 || character > 0x7e)
					return false;
			}
			return true;
		}

		bool IsKnown(CommandKind kind)
		{
			return kind >= CommandKind::WalkToPoint && kind <= CommandKind::Abort;
		}

		bool IsKnown(CommandState state)
		{
			return state >= CommandState::Rejected && state <= CommandState::Cancelled;
		}

		bool IsKnown(TelemetryEventType type)
		{
			return type >= TelemetryEventType::CommandIssued && type <= TelemetryEventType::CommandResult;
		}

		bool IsKnown(TargetSnapshot::InventoryResourceKind kind)
		{
			return kind == TargetSnapshot::InventoryResourceKind::AmmoAmount ||
				kind == TargetSnapshot::InventoryResourceKind::NumCopies;
		}

		const char* InventoryResourceKindName(
			TargetSnapshot::InventoryResourceKind kind)
		{
			switch (kind)
			{
			case TargetSnapshot::InventoryResourceKind::AmmoAmount:
				return "ammo_amount";
			case TargetSnapshot::InventoryResourceKind::NumCopies:
				return "num_copies";
			default:
				return "invalid";
			}
		}

		bool IsFinitePoint(const WorldPoint& point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y) && std::isfinite(point.Z) &&
				std::abs(point.X) <= MaximumCoordinateMagnitude &&
				std::abs(point.Y) <= MaximumCoordinateMagnitude &&
				std::abs(point.Z) <= MaximumCoordinateMagnitude;
		}

		ValidationResult ValidateSelector(const TargetSelector& selector)
		{
			if (selector.ObservationRevision == 0 ||
				!IsBoundedToken(selector.Identity, MaximumTargetIdentityBytes, false) ||
				!IsBoundedToken(selector.ExpectedClass, MaximumClassNameBytes, true))
				return Reject(ValidationStatus::InvalidTarget, "target selector is invalid or unbounded");
			return {};
		}

		ValidationResult ValidateActorTargetCommand(const AutomationCommand& command)
		{
			if (!command.Target || command.Point || !command.AbortCommandId.empty() || command.WaitTicks != 0)
				return Reject(ValidationStatus::InvalidTarget, "actor command requires exactly one target selector");
			ValidationResult selector = ValidateSelector(*command.Target);
			if (!selector)
				return selector;
			if (!command.ArrivalRadius || !std::isfinite(*command.ArrivalRadius) ||
				*command.ArrivalRadius <= 0.0 || *command.ArrivalRadius > MaximumArrivalRadius)
				return Reject(ValidationStatus::InvalidArrivalRadius, "actor command arrival radius is invalid");
			return {};
		}

		std::string EscapeJson(const std::string& value)
		{
			std::ostringstream out;
			out.imbue(std::locale::classic());
			for (unsigned char character : value)
			{
				switch (character)
				{
				case '"': out << "\\\""; break;
				case '\\': out << "\\\\"; break;
				case '\b': out << "\\b"; break;
				case '\f': out << "\\f"; break;
				case '\n': out << "\\n"; break;
				case '\r': out << "\\r"; break;
				case '\t': out << "\\t"; break;
				default:
					if (character < 0x20)
						out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character) << std::dec;
					else
						out << static_cast<char>(character);
				}
			}
			return out.str();
		}

		std::string JsonString(const std::string& value)
		{
			return "\"" + EscapeJson(value) + "\"";
		}

		std::string Fixed(double value)
		{
			if (!std::isfinite(value))
				throw std::invalid_argument("automation protocol contains a non-finite number");
			if (value == 0.0)
				value = 0.0;
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << std::fixed << std::setprecision(6) << value;
			return out.str();
		}

		void WritePoint(std::ostringstream& out, const WorldPoint& point)
		{
			out << "{\"x\":" << Fixed(point.X)
				<< ",\"y\":" << Fixed(point.Y)
				<< ",\"z\":" << Fixed(point.Z) << '}';
		}

		void Hash(uint64_t& value, const std::string& text)
		{
			for (unsigned char character : text)
			{
				value ^= character;
				value *= 1099511628211ULL;
			}
		}

		std::string Hex64(uint64_t value)
		{
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << std::hex << std::setw(16) << std::setfill('0') << value;
			return out.str();
		}

		bool Requires(TargetRequirement value, TargetRequirement flag)
		{
			return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
		}

		TargetRequirement Combine(TargetRequirement left, TargetRequirement right)
		{
			return static_cast<TargetRequirement>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
		}
	}

	const char* CommandKindName(CommandKind kind)
	{
		switch (kind)
		{
		case CommandKind::WalkToPoint: return "walk_to_point";
		case CommandKind::WalkToActor: return "walk_to_actor";
		case CommandKind::AcquireItem: return "acquire_item";
		case CommandKind::Interact: return "interact";
		case CommandKind::Wait: return "wait";
		case CommandKind::Abort: return "abort";
		default: return "unknown";
		}
	}

	const char* CommandStateName(CommandState state)
	{
		switch (state)
		{
		case CommandState::Rejected: return "rejected";
		case CommandState::Accepted: return "accepted";
		case CommandState::Running: return "running";
		case CommandState::Succeeded: return "succeeded";
		case CommandState::Failed: return "failed";
		case CommandState::TimedOut: return "timed_out";
		case CommandState::Cancelled: return "cancelled";
		default: return "unknown";
		}
	}

	const char* TelemetryEventTypeName(TelemetryEventType type)
	{
		switch (type)
		{
		case TelemetryEventType::CommandIssued: return "command_issued";
		case TelemetryEventType::CommandAccepted: return "command_accepted";
		case TelemetryEventType::TargetResolved: return "target_resolved";
		case TelemetryEventType::CommandProgress: return "command_progress";
		case TelemetryEventType::VisualCapturePublished: return "visual_capture_published";
		case TelemetryEventType::InteractionAttempt: return "interaction_attempt";
		case TelemetryEventType::CommandResult: return "command_result";
		default: return "unknown";
		}
	}

	ValidationResult ValidateCommandStructure(const AutomationCommand& command)
	{
		if (!IsKnown(command.Kind))
			return Reject(ValidationStatus::InvalidCommandKind, "command kind is invalid");
		if (!IsCommandId(command.Id))
			return Reject(ValidationStatus::InvalidCommandId, "command ID is invalid or unbounded");
		if (command.DeadlineTick <= command.IssuedTick ||
			command.DeadlineTick - command.IssuedTick > MaximumCommandLifetimeTicks)
			return Reject(ValidationStatus::InvalidTickWindow, "command tick window is invalid or unbounded");

		switch (command.Kind)
		{
		case CommandKind::WalkToPoint:
			if (command.Target || !command.Point || !command.AbortCommandId.empty() || command.WaitTicks != 0)
				return Reject(ValidationStatus::InvalidPoint, "walk-to-point requires exactly one point");
			if (!IsFinitePoint(*command.Point))
				return Reject(ValidationStatus::InvalidPoint, "walk-to-point coordinates are invalid or unbounded");
			if (!command.ArrivalRadius || !std::isfinite(*command.ArrivalRadius) ||
				*command.ArrivalRadius <= 0.0 || *command.ArrivalRadius > MaximumArrivalRadius)
				return Reject(ValidationStatus::InvalidArrivalRadius, "walk-to-point arrival radius is invalid");
			break;
		case CommandKind::WalkToActor:
		case CommandKind::AcquireItem:
		case CommandKind::Interact:
			return ValidateActorTargetCommand(command);
		case CommandKind::Wait:
			if (command.Target || command.Point || command.ArrivalRadius || !command.AbortCommandId.empty() || command.WaitTicks == 0)
				return Reject(ValidationStatus::InvalidWait, "wait command fields are invalid");
			if (command.WaitTicks > command.DeadlineTick - command.IssuedTick)
				return Reject(ValidationStatus::InvalidWait, "wait duration exceeds the command deadline");
			break;
		case CommandKind::Abort:
			if (command.Target || command.Point || command.ArrivalRadius || command.WaitTicks != 0 ||
				!IsCommandId(command.AbortCommandId) || command.AbortCommandId == command.Id)
				return Reject(ValidationStatus::InvalidAbort, "abort command target is invalid");
			break;
		default:
			return Reject(ValidationStatus::InvalidCommandKind, "command kind is invalid");
		}
		return {};
	}

	ValidationResult ValidateCommandForSnapshot(const AutomationCommand& command,
		uint64_t currentTick, uint64_t observationRevision)
	{
		ValidationResult structure = ValidateCommandStructure(command);
		if (!structure)
			return structure;
		if (currentTick < command.IssuedTick)
			return Reject(ValidationStatus::CommandNotYetIssued, "command issue tick is in the future");
		if (currentTick > command.DeadlineTick)
			return Reject(ValidationStatus::CommandExpired, "command deadline has passed");
		if (command.Target && command.Target->ObservationRevision != observationRevision)
			return Reject(ValidationStatus::StaleObservation, "command target observation is stale");
		return {};
	}

	TargetResolutionResult ResolveTarget(const TargetSelector& selector,
		const std::vector<TargetSnapshot>& snapshots, uint64_t observationRevision,
		TargetRequirement requirements)
	{
		ValidationResult selectorValidation = ValidateSelector(selector);
		if (!selectorValidation)
			return { TargetResolutionStatus::InvalidSelector, {}, selectorValidation.Error };
		if (selector.ObservationRevision != observationRevision)
			return { TargetResolutionStatus::StaleSelector, {}, "target selector observation is stale" };

		std::vector<const TargetSnapshot*> matching;
		bool foundWrongRevision = false;
		for (const TargetSnapshot& snapshot : snapshots)
		{
			if (snapshot.Identity != selector.Identity || snapshot.Deleted)
				continue;
			if (snapshot.ObservationRevision != observationRevision)
			{
				foundWrongRevision = true;
				continue;
			}
			matching.push_back(&snapshot);
		}

		if (matching.empty())
		{
			if (foundWrongRevision)
				return { TargetResolutionStatus::SnapshotRevisionMismatch, {}, "target exists only in a different observation revision" };
			return { TargetResolutionStatus::Missing, {}, "target identity is absent from the current observation" };
		}
		if (matching.size() != 1)
			return { TargetResolutionStatus::Ambiguous, {}, "target identity is not unique in the current observation" };

		const TargetSnapshot& target = *matching.front();
		if (!IsBoundedToken(target.Identity, MaximumTargetIdentityBytes, false) ||
			!IsBoundedToken(target.ClassName, MaximumClassNameBytes, false) || !IsFinitePoint(target.Location))
			return { TargetResolutionStatus::InvalidSelector, {}, "resolved target snapshot is invalid or unbounded" };
		if (!selector.ExpectedClass.empty() && selector.ExpectedClass != target.ClassName)
			return { TargetResolutionStatus::ClassMismatch, {}, "target class does not match the command contract" };
		if (Requires(requirements, TargetRequirement::Reachable) && !target.Reachable)
			return { TargetResolutionStatus::Unreachable, {}, "target is not reachable in the current observation" };
		if (Requires(requirements, TargetRequirement::Interactable) && !target.Interactable)
			return { TargetResolutionStatus::NotInteractable, {}, "target is not interactable in the current observation" };
		if (Requires(requirements, TargetRequirement::Acquirable) && !target.Acquirable)
			return { TargetResolutionStatus::NotAcquirable, {}, "target is not acquirable in the current observation" };
		return { TargetResolutionStatus::Resolved, target, {} };
	}

	TargetResolutionResult ResolveTargetForCommand(const AutomationCommand& command,
		const std::vector<TargetSnapshot>& snapshots, uint64_t observationRevision)
	{
		if (!command.Target)
			return { TargetResolutionStatus::InvalidSelector, {}, "command has no actor target" };
		switch (command.Kind)
		{
		case CommandKind::WalkToActor:
			return ResolveTarget(*command.Target, snapshots, observationRevision, TargetRequirement::Reachable);
		case CommandKind::AcquireItem:
			return ResolveTarget(*command.Target, snapshots, observationRevision,
				Combine(TargetRequirement::Reachable, TargetRequirement::Acquirable));
		case CommandKind::Interact:
			return ResolveTarget(*command.Target, snapshots, observationRevision, TargetRequirement::Interactable);
		default:
			return { TargetResolutionStatus::InvalidSelector, {}, "command kind does not resolve an actor target" };
		}
	}

	ValidationResult ValidateObservationSnapshot(const ObservationSnapshot& observation)
	{
		if (observation.Revision == 0 ||
			!IsBoundedToken(observation.PlayerIdentity, MaximumTargetIdentityBytes, false) ||
			!IsFinitePoint(observation.PlayerPosition) ||
			observation.Targets.size() > MaximumObservationTargets)
			return Reject(ValidationStatus::InvalidTarget, "observation header is invalid or unbounded");

		std::set<std::string> identities;
		for (const TargetSnapshot& target : observation.Targets)
		{
			if (target.ObservationRevision != observation.Revision)
				return Reject(ValidationStatus::InvalidTarget, "observation target revision is stale");
			if (!IsBoundedToken(target.Identity, MaximumTargetIdentityBytes, false))
				return Reject(ValidationStatus::InvalidTarget, "observation target identity is invalid or unbounded");
			if (!IsBoundedToken(target.ClassName, MaximumClassNameBytes, false))
				return Reject(ValidationStatus::InvalidTarget, "observation target class is invalid or unbounded");
			if (!IsBoundedToken(target.OwnerIdentity, MaximumTargetIdentityBytes, true))
				return Reject(ValidationStatus::InvalidTarget, "observation target owner is invalid or unbounded");
			if (!IsBoundedToken(target.StateToken, MaximumClassNameBytes, true))
				return Reject(ValidationStatus::InvalidTarget, "observation target state is invalid or unbounded");
			if (!IsBoundedToken(target.TagName, MaximumClassNameBytes, true) ||
				!IsBoundedToken(target.EventName, MaximumClassNameBytes, true))
				return Reject(ValidationStatus::InvalidTarget,
					"observation target tag or event is invalid or unbounded");
			if (target.InventoryResource &&
				(!IsKnown(target.InventoryResource->Kind) ||
					target.InventoryResource->Value > MaximumInventoryResourceValue))
				return Reject(ValidationStatus::InvalidTarget,
					"observation inventory resource is invalid or unbounded");
			if (!IsFinitePoint(target.Location))
				return Reject(ValidationStatus::InvalidTarget, "observation target location is invalid or unbounded");
			if (!identities.insert(target.Identity).second)
				return Reject(ValidationStatus::InvalidTarget,
					"observation target identity is duplicated: " + target.Identity);
		}
		return {};
	}

	std::string ObservationSnapshotJson(const ObservationSnapshot& observation)
	{
		ValidationResult validation = ValidateObservationSnapshot(observation);
		if (!validation)
			throw std::invalid_argument(validation.Error);

		std::vector<TargetSnapshot> targets = observation.Targets;
		std::sort(targets.begin(), targets.end(), [](const TargetSnapshot& left, const TargetSnapshot& right)
		{
			return left.Identity < right.Identity;
		});

		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "{\"schema\":\"surreal-automation-observation-v2\""
			<< ",\"revision\":\"" << observation.Revision << "\""
			<< ",\"tick\":\"" << observation.Tick << "\""
			<< ",\"player_identity\":" << JsonString(observation.PlayerIdentity)
			<< ",\"player_position\":";
		WritePoint(out, observation.PlayerPosition);
		out << ",\"targets\":[";
		for (size_t index = 0; index < targets.size(); index++)
		{
			if (index != 0)
				out << ',';
			const TargetSnapshot& target = targets[index];
			out << "{\"identity\":" << JsonString(target.Identity)
				<< ",\"class\":" << JsonString(target.ClassName)
				<< ",\"owner_identity\":" << JsonString(target.OwnerIdentity)
				<< ",\"state_token\":" << JsonString(target.StateToken)
				<< ",\"tag\":" << JsonString(target.TagName)
				<< ",\"event\":" << JsonString(target.EventName)
				<< ",\"inventory_resource\":";
			if (target.InventoryResource)
			{
				out << "{\"kind\":" << JsonString(
					InventoryResourceKindName(target.InventoryResource->Kind))
					<< ",\"value\":\"" << target.InventoryResource->Value << "\"}";
			}
			else
				out << "null";
			out
				<< ",\"location\":";
			WritePoint(out, target.Location);
			out << ",\"deleted\":" << (target.Deleted ? "true" : "false")
				<< ",\"reachable\":" << (target.Reachable ? "true" : "false")
				<< ",\"interactable\":" << (target.Interactable ? "true" : "false")
				<< ",\"acquirable\":" << (target.Acquirable ? "true" : "false") << '}';
		}
		out << "]}\n";
		return out.str();
	}

	std::string ObservationSnapshotDigest(const ObservationSnapshot& observation)
	{
		uint64_t digest = 1469598103934665603ULL;
		Hash(digest, ObservationSnapshotJson(observation));
		return "fnv1a64:" + Hex64(digest);
	}

	bool IsTerminal(CommandState state)
	{
		return state == CommandState::Rejected || state == CommandState::Succeeded ||
			state == CommandState::Failed || state == CommandState::TimedOut ||
			state == CommandState::Cancelled;
	}

	ValidationResult ValidateCommandResult(const CommandResult& result)
	{
		if (!IsKnown(result.Kind))
			return Reject(ValidationStatus::InvalidCommandKind, "result command kind is invalid");
		if (!IsKnown(result.State))
			return Reject(ValidationStatus::InvalidCommandState, "result command state is invalid");
		if (!IsCommandId(result.CommandId) ||
			!IsBoundedToken(result.TargetIdentity, MaximumTargetIdentityBytes, true) ||
			!IsBoundedDetail(result.Reason))
			return Reject(ValidationStatus::InvalidResult, "command result contains invalid or unbounded text");
		if ((result.State == CommandState::Rejected || result.State == CommandState::Failed ||
			result.State == CommandState::TimedOut || result.State == CommandState::Cancelled) && result.Reason.empty())
			return Reject(ValidationStatus::InvalidResult, "unsuccessful terminal result requires a reason");
		return {};
	}

	ValidationResult ValidateTelemetryEvent(const TelemetryEvent& event)
	{
		if (event.Sequence == 0 || !IsKnown(event.Type) || !IsKnown(event.Kind) ||
			!IsBoundedToken(event.ConfigIdentity, MaximumTargetIdentityBytes, false) ||
			!IsCommandId(event.CommandId) ||
			!IsBoundedToken(event.TargetIdentity, MaximumTargetIdentityBytes, true) ||
			!IsBoundedDetail(event.Detail) ||
			(event.State && !IsKnown(*event.State)) ||
			(event.Position && !IsFinitePoint(*event.Position)) ||
			(event.Distance && (!std::isfinite(*event.Distance) || *event.Distance < 0.0 || *event.Distance > MaximumCoordinateMagnitude)))
			return Reject(ValidationStatus::InvalidTelemetry, "automation telemetry event is invalid or unbounded");

		switch (event.Type)
		{
		case TelemetryEventType::CommandIssued:
			if (event.State)
				return Reject(ValidationStatus::InvalidTelemetry, "command-issued event must not declare a state");
			break;
		case TelemetryEventType::CommandAccepted:
			if (!event.State || *event.State != CommandState::Accepted)
				return Reject(ValidationStatus::InvalidTelemetry, "command-accepted event requires accepted state");
			break;
		case TelemetryEventType::TargetResolved:
			if (event.TargetIdentity.empty())
				return Reject(ValidationStatus::InvalidTelemetry, "target-resolved event requires a target identity");
			break;
		case TelemetryEventType::CommandProgress:
			if (!event.State || *event.State != CommandState::Running)
				return Reject(ValidationStatus::InvalidTelemetry, "command-progress event requires running state");
			break;
		case TelemetryEventType::VisualCapturePublished:
			if (!event.State || *event.State != CommandState::Running ||
				event.TargetIdentity.empty())
				return Reject(ValidationStatus::InvalidTelemetry,
					"visual-capture-published event requires running state and target identity");
			break;
		case TelemetryEventType::InteractionAttempt:
			if (event.TargetIdentity.empty())
				return Reject(ValidationStatus::InvalidTelemetry, "interaction-attempt event requires a target identity");
			break;
		case TelemetryEventType::CommandResult:
			if (!event.State || !IsTerminal(*event.State))
				return Reject(ValidationStatus::InvalidTelemetry, "command-result event requires a terminal state");
			break;
		default:
			return Reject(ValidationStatus::InvalidTelemetry, "automation telemetry event type is invalid");
		}
		return {};
	}

	std::string CommandJson(const AutomationCommand& command)
	{
		ValidationResult validation = ValidateCommandStructure(command);
		if (!validation)
			throw std::invalid_argument(validation.Error);

		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "{\"schema\":\"surreal-automation-command-v1\""
			<< ",\"command_id\":" << JsonString(command.Id)
			<< ",\"kind\":" << JsonString(CommandKindName(command.Kind))
			<< ",\"issued_tick\":\"" << command.IssuedTick << "\""
			<< ",\"deadline_tick\":\"" << command.DeadlineTick << "\""
			<< ",\"target\":";
		if (command.Target)
		{
			out << "{\"observation_revision\":\"" << command.Target->ObservationRevision << "\""
				<< ",\"identity\":" << JsonString(command.Target->Identity)
				<< ",\"expected_class\":" << JsonString(command.Target->ExpectedClass) << '}';
		}
		else
		{
			out << "null";
		}
		out << ",\"point\":";
		if (command.Point)
			WritePoint(out, *command.Point);
		else
			out << "null";
		out << ",\"arrival_radius\":";
		if (command.ArrivalRadius)
			out << Fixed(*command.ArrivalRadius);
		else
			out << "null";
		out << ",\"wait_ticks\":\"" << command.WaitTicks << "\""
			<< ",\"abort_command_id\":" << JsonString(command.AbortCommandId) << "}\n";
		return out.str();
	}

	std::string CommandDigest(const AutomationCommand& command)
	{
		uint64_t digest = 1469598103934665603ULL;
		Hash(digest, CommandJson(command));
		return "fnv1a64:" + Hex64(digest);
	}

	std::string CommandResultJson(const CommandResult& result)
	{
		ValidationResult validation = ValidateCommandResult(result);
		if (!validation)
			throw std::invalid_argument(validation.Error);
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "{\"schema\":\"surreal-automation-result-v1\""
			<< ",\"command_id\":" << JsonString(result.CommandId)
			<< ",\"kind\":" << JsonString(CommandKindName(result.Kind))
			<< ",\"state\":" << JsonString(CommandStateName(result.State))
			<< ",\"tick\":\"" << result.Tick << "\""
			<< ",\"observation_revision\":\"" << result.ObservationRevision << "\""
			<< ",\"target_identity\":" << JsonString(result.TargetIdentity)
			<< ",\"reason\":" << JsonString(result.Reason) << "}\n";
		return out.str();
	}

	std::string TelemetryEventJson(const TelemetryEvent& event)
	{
		ValidationResult validation = ValidateTelemetryEvent(event);
		if (!validation)
			throw std::invalid_argument(validation.Error);
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "{\"schema\":\"surreal-automation-telemetry-v1\""
			<< ",\"seq\":\"" << event.Sequence << "\""
			<< ",\"tick\":\"" << event.Tick << "\""
			<< ",\"observation_revision\":\"" << event.ObservationRevision << "\""
			<< ",\"config_id\":" << JsonString(event.ConfigIdentity)
			<< ",\"event\":" << JsonString(TelemetryEventTypeName(event.Type))
			<< ",\"command_id\":" << JsonString(event.CommandId)
			<< ",\"kind\":" << JsonString(CommandKindName(event.Kind))
			<< ",\"state\":";
		if (event.State)
			out << JsonString(CommandStateName(*event.State));
		else
			out << "null";
		out << ",\"target_identity\":" << JsonString(event.TargetIdentity)
			<< ",\"detail\":" << JsonString(event.Detail)
			<< ",\"position\":";
		if (event.Position)
			WritePoint(out, *event.Position);
		else
			out << "null";
		out << ",\"distance\":";
		if (event.Distance)
			out << Fixed(*event.Distance);
		else
			out << "null";
		out << "}\n";
		return out.str();
	}
}
