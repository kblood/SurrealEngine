#include "HazardSwimEgressGate.h"

#include <cmath>

namespace BotAI
{
	bool IsValidHazardSwimEgressEntry(const HazardSwimEgressEntry& entry)
	{
		return entry.LifeId != 0 && entry.EpisodeId != 0
			&& entry.IsPainZone && std::isfinite(entry.DamagePerSecond)
			&& entry.DamagePerSecond > 0.0 && entry.AnchorKnown
			&& std::isfinite(entry.AnchorX) && std::isfinite(entry.AnchorY)
			&& std::isfinite(entry.AnchorZ) && entry.StaticZone && entry.WaterZone;
	}

	void HazardSwimEgressGate::Reset()
	{
		HasAuthorizedEntry = false;
		AuthorizedLifeId = 0;
		AuthorizedEpisodeId = 0;
	}

	HazardSwimEgressEligibility HazardSwimEgressGate::Evaluate(
		const HazardSwimEgressEntry& entry)
	{
		if (!IsValidHazardSwimEgressEntry(entry))
			return HazardSwimEgressEligibility::Ineligible;

		if (HasAuthorizedEntry && entry.LifeId == AuthorizedLifeId
			&& entry.EpisodeId == AuthorizedEpisodeId)
		{
			return HazardSwimEgressEligibility::Debounced;
		}

		HasAuthorizedEntry = true;
		AuthorizedLifeId = entry.LifeId;
		AuthorizedEpisodeId = entry.EpisodeId;
		return HazardSwimEgressEligibility::Authorized;
	}
}
