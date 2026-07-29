#include "PawnFallingAirRecoverySelector.h"

#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec2& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		bool IsUnitDirection(const vec2& direction)
		{
			if (!IsFinite(direction))
				return false;
			const float lengthSquared = dot(direction, direction);
			return lengthSquared >= 0.99f && lengthSquared <= 1.01f;
		}
	}

	bool IsCertifiedFallingAirRecoveryCandidate(
		const FallingAirRecoveryCandidate& candidate)
	{
		const FallingHazardForecastResult& result = candidate.Forecast.Result;
		return IsUnitDirection(candidate.Direction)
			&& candidate.Forecast.Complete
			&& result.Classification == FallingHazardForecast::NoHarmfulPainObserved
			&& result.Reason
				== FallingHazardForecastReason::NoHarmfulPainAtStaticLanding
			&& !result.TransientHarmfulPainObserved
			&& std::isfinite(result.Elapsed) && result.Elapsed > 0.0f
			&& result.Elapsed <= FallingHazardForecastMaximumElapsed
			&& result.SegmentCount > 0;
	}

	FallingAirRecoverySelection SelectFallingAirRecoveryTrajectory(
		const std::array<FallingAirRecoveryCandidate,
			FallingAirRecoveryMaximumCandidates>& candidates,
		size_t candidateCount)
	{
		FallingAirRecoverySelection selection;
		if (candidateCount == 0 || candidateCount > candidates.size())
			return selection;
		selection.CandidatesTested = candidateCount;
		for (size_t index = 0; index < candidateCount; index++)
		{
			if (!IsCertifiedFallingAirRecoveryCandidate(candidates[index]))
				continue;
			selection.CertifiedLandingCount++;
			if (selection.Decision == FallingAirRecoverySelectionDecision::Selected)
				continue;
			selection.Decision = FallingAirRecoverySelectionDecision::Selected;
			selection.SelectedIndex = index;
			selection.Direction = candidates[index].Direction;
		}
		if (selection.Decision != FallingAirRecoverySelectionDecision::Selected)
			selection.Decision = FallingAirRecoverySelectionDecision::NoCertifiedLanding;
		return selection;
	}
}
