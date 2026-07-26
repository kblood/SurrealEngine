#pragma once

#include <cstdint>

namespace BotWarningDodgeContinuityContract
{
	inline constexpr uint64_t MaximumObservedContinuityTicks = 240;

	enum class TerminalOutcome
	{
		HarmfulWaterExit,
		HarmfulWaterDeath,
		Unknown,
	};

	enum class UnknownReason
	{
		None,
		LifeBoundary,
		PhysicsTransition,
		CommandTransition,
		SupersededLaunch,
		Timeout,
		RunEnd,
		UnprovenWaterTerminal,
	};

	const char* TerminalOutcomeName(TerminalOutcome outcome);
	const char* UnknownReasonName(UnknownReason reason);

	// A launch may continue only while its identity, life, and command snapshot
	// are retained, and native physics remains in the falling/swimming handoff.
	bool IsContinuousSnapshot(bool sameLife, bool sameActor, bool commandUnchanged,
		bool physicsFallingOrSwimming);

	// A water terminal is joinable only after the exact launch life has been
	// observed swimming and the water observer proved a harmful episode.
	bool IsProvenHarmfulWaterTerminal(bool sameLife, bool swimmingObserved,
		bool harmfulWater, bool terminalWasExitOrDeath);
}
