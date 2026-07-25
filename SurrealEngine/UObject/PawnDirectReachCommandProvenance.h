#pragma once

#include <cstdint>
#include <string>

namespace PawnMovement
{
	// This record is intentionally native-only until the benchmark driver
	// resolves its live actor pointer into a same-tick command witness.
	struct DirectReachCommandObservation
	{
		uint64_t Sequence = 0;
		uint64_t LifeId = 0;
		int32_t TargetActorIndex = -1;
		const void* TargetAddress = nullptr;
		std::string TargetName;
		std::string TargetClass;
		bool Reached = false;
		bool CheckNavpoint = false;
		bool ResolvedWallSlide = false;
		int WalkingSimulationIterations = 0;
	};
}
