#include "UObject/ActorMovement.h"

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

static int SimulateWalkingCollisionCallbacks(int progressingIterations)
{
	float x = 0.0f;
	float timeLeft = 1.0f;
	int callbacks = 0;
	for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
	{
		const float oldX = x;
		const float oldTimeLeft = timeLeft;
		callbacks += 2; // forward plane and aligned slide plane
		if (iteration < progressingIterations)
			x += 1.0f;
		if (!MadeWalkingIterationProgress(
			oldX, 0.0f, 0.0f, oldTimeLeft, x, 0.0f, 0.0f, timeLeft))
			break;
	}
	return callbacks;
}

int main()
{
	Check(!HasHorizontalMovement(0.0f, 0.0f), "stationary walking actor");
	Check(HasHorizontalMovement(1.0f, 0.0f), "X-only walking movement");
	Check(HasHorizontalMovement(0.0f, -1.0f), "Y-only walking movement");
	Check(HasHorizontalMovement(1.0f, 1.0f), "diagonal walking movement");

	Check(!HasSpatialMovement(0.0f, 0.0f, 0.0f), "stationary spatial actor");
	Check(HasSpatialMovement(1.0f, 0.0f, 0.0f), "X-only spatial movement");
	Check(HasSpatialMovement(0.0f, 1.0f, 0.0f), "Y-only spatial movement");
	Check(HasSpatialMovement(0.0f, 0.0f, -1.0f), "Z-only spatial movement");

	Check(!MadeWalkingIterationProgress(1.0f, 2.0f, 3.0f, 0.5f,
		1.0f, 2.0f, 3.0f, 0.5f), "unchanged walking iteration terminates");
	Check(MadeWalkingIterationProgress(1.0f, 2.0f, 3.0f, 0.5f,
		1.000001f, 2.0f, 3.0f, 0.5f), "small multi-plane movement remains progress");
	Check(MadeWalkingIterationProgress(1.0f, 2.0f, 3.0f, 0.5f,
		1.0f, 2.0f, 3.0f, 0.25f), "consumed collision time remains progress");
	Check(SimulateWalkingCollisionCallbacks(0) == 2,
		"zero-progress forward and slide attempt emits at most two callbacks");
	Check(SimulateWalkingCollisionCallbacks(2) == 6,
		"two progressing planes continue before the first zero-progress attempt terminates");

	if (failures == 0)
		std::cout << "All actor movement tests passed.\n";
	return failures == 0 ? 0 : 1;
}
