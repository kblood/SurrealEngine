#include "PawnPainLedgeRecovery.h"

#include <algorithm>
#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec2& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		vec2 SafeNormalize(const vec2& value)
		{
			const float lengthSquared = dot(value, value);
			if (!IsFinite(value) || !std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
				return vec2(0.0f);
			return value / std::sqrt(lengthSquared);
		}

		PainLedgeRecoveryState ClearedState()
		{
			return {};
		}
	}

	PainLedgeVetoRecord RecordPainLedgeVeto(const PainLedgeRecoveryState& previous,
		const vec3& origin, const vec2& unsafeDirection, float durationSeconds,
		float repeatRadius, float repeatAlignment)
	{
		PainLedgeVetoRecord result;
		const vec2 normalizedDirection = SafeNormalize(unsafeDirection);
		if (!IsFinite(origin) || normalizedDirection == vec2(0.0f)
			|| !std::isfinite(durationSeconds) || durationSeconds <= 0.0f)
			return result;

		if (previous.Active && previous.RemainingSeconds > 0.0f
			&& std::isfinite(repeatRadius) && repeatRadius >= 0.0f
			&& std::isfinite(repeatAlignment))
		{
			const vec2 originDelta = origin.xy() - previous.Origin.xy();
			const vec2 previousDirection = SafeNormalize(previous.UnsafeDirection);
			result.Repeat = dot(originDelta, originDelta) <= repeatRadius * repeatRadius
				&& dot(previousDirection, normalizedDirection) >= repeatAlignment;
		}

		result.State.Active = true;
		result.State.Origin = origin;
		result.State.UnsafeDirection = normalizedDirection;
		result.State.RemainingSeconds = durationSeconds;
		return result;
	}

	PainLedgeRecoveryAdvance AdvancePainLedgeRecovery(const PainLedgeRecoveryState& state,
		const vec3& location, float elapsed, float recoveryRadius, bool validContext)
	{
		PainLedgeRecoveryAdvance result;
		if (!state.Active)
			return result;
		if (!validContext || !IsFinite(location) || !std::isfinite(elapsed) || elapsed < 0.0f
			|| !std::isfinite(recoveryRadius) || recoveryRadius < 0.0f)
			return result;

		const vec2 originDelta = location.xy() - state.Origin.xy();
		if (dot(originDelta, originDelta) > recoveryRadius * recoveryRadius)
		{
			result.Escaped = state.RecoveryAttempted;
			return result;
		}

		result.State = state;
		result.State.RemainingSeconds = std::max(0.0f, state.RemainingSeconds - elapsed);
		if (result.State.RemainingSeconds <= 0.0f)
			result.State = ClearedState();
		return result;
	}

	PainLedgeRecoveryRequest EvaluatePainLedgeRecoveryRequest(const PainLedgeRecoveryState& state,
		const vec2& requestedDirection, float minimumAlignment)
	{
		if (!state.Active)
			return PainLedgeRecoveryRequest::Inactive;
		const vec2 requested = SafeNormalize(requestedDirection);
		const vec2 unsafe = SafeNormalize(state.UnsafeDirection);
		if (requested == vec2(0.0f) || unsafe == vec2(0.0f) || !std::isfinite(minimumAlignment))
			return PainLedgeRecoveryRequest::Clear;
		return dot(requested, unsafe) >= minimumAlignment
			? PainLedgeRecoveryRequest::Recover : PainLedgeRecoveryRequest::Clear;
	}

	bool ShouldRejectPainLedgeInventoryCommand(const PainLedgeRecoveryState& state,
		bool directInventoryCommand, const vec2& directDirection, float minimumAlignment)
	{
		if (!directInventoryCommand || !state.Active
			|| !std::isfinite(state.RemainingSeconds) || state.RemainingSeconds <= 0.0f)
			return false;
		return EvaluatePainLedgeRecoveryRequest(state, directDirection, minimumAlignment)
			== PainLedgeRecoveryRequest::Recover;
	}

	std::array<vec2, 3> PainLedgeRecoveryCandidateDirections(const vec2& unsafeDirection)
	{
		const vec2 unsafe = SafeNormalize(unsafeDirection);
		if (unsafe == vec2(0.0f))
			return {};
		return {
			-unsafe,
			vec2(-unsafe.y, unsafe.x),
			vec2(unsafe.y, -unsafe.x)
		};
	}

	int SelectPainLedgeRecoveryCandidate(
		const std::array<PainLedgeRecoveryCandidateProbe, 3>& probes)
	{
		for (size_t index = 0; index < probes.size(); index++)
		{
			const PainLedgeRecoveryCandidateProbe& probe = probes[index];
			if (probe.SweepClear && probe.NonPainFootRegion && probe.WalkableSupport)
				return static_cast<int>(index);
		}
		return -1;
	}
}
