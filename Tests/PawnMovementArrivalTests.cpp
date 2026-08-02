#include "UObject/PawnMovementArrival.h"

#include <cmath>
#include <iostream>
#include <string>

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static PawnMovement::ArrivalQuery Query(float distance, float speed, float elapsed, float acceptanceRadius = 1.0f)
{
	return {
		.DistanceSquared = distance * distance,
		.SpeedSquared = speed * speed,
		.Elapsed = elapsed,
		.AcceptanceRadius = acceptanceRadius,
		.VerticallyReachable = true
	};
}

static void TestDoesNotCompleteSixtySevenUnitsEarly()
{
	const PawnMovement::ArrivalQuery query = Query(50.0f, 300.0f, 1.0f / 60.0f);
	const bool oldExpressionWouldComplete = query.DistanceSquared < query.SpeedSquared * 0.05f;
	Check(oldExpressionWouldComplete, "fixture reproduces the old early-completion expression");
	Check(!PawnMovement::HasArrived(query), "elapsed-step envelope does not complete movement 50 units early");
	Check(std::abs(PawnMovement::ArrivalThreshold(query) - 6.0f) < 0.0001f, "60 Hz threshold covers one movement step plus one unit");
}

static void TestCompletesWithinCurrentMovementStep()
{
	Check(PawnMovement::HasArrived(Query(5.5f, 300.0f, 1.0f / 60.0f)), "goal inside this frame's movement step is complete");
	Check(!PawnMovement::HasArrived(Query(6.5f, 300.0f, 1.0f / 60.0f)), "goal beyond this frame's movement step remains active");
}

static void TestAcceptanceRadiusSupportsActorTargets()
{
	Check(PawnMovement::HasArrived(Query(33.0f, 300.0f, 1.0f / 60.0f, 34.0f)), "touching colliding cylinders counts as arrival");
	Check(!PawnMovement::HasArrived(Query(35.0f, 300.0f, 1.0f / 60.0f, 34.0f)), "movement remains active outside actor touch radius");
	Check(PawnMovement::HasArrived(Query(19.0f, 0.0f, 1.0f / 60.0f, 20.0f)), "point-like navigation targets use the pawn radius");
}

static void TestVerticalReachabilityVetoesHorizontalArrival()
{
	PawnMovement::ArrivalQuery query = Query(0.0f, 0.0f, 1.0f / 60.0f, 20.0f);
	query.VerticallyReachable = false;
	Check(!PawnMovement::HasArrived(query), "horizontal overlap cannot finish an unreachable target on another height");
}

static void TestNegativeRuntimeInputsFailConservatively()
{
	Check(std::abs(PawnMovement::ArrivalThreshold(Query(2.0f, 300.0f, -1.0f)) - 1.0f) < 0.0001f, "negative elapsed time does not enlarge the envelope");
	PawnMovement::ArrivalQuery negativeSpeed = Query(0.5f, 0.0f, 0.0f);
	negativeSpeed.SpeedSquared = -100.0f;
	Check(PawnMovement::HasArrived(negativeSpeed), "negative squared speed is clamped to zero");
}

static void TestDirectReachAcceptanceMatchesWhereAWalkComesToRest()
{
	const PawnMovement::DirectReachGoal chair = {
		.GoalCollides = true,
		.PawnRadius = 20.0f,
		.PawnHeight = 47.0f,
		.GoalRadius = 23.0f,
		.GoalHeight = 24.0f,
		.MaxStepHeight = 24.0f,
		.SweepMargin = 1.0f };
	const float radius = PawnMovement::DirectReachAcceptanceRadius(chair);
	Check(radius >= 44.0f, "a pawn resting against a blocking goal has reached it");
	Check(radius < 45.0f, "the acceptance radius stays at the touch distance");
	Check(std::abs(PawnMovement::DirectReachVerticalReach(chair) - 95.0f) < 0.0001f,
		"vertical reach spans both cylinders and a step");

	PawnMovement::DirectReachGoal marker = chair;
	marker.GoalCollides = false;
	Check(PawnMovement::DirectReachAcceptanceRadius(marker) < 2.0f,
		"a goal that does not collide is still walked onto");
	Check(std::abs(PawnMovement::DirectReachVerticalReach(marker) - 47.0f) < 0.0001f,
		"vertical reach to a non-colliding goal is the pawn's own height");

	PawnMovement::DirectReachGoal tiny = chair;
	tiny.PawnRadius = 0.0f;
	tiny.GoalRadius = 0.0f;
	tiny.SweepMargin = 0.0f;
	Check(PawnMovement::DirectReachAcceptanceRadius(tiny) >= 1.0f,
		"acceptance never falls below one unit");
}

int main()
{
	TestDoesNotCompleteSixtySevenUnitsEarly();
	TestCompletesWithinCurrentMovementStep();
	TestAcceptanceRadiusSupportsActorTargets();
	TestVerticalReachabilityVetoesHorizontalArrival();
	TestNegativeRuntimeInputsFailConservatively();
	TestDirectReachAcceptanceMatchesWhereAWalkComesToRest();
	if (Failures == 0)
		std::cout << "Pawn movement arrival tests passed\n";
	return Failures == 0 ? 0 : 1;
}
