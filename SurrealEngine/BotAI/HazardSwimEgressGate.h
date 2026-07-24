#pragma once

#include <cstdint>

namespace BotAI
{
	struct HazardSwimEgressEntry
	{
		uint64_t LifeId = 0;
		uint64_t EpisodeId = 0;
		bool IsPainZone = false;
		double DamagePerSecond = 0.0;
		bool AnchorKnown = false;
		double AnchorX = 0.0;
		double AnchorY = 0.0;
		double AnchorZ = 0.0;
		bool StaticZone = false;
		bool WaterZone = false;
	};

	enum class HazardSwimEgressEligibility
	{
		Ineligible,
		Authorized,
		Debounced
	};

	bool IsValidHazardSwimEgressEntry(const HazardSwimEgressEntry& entry);

	class HazardSwimEgressGate
	{
	public:
		void Reset();
		HazardSwimEgressEligibility Evaluate(const HazardSwimEgressEntry& entry);

	private:
		bool HasAuthorizedEntry = false;
		uint64_t AuthorizedLifeId = 0;
		uint64_t AuthorizedEpisodeId = 0;
	};
}
