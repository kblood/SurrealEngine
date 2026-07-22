#pragma once

#include "Input/InputComposition.h"

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
	XRStartupIntroFireEvent Update(InputSourceId source, bool pressed,
		bool startupIntroActive, bool menuActive);
	XRStartupIntroFireEvent ReleaseSource(InputSourceId source);

private:
	std::array<bool, 2> mirrored = {};
};
