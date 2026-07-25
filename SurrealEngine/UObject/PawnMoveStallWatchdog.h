#pragma once

#include "Math/vec.h"

#include <cstdint>

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
		TargetlessTimeout
	};

	struct MoveStallRecoveryContext
	{
		bool Detected = false;
		MoveStallLatentMode LatentMode = MoveStallLatentMode::Other;
		bool LiveNavigationMoveToward = false;
		bool TargetlessMoveToTimeoutEnabled = false;
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

	MoveStallWatchdogState RecordMoveStallCommand(const MoveStallWatchdogState& state,
		const MoveStallCommandKey& command);
	bool ShouldForceMoveStallReplan(bool detected, bool liveNavigationMoveToward);
	MoveStallRecoveryDecision SelectMoveStallRecovery(const MoveStallRecoveryContext& context);
	MoveStallWatchdogObservation ObserveMoveStall(const MoveStallWatchdogState& state,
		const vec3& location, float elapsed, bool eligibleContext, bool latentMovementIntent,
		float progressRadius, float detectionSeconds);
}
