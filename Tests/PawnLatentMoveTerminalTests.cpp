#include "UObject/PawnLatentMoveTerminal.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	int Failures = 0;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	PawnMovement::LatentMoveTerminalInput Terminal(float timer, float distance,
		float speed = 0.0f, float elapsed = 1.0f / 60.0f, float acceptanceRadius = 1.0f)
	{
		return {
			.TerminalObserved = true,
			.MoveTimer = timer,
			.DistanceSquared = distance * distance,
			.SpeedSquared = speed * speed,
			.Elapsed = elapsed,
			.AcceptanceRadius = acceptanceRadius,
			.VerticallyReachable = true
		};
	}

	void TestArrivalPrecedesHarmlessExpiry()
	{
		auto input = Terminal(-0.01f, 5.0f, 300.0f);
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::Arrival,
			"an in-envelope terminal must classify as arrival even when its timer expired");
	}

	void TestExpiredOutsideAcceptance()
	{
		auto input = Terminal(-0.01f, 50.0f, 300.0f);
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::TimerExpiredOutsideAcceptance,
			"a finite expired timer outside the arrival envelope must be retained");
	}

	void TestVerticalReachabilityBlocksArrival()
	{
		auto input = Terminal(-0.01f, 0.0f);
		input.VerticallyReachable = false;
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::TimerExpiredOutsideAcceptance,
			"horizontal overlap on an unreachable height must not be recorded as arrival");
	}

	void TestUnknownTerminalIsFailClosed()
	{
		auto input = Terminal(0.0f, 50.0f, 300.0f);
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::Unknown,
			"a terminal without arrival or expired timer evidence must remain unknown");
		input.TerminalObserved = false;
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::Unknown,
			"a nonterminal observation must not be classified");
	}

	void TestInvalidInputsFailClosed()
	{
		auto input = Terminal(-0.01f, 50.0f, 300.0f);
		input.MoveTimer = std::numeric_limits<float>::quiet_NaN();
		Check(!PawnMovement::IsValidLatentMoveTerminalInput(input),
			"nonfinite timer must be invalid");
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::InvalidInput,
			"nonfinite timer must fail closed");
		input = Terminal(-0.01f, 50.0f);
		input.DistanceSquared = -1.0f;
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::InvalidInput,
			"negative distance squared must fail closed");
		input = Terminal(-0.01f, 50.0f);
		input.Elapsed = -0.01f;
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::InvalidInput,
			"negative elapsed time must fail closed");
		input = Terminal(-0.01f, 50.0f);
		input.SpeedSquared = std::numeric_limits<float>::max();
		input.Elapsed = std::numeric_limits<float>::max();
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::InvalidInput,
			"nonfinite derived arrival envelope must fail closed");
		input = Terminal(-0.01f, 50.0f);
		input.AcceptanceRadius = std::numeric_limits<float>::max();
		Check(PawnMovement::ClassifyLatentMoveTerminal(input)
			== PawnMovement::LatentMoveTerminalCause::InvalidInput,
			"overflowed arrival threshold square must fail closed");
	}
}

int main()
{
	TestArrivalPrecedesHarmlessExpiry();
	TestExpiredOutsideAcceptance();
	TestVerticalReachabilityBlocksArrival();
	TestUnknownTerminalIsFailClosed();
	TestInvalidInputsFailClosed();
	if (Failures == 0)
		std::cout << "Pawn latent move terminal tests passed\n";
	return Failures == 0 ? 0 : 1;
}
