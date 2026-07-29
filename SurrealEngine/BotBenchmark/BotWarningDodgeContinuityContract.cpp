#include "BotWarningDodgeContinuityContract.h"

namespace BotWarningDodgeContinuityContract
{
	const char* TerminalOutcomeName(TerminalOutcome outcome)
	{
		switch (outcome)
		{
		case TerminalOutcome::HarmfulWaterExit: return "harmful_water_exit";
		case TerminalOutcome::HarmfulWaterDeath: return "harmful_water_death";
		case TerminalOutcome::Unknown: return "unknown";
		}
		return "unknown";
	}

	const char* UnknownReasonName(UnknownReason reason)
	{
		switch (reason)
		{
		case UnknownReason::None: return "";
		case UnknownReason::LifeBoundary: return "life_boundary";
		case UnknownReason::PhysicsTransition: return "physics_transition";
		case UnknownReason::CommandTransition: return "command_transition";
		case UnknownReason::SupersededLaunch: return "superseded_launch";
		case UnknownReason::Timeout: return "timeout";
		case UnknownReason::RunEnd: return "run_end";
		case UnknownReason::UnprovenWaterTerminal: return "unproven_water_terminal";
		}
		return "unproven_water_terminal";
	}

	bool IsContinuousSnapshot(bool sameLife, bool sameActor, bool commandUnchanged,
		bool physicsFallingOrSwimming)
	{
		return sameLife && sameActor && commandUnchanged && physicsFallingOrSwimming;
	}

	bool IsProvenHarmfulWaterTerminal(bool sameLife, bool swimmingObserved,
		bool harmfulWater, bool terminalWasExitOrDeath)
	{
		return sameLife && swimmingObserved && harmfulWater && terminalWasExitOrDeath;
	}
}
