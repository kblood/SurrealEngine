#include "UObject/ActorMovement.h"
#include "UObject/PawnReachSpecCapabilityFilter.h"

#include <iostream>
#include <string>
#include <cmath>

static int failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		failures++;
	}
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

	constexpr uint32_t walk = 1u;
	constexpr uint32_t fly = 2u;
	constexpr uint32_t jump = 8u;
	const uint32_t walkerMask = walk | jump;
	Check(PawnMovement::ReachSpecSupportedByCapabilities(0u, walkerMask),
		"reach spec without requirements");
	Check(PawnMovement::ReachSpecSupportedByCapabilities(walk | jump, walkerMask),
		"reach spec supported by all pawn capabilities");
	Check(!PawnMovement::ReachSpecSupportedByCapabilities(walk | fly, walkerMask),
		"reach spec with an unsupported capability");
	Check(!PawnMovement::ReachSpecSupportedByCapabilities(128u, walkerMask),
		"reach spec with an unknown capability flag");

	const ActorMovement::FallPredictionState fallState = {
		.Location = vec3(0.0f), .Velocity = vec3(100.0f, 0.0f, 0.0f)
	};
	const ActorMovement::FallPredictionStep fallStep = {
		.Gravity = vec3(0.0f, 0.0f, -950.0f),
		.GroundSpeed = 400.0f, .TerminalVelocity = 2000.0f, .Elapsed = 0.1f
	};
	const auto predictedFall = ActorMovement::PredictFallStep(fallState, fallStep);
	Check(predictedFall.Valid && std::abs(predictedFall.Velocity.z + 95.0f) < 0.0001f
		&& std::abs(predictedFall.Location.x - 10.0f) < 0.0001f
		&& std::abs(predictedFall.Location.z + 9.5f) < 0.0001f,
		"pain-zone forecast matches the walking-to-falling integrator");
	Check(ActorMovement::BoundedFallSegmentSampleCount(156.25f, 25.0f, 8) == 7,
		"terminal-speed fall segments are sampled within step height");
	Check(ActorMovement::ShouldVetoPainZoneLedge(true, true, false, true),
		"AI bot is stopped before a newly predicted harmful pain zone");
	Check(!ActorMovement::ShouldVetoPainZoneLedge(false, true, false, true)
		&& !ActorMovement::ShouldVetoPainZoneLedge(true, true, true, true),
		"human pawns and bots already escaping pain retain normal ledge movement");

	if (failures == 0)
		std::cout << "All actor movement tests passed.\n";
	return failures == 0 ? 0 : 1;
}
