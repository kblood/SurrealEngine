#pragma once

#include <cstdint>

namespace BotAI
{
	struct HarmfulZoneEntry
	{
		uint64_t LifeId = 0;
		uint64_t EpisodeId = 0;
		bool IsPainZone = false;
		double DamagePerSecond = 0.0;
	};

	enum class HarmfulZoneEscapeEligibility
	{
		Ineligible,
		Authorized,
		Debounced
	};

	bool HasExactPositiveDamagePerSecond(const HarmfulZoneEntry& entry);

	// The caller assigns monotonically distinct life and zone-entry episode IDs.
	// This gate deliberately has no movement side effects or engine dependencies.
	class HarmfulZoneEscapeGate
	{
	public:
		void Reset();
		HarmfulZoneEscapeEligibility Evaluate(const HarmfulZoneEntry& entry);

	private:
		bool HasAuthorizedEntry = false;
		uint64_t AuthorizedLifeId = 0;
		uint64_t AuthorizedEpisodeId = 0;
	};
}
