#include "BotSpectatorMatch.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace
{
	int ParseDifficulty(const std::string& text)
	{
		if (text.empty())
			return 3;
		int value = 0;
		const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
		if (result.ec != std::errc() || result.ptr != text.data() + text.size() || value < 0 || value > 7)
			throw std::invalid_argument("bot spectator difficulty must be a decimal integer between 0 and 7");
		return value;
	}

	double ParseDuration(const std::string& text)
	{
		if (text.empty())
			return 0.0;
		char* end = nullptr;
		const double value = std::strtod(text.c_str(), &end);
		if (end != text.c_str() + text.size() || !std::isfinite(value) || value < 0.0 || value > 86400.0)
			throw std::invalid_argument(
				"bot spectator duration must be finite and between 0 and 86400 seconds");
		return value;
	}
}

BotSpectatorMatchConfig::BotSpectatorMatchConfig(
	std::string url,
	double durationSeconds,
	BotBenchmarkRoster roster)
	: URL(std::move(url)), DurationSeconds(durationSeconds), Roster(std::move(roster))
{
}

BotSpectatorMatchConfig BotSpectatorMatchConfig::Parse(
	std::string url,
	std::string difficulty,
	std::string durationSeconds,
	std::optional<std::string> botCount,
	std::optional<std::string> perBotSkills)
{
	if (url.empty())
		url = "DM-Morbias][?Game=Botpack.DeathMatchPlus";
	const int parsedDifficulty = ParseDifficulty(difficulty);
	if (!botCount)
		botCount = "4";
	BotBenchmarkRoster roster = BotBenchmarkRoster::Parse(
		std::move(botCount), std::move(perBotSkills), {}, parsedDifficulty);
	return BotSpectatorMatchConfig(
		std::move(url), ParseDuration(durationSeconds), std::move(roster));
}
