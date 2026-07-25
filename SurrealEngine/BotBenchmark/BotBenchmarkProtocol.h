#pragma once

#include "BotBenchmarkRoster.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct BotBenchmarkActualParticipant
{
	size_t RosterIndex = 0;
	std::string Identity;
	std::string Actor;
	std::string PlayerName;
	std::string ClassName;
};

class BotBenchmarkRunConfig
{
public:
	static BotBenchmarkRunConfig Parse(
		std::string url,
		std::string outputDirectory,
		std::string seed,
		std::string maxTicks,
		std::string fixedDelta,
		std::string difficulty,
		std::optional<std::string> botCount = {},
		std::optional<std::string> perBotSkills = {},
		std::optional<std::string> requestedNames = {},
		std::optional<std::string> harmfulZoneEscape = {},
		std::optional<std::string> walkingPreflightPositiveDpsVeto = {},
		std::optional<std::string> hazardSwimEgress = {},
		std::optional<std::string> hazardSwimEgressLive = {});

	const std::string& GetURL() const { return URL; }
	const std::string& GetOutputDirectory() const { return OutputDirectory; }
	uint64_t GetSeed() const { return Seed; }
	uint64_t GetMaxTicks() const { return MaxTicks; }
	float GetFixedDelta() const { return FixedDelta; }
	int GetDifficulty() const { return Difficulty; }
	const BotBenchmarkRoster& GetRoster() const { return Roster; }
	bool IsHarmfulZoneEscapeEnabled() const { return HarmfulZoneEscapeEnabled; }
	bool IsWalkingPreflightPositiveDpsVetoEnabled() const
	{
		return WalkingPreflightPositiveDpsVetoEnabled;
	}
	bool IsHazardSwimEgressEnabled() const { return HazardSwimEgressEnabled; }
	bool IsHazardSwimEgressLiveEnabled() const { return HazardSwimEgressLiveEnabled; }

private:
	BotBenchmarkRunConfig(std::string url, std::string outputDirectory, uint64_t seed,
		uint64_t maxTicks, float fixedDelta, int difficulty, BotBenchmarkRoster roster,
		bool harmfulZoneEscapeEnabled, bool walkingPreflightPositiveDpsVetoEnabled,
		bool hazardSwimEgressEnabled, bool hazardSwimEgressLiveEnabled);

	std::string URL;
	std::string OutputDirectory;
	uint64_t Seed = 0;
	uint64_t MaxTicks = 0;
	float FixedDelta = 0.0f;
	int Difficulty = 0;
	BotBenchmarkRoster Roster;
	bool HarmfulZoneEscapeEnabled = false;
	bool WalkingPreflightPositiveDpsVetoEnabled = false;
	bool HazardSwimEgressEnabled = false;
	bool HazardSwimEgressLiveEnabled = false;
};

class BotBenchmarkRunSummary
{
public:
	BotBenchmarkRunSummary(std::string status, int exitCode, uint64_t ticks,
		double simulatedSeconds, std::string game, std::string version,
		std::string map, std::string failureReason,
		std::vector<BotBenchmarkActualParticipant> actualRoster);

	std::string ToJson(const BotBenchmarkRunConfig& config) const;

private:
	std::string Status;
	int ExitCode = 0;
	uint64_t Ticks = 0;
	double SimulatedSeconds = 0.0;
	std::string Game;
	std::string Version;
	std::string Map;
	std::string FailureReason;
	std::vector<BotBenchmarkActualParticipant> ActualRoster;
};
