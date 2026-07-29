#pragma once

#include "Math/vec.h"

#include <array>
#include <cstdint>

namespace PawnMovement
{
	struct WallAdjustRecoveryState
	{
		bool Active = false;
		vec3 Origin;
		float RemainingSeconds = 0.0f;
		float AgeSeconds = 0.0f;
		uint32_t ObservationCount = 0;
		uint32_t RepeatOrdinal = 0;
		bool SteeringActive = false;
		bool RecoveryAttempted = false;
		bool ReplanPending = false;
		vec3 SteeringOrigin;
		vec2 UnsafeDirection;
		vec2 EscapeDirection;
		float SteeringRemainingSeconds = 0.0f;
	};

	struct WallAdjustObservation
	{
		WallAdjustRecoveryState State;
		bool Repeated = false;
		bool Escalate = false;
	};

	struct WallAdjustRecoveryAdvance
	{
		WallAdjustRecoveryState State;
		bool Escaped = false;
		bool TimedOut = false;
	};

	enum class WallAdjustSteeringRequest
	{
		Inactive,
		Apply,
		Clear
	};

	struct WallAdjustCandidateProbe
	{
		bool SweepClear = false;
		bool NonPainFootRegion = false;
		bool WalkableSupport = false;
	};

	WallAdjustRecoveryAdvance AdvanceWallAdjustRecovery(const WallAdjustRecoveryState& state,
		const vec3& location, float elapsed, float repeatRadius, float escapeRadius, bool validContext);
	WallAdjustObservation ObserveWallAdjust(const WallAdjustRecoveryState& previous,
		const vec3& location, float windowSeconds, float repeatRadius,
		float minimumSustainedSeconds, uint32_t minimumObservations);
	WallAdjustRecoveryState BeginWallAdjustSteering(const WallAdjustRecoveryState& state,
		const vec3& origin, const vec2& unsafeDirection, const vec2& escapeDirection,
		float steeringSeconds);
	WallAdjustSteeringRequest EvaluateWallAdjustSteeringRequest(
		const WallAdjustRecoveryState& state, const vec2& requestedDirection,
		float minimumUnsafeAlignment);
	std::array<vec2, 3> WallAdjustEscapeDirections(const vec2& approachDirection);
	std::array<int, 3> WallAdjustCandidateOrder(uint32_t repeatOrdinal);
	int SelectWallAdjustCandidate(const std::array<WallAdjustCandidateProbe, 3>& probes,
		const std::array<int, 3>& order);
}
