#include "BotBenchmark/BotBenchmarkHazardDeathPartition.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	using namespace BotBenchmarkHazardDeathPartition;
	Check(ClassifyPrefix({}) == HazardPrefix::None,
		"a death without terminal hazard evidence must remain unpartitioned");
	Check(ClassifyPrefix({ false, true }) == HazardPrefix::FallingDeathWithoutWaterEgress,
		"a terminal fall without water egress must retain the falling prefix");
	Check(ClassifyPrefix({ true, false }) == HazardPrefix::WaterEgressDeath,
		"a terminal water episode must retain the egress prefix");
	Check(ClassifyPrefix({ true, true }) == HazardPrefix::WaterEgressDeath,
		"water egress must take precedence when a fall led into the terminal water episode");
	return 0;
}
