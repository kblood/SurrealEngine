#include "PawnLatentMoveTerminal.h"

#include <algorithm>
#include <cmath>

namespace PawnMovement
{
	bool IsValidLatentMoveTerminalInput(const LatentMoveTerminalInput& input)
	{
		return std::isfinite(input.MoveTimer)
			&& std::isfinite(input.DistanceSquared) && input.DistanceSquared >= 0.0f
			&& std::isfinite(input.SpeedSquared) && input.SpeedSquared >= 0.0f
			&& std::isfinite(input.Elapsed) && input.Elapsed >= 0.0f
			&& std::isfinite(input.AcceptanceRadius) && input.AcceptanceRadius >= 0.0f;
	}

	LatentMoveTerminalCause ClassifyLatentMoveTerminal(const LatentMoveTerminalInput& input)
	{
		if (!input.TerminalObserved)
			return LatentMoveTerminalCause::Unknown;
		if (!IsValidLatentMoveTerminalInput(input))
			return LatentMoveTerminalCause::InvalidInput;

		const float stepDistance = std::sqrt(input.SpeedSquared) * input.Elapsed;
		const float threshold = std::max(input.AcceptanceRadius, stepDistance + 1.0f);
		if (!std::isfinite(stepDistance) || !std::isfinite(threshold))
			return LatentMoveTerminalCause::InvalidInput;
		const float thresholdSquared = threshold * threshold;
		if (!std::isfinite(thresholdSquared))
			return LatentMoveTerminalCause::InvalidInput;
		const bool arrived = input.VerticallyReachable
			&& input.DistanceSquared <= thresholdSquared;
		if (arrived)
			return LatentMoveTerminalCause::Arrival;
		if (input.MoveTimer < 0.0f)
			return LatentMoveTerminalCause::TimerExpiredOutsideAcceptance;
		return LatentMoveTerminalCause::Unknown;
	}
}
