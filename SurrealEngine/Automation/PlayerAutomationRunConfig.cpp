#include "PlayerAutomationRunConfig.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Automation
{
	namespace
	{
		uint64_t ParseUInt64(const std::string& text, uint64_t defaultValue, const char* name)
		{
			if (text.empty())
				return defaultValue;
			if (text.front() == '-')
				throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
			size_t end = 0;
			const uint64_t value = std::stoull(text, &end);
			if (end != text.size())
				throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
			return value;
		}

		double ParseDouble(const std::string& text, const char* name)
		{
			if (text.empty())
				throw std::invalid_argument(std::string(name) + " is required");
			size_t end = 0;
			const double value = std::stod(text, &end);
			if (end != text.size() || !std::isfinite(value))
				throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
			return value;
		}

		float ParseFloat(const std::string& text, float defaultValue, const char* name)
		{
			if (text.empty())
				return defaultValue;
			size_t end = 0;
			const float value = std::stof(text, &end);
			if (end != text.size() || !std::isfinite(value))
				throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
			return value;
		}

		std::string EscapeJson(const std::string& value)
		{
			std::ostringstream out;
			for (unsigned char c : value)
			{
				switch (c)
				{
				case '"': out << "\\\""; break;
				case '\\': out << "\\\\"; break;
				case '\b': out << "\\b"; break;
				case '\f': out << "\\f"; break;
				case '\n': out << "\\n"; break;
				case '\r': out << "\\r"; break;
				case '\t': out << "\\t"; break;
				default:
					if (c < 0x20)
						out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
					else
						out << static_cast<char>(c);
				}
			}
			return out.str();
		}

		std::string JsonString(const std::string& value)
		{
			return "\"" + EscapeJson(value) + "\"";
		}

		bool IsBoundedToken(const std::string& value, size_t maximumBytes)
		{
			if (value.empty() || value.size() > maximumBytes)
				return false;
			for (unsigned char character : value)
			{
				if (character < 0x21 || character > 0x7e)
					return false;
			}
			return true;
		}

		bool IsLowerHex(const std::string& value, size_t length)
		{
			if (value.size() != length)
				return false;
			for (unsigned char character : value)
			{
				if (!((character >= '0' && character <= '9') ||
					(character >= 'a' && character <= 'f')))
					return false;
			}
			return true;
		}

		bool ParseBoolean(const std::string& value, const char* name)
		{
			if (value == "true")
				return true;
			if (value == "false")
				return false;
			throw std::invalid_argument(std::string(name) + " must be true or false");
		}
	}

	const char* AutomationCapturePhaseName(AutomationCapturePhase phase)
	{
		switch (phase)
		{
		case AutomationCapturePhase::PostSimulation: return "post_simulation";
		case AutomationCapturePhase::PreAction: return "pre_action";
		case AutomationCapturePhase::PreStockInteraction: return "pre_stock_interaction";
		case AutomationCapturePhase::PrePickup: return "pre_pickup";
		default: return "unknown";
		}
	}

	PlayerAutomationRunConfig::PlayerAutomationRunConfig(std::string url,
		std::string outputDirectory, uint64_t seed, uint64_t maxTicks,
		float fixedDelta, AutomationCommand command,
		std::optional<AutomationCommand> abortCommand,
		std::optional<AutomationCommand> followupCommandTemplate,
		std::optional<TargetSelector> sightProbeTarget,
		std::optional<AutomationCaptureRequest> captureRequest)
		: URL(std::move(url)), OutputDirectory(std::move(outputDirectory)), Seed(seed),
		MaxTicks(maxTicks), FixedDelta(fixedDelta), Command(std::move(command)),
		AbortCommand(std::move(abortCommand)),
		FollowupCommandTemplate(std::move(followupCommandTemplate)),
		SightProbeTarget(std::move(sightProbeTarget)),
		CaptureRequest(std::move(captureRequest))
	{
	}

	PlayerAutomationRunConfig PlayerAutomationRunConfig::Parse(
		std::string url, std::string outputDirectory,
		std::string targetX, std::string targetY, std::string targetZ,
		std::string arrivalRadius, std::string seed, std::string maxTicks,
		std::string fixedDelta, std::string action, std::string waitTicks,
		std::string targetIdentity, std::string targetClass, std::string abortAtTick,
		std::string followupTargetIdentity, std::string captureTick,
		std::string captureSessionId, std::string captureSourceRevision,
		std::string captureSourceDirty, std::string capturePhase)
	{
		if (url.empty())
			throw std::invalid_argument("player automation URL is required");
		if (outputDirectory.empty())
			throw std::invalid_argument("player automation output directory is required");
		if (url.size() > 2048 || outputDirectory.size() > 2048)
			throw std::invalid_argument("player automation URL or output directory is unbounded");

		const uint64_t parsedSeed = ParseUInt64(seed, 104729, "player automation seed");
		const uint64_t parsedTicks = ParseUInt64(maxTicks, 900, "player automation tick limit");
		const float parsedDelta = ParseFloat(fixedDelta, 1.0f / 60.0f, "player automation fixed delta");
		if (parsedTicks == 0 || parsedTicks > MaximumCommandLifetimeTicks)
			throw std::invalid_argument("player automation tick limit is outside the bounded command lifetime");
		if (parsedDelta <= 0.0f || parsedDelta > 1.0f)
			throw std::invalid_argument("player automation fixed delta must be finite and between 0 and 1");

		if (action.empty())
			action = "walk_to_point";
		AutomationCommand command;
		std::optional<TargetSelector> sightProbeTarget;
		command.IssuedTick = 0;
		command.DeadlineTick = parsedTicks;
		if (action == "walk_to_point")
		{
			if (!waitTicks.empty() || !targetIdentity.empty() || !targetClass.empty())
				throw std::invalid_argument("walk-to-point does not accept wait or actor target fields");
			command.Id = "walk-to-point-1";
			command.Kind = CommandKind::WalkToPoint;
			command.Point = WorldPoint{
				ParseDouble(targetX, "player automation target X"),
				ParseDouble(targetY, "player automation target Y"),
				ParseDouble(targetZ, "player automation target Z") };
			command.ArrivalRadius = ParseDouble(arrivalRadius.empty() ? "40" : arrivalRadius,
				"player automation arrival radius");
		}
		else if (action == "sight_probe")
		{
			if (!targetX.empty() || !targetY.empty() || !targetZ.empty() ||
				!arrivalRadius.empty() || !waitTicks.empty())
				throw std::invalid_argument(
					"sight probe does not accept movement, arrival, or wait fields");
			if (!IsBoundedToken(targetIdentity, MaximumTargetIdentityBytes) ||
				!IsBoundedToken(targetClass, MaximumClassNameBytes))
				throw std::invalid_argument(
					"sight probe requires a bounded exact target identity and class");
			command.Id = "deus-ex-sight-probe-1";
			command.Kind = CommandKind::Wait;
			command.WaitTicks = 1;
			sightProbeTarget = TargetSelector{
				1, std::move(targetIdentity), std::move(targetClass) };
		}
		else if (action == "wait")
		{
			if (!targetX.empty() || !targetY.empty() || !targetZ.empty() || !arrivalRadius.empty() ||
				!targetIdentity.empty() || !targetClass.empty())
				throw std::invalid_argument("wait action does not accept movement or actor target fields");
			command.Id = "wait-1";
			command.Kind = CommandKind::Wait;
			command.WaitTicks = ParseUInt64(waitTicks, 60, "player automation wait ticks");
		}
		else if (action == "walk_to_actor" || action == "acquire_item" || action == "interact")
		{
			if (!targetX.empty() || !targetY.empty() || !targetZ.empty() || !waitTicks.empty())
				throw std::invalid_argument("actor-target action does not accept point or wait fields");
			if (targetIdentity.empty() || targetClass.empty())
				throw std::invalid_argument("actor-target action requires an exact target identity and class");
			if (action == "walk_to_actor")
			{
				command.Id = "walk-to-actor-1";
				command.Kind = CommandKind::WalkToActor;
			}
			else if (action == "acquire_item")
			{
				command.Id = "acquire-item-1";
				command.Kind = CommandKind::AcquireItem;
			}
			else
			{
				command.Id = "interact-1";
				command.Kind = CommandKind::Interact;
			}
			command.Target = TargetSelector{ 1, std::move(targetIdentity), std::move(targetClass) };
			command.ArrivalRadius = ParseDouble(arrivalRadius.empty() ?
				(action == "walk_to_actor" ? "40" : "48") : arrivalRadius,
				"player automation arrival radius");
		}
		else
		{
			throw std::invalid_argument("unsupported player automation action: " + action);
		}
		const ValidationResult validation = ValidateCommandStructure(command);
		if (!validation)
			throw std::invalid_argument("invalid player automation command: " + validation.Error);

		std::optional<AutomationCommand> followupCommandTemplate;
		if (!followupTargetIdentity.empty())
		{
			if (command.Kind != CommandKind::AcquireItem || !command.Target ||
				!command.ArrivalRadius)
			{
				throw std::invalid_argument(
					"follow-up target is supported only for an exact acquire-item command");
			}
			if (!IsBoundedToken(followupTargetIdentity, MaximumTargetIdentityBytes) ||
				followupTargetIdentity == command.Target->Identity)
			{
				throw std::invalid_argument(
					"follow-up acquisition requires a distinct bounded target identity");
			}
			AutomationCommand followup = command;
			followup.Id = "acquire-item-2";
			followup.Target = TargetSelector{ 1, std::move(followupTargetIdentity),
				command.Target->ExpectedClass };
			const ValidationResult followupValidation = ValidateCommandStructure(followup);
			if (!followupValidation)
			{
				throw std::invalid_argument(
					"invalid follow-up acquisition template: " + followupValidation.Error);
			}
			followupCommandTemplate = std::move(followup);
		}

		std::optional<AutomationCommand> abortCommand;
		if (!abortAtTick.empty())
		{
			if (followupCommandTemplate)
				throw std::invalid_argument(
					"scheduled abort is not supported for a follow-up acquisition run");
			if (sightProbeTarget)
				throw std::invalid_argument("sight probe does not accept a scheduled abort");
			const uint64_t parsedAbortTick = ParseUInt64(
				abortAtTick, 0, "player automation abort tick");
			if (parsedAbortTick == 0 || parsedAbortTick >= parsedTicks)
				throw std::invalid_argument(
					"player automation abort tick is outside the bounded run window");
			AutomationCommand abort;
			abort.Id = "abort-1";
			abort.Kind = CommandKind::Abort;
			abort.IssuedTick = parsedAbortTick;
			abort.DeadlineTick = parsedAbortTick + 1;
			abort.AbortCommandId = command.Id;
			const ValidationResult abortValidation = ValidateCommandStructure(abort);
			if (!abortValidation)
				throw std::invalid_argument(
					"invalid player automation abort command: " + abortValidation.Error);
			abortCommand = std::move(abort);
		}

		std::optional<AutomationCaptureRequest> captureRequest;
		const bool anyCaptureField = !captureTick.empty() || !captureSessionId.empty() ||
			!captureSourceRevision.empty() || !captureSourceDirty.empty() ||
			!capturePhase.empty();
		const bool allCaptureFields = !captureTick.empty() && !captureSessionId.empty() &&
			!captureSourceRevision.empty() && !captureSourceDirty.empty();
		if (anyCaptureField && !allCaptureFields)
			throw std::invalid_argument("player automation capture fields must be supplied together");
		if (allCaptureFields)
		{
			AutomationCapturePhase parsedCapturePhase =
				AutomationCapturePhase::PostSimulation;
			if (capturePhase == "pre_action")
				parsedCapturePhase = AutomationCapturePhase::PreAction;
			else if (capturePhase == "pre_stock_interaction")
				parsedCapturePhase = AutomationCapturePhase::PreStockInteraction;
			else if (capturePhase == "pre_pickup")
				parsedCapturePhase = AutomationCapturePhase::PrePickup;
			else if (!capturePhase.empty() && capturePhase != "post_simulation")
				throw std::invalid_argument("unsupported player automation capture phase");
			const uint64_t requestedTick = ParseUInt64(
				captureTick, 0, "player automation capture tick");
			if ((parsedCapturePhase == AutomationCapturePhase::PreAction &&
					requestedTick != 0) ||
				(parsedCapturePhase != AutomationCapturePhase::PreAction &&
					(requestedTick == 0 || requestedTick > parsedTicks)))
				throw std::invalid_argument(
					"player automation capture tick is outside the bounded run window");
			if (parsedCapturePhase == AutomationCapturePhase::PreAction &&
				command.Kind != CommandKind::WalkToActor)
				throw std::invalid_argument(
					"pre-action capture requires one exact walk-to-actor command");
			if (parsedCapturePhase == AutomationCapturePhase::PreStockInteraction &&
				command.Kind != CommandKind::Interact)
				throw std::invalid_argument(
					"pre-stock-interaction capture requires one exact interact command");
			if (parsedCapturePhase == AutomationCapturePhase::PrePickup &&
				command.Kind != CommandKind::AcquireItem)
				throw std::invalid_argument(
					"pre-pickup capture requires one exact acquisition command");
			if (!IsBoundedToken(captureSessionId, 128))
				throw std::invalid_argument(
					"player automation capture session id must be a bounded token");
			if (!IsLowerHex(captureSourceRevision, 40))
				throw std::invalid_argument(
					"player automation capture source revision must be lowercase Git hex");
			if (followupCommandTemplate)
				throw std::invalid_argument(
					"player automation capture does not support a follow-up command");
			if (sightProbeTarget)
				throw std::invalid_argument(
					"player automation capture does not support a sight probe");
			captureRequest = AutomationCaptureRequest{ requestedTick,
				std::move(captureSessionId), std::move(captureSourceRevision),
				ParseBoolean(captureSourceDirty,
					"player automation capture source-dirty field"), parsedCapturePhase };
		}

		return PlayerAutomationRunConfig(std::move(url), std::move(outputDirectory),
			parsedSeed, parsedTicks, parsedDelta, std::move(command),
			std::move(abortCommand), std::move(followupCommandTemplate),
			std::move(sightProbeTarget), std::move(captureRequest));
	}

	AutomationCommand PlayerAutomationRunConfig::MaterializeFollowupCommand(
		uint64_t issuedTick, uint64_t observationRevision) const
	{
		if (!FollowupCommandTemplate)
			throw std::invalid_argument("player automation run has no follow-up command");
		if (issuedTick >= MaxTicks || observationRevision == 0)
			throw std::invalid_argument(
				"follow-up command cannot be materialized outside the bounded run");
		AutomationCommand command = *FollowupCommandTemplate;
		command.IssuedTick = issuedTick;
		command.DeadlineTick = MaxTicks;
		command.Target->ObservationRevision = observationRevision;
		const ValidationResult validation = ValidateCommandForSnapshot(
			command, issuedTick, observationRevision);
		if (!validation)
			throw std::invalid_argument(
				"materialized follow-up command is invalid: " + validation.Error);
		return command;
	}

	std::string PlayerAutomationRunConfig::ConfigIdentity() const
	{
		std::ostringstream canonical;
		canonical << CommandDigest(Command) << '|';
		if (AbortCommand)
			canonical << CommandDigest(*AbortCommand);
		canonical << '|';
		if (FollowupCommandTemplate)
			canonical << CommandDigest(*FollowupCommandTemplate);
		canonical << '|' << URL.size() << ':' << URL << '|' << Seed << '|'
			<< MaxTicks << '|' << std::setprecision(9) << FixedDelta;
		if (SightProbeTarget)
		{
			canonical << "|deus_ex_ai_can_see_probe_v2|"
				<< SightProbeTarget->ObservationRevision << '|'
				<< SightProbeTarget->Identity.size() << ':' << SightProbeTarget->Identity << '|'
				<< SightProbeTarget->ExpectedClass.size() << ':' << SightProbeTarget->ExpectedClass;
		}
		if (CaptureRequest)
		{
			canonical << (CaptureRequest->Phase ==
				AutomationCapturePhase::PostSimulation ?
				"|visual_capture_v1|" : "|visual_capture_v2|");
			if (CaptureRequest->Phase != AutomationCapturePhase::PostSimulation)
				canonical << AutomationCapturePhaseName(CaptureRequest->Phase) << '|';
			canonical << CaptureRequest->Tick << '|'
				<< CaptureRequest->SessionId.size() << ':' << CaptureRequest->SessionId << '|'
				<< CaptureRequest->SourceRevision << '|'
				<< (CaptureRequest->SourceDirty ? "dirty" : "clean");
		}
		uint64_t hash = 14695981039346656037ULL;
		for (unsigned char c : canonical.str())
		{
			hash ^= c;
			hash *= 1099511628211ULL;
		}
		std::ostringstream out;
		out << std::hex << std::setfill('0') << std::setw(16) << hash;
		return out.str();
	}

	std::string PlayerAutomationRunConfig::ManifestJson() const
	{
		std::ostringstream out;
		out << std::setprecision(9);
		out << "{\n"
			<< "  \"schema\": \"" << (CaptureRequest ?
				(CaptureRequest->Phase == AutomationCapturePhase::PostSimulation ?
					"surreal-player-automation-manifest-v4" :
					"surreal-player-automation-manifest-v5") : FollowupCommandTemplate ?
				"surreal-player-automation-manifest-v3" : SightProbeTarget ?
				"surreal-player-automation-manifest-v2" :
				"surreal-player-automation-manifest-v1") << "\",\n"
			<< "  \"mode\": \"" << (CaptureRequest ?
				(CaptureRequest->Phase == AutomationCapturePhase::PostSimulation ?
					"deterministic-player-automation-visual-capture" :
					"deterministic-player-automation-pre-action-visual-capture") :
				FollowupCommandTemplate ?
				"deterministic-player-automation-sequence" :
				"deterministic-player-automation") << "\",\n"
			<< "  \"config_identity\": " << JsonString(ConfigIdentity()) << ",\n"
			<< "  \"url\": " << JsonString(URL) << ",\n"
			<< "  \"output_directory\": " << JsonString(OutputDirectory) << ",\n"
			<< "  \"seed\": \"" << Seed << "\",\n"
			<< "  \"max_ticks\": \"" << MaxTicks << "\",\n"
			<< "  \"fixed_delta\": " << FixedDelta << ",\n";
		if (SightProbeTarget)
		{
			out << "  \"probe\": {\"type\":\"deus_ex_ai_can_see\","
				<< "\"artifact\":\"sight-probe.json\",\"selector\":{"
				<< "\"observation_revision\":\"" << SightProbeTarget->ObservationRevision
				<< "\",\"identity\":" << JsonString(SightProbeTarget->Identity)
				<< ",\"class\":" << JsonString(SightProbeTarget->ExpectedClass)
				<< "},\"contract\":{\"gameplay_property_mutation\":false,"
				<< "\"collision_bookkeeping_mutation\":true,\"visibility\":1.0,"
				<< "\"check_visibility\":false,\"evaluations\":["
				<< "\"scalar\",\"direction\",\"los\",\"cylinder_los\"]}},\n";
		}
		if (CaptureRequest)
		{
			out << "  \"visual_capture\": {\"schema\":"
				<< (CaptureRequest->Phase == AutomationCapturePhase::PostSimulation ?
					"\"surreal-player-automation-capture-request-v1\"," :
					"\"surreal-player-automation-capture-request-v2\",")
				<< "\"tick\":\"" << CaptureRequest->Tick << "\","
				<< "\"session_id\":" << JsonString(CaptureRequest->SessionId) << ','
				<< "\"source_revision\":" << JsonString(CaptureRequest->SourceRevision) << ','
				<< "\"source_dirty\":" << (CaptureRequest->SourceDirty ? "true" : "false")
				<< (CaptureRequest->Phase == AutomationCapturePhase::PostSimulation ?
					std::string() : ",\"phase\":" +
					JsonString(AutomationCapturePhaseName(CaptureRequest->Phase)))
				<< ",\"output_directory\":\"visual-capture\"},\n";
		}
		out
			<< "  \"command_digest\": " << JsonString(CommandDigest(Command)) << ",\n"
			<< "  \"command\": " << CommandJson(Command) << ",\n"
			<< "  \"abort_command_digest\": ";
		if (AbortCommand)
			out << JsonString(CommandDigest(*AbortCommand));
		else
			out << "null";
		out << ",\n  \"abort_command\": ";
		if (AbortCommand)
			out << CommandJson(*AbortCommand);
		else
			out << "null\n";
		out << ",\n  \"followup_command_template_digest\": ";
		if (FollowupCommandTemplate)
			out << JsonString(CommandDigest(*FollowupCommandTemplate));
		else
			out << "null";
		out << ",\n  \"followup_command_template\": ";
		if (FollowupCommandTemplate)
			out << CommandJson(*FollowupCommandTemplate);
		else
			out << "null\n";
		out << ",\n  \"followup_command_artifact\": ";
		if (FollowupCommandTemplate)
			out << "\"followup-command.json\"";
		else
			out << "null";
		out << ",\n  \"followup_observation_artifact\": ";
		if (FollowupCommandTemplate)
			out << "\"followup-observation.json\"";
		else
			out << "null";
		out << ",\n  \"sequence_summary_artifact\": ";
		if (FollowupCommandTemplate)
			out << "\"sequence-summary.json\"";
		else
			out << "null";
		out << "\n";
		out
			<< "}\n";
		return out.str();
	}
}
