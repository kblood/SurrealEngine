#pragma once

namespace PawnMovement
{
	enum class LatentMoveTerminalCause
	{
		Arrival,
		TimerExpiredOutsideAcceptance,
		InvalidInput,
		Unknown
	};

	// Values are sampled after the native movement tick decrements MoveTimer.
	// This model only classifies a terminal that the caller already observed.
	struct LatentMoveTerminalInput
	{
		bool TerminalObserved = false;
		float MoveTimer = 0.0f;
		float DistanceSquared = 0.0f;
		float SpeedSquared = 0.0f;
		float Elapsed = 0.0f;
		float AcceptanceRadius = 0.0f;
		bool VerticallyReachable = false;
	};

	bool IsValidLatentMoveTerminalInput(const LatentMoveTerminalInput& input);
	LatentMoveTerminalCause ClassifyLatentMoveTerminal(const LatentMoveTerminalInput& input);
}
