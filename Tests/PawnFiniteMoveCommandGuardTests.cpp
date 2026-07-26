#include "UObject/PawnFiniteMoveCommandGuard.h"

#include <iostream>
#include <limits>
#include <string>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << "Pawn finite MoveTo command guard test failed: " << message << '\n';
		return 1;
	}
}

int main()
{
	using namespace PawnMovement;
	if (!IsFiniteMoveCommandDestination(vec3(1.0f, -2.0f, 3.0f)))
		return Fail("finite destination was rejected");
	if (IsFiniteMoveCommandDestination(vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f)))
		return Fail("NaN destination was accepted");
	if (IsFiniteMoveCommandDestination(vec3(0.0f, std::numeric_limits<float>::infinity(), 0.0f)))
		return Fail("positive infinity destination was accepted");
	if (IsFiniteMoveCommandDestination(vec3(0.0f, 0.0f, -std::numeric_limits<float>::infinity())))
		return Fail("negative infinity destination was accepted");
	if (std::string(MoveCommandComponentClassName(
		ClassifyMoveCommandComponent(std::numeric_limits<float>::quiet_NaN()))) != "nan")
		return Fail("NaN classification was not JSON-safe");
	if (std::string(MoveCommandComponentClassName(
		ClassifyMoveCommandComponent(std::numeric_limits<float>::infinity()))) != "positive_infinity")
		return Fail("positive infinity classification was not JSON-safe");
	return 0;
}
