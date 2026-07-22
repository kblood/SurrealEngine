#include "UObject/DXAIPerception.h"

#include <cmath>
#include <iostream>
#include <string>

static int failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		failures++;
	}
}

static bool Close(float left, float right)
{
	return std::abs(left - right) < 0.0001f;
}

static void TestHearing()
{
	Check(Close(ComputeDXAIHearing(1.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f), 1.0f), "sound at the listener");
	Check(Close(ComputeDXAIHearing(1.0f, 100.0f, 0.0f, 50.0f, 0.0f, 0.0f), 0.5f), "horizontal distance attenuation");
	Check(Close(ComputeDXAIHearing(1.0f, 100.0f, 0.0f, 0.0f, 0.0f, 25.0f), 0.5f), "vertical distance is doubled");
	Check(ComputeDXAIHearing(1.0f, 100.0f, 0.0f, 100.0f, 0.0f, 0.0f) == 0.0f, "sound at radius is inaudible");
	Check(Close(ComputeDXAIHearing(1.0f, -1.0f, 0.0f, 400.0f, 0.0f, 0.0f), 0.5f), "non-positive radius defaults to 800");
	Check(ComputeDXAIHearing(0.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f) == 0.0f, "non-positive volume");
	Check(ComputeDXAIHearing(1.0f, 100.0f, 0.6f, 50.0f, 0.0f, 0.0f) == 0.0f, "hearing threshold");
}

static void TestSight()
{
	Check(Close(ComputeDXAISight(1.0f, 1.0f, 20.0f, 40.0f, 200000.0f, 0.0f, 0.0f), 0.64f), "apparent angular size");
	Check(ComputeDXAISight(1.0f, 1.0f, 20.0f, 40.0f, 200000.0f, 0.02f, 0.0f) == 0.0f, "minimum angular size");
	Check(Close(ComputeDXAISight(1.0f, 0.5f, 20.0f, 40.0f, 200000.0f, 0.0f, 0.1f), 0.22f), "light and visibility threshold");
	Check(ComputeDXAISight(0.0f, 1.0f, 20.0f, 40.0f, 1.0f, 0.0f, 0.0f) == 0.0f, "zero supplied visibility");
	Check(ComputeDXAISight(1.0f, 0.0f, 20.0f, 40.0f, 1.0f, 0.0f, 0.0f) == 0.0f, "zero light visibility");
}

static void TestMotionVisibility()
{
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 30.0f, true), 0.5f), "motion floor");
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 115.0f, true), 0.625f), "motion interpolation");
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 200.0f, true), 0.75f), "motion ceiling");
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 200.0f, false), 0.5f), "velocity can be excluded");
	Check(ComputeDXAIMotionVisibility(1.0f, 200.0f, true) == 1.0f, "motion visibility clamps to one");
}

int main()
{
	TestHearing();
	TestSight();
	TestMotionVisibility();
	if (failures == 0)
		std::cout << "All Deus Ex AI perception tests passed.\n";
	return failures == 0 ? 0 : 1;
}
