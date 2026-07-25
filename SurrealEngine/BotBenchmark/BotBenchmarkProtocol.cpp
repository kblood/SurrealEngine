#include "BotBenchmarkProtocol.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
	uint64_t ParseUInt64(const std::string& text, uint64_t defaultValue, const char* name)
	{
		if (text.empty())
			return defaultValue;
		if (text.front() == '-')
			throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
		size_t end = 0;
		uint64_t value = std::stoull(text, &end);
		if (end != text.size())
			throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
		return value;
	}

	float ParseFloat(const std::string& text, float defaultValue, const char* name)
	{
		if (text.empty())
			return defaultValue;
		size_t end = 0;
		float value = std::stof(text, &end);
		if (end != text.size())
			throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
		return value;
	}

	int ParseInt(const std::string& text, int defaultValue, const char* name)
	{
		if (text.empty())
			return defaultValue;
		size_t end = 0;
		int value = std::stoi(text, &end);
		if (end != text.size())
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

	bool ParseExactBoolean(const std::optional<std::string>& value, const char* name)
	{
		if (!value.has_value())
			return false;
		if (*value == "0")
			return false;
		if (*value == "1")
			return true;
		throw std::invalid_argument(std::string("invalid ") + name + ": expected 0 or 1");
	}

	std::string Fixed(double value, int precision)
	{
		if (!std::isfinite(value))
			throw std::invalid_argument("bot benchmark summary contains a non-finite number");
		if (value == 0.0)
			value = 0.0;
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::fixed << std::setprecision(precision) << value;
		return out.str();
	}

	void WriteRequestedRoster(std::ostringstream& out, const BotBenchmarkRoster& roster, const std::string& indent)
	{
		out << indent << "\"requested_roster\": [";
		const auto& participants = roster.GetParticipants();
		if (!participants.empty())
			out << '\n';
		for (size_t index = 0; index < participants.size(); index++)
		{
			const auto& participant = participants[index];
			out << indent << "  {\"roster_index\": " << participant.RosterIndex
				<< ", \"requested_name\": " << JsonString(participant.RequestedName)
				<< ", \"external_skill\": " << participant.ExternalSkill
				<< ", \"identity_fragment\": " << JsonString(participant.CanonicalIdentityFragment) << "}";
			out << (index + 1 == participants.size() ? "\n" : ",\n");
		}
		out << indent << ']';
	}

	void WriteActualRoster(std::ostringstream& out, std::vector<BotBenchmarkActualParticipant> participants)
	{
		std::sort(participants.begin(), participants.end(), [](const auto& left, const auto& right)
		{
			return left.RosterIndex < right.RosterIndex;
		});
		out << "  \"actual_roster\": [";
		if (participants.empty())
		{
			out << ']';
			return;
		}
		out << '\n';
		for (size_t index = 0; index < participants.size(); index++)
		{
			const auto& participant = participants[index];
			if (index != 0 && participants[index - 1].RosterIndex == participant.RosterIndex)
				throw std::invalid_argument("bot benchmark actual roster contains a duplicate index");
			out << "    {\"roster_index\": " << participant.RosterIndex
				<< ", \"identity\": " << JsonString(participant.Identity)
				<< ", \"actor\": " << JsonString(participant.Actor)
				<< ", \"player_name\": " << JsonString(participant.PlayerName)
				<< ", \"class\": " << JsonString(participant.ClassName) << "}";
			out << (index + 1 == participants.size() ? "\n" : ",\n");
		}
		out << "  ]";
	}
}

BotBenchmarkRunConfig::BotBenchmarkRunConfig(std::string url, std::string outputDirectory,
	uint64_t seed, uint64_t maxTicks, float fixedDelta, int difficulty, BotBenchmarkRoster roster,
	bool harmfulZoneEscapeEnabled, bool walkingPreflightPositiveDpsVetoEnabled,
	bool hazardSwimEgressEnabled, bool hazardSwimEgressLiveEnabled,
	bool failedNavigationAvoidanceEnabled, bool fallingHazardRecoveryEnabled,
	bool fallingHazardRecoveryLiveEnabled, bool targetlessMoveToTimeoutEnabled,
	bool directActorMoveTowardTimeoutEnabled, bool targetSelectionObserverEnabled,
	bool inventoryDirectReachSupportObserverEnabled)
	: URL(std::move(url)), OutputDirectory(std::move(outputDirectory)), Seed(seed),
	MaxTicks(maxTicks), FixedDelta(fixedDelta), Difficulty(difficulty), Roster(std::move(roster)),
	HarmfulZoneEscapeEnabled(harmfulZoneEscapeEnabled),
	WalkingPreflightPositiveDpsVetoEnabled(walkingPreflightPositiveDpsVetoEnabled),
	HazardSwimEgressEnabled(hazardSwimEgressEnabled),
	HazardSwimEgressLiveEnabled(hazardSwimEgressLiveEnabled),
	FailedNavigationAvoidanceEnabled(failedNavigationAvoidanceEnabled),
	FallingHazardRecoveryEnabled(fallingHazardRecoveryEnabled),
	FallingHazardRecoveryLiveEnabled(fallingHazardRecoveryLiveEnabled),
	TargetlessMoveToTimeoutEnabled(targetlessMoveToTimeoutEnabled),
	DirectActorMoveTowardTimeoutEnabled(directActorMoveTowardTimeoutEnabled),
	TargetSelectionObserverEnabled(targetSelectionObserverEnabled),
	InventoryDirectReachSupportObserverEnabled(inventoryDirectReachSupportObserverEnabled)
{
}

BotBenchmarkRunConfig BotBenchmarkRunConfig::Parse(std::string url, std::string outputDirectory,
	std::string seed, std::string maxTicks, std::string fixedDelta, std::string difficulty,
	std::optional<std::string> botCount, std::optional<std::string> perBotSkills,
	std::optional<std::string> requestedNames, std::optional<std::string> harmfulZoneEscape,
	std::optional<std::string> walkingPreflightPositiveDpsVeto,
	std::optional<std::string> hazardSwimEgress, std::optional<std::string> hazardSwimEgressLive,
	std::optional<std::string> failedNavigationAvoidance,
	std::optional<std::string> fallingHazardRecovery,
	std::optional<std::string> fallingHazardRecoveryLive,
	std::optional<std::string> targetlessMoveToTimeout,
	std::optional<std::string> directActorMoveTowardTimeout,
	std::optional<std::string> targetSelectionObserver,
	std::optional<std::string> inventoryDirectReachSupportObserver)
{
	if (url.empty())
		url = "DM-Morbias][?Game=Botpack.DeathMatchPlus";
	if (outputDirectory.empty())
		outputDirectory = "botbench-output";

	const uint64_t parsedSeed = ParseUInt64(seed, 104729, "bot benchmark seed");
	const uint64_t parsedTicks = ParseUInt64(maxTicks, 600, "bot benchmark tick limit");
	const float parsedDelta = ParseFloat(fixedDelta, 1.0f / 60.0f, "bot benchmark fixed delta");
	const int parsedDifficulty = ParseInt(difficulty, 3, "bot benchmark difficulty");
	if (parsedTicks == 0 || parsedTicks > 10000000)
		throw std::invalid_argument("bot benchmark tick limit must be between 1 and 10000000");
	if (!std::isfinite(parsedDelta) || parsedDelta <= 0.0f || parsedDelta > 1.0f)
		throw std::invalid_argument("bot benchmark fixed delta must be finite and between 0 and 1");
	if (parsedDifficulty < 0 || parsedDifficulty > 7)
		throw std::invalid_argument("bot benchmark difficulty must be between 0 and 7");
	BotBenchmarkRoster roster = BotBenchmarkRoster::Parse(
		std::move(botCount), std::move(perBotSkills), std::move(requestedNames), parsedDifficulty);
	const bool parsedHarmfulZoneEscape = ParseExactBoolean(
		harmfulZoneEscape, "bot benchmark harmful-zone escape");
	const bool parsedWalkingPreflightPositiveDpsVeto = ParseExactBoolean(
		walkingPreflightPositiveDpsVeto, "bot benchmark walking-preflight positive-DPS veto");
	const bool parsedHazardSwimEgress = ParseExactBoolean(
		hazardSwimEgress, "bot benchmark hazard-swim egress");
	const bool parsedHazardSwimEgressLive = ParseExactBoolean(
		hazardSwimEgressLive, "bot benchmark hazard-swim egress live");
	const bool parsedFailedNavigationAvoidance = ParseExactBoolean(
		failedNavigationAvoidance, "bot benchmark failed-navigation avoidance");
	const bool parsedFallingHazardRecovery = ParseExactBoolean(
		fallingHazardRecovery, "bot benchmark falling-hazard recovery");
	const bool parsedFallingHazardRecoveryLive = ParseExactBoolean(
		fallingHazardRecoveryLive, "bot benchmark falling-hazard recovery live");
	const bool parsedTargetlessMoveToTimeout = ParseExactBoolean(
		targetlessMoveToTimeout, "bot benchmark targetless MoveTo timeout");
	const bool parsedDirectActorMoveTowardTimeout = ParseExactBoolean(
		directActorMoveTowardTimeout, "bot benchmark direct-actor MoveToward timeout");
	const bool parsedTargetSelectionObserver = ParseExactBoolean(
		targetSelectionObserver, "bot benchmark target-selection observer");
	const bool parsedInventoryDirectReachSupportObserver = ParseExactBoolean(
		inventoryDirectReachSupportObserver,
		"bot benchmark inventory direct-reach support observer");

	return BotBenchmarkRunConfig(std::move(url), std::move(outputDirectory), parsedSeed,
		parsedTicks, parsedDelta, parsedDifficulty, std::move(roster), parsedHarmfulZoneEscape,
		parsedWalkingPreflightPositiveDpsVeto, parsedHazardSwimEgress,
		parsedHazardSwimEgressLive, parsedFailedNavigationAvoidance,
		parsedFallingHazardRecovery, parsedFallingHazardRecoveryLive,
		parsedTargetlessMoveToTimeout, parsedDirectActorMoveTowardTimeout,
		parsedTargetSelectionObserver, parsedInventoryDirectReachSupportObserver);
}

BotBenchmarkRunSummary::BotBenchmarkRunSummary(std::string status, int exitCode, uint64_t ticks,
	double simulatedSeconds, std::string game, std::string version, std::string map, std::string failureReason,
	std::vector<BotBenchmarkActualParticipant> actualRoster)
	: Status(std::move(status)), ExitCode(exitCode), Ticks(ticks), SimulatedSeconds(simulatedSeconds),
	Game(std::move(game)), Version(std::move(version)), Map(std::move(map)),
	FailureReason(std::move(failureReason)), ActualRoster(std::move(actualRoster))
{
}

std::string BotBenchmarkRunSummary::ToJson(const BotBenchmarkRunConfig& config) const
{
	for (const auto& participant : ActualRoster)
	{
		if (participant.RosterIndex >= config.GetRoster().GetCount())
			throw std::invalid_argument("bot benchmark actual roster index exceeds requested roster");
	}
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << "{\n"
		<< "  \"schema\": \"surreal-bot-benchmark-summary-v2\",\n"
		<< "  \"status\": " << JsonString(Status) << ",\n"
		<< "  \"exit_code\": " << ExitCode << ",\n"
		<< "  \"ticks\": \"" << Ticks << "\",\n"
		<< "  \"simulated_seconds\": " << Fixed(SimulatedSeconds, 9) << ",\n"
		<< "  \"game\": " << JsonString(Game) << ",\n"
		<< "  \"version\": " << JsonString(Version) << ",\n"
		<< "  \"map\": " << JsonString(Map) << ",\n"
		<< "  \"failure_reason\": " << JsonString(FailureReason) << ",\n";
	WriteRequestedRoster(out, config.GetRoster(), "  ");
	out << ",\n";
	WriteActualRoster(out, ActualRoster);
	out << ",\n"
		<< "  \"config\": {\n"
		<< "    \"url\": " << JsonString(config.GetURL()) << ",\n"
		<< "    \"output_directory\": " << JsonString(config.GetOutputDirectory()) << ",\n"
		<< "    \"seed\": \"" << config.GetSeed() << "\",\n"
		<< "    \"max_ticks\": \"" << config.GetMaxTicks() << "\",\n"
		<< "    \"fixed_delta\": " << Fixed(config.GetFixedDelta(), 9) << ",\n"
		<< "    \"difficulty\": " << config.GetDifficulty() << ",\n"
		<< "    \"bot_count\": " << config.GetRoster().GetCount() << ",\n"
		<< "    \"harmful_zone_escape_enabled\": "
		<< (config.IsHarmfulZoneEscapeEnabled() ? "true" : "false") << ",\n"
		<< "    \"walking_preflight_positive_dps_veto_enabled\": "
		<< (config.IsWalkingPreflightPositiveDpsVetoEnabled() ? "true" : "false") << ",\n"
		<< "    \"hazard_swim_egress_enabled\": "
		<< (config.IsHazardSwimEgressEnabled() ? "true" : "false") << ",\n"
		<< "    \"hazard_swim_egress_live_enabled\": "
		<< (config.IsHazardSwimEgressLiveEnabled() ? "true" : "false") << ",\n"
		<< "    \"failed_navigation_avoidance_enabled\": "
		<< (config.IsFailedNavigationAvoidanceEnabled() ? "true" : "false") << ",\n"
		<< "    \"falling_hazard_recovery_enabled\": "
		<< (config.IsFallingHazardRecoveryEnabled() ? "true" : "false") << ",\n"
		<< "    \"falling_hazard_recovery_live_enabled\": "
		<< (config.IsFallingHazardRecoveryLiveEnabled() ? "true" : "false") << ",\n"
		<< "    \"targetless_move_to_timeout_enabled\": "
		<< (config.IsTargetlessMoveToTimeoutEnabled() ? "true" : "false") << ",\n"
		<< "    \"direct_actor_move_toward_timeout_enabled\": "
		<< (config.IsDirectActorMoveTowardTimeoutEnabled() ? "true" : "false") << ",\n"
		<< "    \"target_selection_observer_enabled\": "
		<< (config.IsTargetSelectionObserverEnabled() ? "true" : "false") << ",\n"
		<< "    \"inventory_direct_reach_support_observer_enabled\": "
		<< (config.IsInventoryDirectReachSupportObserverEnabled() ? "true" : "false") << "\n"
		<< "  }\n"
		<< "}\n";
	return out.str();
}
