#pragma once

#include "Math/vec.h"

#include <array>

namespace PawnMovement
{
	struct PainLedgeRecoveryState
	{
		bool Active = false;
		bool RecoveryAttempted = false;
		vec3 Origin;
		vec2 UnsafeDirection;
		float RemainingSeconds = 0.0f;
	};

	struct PainLedgeVetoRecord
	{
		PainLedgeRecoveryState State;
		bool Repeat = false;
	};

	struct PainLedgeRecoveryAdvance
	{
		PainLedgeRecoveryState State;
		bool Escaped = false;
	};

	enum class PainLedgeRecoveryRequest
	{
		Inactive,
		Recover,
		Clear
	};

	struct PainLedgeRecoveryCandidateProbe
	{
		bool SweepClear = false;
		bool NonPainFootRegion = false;
		bool WalkableSupport = false;
	};

	PainLedgeVetoRecord RecordPainLedgeVeto(const PainLedgeRecoveryState& previous,
		const vec3& origin, const vec2& unsafeDirection, float durationSeconds,
		float repeatRadius, float repeatAlignment);
	PainLedgeRecoveryAdvance AdvancePainLedgeRecovery(const PainLedgeRecoveryState& state,
		const vec3& location, float elapsed, float recoveryRadius, bool validContext);
	PainLedgeRecoveryRequest EvaluatePainLedgeRecoveryRequest(const PainLedgeRecoveryState& state,
		const vec2& requestedDirection, float minimumAlignment);
	bool ShouldRejectPainLedgeInventoryCommand(const PainLedgeRecoveryState& state,
		bool directInventoryCommand, const vec2& directDirection, float minimumAlignment);
	std::array<vec2, 3> PainLedgeRecoveryCandidateDirections(const vec2& unsafeDirection);
	int SelectPainLedgeRecoveryCandidate(
		const std::array<PainLedgeRecoveryCandidateProbe, 3>& probes);
}
