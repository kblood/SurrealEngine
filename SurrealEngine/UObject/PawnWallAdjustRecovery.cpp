#include "PawnWallAdjustRecovery.h"

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
	}

	WallAdjustRecoveryAdvance AdvanceWallAdjustRecovery(const WallAdjustRecoveryState& state,
		const vec3& location, float elapsed, float repeatRadius, float escapeRadius, bool validContext)
	{
		WallAdjustRecoveryAdvance advance;
		if (!state.Active)
			return advance;
		if (!validContext || !IsFinite(location) || !std::isfinite(elapsed) || elapsed < 0.0f
			|| !std::isfinite(repeatRadius) || repeatRadius < 0.0f
			|| !std::isfinite(escapeRadius) || escapeRadius < 0.0f)
			return advance;
		if (state.ReplanPending)
		{
			advance.State = state;
			return advance;
		}

		if (state.SteeringActive)
		{
			const vec2 escapeDelta = location.xy() - state.SteeringOrigin.xy();
			if (dot(escapeDelta, escapeDelta) > escapeRadius * escapeRadius)
			{
				advance.Escaped = state.RecoveryAttempted;
				return advance;
			}

			advance.State = state;
			advance.State.SteeringRemainingSeconds = std::max(
				0.0f, state.SteeringRemainingSeconds - elapsed);
			if (advance.State.SteeringRemainingSeconds <= 0.0f)
			{
				advance.State.SteeringActive = false;
				advance.State.ReplanPending = true;
				advance.TimedOut = true;
			}
			return advance;
		}

		const vec2 originDelta = location.xy() - state.Origin.xy();
		if (dot(originDelta, originDelta) > repeatRadius * repeatRadius)
			return advance;

		advance.State = state;
		advance.State.AgeSeconds += elapsed;
		advance.State.RemainingSeconds = std::max(0.0f, state.RemainingSeconds - elapsed);
		if (advance.State.RemainingSeconds <= 0.0f)
			advance.State = {};
		return advance;
	}

	WallAdjustObservation ObserveWallAdjust(const WallAdjustRecoveryState& previous,
		const vec3& location, float windowSeconds, float repeatRadius,
		float minimumSustainedSeconds, uint32_t minimumObservations)
	{
		WallAdjustObservation result;
		if (!IsFinite(location) || !std::isfinite(windowSeconds) || windowSeconds <= 0.0f
			|| !std::isfinite(repeatRadius) || repeatRadius < 0.0f
			|| !std::isfinite(minimumSustainedSeconds) || minimumSustainedSeconds < 0.0f
			|| minimumObservations == 0)
			return result;
		if (previous.ReplanPending)
		{
			result.State = previous;
			return result;
		}

		bool repeated = false;
		if (previous.Active && previous.RemainingSeconds > 0.0f)
		{
			if (previous.SteeringActive)
				repeated = true;
			else
			{
				const vec2 originDelta = location.xy() - previous.Origin.xy();
				repeated = dot(originDelta, originDelta) <= repeatRadius * repeatRadius;
			}
		}

		if (repeated)
		{
			result.State = previous;
			result.State.ObservationCount++;
			result.Repeated = true;
			if (!previous.SteeringActive
				&& previous.AgeSeconds >= minimumSustainedSeconds
				&& result.State.ObservationCount >= minimumObservations)
			{
				result.State.RepeatOrdinal++;
				result.Escalate = true;
			}
			return result;
		}

		result.State.Active = true;
		result.State.Origin = location;
		result.State.RemainingSeconds = windowSeconds;
		result.State.ObservationCount = 1;
		return result;
	}

	WallAdjustRecoveryState BeginWallAdjustSteering(const WallAdjustRecoveryState& state,
		const vec3& origin, const vec2& unsafeDirection, const vec2& escapeDirection,
		float steeringSeconds)
	{
		WallAdjustRecoveryState result = state;
		const vec2 unsafe = SafeNormalize(unsafeDirection);
		const vec2 escape = SafeNormalize(escapeDirection);
		if (!state.Active || !IsFinite(origin) || unsafe == vec2(0.0f) || escape == vec2(0.0f)
			|| !std::isfinite(steeringSeconds) || steeringSeconds <= 0.0f)
			return {};
		result.SteeringActive = true;
		result.RecoveryAttempted = true;
		result.ReplanPending = false;
		result.SteeringOrigin = origin;
		result.UnsafeDirection = unsafe;
		result.EscapeDirection = escape;
		result.SteeringRemainingSeconds = steeringSeconds;
		return result;
	}

	WallAdjustSteeringRequest EvaluateWallAdjustSteeringRequest(
		const WallAdjustRecoveryState& state, const vec2& requestedDirection,
		float minimumUnsafeAlignment)
	{
		if (!state.Active || !state.SteeringActive)
			return WallAdjustSteeringRequest::Inactive;
		const vec2 requested = SafeNormalize(requestedDirection);
		const vec2 unsafe = SafeNormalize(state.UnsafeDirection);
		const vec2 escape = SafeNormalize(state.EscapeDirection);
		if (requested == vec2(0.0f) || unsafe == vec2(0.0f) || escape == vec2(0.0f)
			|| !std::isfinite(minimumUnsafeAlignment))
			return WallAdjustSteeringRequest::Clear;
		return dot(requested, unsafe) >= minimumUnsafeAlignment
			? WallAdjustSteeringRequest::Apply : WallAdjustSteeringRequest::Clear;
	}

	std::array<vec2, 3> WallAdjustEscapeDirections(const vec2& approachDirection)
	{
		const vec2 approach = SafeNormalize(approachDirection);
		if (approach == vec2(0.0f))
			return {};
		return {
			-approach,
			vec2(-approach.y, approach.x),
			vec2(approach.y, -approach.x)
		};
	}

	std::array<int, 3> WallAdjustCandidateOrder(uint32_t repeatOrdinal)
	{
		const int first = repeatOrdinal == 0 ? 0 : static_cast<int>((repeatOrdinal - 1) % 3);
		return { first, (first + 1) % 3, (first + 2) % 3 };
	}

	int SelectWallAdjustCandidate(const std::array<WallAdjustCandidateProbe, 3>& probes,
		const std::array<int, 3>& order)
	{
		for (int index : order)
		{
			if (index < 0 || index >= static_cast<int>(probes.size()))
				continue;
			const WallAdjustCandidateProbe& probe = probes[static_cast<size_t>(index)];
			if (probe.SweepClear && probe.NonPainFootRegion && probe.WalkableSupport)
				return index;
		}
		return -1;
	}
}
