#pragma once

#include <cstdint>

namespace BotBenchmarkHazardDeathPartition
{
	enum class HazardPrefix : uint8_t
	{
		None,
		WaterEgressDeath,
		FallingDeathWithoutWaterEgress,
	};

	struct TerminalEvidence
	{
		bool WaterEgressDeath = false;
		bool FallingDeath = false;
	};

	HazardPrefix ClassifyPrefix(TerminalEvidence evidence);
	const char* HazardPrefixName(HazardPrefix prefix);
}
