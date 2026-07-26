#include "UObject/PawnVisionCone.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	void TestForwardTargetIsVisible()
	{
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(100.0f, 0.0f, 0.0f), 0.5f),
			"a target in front of the pawn is inside its vision cone");
	}

	void TestBoundaryTargetIsVisible()
	{
		const vec3 target(50.0f, 50.0f * std::sqrt(3.0f), 0.0f);
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			target, 0.5f),
			"a target exactly on the peripheral-vision boundary is visible");
	}

	void TestBehindTargetIsNotVisible()
	{
		Check(!PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-100.0f, 0.0f, 0.0f), 0.5f),
			"a target behind the pawn is outside an enabled vision cone");
	}

	void TestTranslationDoesNotChangeConeResult()
	{
		const vec3 observer(0.0f);
		const vec3 target(100.0f, 20.0f, 0.0f);
		const vec3 translation(4000.0f, -7000.0f, 250.0f);
		const bool atOrigin = PawnMovement::IsWithinPawnVisionCone(observer,
			vec3(1.0f, 0.0f, 0.0f), target, 0.9f);
		const bool translated = PawnMovement::IsWithinPawnVisionCone(observer + translation,
			vec3(1.0f, 0.0f, 0.0f), target + translation, 0.9f);
		Check(atOrigin == translated,
			"vision-cone classification is independent of world origin");
	}

	void TestZeroLengthTargetIsVisible()
	{
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(12.0f, -4.0f, 8.0f),
			vec3(1.0f, 0.0f, 0.0f), vec3(12.0f, -4.0f, 8.0f), 0.5f),
			"a co-located target does not fail a vision check because its direction is undefined");
	}

	void TestDisabledPeripheralVisionDoesNotRejectDirection()
	{
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-100.0f, 0.0f, 0.0f), 0.0f),
			"zero peripheral vision disables the cone rejection");
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-100.0f, 0.0f, 0.0f), -0.5f),
			"negative peripheral vision disables the cone rejection");
	}
}

int main()
{
	TestForwardTargetIsVisible();
	TestBoundaryTargetIsVisible();
	TestBehindTargetIsNotVisible();
	TestTranslationDoesNotChangeConeResult();
	TestZeroLengthTargetIsVisible();
	TestDisabledPeripheralVisionDoesNotRejectDirection();
	if (Failures == 0)
		std::cout << "Pawn vision cone tests passed\n";
	return Failures == 0 ? 0 : 1;
}
