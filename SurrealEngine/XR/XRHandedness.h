#pragma once

#include "XR/XRCommon.h"

// Provider-neutral hand-role policy. Physical locomotion remains on the
// conventional left-move/right-turn sticks; this policy owns only roles that
// follow the player's dominant hand.
struct XRHandedness
{
	XRHand Dominant = XRHand::Right;

	constexpr XRHand OffHand() const { return XROppositeHand(Dominant); }
	constexpr bool MirrorWeaponPresentation() const
	{
		return Dominant == XRHand::Left;
	}
};
