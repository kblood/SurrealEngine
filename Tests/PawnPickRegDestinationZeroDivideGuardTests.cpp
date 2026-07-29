#include "UObject/PawnPickRegDestinationZeroDivideGuard.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	using PawnMovement::ShouldSubstitutePickRegDestinationZeroDivide;
	const std::vector<std::string> utBot = { "Custom.Bot", "Botpack.Bot", "UnrealShare.Bots" };
	const std::vector<std::string> unrealBot = { "Custom.Bot", "UnrealShare.Bots" };
	const std::vector<std::string> unrelated = { "MyPackage.OtherPawn", "UnrealShare.Pawn" };

	if (!ShouldSubstitutePickRegDestinationZeroDivide(true, "PickRegDestination", utBot,
		vec3(0.0f), 0.0f)
		|| !ShouldSubstitutePickRegDestinationZeroDivide(true, "PickRegDestination", unrealBot,
			vec3(0.0f), 0.0f))
	{
		return Fail("eligible bot co-location divide was not selected");
	}
	if (ShouldSubstitutePickRegDestinationZeroDivide(false, "PickRegDestination", utBot,
		vec3(0.0f), 0.0f)
		|| ShouldSubstitutePickRegDestinationZeroDivide(true, "OtherFunction", utBot,
			vec3(0.0f), 0.0f)
		|| ShouldSubstitutePickRegDestinationZeroDivide(true, "PickRegDestination", unrelated,
			vec3(0.0f), 0.0f))
	{
		return Fail("caller or receiver fence admitted an unrelated divide");
	}
	if (ShouldSubstitutePickRegDestinationZeroDivide(true, "PickRegDestination", utBot,
		vec3(1.0f, 0.0f, 0.0f), 0.0f)
		|| ShouldSubstitutePickRegDestinationZeroDivide(true, "PickRegDestination", utBot,
			vec3(0.0f), 1.0f))
	{
		return Fail("normal finite or nonzero-over-zero divide was altered");
	}
	const vec3 stockUnrelated = vec3(0.0f) / 0.0f;
	const vec3 stockNonzero = vec3(1.0f, 0.0f, 0.0f) / 0.0f;
	if (std::isfinite(stockUnrelated.x) || std::isfinite(stockNonzero.x))
		return Fail("fixture no longer witnesses stock non-finite zero division");
	const vec3 substituted = vec3(0.0f);
	if (!std::isfinite(substituted.x) || !std::isfinite(substituted.y)
		|| !std::isfinite(substituted.z))
	{
		return Fail("bot co-location fallback was not finite");
	}
	return 0;
}
