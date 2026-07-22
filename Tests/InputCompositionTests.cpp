#include "Input/InputComposition.h"

#include <cmath>
#include <iostream>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool NearlyEqual(float a, float b)
	{
		return std::abs(a - b) < 0.0001f;
	}
}

int main()
{
	InputComposition input;
	const InputControlId keyboardFire = { InputSourceId::KeyboardMouse, 1 };
	const InputControlId keyboardAlternateFire = { InputSourceId::KeyboardMouse, 5 };
	const InputControlId rightXRFire = { InputSourceId::XRRight, 1 };
	const InputControlId keyboardForward = { InputSourceId::KeyboardMouse, 2 };
	const InputControlId keyboardBack = { InputSourceId::KeyboardMouse, 3 };
	const InputControlId gamepadMove = { InputSourceId::Gamepad, 7 };

	input.SetButton("bFire", keyboardFire);
	input.SetButton("bFire", rightXRFire);
	if (!input.IsButtonActive("bFire") || !input.IsControlActive(1))
		return Fail("button did not activate");
	ReleasedInputActions released = input.ReleaseControl(keyboardFire);
	if (!released.Buttons.empty() || !input.IsButtonActive("bFire"))
		return Fail("releasing one source released a button held by another source");
	released = input.ReleaseControl(rightXRFire);
	if (released.Buttons.size() != 1 || released.Buttons[0] != "bFire" || input.IsButtonActive("bFire"))
		return Fail("last button contributor did not release the action");

	input.SetButton("bAltFire", keyboardFire);
	input.SetButton("bAltFire", keyboardAlternateFire);
	released = input.ReleaseControl(keyboardFire);
	if (!released.Buttons.empty() || !input.IsButtonActive("bAltFire") || input.IsControlActive(1))
		return Fail("same-source controls did not compose independently");
	released = input.ReleaseControl(keyboardAlternateFire);
	if (released.Buttons.size() != 1 || released.Buttons[0] != "bAltFire")
		return Fail("same-source button did not release with its last control");

	input.SetAxis("aBaseY", keyboardForward, 20.0f);
	input.SetAxis("aBaseY", keyboardBack, -20.0f);
	input.SetAxis("aBaseY", gamepadMove, 4.25f);
	if (!NearlyEqual(input.GetAxisValue("aBaseY"), 4.25f))
		return Fail("axis contributions were not added deterministically");
	released = input.ReleaseSource(InputSourceId::KeyboardMouse);
	if (!released.Axes.empty() || !NearlyEqual(input.GetAxisValue("aBaseY"), 4.25f))
		return Fail("releasing one source removed another source's axis");
	released = input.ReleaseSource(InputSourceId::Gamepad);
	if (released.Axes.size() != 1 || released.Axes[0] != "aBaseY" || !NearlyEqual(input.GetAxisValue("aBaseY"), 0.0f))
		return Fail("last axis contributor did not release the action");

	input.SetButton("bDuck", { InputSourceId::XRLeft, 9 });
	input.SetAxis("aTurn", { InputSourceId::Synthetic, 4 }, 1.5f);
	released = input.Clear();
	if (released.Buttons.size() != 1 || released.Axes.size() != 1 ||
		input.IsButtonActive("bDuck") || !NearlyEqual(input.GetAxisValue("aTurn"), 0.0f))
		return Fail("clear did not report and remove active actions");

	return 0;
}
