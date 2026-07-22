#include "BotBenchmark/BotBenchmarkProtocol.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	const BotBenchmarkRunConfig defaults = BotBenchmarkRunConfig::Parse({}, {}, {}, {}, {}, {});
	if (defaults.GetSeed() != 104729 || defaults.GetMaxTicks() != 600 ||
		defaults.GetDifficulty() != 3 || defaults.GetURL().empty())
		return Fail("default bot benchmark configuration was incorrect");

	const BotBenchmarkRunConfig parsed = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7");
	if (parsed.GetSeed() != UINT64_MAX || parsed.GetMaxTicks() != 72 ||
		parsed.GetFixedDelta() != 0.02f || parsed.GetDifficulty() != 7)
		return Fail("explicit bot benchmark configuration was incorrect");

	bool rejectedDifficulty = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, {}, {}, {}, "8");
	}
	catch (const std::invalid_argument&)
	{
		rejectedDifficulty = true;
	}
	if (!rejectedDifficulty)
		return Fail("invalid bot difficulty was accepted");

	bool rejectedTicks = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, {}, "0", {}, {});
	}
	catch (const std::invalid_argument&)
	{
		rejectedTicks = true;
	}
	if (!rejectedTicks)
		return Fail("zero bot benchmark ticks were accepted");

	bool rejectedNegativeSeed = false;
	try
	{
		BotBenchmarkRunConfig::Parse({}, {}, "-1", {}, {}, {});
	}
	catch (const std::invalid_argument&)
	{
		rejectedNegativeSeed = true;
	}
	if (!rejectedNegativeSeed)
		return Fail("negative bot benchmark seed was accepted");

	const BotBenchmarkRunSummary summary("failed", 2, 12, 0.24,
		"UT\n99", "436", "DM-Test", "Botpack.Bot", "A\"Bot", "controlled failure");
	const std::string json = summary.ToJson(parsed);
	if (json.find("surreal-bot-benchmark-summary-v1") == std::string::npos ||
		json.find("\"seed\": \"18446744073709551615\"") == std::string::npos ||
		json.find("UT\\n99") == std::string::npos || json.find("A\\\"Bot") == std::string::npos ||
		json.find("\"exit_code\": 2") == std::string::npos)
		return Fail("bot benchmark summary did not preserve exact config or JSON escaping");

	return 0;
}
