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
	std::string PhysicsMode;
	std::string LatentAction;
	double AccelerationX = 0.0;
	double AccelerationY = 0.0;
	double AccelerationZ = 0.0;
	double DestinationX = 0.0;
	double DestinationY = 0.0;
	double DestinationZ = 0.0;
	double MoveTimer = 0.0;
	std::string MoveTargetIdentity;
	std::string MoveTargetName;
	int Health = 0;
	double Score = 0.0;
	double PriDeaths = 0.0;
	bool MovementIntent = false;
	bool InHazardZone = false;
	uint64_t KillsExact = 0;
	uint64_t DeathsExact = 0;
	uint64_t SuicidesExact = 0;
	uint64_t EnvironmentalDeathsExact = 0;
	uint64_t HazardExposedDeathsProxy = 0;
	uint64_t DirectSelfKills = 0;
	uint64_t DirectEnemyKills = 0;
	uint64_t UnassistedEnvironmentalDeaths = 0;
	uint64_t RecentEnemyContributedEnvironmentalDeathsProxy = 0;
	uint64_t AmbiguousDeaths = 0;
	uint64_t RecentEnemyMomentumContributedEnvironmentalDeathsProxy = 0;
	uint64_t HitWallEventsExact = 0;
	uint64_t PainLedgeVetoesExact = 0;
	uint64_t PainLedgeRepeatVetoesExact = 0;
	uint64_t PainLedgeRecoveryAttemptsExact = 0;
	uint64_t PainLedgeRecoveryEscapesExact = 0;
	uint64_t WallAdjustCallsExact = 0;
	uint64_t WallAdjustRepeatsExact = 0;
	uint64_t WallAdjustRecoveryAttemptsExact = 0;
	uint64_t WallAdjustRecoverySuccessesExact = 0;
	uint64_t WallAdjustForcedReplansExact = 0;
	uint64_t MoveStallDetectionsExact = 0;
	uint64_t MoveStallEpisodeResetsExact = 0;
	uint64_t MoveStallForcedReplansExact = 0;
	uint64_t MoveStallNavigationForcedReplansExact = 0;
	uint64_t MoveStallTargetlessMoveToTimeoutsExact = 0;
	double MoveStallEligibleSeconds = 0.0;
	uint64_t FailedNavigationAvoidanceActivationsExact = 0;
	uint64_t FailedNavigationSafeguardSuppressionsExact = 0;
	uint64_t FailedNavigationRoutePenaltyApplicationsExact = 0;
	uint64_t FallingSeamDetectionsExact = 0;
	uint64_t HorizontalCornerCandidateProbesExact = 0;
	uint64_t HorizontalCornerAuthorizedEscapesExact = 0;
	uint64_t HorizontalCornerTargetProgressRejectsExact = 0;
	uint64_t HorizontalCornerUnknownOrUnsafeSupportExact = 0;
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
