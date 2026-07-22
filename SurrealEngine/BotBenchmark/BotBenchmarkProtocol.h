#pragma once

#include <cstdint>
#include <string>

class BotBenchmarkRunConfig
{
public:
	static BotBenchmarkRunConfig Parse(
		std::string url,
		std::string outputDirectory,
		std::string seed,
		std::string maxTicks,
		std::string fixedDelta,
		std::string difficulty);

	const std::string& GetURL() const { return URL; }
	const std::string& GetOutputDirectory() const { return OutputDirectory; }
	uint64_t GetSeed() const { return Seed; }
	uint64_t GetMaxTicks() const { return MaxTicks; }
	float GetFixedDelta() const { return FixedDelta; }
	int GetDifficulty() const { return Difficulty; }

private:
	BotBenchmarkRunConfig(std::string url, std::string outputDirectory, uint64_t seed,
		uint64_t maxTicks, float fixedDelta, int difficulty);

	std::string URL;
	std::string OutputDirectory;
	uint64_t Seed = 0;
	uint64_t MaxTicks = 0;
	float FixedDelta = 0.0f;
	int Difficulty = 0;
};

class BotBenchmarkRunSummary
{
public:
	BotBenchmarkRunSummary(std::string status, int exitCode, uint64_t ticks,
		double simulatedSeconds, std::string game, std::string version,
		std::string map, std::string botClass, std::string botName,
		std::string failureReason);

	std::string ToJson(const BotBenchmarkRunConfig& config) const;

private:
	std::string Status;
	int ExitCode = 0;
	uint64_t Ticks = 0;
	double SimulatedSeconds = 0.0;
	std::string Game;
	std::string Version;
	std::string Map;
	std::string BotClass;
	std::string BotName;
	std::string FailureReason;
};
