#pragma once

namespace PawnMovement
{
	enum class LedgeTransition
	{
		Abort,
		RestoreGrounded,
		BeginFalling
	};

	bool ShouldDispatchMayFall(bool canJump);
	LedgeTransition ResolveLedgeTransition(bool deleteMe, bool stillWalking, bool canJump);
}
