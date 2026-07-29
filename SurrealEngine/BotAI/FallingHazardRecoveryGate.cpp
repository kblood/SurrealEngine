#include "FallingHazardRecoveryGate.h"

#include <cmath>

namespace BotAI
{
	bool IsValidFallingHazardRecoveryEntry(
		const FallingHazardRecoveryEntry& entry)
	{
		if (entry.LifeId == 0 || entry.FallEpisodeId == 0
			|| !entry.PrefixPromoted || !entry.AnchorKnown
			|| !std::isfinite(entry.AnchorX) || !std::isfinite(entry.AnchorY)
			|| !std::isfinite(entry.CurrentX) || !std::isfinite(entry.CurrentY))
		{
			return false;
		}
		const double dx = entry.AnchorX - entry.CurrentX;
		const double dy = entry.AnchorY - entry.CurrentY;
		const double distance = std::sqrt(dx * dx + dy * dy);
		return std::isfinite(distance)
			&& distance >= FallingHazardRecoveryMinimumAnchorDistance
			&& distance <= FallingHazardRecoveryMaximumAnchorDistance;
	}

	void FallingHazardRecoveryGate::Reset()
	{
		HasAuthorizedEntry = false;
		AuthorizedLifeId = 0;
		AuthorizedFallEpisodeId = 0;
	}

	FallingHazardRecoveryEligibility FallingHazardRecoveryGate::Evaluate(
		const FallingHazardRecoveryEntry& entry)
	{
		if (!IsValidFallingHazardRecoveryEntry(entry))
			return FallingHazardRecoveryEligibility::Ineligible;
		if (HasAuthorizedEntry && entry.LifeId == AuthorizedLifeId
			&& entry.FallEpisodeId == AuthorizedFallEpisodeId)
		{
			return FallingHazardRecoveryEligibility::Debounced;
		}
		HasAuthorizedEntry = true;
		AuthorizedLifeId = entry.LifeId;
		AuthorizedFallEpisodeId = entry.FallEpisodeId;
		return FallingHazardRecoveryEligibility::Authorized;
	}
}
