#pragma once

#include "UObject/PawnMoveStallWatchdog.h"

#include <cstdint>
#include <string>
#include <vector>

class Engine;
class HeadlessDriverRegistry;

// A narrow cross-game fixture for the production move-stall watchdog. Its
// default mode creates a targetless native MoveTo with zero acceleration and
// supplies one safe measured displacement. The explicit direct-actor mode
// instead verifies the default-off MoveToward timeout against a live, non-pawn,
// non-navigation actor.
struct BotMoveStallRecoveryFixtureConfig
{
	std::string URL;
	int ExternalSkill = 3;
	bool DirectActorMoveTowardTimeout = false;
};

struct BotMoveStallRecoveryFixtureResult
{
	bool Ran = false;
	bool Passed = false;
	bool DirectActorFixtureMode = false;
	bool SafeWalkingStart = false;
	bool TargetlessMoveToArmed = false;
	bool DirectActorMoveTowardArmed = false;
	bool DirectActorTimeoutApplied = false;
	bool DirectActorTargetDestroyRequested = false;
	bool StalledWithoutDisplacement = false;
	bool SafeRecoveryRelocation = false;
	std::string FailureReason;
	std::string PawnActor;
	std::string DirectActorTarget;
	std::string DirectActorTargetClass;
	uint64_t Detections = 0;
	uint64_t EpisodeStarts = 0;
	uint64_t EpisodeResets = 0;
	uint64_t ClearedWithin2Seconds = 0;
	uint64_t ClearedAfter2SecondsWithin5Seconds = 0;
	uint64_t ReplannedWithin5Seconds = 0;
	uint64_t Missed5SecondDeadline = 0;
	uint64_t ExcludedIntentionalStops = 0;
	uint64_t CensoredLifeBoundaries = 0;
	uint64_t CensoredRunEnd = 0;
	uint64_t Unknown = 0;
	uint64_t RecordOverflows = 0;
	std::vector<PawnMoveStallRecoveryEpisodeRecord> Records;
	std::vector<PawnMoveStallRecoveryDecisionRecord> DecisionRecords;
};

class BotMoveStallRecoveryFixture
{
public:
	static BotMoveStallRecoveryFixtureResult Run(
		Engine& engine, const BotMoveStallRecoveryFixtureConfig& config);
};

void RegisterBotMoveStallRecoveryFixtureDriver(HeadlessDriverRegistry& registry);
