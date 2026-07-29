#pragma once

#include "Math/vec.h"

#include <string>
#include <vector>
#include <cstdint>

namespace PawnMovement
{
	struct PickRegDestinationZeroDivideGuardRecord
	{
		uint64_t Sequence = 0;
		uint64_t ObserverTick = 0;
		uint64_t CallerInvocationToken = 0;
		uint64_t SourceLifeId = 0;
		int32_t SourceActorIndex = -1;
	};

	bool IsExactZeroVector(const vec3& value);
	bool HasPickRegDestinationBotReceiver(const std::vector<std::string>& classAncestry);
	bool ShouldSubstitutePickRegDestinationZeroDivide(bool benchmarkEnabled,
		const std::string& callerFunction, const std::vector<std::string>& classAncestry,
		const vec3& numerator, float denominator);
}
