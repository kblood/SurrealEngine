#include "GameSupport/DeusEx/AIPerception.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

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

static void TestLightLevel()
{
	Check(Close(ComputeDXAILightLevel(0.25f), 0.1f), "lightmap value scales to the AI light level");
	Check(Close(ComputeDXAILightLevel(0.0f), 0.004255f), "an unlit spot reports the floor");
	Check(Close(ComputeDXAILightLevel(0.001f), 0.004255f), "a nearly unlit spot cannot fall below the floor");
	Check(ComputeDXAILightLevel(10.0f) == 1.0f, "the light level clamps to one");
	Check(Close(ComputeDXAILightLevel(std::numeric_limits<float>::quiet_NaN()), 0.004255f), "NaN reports the floor");
	CheckUnitResult(ComputeDXAILightLevel(0.5f), "lit light level");
}

static void TestSightDirection()
{
	Check(PassesDXAISightDirection(100.0f, 0.0f, 0.0f, 1.0f, 2.0f,
		10000.0f, 90.0f, 1.0f), "forward target is inside the sight direction gate");
	Check(PassesDXAISightDirection(1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
		2.0f, 90.0f, 1.0f), "horizontal field-of-view boundary is inclusive");
	Check(!PassesDXAISightDirection(1.0f, 1.01f, 0.0f, 0.0f, 0.0f,
		2.0201f, 90.0f, 1.0f), "target outside horizontal field of view is rejected");
	Check(!PassesDXAISightDirection(-1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		1.0f, 180.0f, 1.0f), "target directly behind the observer is rejected");
	Check(!PassesDXAISightDirection(1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
		2.0f, 90.0f, 2.0f), "vertical field of view respects aspect ratio");
	Check(PassesDXAISightDirection(1.0f, 4.0f, 0.0f, 4.0f, 0.0f,
		17.0f, 90.0f, 1.0f), "visible cylinder radius can overlap the field-of-view edge");
	Check(PassesDXAISightDirection(-1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 1.0f), "non-positive horizontal field of view disables the direction gate");
	Check(PassesDXAISightDirection(1.0f, 0.0f, 0.5f, 0.0f, 0.0f,
		1.25f, 90.0f, 0.0f), "non-positive aspect ratio uses the observed one-to-one default");
	Check(!PassesDXAISightDirection(std::numeric_limits<float>::quiet_NaN(),
		0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 90.0f, 1.0f),
		"non-finite sight direction input fails closed");
}

static void TestSightLineOfSight()
{
	const DXAISightTracePlan actorPlan = BuildDXAISightTracePlan(false, 24.0f, 40.0f);
	Check(actorPlan.Valid && actorPlan.PrimaryZOffset == 0.0f &&
		actorPlan.TopZOffset == 40.0f && actorPlan.BottomZOffset == -40.0f,
		"non-pawn LOS plan uses center and full collision-height endpoints");
	const DXAISightTracePlan pawnPlan = BuildDXAISightTracePlan(true, 24.0f, 40.0f);
	Check(pawnPlan.Valid && pawnPlan.PrimaryZOffset == 24.0f &&
		pawnPlan.TopZOffset == 40.0f && pawnPlan.BottomZOffset == -40.0f,
		"pawn LOS plan uses target eye and full collision-height endpoints");
	Check(!BuildDXAISightTracePlan(true,
		std::numeric_limits<float>::quiet_NaN(), 40.0f).Valid,
		"non-finite target eye height invalidates the LOS plan");
	Check(!BuildDXAISightTracePlan(false, 0.0f,
		std::numeric_limits<float>::infinity()).Valid,
		"non-finite collision height invalidates the LOS plan");

	auto evaluate = [](bool checkCylinder, bool primary, bool top, bool bottom,
		std::vector<DXAISightTraceEndpoint>& order)
	{
		return TraceDXAISightLineOfSight(checkCylinder,
			[&](DXAISightTraceEndpoint endpoint)
			{
				order.push_back(endpoint);
				switch (endpoint)
				{
				case DXAISightTraceEndpoint::Primary: return primary;
				case DXAISightTraceEndpoint::Top: return top;
				case DXAISightTraceEndpoint::Bottom: return bottom;
				default: return false;
				}
			});
	};

	std::vector<DXAISightTraceEndpoint> order;
	DXAISightLineOfSightResult result = evaluate(false, true, false, false, order);
	Check(result.Visible && result.TraceCount == 1 && order.size() == 1 &&
		order[0] == DXAISightTraceEndpoint::Primary,
		"visible primary short-circuits after one trace");

	order.clear();
	result = evaluate(false, false, true, true, order);
	Check(!result.Visible && result.TraceCount == 1 && !result.TopTested &&
		!result.BottomTested && order.size() == 1,
		"blocked primary cannot use unrequested cylinder endpoints");

	order.clear();
	result = evaluate(true, false, true, true, order);
	Check(result.Visible && result.TraceCount == 2 && result.TopTested &&
		result.TopVisible && !result.BottomTested && order.size() == 2 &&
		order[1] == DXAISightTraceEndpoint::Top,
		"visible top short-circuits before the bottom trace");

	order.clear();
	result = evaluate(true, false, false, true, order);
	Check(result.Visible && result.TraceCount == 3 && result.BottomTested &&
		result.BottomVisible && order.size() == 3 &&
		order[2] == DXAISightTraceEndpoint::Bottom,
		"visible bottom passes after the preserved three-trace order");

	order.clear();
	result = evaluate(true, false, false, false, order);
	Check(!result.Visible && result.TraceCount == 3 && result.TopTested &&
		result.BottomTested && order.size() == 3,
		"fully occluded cylinder consumes exactly three traces and fails");
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
	TestLightLevel();
	TestSightDirection();
	TestSightLineOfSight();
	TestMotionVisibility();
	if (failures == 0)
		std::cout << "All Deus Ex AI perception tests passed.\n";
	return failures == 0 ? 0 : 1;
}
