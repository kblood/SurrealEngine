#include "BotBenchmarkHazardDeathPartition.h"

namespace BotBenchmarkHazardDeathPartition
{
	HazardPrefix ClassifyPrefix(TerminalEvidence evidence)
	{
		if (evidence.WaterEgressDeath)
			return HazardPrefix::WaterEgressDeath;
		if (evidence.FallingDeath)
			return HazardPrefix::FallingDeathWithoutWaterEgress;
		return HazardPrefix::None;
	}

	const char* HazardPrefixName(HazardPrefix prefix)
	{
		switch (prefix)
		{
		case HazardPrefix::WaterEgressDeath:
			return "water_egress_death";
		case HazardPrefix::FallingDeathWithoutWaterEgress:
			return "falling_death_without_water_egress";
		default:
			return "none";
		}
	}
}
