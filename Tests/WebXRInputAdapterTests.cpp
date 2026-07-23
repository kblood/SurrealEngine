#include "Platform/WebXR/WebXRInputAdapter.h"

#include <cmath>
#include <iostream>

namespace
{
	bool Expect(bool condition, const char* message)
	{
		if (!condition) std::cerr << message << '\n';
		return condition;
	}

	WebXR::DecodedInputSnapshot ActiveSnapshot()
	{
		WebXR::DecodedInputSnapshot source;
		source.Timestamp = 12.0;
		source.SessionActive = true;
		source.ActionFocused = true;
		auto& left = source.Sources[0];
		left.Connected = true;
		left.AimPoseValid = true;
		left.AimPosition = { -0.2f, 1.3f, -0.5f };
		left.AimOrientation = { 0.0f, 0.0f, 0.0f, 1.0f };
		left.PressedButtons = 1u << WebXR::InputPrimary;
		left.ButtonValues[WebXR::InputPrimary] = 1.0f;
		left.Axes[2] = -0.75f;
		auto& right = source.Sources[1];
		right.Connected = true;
		right.GripPoseValid = true;
		right.GripPosition = { 0.2f, 1.1f, -0.4f };
		right.GripOrientation = { 0.0f, 0.0f, 0.0f, 1.0f };
		right.PressedButtons = 1u << WebXR::InputTrigger;
		right.ButtonValues[WebXR::InputTrigger] = 0.0f;
		return source;
	}
}

int main()
{
	const WebXR::DecodedInputSnapshot source = ActiveSnapshot();
	const WebXR::AdaptedInputSnapshot adapted = WebXR::AdaptInputSnapshot(source);
	if (!Expect(adapted.Session.IsRunning() && adapted.Session.AcceptsInput(),
		"active focused session was not represented")) return 1;
	if (!Expect(adapted.Controllers.ForHand(XRHand::Left).Primary.Pressed &&
		adapted.Controllers.ForHand(XRHand::Right).Select.Pressed &&
		adapted.Controllers.ForHand(XRHand::Right).Select.Value == 1.0f,
		"semantic controller buttons were not adapted")) return 1;
	if (!Expect(adapted.Spaces.AimFor(XRHand::Left).Valid &&
		adapted.Spaces.AimFor(XRHand::Left).Position.X == -0.2f &&
		adapted.Spaces.GripFor(XRHand::Right).Valid,
		"canonical-metre aim/grip samples were not adapted")) return 1;

	InputComposition composition;
	composition.SetButton("bFire", { InputSourceId::KeyboardMouse, 1 });
	WebXR::InputActionBindings bindings;
	bindings.Select = "bFire";
	bindings.Primary = "bUse";
	bindings.ThumbstickX = "aStrafe";
	WebXR::ComposeInputSnapshot(adapted, bindings, composition);
	if (!Expect(composition.IsButtonActive("bFire") && composition.IsButtonActive("bUse") &&
		std::abs(composition.GetAxisValue("aStrafe") + 0.75f) < 0.0001f,
		"XR semantics did not enter ordinary input composition")) return 1;

	WebXR::DecodedInputSnapshot inactive;
	const auto neutral = WebXR::AdaptInputSnapshot(inactive);
	const ReleasedInputActions released = WebXR::ComposeInputSnapshot(neutral, bindings, composition);
	if (!Expect(composition.IsButtonActive("bFire"), "neutral XR snapshot removed keyboard/mouse input")) return 1;
	if (!Expect(!composition.IsButtonActive("bUse") && composition.Axes().find("aStrafe") == composition.Axes().end(),
		"neutral XR snapshot did not release XR controls")) return 1;
	if (!Expect(released.Buttons.size() == 1 && released.Buttons[0] == "bUse" &&
		released.Axes.size() == 1 && released.Axes[0] == "aStrafe",
		"released XR actions were not reported")) return 1;

	WebXR::DecodedInputSnapshot blurred = ActiveSnapshot();
	blurred.ActionFocused = false;
	const auto visible = WebXR::AdaptInputSnapshot(blurred);
	if (!Expect(visible.Session.Focus == XRSessionFocus::Visible &&
		visible.Controllers.ForHand(XRHand::Right).Connected &&
		!visible.Controllers.ForHand(XRHand::Right).Select.Pressed &&
		visible.Controllers.ForHand(XRHand::Left).Thumbstick.X == 0.0f,
		"loss of action focus did not neutralize actions while preserving connection")) return 1;

	std::cout << "WebXR shared semantic input adapter tests passed\n";
	return 0;
}
