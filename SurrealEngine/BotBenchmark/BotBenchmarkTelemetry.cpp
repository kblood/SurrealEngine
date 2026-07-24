#include "BotBenchmarkTelemetry.h"
#include "BotBenchmarkProtocol.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace
{
	std::string EscapeJson(const std::string& value)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
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

	std::string Fixed(double value, int precision)
	{
		if (!std::isfinite(value))
			throw std::invalid_argument("bot benchmark telemetry contains a non-finite number");
		if (value == 0.0)
			value = 0.0;
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::fixed << std::setprecision(precision) << value;
		return out.str();
	}

	void Hash(uint64_t& value, const std::string& text)
	{
		for (unsigned char c : text)
		{
			value ^= c;
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

	void WriteBot(std::ostringstream& out, const BotBenchmarkBotState& bot)
	{
		out << "{\"identity\":" << JsonString(bot.Identity)
			<< ",\"actor\":" << JsonString(bot.Actor)
			<< ",\"player_name\":" << JsonString(bot.PlayerName)
			<< ",\"class\":" << JsonString(bot.ClassName)
			<< ",\"position\":{"
			<< "\"x\":" << Fixed(bot.PositionX, 6)
			<< ",\"y\":" << Fixed(bot.PositionY, 6)
			<< ",\"z\":" << Fixed(bot.PositionZ, 6) << "}"
			<< ",\"velocity\":{"
			<< "\"x\":" << Fixed(bot.VelocityX, 6)
			<< ",\"y\":" << Fixed(bot.VelocityY, 6)
			<< ",\"z\":" << Fixed(bot.VelocityZ, 6) << "}"
			<< ",\"health\":" << bot.Health
			<< ",\"score\":" << Fixed(bot.Score, 6)
			<< ",\"pri_deaths\":" << Fixed(bot.PriDeaths, 6)
			<< ",\"movement_intent\":" << (bot.MovementIntent ? "true" : "false")
			<< ",\"in_hazard_zone\":" << (bot.InHazardZone ? "true" : "false")
			<< ",\"kills_exact\":\"" << bot.KillsExact << "\""
			<< ",\"deaths_exact\":\"" << bot.DeathsExact << "\""
			<< ",\"suicides_exact\":\"" << bot.SuicidesExact << "\""
			<< ",\"environmental_deaths_exact\":\"" << bot.EnvironmentalDeathsExact << "\""
			<< ",\"hazard_exposed_deaths_proxy\":\"" << bot.HazardExposedDeathsProxy << "\""
			<< ",\"hit_wall_events_exact\":\"" << bot.HitWallEventsExact << "\""
			<< ",\"state\":" << JsonString(bot.State) << "}";
	}

	void WriteRequestedRoster(std::ostringstream& out, const BotBenchmarkRoster& roster)
	{
		out << "  \"requested_roster\": [\n";
		const auto& participants = roster.GetParticipants();
		for (size_t index = 0; index < participants.size(); index++)
		{
			const auto& participant = participants[index];
			out << "    {\"roster_index\": " << participant.RosterIndex
				<< ", \"requested_name\": " << JsonString(participant.RequestedName)
				<< ", \"external_skill\": " << participant.ExternalSkill
				<< ", \"identity_fragment\": " << JsonString(participant.CanonicalIdentityFragment) << "}";
			out << (index + 1 == participants.size() ? "\n" : ",\n");
		}
		out << "  ]";
	}
}

uint64_t BotBenchmarkTelemetryProtocol::EventCap(uint64_t maxTicks)
{
	if (maxTicks > UINT64_MAX - 2)
		throw std::overflow_error("bot benchmark telemetry event cap overflow");
	return maxTicks + 2;
}

std::string BotBenchmarkTelemetryProtocol::ConfigIdentity(const BotBenchmarkRunConfig& config)
{
	std::ostringstream canonical;
	canonical.imbue(std::locale::classic());
	canonical << "url=" << config.GetURL() << '\n'
		<< "seed=" << config.GetSeed() << '\n'
		<< "max_ticks=" << config.GetMaxTicks() << '\n'
		<< "fixed_delta=" << Fixed(config.GetFixedDelta(), 9) << '\n'
		<< "difficulty=" << config.GetDifficulty() << '\n'
		<< "bot_count=" << config.GetRoster().GetCount() << '\n';
	for (const auto& participant : config.GetRoster().GetParticipants())
		canonical << "roster=" << participant.CanonicalIdentityFragment << '\n';
	uint64_t digest = 1469598103934665603ULL;
	Hash(digest, canonical.str());
	return "fnv1a64:" + Hex64(digest);
}

std::string BotBenchmarkTelemetryProtocol::ManifestJson(const BotBenchmarkRunConfig& config)
{
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << "{\n"
		<< "  \"schema\": \"surreal-bot-benchmark-manifest-v2\",\n"
		<< "  \"driver\": \"bot-benchmark\",\n"
		<< "  \"config_id\": " << JsonString(ConfigIdentity(config)) << ",\n"
		<< "  \"url\": " << JsonString(config.GetURL()) << ",\n"
		<< "  \"output_directory\": " << JsonString(config.GetOutputDirectory()) << ",\n"
		<< "  \"seed\": \"" << config.GetSeed() << "\",\n"
		<< "  \"max_ticks\": \"" << config.GetMaxTicks() << "\",\n"
		<< "  \"fixed_delta\": " << Fixed(config.GetFixedDelta(), 9) << ",\n"
		<< "  \"difficulty\": " << config.GetDifficulty() << ",\n"
		<< "  \"bot_count\": " << config.GetRoster().GetCount() << ",\n";
	WriteRequestedRoster(out, config.GetRoster());
	out << ",\n"
		<< "  \"telemetry_event_cap\": \"" << EventCap(config.GetMaxTicks()) << "\"\n"
		<< "}\n";
	return out.str();
}

std::string BotBenchmarkTelemetryProtocol::EventJson(const std::string& configIdentity, BotBenchmarkTelemetryEvent event)
{
	std::sort(event.Bots.begin(), event.Bots.end(), [](const BotBenchmarkBotState& a, const BotBenchmarkBotState& b)
	{
		if (a.Identity != b.Identity)
			return a.Identity < b.Identity;
		return a.Actor < b.Actor;
	});

	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << "{\"schema\":\"surreal-bot-benchmark-telemetry-v2\""
		<< ",\"seq\":\"" << event.Sequence << "\""
		<< ",\"config_id\":" << JsonString(configIdentity)
		<< ",\"tick\":\"" << event.Tick << "\""
		<< ",\"simulated_seconds\":" << Fixed(event.SimulatedSeconds, 9)
		<< ",\"type\":" << JsonString(event.Type)
		<< ",\"map\":" << JsonString(event.Map)
		<< ",\"status\":" << JsonString(event.Status)
		<< ",\"failure_reason\":" << JsonString(event.FailureReason)
		<< ",\"bots\":[";
	for (size_t index = 0; index < event.Bots.size(); index++)
	{
		if (index != 0)
			out << ',';
		WriteBot(out, event.Bots[index]);
	}
	out << "]}\n";
	return out.str();
}
