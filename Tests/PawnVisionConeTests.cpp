#include "UObject/PawnVisionCone.h"

#include <cmath>
#include <iostream>
#include <limits>
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
			vec3(-1000.0f, 0.0f, 0.0f), 0.5f),
			"a sufficiently distant target behind the pawn is outside an enabled vision cone");
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

	void TestNearbyTargetsReceiveRetailCompatibilitySlack()
	{
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-100.0f, 0.0f, 0.0f), 0.7f),
			"retail compatibility slack widens the cone for a nearby target");
		Check(!PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-200.0f, 0.0f, 0.0f), 0.7f),
			"retail compatibility slack does not make the cone universally permissive");
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-100.0f, 0.0f, 0.0f), 0.0f),
			"zero peripheral vision includes a nearby target behind the pawn");
		Check(!PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-500.0f, 0.0f, 0.0f), -0.5f),
			"negative peripheral vision still rejects a sufficiently distant target behind the pawn");
		Check(PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(-10.0f, 100.0f, 0.0f), -0.5f),
			"negative peripheral vision widens the cone without disabling it");
	}

	void TestZeroLengthForwardFailsClosedWhenVisionIsEnabled()
	{
		Check(!PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(0.0f),
			vec3(100.0f, 0.0f, 0.0f), 0.5f),
			"an enabled vision cone rejects a target when forward direction is undefined");
	}

	void TestNonFiniteInputsFailClosed()
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		Check(!PawnMovement::IsWithinPawnVisionCone(vec3(nan, 0.0f, 0.0f),
			vec3(1.0f, 0.0f, 0.0f), vec3(100.0f, 0.0f, 0.0f), 0.5f),
			"a non-finite observer location fails closed");
		Check(!PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(nan, 0.0f, 0.0f), 0.5f),
			"a non-finite target location fails closed");
		Check(!PawnMovement::IsWithinPawnVisionCone(vec3(0.0f), vec3(1.0f, 0.0f, 0.0f),
			vec3(100.0f, 0.0f, 0.0f), nan),
			"a non-finite peripheral-vision threshold fails closed");
	}
}

int main()
{
	TestForwardTargetIsVisible();
	TestBoundaryTargetIsVisible();
	TestBehindTargetIsNotVisible();
	TestTranslationDoesNotChangeConeResult();
	TestZeroLengthTargetIsVisible();
	TestNearbyTargetsReceiveRetailCompatibilitySlack();
	TestZeroLengthForwardFailsClosedWhenVisionIsEnabled();
	TestNonFiniteInputsFailClosed();
	if (Failures == 0)
		std::cout << "Pawn vision cone tests passed\n";
	return Failures == 0 ? 0 : 1;
}
