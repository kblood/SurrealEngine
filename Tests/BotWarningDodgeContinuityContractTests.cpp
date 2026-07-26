#include "BotBenchmark/BotWarningDodgeContinuityContract.h"

#include <iostream>
#include <string_view>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	using namespace BotWarningDodgeContinuityContract;
	if (!IsContinuousSnapshot(true, true, true, true)
		|| IsContinuousSnapshot(false, true, true, true)
		|| IsContinuousSnapshot(true, false, true, true)
		|| IsContinuousSnapshot(true, true, false, true)
		|| IsContinuousSnapshot(true, true, true, false))
	{
		return Fail("warning-dodge continuity did not fail closed");
	}
	if (!IsProvenHarmfulWaterTerminal(true, true, true, true)
		|| IsProvenHarmfulWaterTerminal(false, true, true, true)
		|| IsProvenHarmfulWaterTerminal(true, false, true, true)
		|| IsProvenHarmfulWaterTerminal(true, true, false, true)
		|| IsProvenHarmfulWaterTerminal(true, true, true, false))
	{
		return Fail("warning-dodge water terminal join did not fail closed");
	}
	if (std::string_view(TerminalOutcomeName(TerminalOutcome::HarmfulWaterExit)) != "harmful_water_exit"
		|| std::string_view(TerminalOutcomeName(TerminalOutcome::HarmfulWaterDeath)) != "harmful_water_death"
		|| std::string_view(UnknownReasonName(UnknownReason::PhysicsTransition)) != "physics_transition")
	{
		return Fail("warning-dodge terminal names changed");
	}
	return 0;
}
