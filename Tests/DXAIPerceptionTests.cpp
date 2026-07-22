#include "UObject/DXAIPerception.h"

#include <cmath>
#include <iostream>
#include <limits>
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

static void CheckUnitResult(float value, const std::string& message)
{
	Check(std::isfinite(value), message + " is finite");
	Check(value >= 0.0f && value <= 1.0f, message + " is in [0, 1]");
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
	CheckUnitResult(ComputeDXAIHearing(std::numeric_limits<float>::max(), 100.0f, -1.0f, 1.0f, 1.0f, 1.0f), "large finite hearing inputs");
	Check(ComputeDXAIHearing(std::numeric_limits<float>::quiet_NaN(), 100.0f, 0.0f, 0.0f, 0.0f, 0.0f) == 0.0f, "NaN hearing input fails closed");
	Check(ComputeDXAIHearing(1.0f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f, 0.0f) == 0.0f, "infinite hearing input fails closed");
}

static void TestSight()
{
	Check(Close(ComputeDXAISight(1.0f, 1.0f, 20.0f, 40.0f, 200000.0f, 0.0f, 0.0f), 0.64f), "apparent angular size");
	Check(ComputeDXAISight(1.0f, 1.0f, 20.0f, 40.0f, 200000.0f, 0.02f, 0.0f) == 0.0f, "minimum angular size");
	Check(Close(ComputeDXAISight(1.0f, 0.5f, 20.0f, 40.0f, 200000.0f, 0.0f, 0.1f), 0.22f), "light and visibility threshold");
	Check(ComputeDXAISight(0.0f, 1.0f, 20.0f, 40.0f, 1.0f, 0.0f, 0.0f) == 0.0f, "zero supplied visibility");
	Check(ComputeDXAISight(1.0f, 0.0f, 20.0f, 40.0f, 1.0f, 0.0f, 0.0f) == 0.0f, "zero light visibility");
	Check(Close(ComputeDXAISight(1.0f, 1.0f, 1.0f, 0.0f, 100.0f, 0.01f, 0.0f), 0.64f), "angular threshold is inclusive");
	CheckUnitResult(ComputeDXAISight(1.0f, 1.0f, std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 1.0f, 0.0f, 0.0f), "large finite sight inputs");
	Check(ComputeDXAISight(1.0f, 1.0f, 20.0f, 40.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f) == 0.0f, "NaN sight input fails closed");
	Check(ComputeDXAISight(1.0f, std::numeric_limits<float>::infinity(), 20.0f, 40.0f, 1.0f, 0.0f, 0.0f) == 0.0f, "infinite sight input fails closed");
}

static void TestMotionVisibility()
{
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 30.0f, true), 0.5f), "motion floor");
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 115.0f, true), 0.625f), "motion interpolation");
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 200.0f, true), 0.75f), "motion ceiling");
	Check(Close(ComputeDXAIMotionVisibility(0.5f, 200.0f, false), 0.5f), "velocity can be excluded");
	Check(ComputeDXAIMotionVisibility(1.0f, 200.0f, true) == 1.0f, "motion visibility clamps to one");
	Check(ComputeDXAIMotionVisibility(-1.0f, -100.0f, true) == 0.0f, "negative finite motion inputs clamp to zero");
	Check(ComputeDXAIMotionVisibility(0.5f, std::numeric_limits<float>::quiet_NaN(), true) == 0.0f, "NaN motion input fails closed");
	Check(ComputeDXAIMotionVisibility(0.5f, std::numeric_limits<float>::infinity(), true) == 0.0f, "infinite motion input fails closed");
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
