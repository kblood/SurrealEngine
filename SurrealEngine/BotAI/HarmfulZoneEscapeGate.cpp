#include "HarmfulZoneEscapeGate.h"

#include <cmath>

namespace BotAI
{
	bool HasExactPositiveDamagePerSecond(const HarmfulZoneEntry& entry)
	{
		return entry.IsPainZone && std::isfinite(entry.DamagePerSecond) && entry.DamagePerSecond > 0.0;
	}

	void HarmfulZoneEscapeGate::Reset()
	{
		HasAuthorizedEntry = false;
		AuthorizedLifeId = 0;
		AuthorizedEpisodeId = 0;
	}

	HarmfulZoneEscapeEligibility HarmfulZoneEscapeGate::Evaluate(const HarmfulZoneEntry& entry)
	{
		if (entry.LifeId == 0 || entry.EpisodeId == 0 || !HasExactPositiveDamagePerSecond(entry))
			return HarmfulZoneEscapeEligibility::Ineligible;

		if (HasAuthorizedEntry && entry.LifeId == AuthorizedLifeId && entry.EpisodeId == AuthorizedEpisodeId)
			return HarmfulZoneEscapeEligibility::Debounced;

		HasAuthorizedEntry = true;
		AuthorizedLifeId = entry.LifeId;
		AuthorizedEpisodeId = entry.EpisodeId;
		return HarmfulZoneEscapeEligibility::Authorized;
	}
}
