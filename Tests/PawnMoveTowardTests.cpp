#include "UObject/PawnMoveToward.h"

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

static bool SameVector(const vec3& left, const vec3& right)
{
	return left.x == right.x && left.y == right.y && left.z == right.z;
}

static void TestDestinationTracksCurrentTargetLocation()
{
	const PawnMovement::MoveTowardPoll first = PawnMovement::PrepareMoveTowardPoll({ 100.0f, 20.0f, 8.0f }, true);
	Check(SameVector(first.Destination, { 100.0f, 20.0f, 8.0f }), "first poll starts at the target location");

	// AlterDestination may have changed the previous Destination to this value.
	// The next poll must not accumulate that temporary lateral offset.
	const vec3 previousAlteredDestination(100.0f, 140.0f, 8.0f);
	const PawnMovement::MoveTowardPoll second = PawnMovement::PrepareMoveTowardPoll({ 112.0f, 24.0f, 8.0f }, true);
	Check(!SameVector(second.Destination, previousAlteredDestination), "temporary offset is not reused on the next poll");
	Check(SameVector(second.Destination, { 112.0f, 24.0f, 8.0f }), "next poll refreshes from a moving target");
}

static void TestAlterDestinationOnlyRunsForAdvancedTactics()
{
	Check(PawnMovement::PrepareMoveTowardPoll({}, true).ApplyAdvancedTactics, "advanced tactics requests AlterDestination");
	Check(!PawnMovement::PrepareMoveTowardPoll({}, false).ApplyAdvancedTactics, "ordinary MoveToward does not request AlterDestination");
}

static void TestScriptSideInvalidationAbortsMovement()
{
	Check(PawnMovement::AbortMoveTowardAfterAlterDestination(true, true), "deleted pawn aborts after script callback");
	Check(PawnMovement::AbortMoveTowardAfterAlterDestination(false, false), "invalidated target aborts after script callback");
	Check(PawnMovement::AbortMoveTowardAfterAlterDestination(true, false), "both invalid states abort safely");
	Check(!PawnMovement::AbortMoveTowardAfterAlterDestination(false, true), "valid pawn and target continue movement");
}

static void TestTacticalDestinationSafetySelection()
{
	const vec3 unaltered(100.0f, 20.0f, 8.0f);
	const vec3 preferred(40.0f, 140.0f, 8.0f);
	const vec3 opposite(160.0f, -100.0f, 8.0f);
	Check(SameVector(PawnMovement::ReflectTacticalDestination(unaltered, preferred), opposite),
		"opposite tactical destination reflects the script offset around the target");
	Check(SameVector(PawnMovement::SelectTacticalDestination(unaltered, preferred, true, false), preferred),
		"clear preferred tactical destination is retained");
	Check(SameVector(PawnMovement::SelectTacticalDestination(unaltered, preferred, false, true), opposite),
		"blocked preferred destination selects a clear reflected side");
	Check(SameVector(PawnMovement::SelectTacticalDestination(unaltered, preferred, false, false), unaltered),
		"two blocked tactical sides fall back to the unaltered target");
}

int main()
{
	TestDestinationTracksCurrentTargetLocation();
	TestAlterDestinationOnlyRunsForAdvancedTactics();
	TestScriptSideInvalidationAbortsMovement();
	TestTacticalDestinationSafetySelection();
	if (Failures == 0)
		std::cout << "Pawn MoveToward tests passed\n";
	return Failures == 0 ? 0 : 1;
}
