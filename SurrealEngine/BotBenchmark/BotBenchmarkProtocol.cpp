#include "BotBenchmarkProtocol.h"

#include <cmath>
#include <iomanip>
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
}

BotBenchmarkRunConfig::BotBenchmarkRunConfig(std::string url, std::string outputDirectory,
	uint64_t seed, uint64_t maxTicks, float fixedDelta, int difficulty)
	: URL(std::move(url)), OutputDirectory(std::move(outputDirectory)), Seed(seed),
	MaxTicks(maxTicks), FixedDelta(fixedDelta), Difficulty(difficulty)
{
}

BotBenchmarkRunConfig BotBenchmarkRunConfig::Parse(std::string url, std::string outputDirectory,
	std::string seed, std::string maxTicks, std::string fixedDelta, std::string difficulty)
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

	return BotBenchmarkRunConfig(std::move(url), std::move(outputDirectory), parsedSeed,
		parsedTicks, parsedDelta, parsedDifficulty);
}

BotBenchmarkRunSummary::BotBenchmarkRunSummary(std::string status, int exitCode, uint64_t ticks,
	double simulatedSeconds, std::string game, std::string version, std::string map,
	std::string botClass, std::string botName, std::string failureReason)
	: Status(std::move(status)), ExitCode(exitCode), Ticks(ticks), SimulatedSeconds(simulatedSeconds),
	Game(std::move(game)), Version(std::move(version)), Map(std::move(map)),
	BotClass(std::move(botClass)), BotName(std::move(botName)), FailureReason(std::move(failureReason))
{
}

std::string BotBenchmarkRunSummary::ToJson(const BotBenchmarkRunConfig& config) const
{
	std::ostringstream out;
	out << std::setprecision(9);
	out << "{\n"
		<< "  \"schema\": \"surreal-bot-benchmark-summary-v1\",\n"
		<< "  \"status\": " << JsonString(Status) << ",\n"
		<< "  \"exit_code\": " << ExitCode << ",\n"
		<< "  \"ticks\": \"" << Ticks << "\",\n"
		<< "  \"simulated_seconds\": " << SimulatedSeconds << ",\n"
		<< "  \"game\": " << JsonString(Game) << ",\n"
		<< "  \"version\": " << JsonString(Version) << ",\n"
		<< "  \"map\": " << JsonString(Map) << ",\n"
		<< "  \"bot_class\": " << JsonString(BotClass) << ",\n"
		<< "  \"bot_name\": " << JsonString(BotName) << ",\n"
		<< "  \"failure_reason\": " << JsonString(FailureReason) << ",\n"
		<< "  \"config\": {\n"
		<< "    \"url\": " << JsonString(config.GetURL()) << ",\n"
		<< "    \"output_directory\": " << JsonString(config.GetOutputDirectory()) << ",\n"
		<< "    \"seed\": \"" << config.GetSeed() << "\",\n"
		<< "    \"max_ticks\": \"" << config.GetMaxTicks() << "\",\n"
		<< "    \"fixed_delta\": " << config.GetFixedDelta() << ",\n"
		<< "    \"difficulty\": " << config.GetDifficulty() << "\n"
		<< "  }\n"
		<< "}\n";
	return out.str();
}
