#pragma once

#include "Input/InputComposition.h"
#include "XR/XRCommon.h"

#include <array>
#include <cstdint>

enum class XRStartupIntroFireControl : uint8_t
{
	None,
	Primary,
	Alternate
};

struct XRStartupIntroFireEvent
{
	XRStartupIntroFireControl Control = XRStartupIntroFireControl::None;
	bool Pressed = false;

	explicit operator bool() const { return Control != XRStartupIntroFireControl::None; }
};

// Scripted UE1 startup maps listen for ordinary fire key events before normal
// controller bindings are available. This route mirrors only trigger edges
// while that explicit startup gate is active and always balances a held edge.
class XRStartupIntroTriggerRoute
{
public:
	explicit XRStartupIntroTriggerRoute(XRHand dominantHand = XRHand::Right)
		: dominantHand(dominantHand) {}
	void SetDominantHand(XRHand hand) { dominantHand = hand; }
	XRStartupIntroFireEvent Update(InputSourceId source, bool pressed,
		bool startupIntroActive, bool menuActive);
	XRStartupIntroFireEvent ReleaseSource(InputSourceId source);

private:
	std::array<XRStartupIntroFireControl, 2> mirrored = {};
	XRHand dominantHand = XRHand::Right;
};
