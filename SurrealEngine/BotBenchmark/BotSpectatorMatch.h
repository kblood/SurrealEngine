#pragma once

#include "BotBenchmarkRoster.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class Engine;
class UPawn;

class BotSpectatorMatchConfig
{
public:
	static BotSpectatorMatchConfig Parse(
		std::string url,
		std::string difficulty,
		std::string durationSeconds,
		std::optional<std::string> botCount = {},
		std::optional<std::string> perBotSkills = {});

	const std::string& GetURL() const { return URL; }
	double GetDurationSeconds() const { return DurationSeconds; }
	const BotBenchmarkRoster& GetRoster() const { return Roster; }

private:
	BotSpectatorMatchConfig(std::string url, double durationSeconds, BotBenchmarkRoster roster);

	std::string URL;
	double DurationSeconds = 0.0;
	BotBenchmarkRoster Roster;
};

class BotSpectatorMatch
{
public:
	explicit BotSpectatorMatch(BotSpectatorMatchConfig config);

	void Setup(Engine& engine);
	void Tick(Engine& engine, float elapsedSeconds);

private:
	void FollowLiveBot(Engine& engine);

	BotSpectatorMatchConfig Config;
	double ElapsedSeconds = 0.0;
	std::vector<UPawn*> ControlledBots;
};

// Returns null unless --bot-spectator is present. This must be called during
// Engine::Setup while the process command-line object is still alive on web.
std::unique_ptr<BotSpectatorMatch> CreateBotSpectatorMatchFromCommandLine();
