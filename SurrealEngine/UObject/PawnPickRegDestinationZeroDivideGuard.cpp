#include "PawnPickRegDestinationZeroDivideGuard.h"

namespace PawnMovement
{
	bool IsExactZeroVector(const vec3& value)
	{
		return value.x == 0.0f && value.y == 0.0f && value.z == 0.0f;
	}

	bool HasPickRegDestinationBotReceiver(const std::vector<std::string>& classAncestry)
	{
		for (const std::string& className : classAncestry)
		{
			if (className == "Botpack.Bot" || className == "UnrealShare.Bots")
				return true;
		}
		return false;
	}

	bool ShouldSubstitutePickRegDestinationZeroDivide(bool benchmarkEnabled,
		const std::string& callerFunction, const std::vector<std::string>& classAncestry,
		const vec3& numerator, float denominator)
	{
		return benchmarkEnabled && callerFunction == "PickRegDestination"
			&& HasPickRegDestinationBotReceiver(classAncestry)
			&& IsExactZeroVector(numerator) && denominator == 0.0f;
	}

}
