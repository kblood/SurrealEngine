#pragma once

#include "Math/vec.h"

#include <cstdint>
#include <string>

namespace PawnMovement
{
	enum class MoveStallLatentMode : uint8_t
	{
		Other,
		MoveTo,
		MoveToward,
		StrafeTo,
		StrafeFacing
	};

	enum class MoveStallRecoveryDecision : uint8_t
	{
		None,
		NavigationReplan,
		TargetlessTimeout,
		DirectActorMoveTowardTimeout
	};

	struct MoveStallRecoveryContext
	{
		bool Detected = false;
		MoveStallLatentMode LatentMode = MoveStallLatentMode::Other;
		bool LiveNavigationMoveToward = false;
		bool LiveDirectActorMoveToward = false;
		bool NavigationReplanEnabled = false;
		bool TargetlessMoveToTimeoutEnabled = false;
		bool DirectActorMoveTowardTimeoutEnabled = false;
		bool Targetless = false;
		float MoveTimer = 0.0f;
		vec3 Location;
		vec3 Destination;
		float AcceptanceRadius = 0.0f;
	};

	struct MoveStallCommandKey
	{
		MoveStallLatentMode LatentMode = MoveStallLatentMode::Other;
		const void* Target = nullptr;
		vec3 Destination;
	};

	struct MoveStallWatchdogState
	{
		bool Active = false;
		bool CommandSeenSinceObservation = false;
		bool DetectionReported = false;
		vec3 Anchor;
		float NoProgressSeconds = 0.0f;
		uint8_t MissingIntentObservations = 0;
		MoveStallCommandKey Command;
	};

	struct MoveStallWatchdogObservation
	{
		MoveStallWatchdogState State;
		bool Detected = false;
		bool EpisodeReset = false;
		float EligibleSeconds = 0.0f;
	};

	// These outcomes start at the watchdog's one-shot detection, not its original
	// no-progress anchor. They deliberately distinguish an actual new navigation
	// command from a same-key reissue, which is not a recovery.
	enum class MoveStallRecoveryEpisodeEvent : uint8_t
	{
		None,
		Cleared,
		QualifiedNavigationReplan,
		IntentionalStop,
		LifeBoundary,
		RunEnd,
		Unknown
	};

	enum class MoveStallRecoveryEpisodeOutcome : uint8_t
	{
		None,
		ClearedWithin2Seconds,
		ClearedAfter2SecondsWithin5Seconds,
		ReplannedWithin5Seconds,
		Missed5SecondDeadline,
		ExcludedIntentionalStop,
		CensoredLifeBoundary,
		CensoredRunEnd,
		Unknown
	};

	struct MoveStallRecoveryEpisodeState
	{
		bool Active = false;
		float SecondsSinceDetection = 0.0f;
	};

	struct MoveStallRecoveryEpisodeUpdate
	{
		MoveStallRecoveryEpisodeState State;
		bool Started = false;
		bool Terminal = false;
		MoveStallRecoveryEpisodeOutcome Outcome = MoveStallRecoveryEpisodeOutcome::None;
	};

	MoveStallWatchdogState RecordMoveStallCommand(const MoveStallWatchdogState& state,
		const MoveStallCommandKey& command);
	bool SameMoveStallCommand(const MoveStallCommandKey& left,
		const MoveStallCommandKey& right);
	bool ShouldForceMoveStallReplan(bool detected, bool liveNavigationMoveToward);
	MoveStallRecoveryDecision SelectMoveStallRecovery(const MoveStallRecoveryContext& context);
	MoveStallRecoveryEpisodeUpdate StartMoveStallRecoveryEpisode();
	MoveStallRecoveryEpisodeUpdate AdvanceMoveStallRecoveryEpisode(
		const MoveStallRecoveryEpisodeState& state, float elapsed,
		MoveStallRecoveryEpisodeEvent event);
	MoveStallWatchdogObservation ObserveMoveStall(const MoveStallWatchdogState& state,
		const vec3& location, float elapsed, bool eligibleContext, bool latentMovementIntent,
		float progressRadius, float detectionSeconds);
}

// Telemetry-facing terminal record. Keep this independent of UPawn so the
// isolated telemetry target does not need to include the full object runtime.
struct PawnMoveStallRecoveryEpisodeRecord
{
	std::string SourcePawnActor;
	uint64_t Sequence = 0;
	uint64_t LifeId = 0;
	uint64_t EpisodeId = 0;
	float SecondsSinceDetection = 0.0f;
	PawnMovement::MoveStallRecoveryEpisodeOutcome Outcome =
		PawnMovement::MoveStallRecoveryEpisodeOutcome::None;
};

// A bounded, detection-time witness. Unlike the terminal episode record, this
// captures the exact selector input and decision before any recovery writes.
struct PawnMoveStallRecoveryDecisionRecord
{
	std::string SourcePawnActor;
	uint64_t Sequence = 0;
	uint64_t LifeId = 0;
	uint64_t EpisodeId = 0;
	uint64_t NativeTick = 0;
	PawnMovement::MoveStallLatentMode LatentMode = PawnMovement::MoveStallLatentMode::Other;
	PawnMovement::MoveStallRecoveryDecision Decision =
		PawnMovement::MoveStallRecoveryDecision::None;
	float NoProgressSeconds = 0.0f;
	float NoProgressDisplacement = 0.0f;
	float ProgressRadius = 0.0f;
	bool MoveTargetKnown = false;
	bool MoveTargetLive = false;
	int32_t MoveTargetActorIndex = -1;
	const void* MoveTargetAddress = nullptr;
	std::string MoveTargetName;
	std::string MoveTargetClass;
	bool MoveTargetIsInventory = false;
	bool MarkerKnown = false;
	bool MarkerLive = false;
	int32_t MarkerActorIndex = -1;
	const void* MarkerAddress = nullptr;
	std::string MarkerName;
	std::string MarkerClass;
	float MoveTimer = 0.0f;
};
