#include "PawnLedgeTransition.h"

namespace PawnMovement
{
	bool ShouldDispatchMayFall(bool canJump)
	{
		return canJump;
	}

	LedgeTransition ResolveLedgeTransition(bool deleteMe, bool stillWalking, bool canJump)
	{
		if (deleteMe || !stillWalking)
			return LedgeTransition::Abort;
		return canJump ? LedgeTransition::BeginFalling : LedgeTransition::RestoreGrounded;
	}
}
