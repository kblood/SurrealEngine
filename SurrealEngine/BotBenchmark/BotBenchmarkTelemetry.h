#pragma once

#include <cstdint>
#include <string>
#include <vector>

class BotBenchmarkRunConfig;

struct BotBenchmarkBotState
{
	std::string Identity;
	std::string Actor;
	std::string PlayerName;
	std::string ClassName;
	std::string State;
	double PositionX = 0.0;
	double PositionY = 0.0;
	double PositionZ = 0.0;
	double VelocityX = 0.0;
	double VelocityY = 0.0;
	double VelocityZ = 0.0;
	int Health = 0;
};

struct BotBenchmarkTelemetryEvent
{
	uint64_t Sequence = 0;
	uint64_t Tick = 0;
	double SimulatedSeconds = 0.0;
	std::string Type;
	std::string Map;
	std::string Status;
	std::string FailureReason;
	std::vector<BotBenchmarkBotState> Bots;
};

class BotBenchmarkTelemetryProtocol
{
public:
	static uint64_t EventCap(uint64_t maxTicks);
	static std::string ConfigIdentity(const BotBenchmarkRunConfig& config);
	static std::string ManifestJson(const BotBenchmarkRunConfig& config);
	static std::string EventJson(const std::string& configIdentity, BotBenchmarkTelemetryEvent event);
};
