#include "UObject/PawnLedgeTransition.h"

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

static void TestMayFallDispatchUsesCanJump()
{
	Check(PawnMovement::ShouldDispatchMayFall(true), "a walking pawn allowed to jump receives MayFall before a ledge");
	Check(!PawnMovement::ShouldDispatchMayFall(false), "a ledge-avoiding state that already cleared bCanJump does not receive MayFall");
}

static void TestScriptCanVetoTheFall()
{
	Check(PawnMovement::ResolveLedgeTransition(false, true, false) == PawnMovement::LedgeTransition::RestoreGrounded,
		"clearing bCanJump vetoes the unsupported step and restores the grounded position");
	Check(PawnMovement::ResolveLedgeTransition(false, true, true) == PawnMovement::LedgeTransition::BeginFalling,
		"leaving bCanJump set permits the normal walking-to-falling transition");
}

static void TestCallbackSideEffectsAbortStaleWalkingWork()
{
	Check(PawnMovement::ResolveLedgeTransition(true, true, true) == PawnMovement::LedgeTransition::Abort,
		"a Pawn deleted by MayFall aborts the walking transition");
	Check(PawnMovement::ResolveLedgeTransition(false, false, true) == PawnMovement::LedgeTransition::Abort,
		"a physics change made by MayFall aborts the stale walking transition");
	Check(PawnMovement::ResolveLedgeTransition(true, false, false) == PawnMovement::LedgeTransition::Abort,
		"callback lifetime and physics changes take precedence over the jump flag");
}

int main()
{
	TestMayFallDispatchUsesCanJump();
	TestScriptCanVetoTheFall();
	TestCallbackSideEffectsAbortStaleWalkingWork();
	if (Failures == 0)
		std::cout << "Pawn ledge transition tests passed\n";
	return Failures == 0 ? 0 : 1;
}
