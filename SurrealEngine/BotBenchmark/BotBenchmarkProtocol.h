#pragma once

#include "BotBenchmarkAiFrameTiming.h"

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
		std::optional<std::string> hazardSwimEgressLive = {},
		std::optional<std::string> failedNavigationAvoidance = {},
		std::optional<std::string> fallingHazardRecovery = {},
		std::optional<std::string> fallingHazardRecoveryLive = {},
		std::optional<std::string> targetlessMoveToTimeout = {},
		std::optional<std::string> directActorMoveTowardTimeout = {},
		std::optional<std::string> targetSelectionObserver = {},
		std::optional<std::string> inventoryDirectReachSupportObserver = {},
		std::optional<std::string> inventoryMarkerDirectReachSafety = {},
		std::optional<std::string> nativePathCommitObserver = {},
		std::optional<std::string> directReachCommandObserver = {},
		std::optional<std::string> pickTargetObserver = {});

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
	bool IsFailedNavigationAvoidanceEnabled() const { return FailedNavigationAvoidanceEnabled; }
	bool IsFallingHazardRecoveryEnabled() const { return FallingHazardRecoveryEnabled; }
	bool IsFallingHazardRecoveryLiveEnabled() const { return FallingHazardRecoveryLiveEnabled; }
	bool IsTargetlessMoveToTimeoutEnabled() const { return TargetlessMoveToTimeoutEnabled; }
	bool IsDirectActorMoveTowardTimeoutEnabled() const
	{
		return DirectActorMoveTowardTimeoutEnabled;
	}
	bool IsTargetSelectionObserverEnabled() const { return TargetSelectionObserverEnabled; }
	bool IsPickTargetObserverEnabled() const { return PickTargetObserverEnabled; }
	bool IsInventoryDirectReachSupportObserverEnabled() const
	{
		return InventoryDirectReachSupportObserverEnabled;
	}
	bool IsInventoryMarkerDirectReachSafetyEnabled() const
	{
		return InventoryMarkerDirectReachSafetyEnabled;
	}
	bool IsNativePathCommitObserverEnabled() const { return NativePathCommitObserverEnabled; }
	bool IsDirectReachCommandObserverEnabled() const { return DirectReachCommandObserverEnabled; }

private:
	BotBenchmarkRunConfig(std::string url, std::string outputDirectory, uint64_t seed,
		uint64_t maxTicks, float fixedDelta, int difficulty, BotBenchmarkRoster roster,
		bool harmfulZoneEscapeEnabled, bool walkingPreflightPositiveDpsVetoEnabled,
		bool hazardSwimEgressEnabled, bool hazardSwimEgressLiveEnabled,
		bool failedNavigationAvoidanceEnabled, bool fallingHazardRecoveryEnabled,
		bool fallingHazardRecoveryLiveEnabled, bool targetlessMoveToTimeoutEnabled,
		bool directActorMoveTowardTimeoutEnabled, bool targetSelectionObserverEnabled,
		bool inventoryDirectReachSupportObserverEnabled, bool nativePathCommitObserverEnabled,
		bool inventoryMarkerDirectReachSafetyEnabled, bool directReachCommandObserverEnabled,
		bool pickTargetObserverEnabled);

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
	bool FailedNavigationAvoidanceEnabled = false;
	bool FallingHazardRecoveryEnabled = false;
	bool FallingHazardRecoveryLiveEnabled = false;
	bool TargetlessMoveToTimeoutEnabled = false;
	bool DirectActorMoveTowardTimeoutEnabled = false;
	bool TargetSelectionObserverEnabled = false;
	bool PickTargetObserverEnabled = false;
	bool InventoryDirectReachSupportObserverEnabled = false;
	bool InventoryMarkerDirectReachSafetyEnabled = false;
	bool NativePathCommitObserverEnabled = false;
	bool DirectReachCommandObserverEnabled = false;
};

// Each component is measured inside the benchmark-only AI timing scope. The
// aggregate remains the release-gate value; components make an over-budget
// observation run actionable without timing game simulation or file I/O.
struct BotBenchmarkAiFrameTimingComponents
{
	BotBenchmarkAiFrameTimingSummary NavigationCoverage;
	BotBenchmarkAiFrameTimingSummary ShadowObservationAndPolicy;
	BotBenchmarkAiFrameTimingSummary StateSampling;
};

class BotBenchmarkRunSummary
{
public:
	BotBenchmarkRunSummary(std::string status, int exitCode, uint64_t ticks,
		double simulatedSeconds, std::string game, std::string version,
		std::string map, std::string failureReason,
		std::vector<BotBenchmarkActualParticipant> actualRoster,
		BotBenchmarkAiFrameTimingSummary aiFrameTiming = {},
		BotBenchmarkAiFrameTimingComponents aiFrameTimingComponents = {});

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
	BotBenchmarkAiFrameTimingSummary AiFrameTiming;
	BotBenchmarkAiFrameTimingComponents AiFrameTimingComponents;
};
