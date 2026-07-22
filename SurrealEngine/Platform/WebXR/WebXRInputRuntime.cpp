#include "Platform/WebXR/WebXRInputRuntime.h"

namespace
{
	int XRSourceIndex(InputSourceId source)
	{
		if (source == InputSourceId::XRLeft)
			return 0;
		if (source == InputSourceId::XRRight)
			return 1;
		return -1;
	}

	WebXR::StartupIntroFireControl FireControlForSource(InputSourceId source)
	{
		return source == InputSourceId::XRRight ? WebXR::StartupIntroFireControl::Primary :
			source == InputSourceId::XRLeft ? WebXR::StartupIntroFireControl::Alternate :
			WebXR::StartupIntroFireControl::None;
	}

	InputSourceId SourceForHand(size_t handIndex)
	{
		return handIndex == static_cast<size_t>(XRHand::Left) ?
			InputSourceId::XRLeft : InputSourceId::XRRight;
	}

	std::array<bool, WebXR::InputButtonCount> PressedButtons(const XRHandControllerState& hand)
	{
		return {
			hand.Select.Pressed,
			hand.Squeeze.Pressed,
			hand.Primary.Pressed,
			hand.Secondary.Pressed,
			hand.Menu.Pressed,
			hand.ThumbstickClick.Pressed
		};
	}
}

WebXR::StartupIntroFireEvent WebXR::StartupIntroTriggerRoute::Update(InputSourceId source,
	bool pressed, bool startupIntroActive, bool menuActive)
{
	const int sourceIndex = XRSourceIndex(source);
	if (sourceIndex < 0)
		return {};

	if (mirrored[sourceIndex])
	{
		if (!pressed)
		{
			mirrored[sourceIndex] = false;
			return { FireControlForSource(source), false };
		}
		return {};
	}

	if (pressed && startupIntroActive && !menuActive)
	{
		mirrored[sourceIndex] = true;
		return { FireControlForSource(source), true };
	}
	return {};
}

WebXR::StartupIntroFireEvent WebXR::StartupIntroTriggerRoute::ReleaseSource(InputSourceId source)
{
	const int sourceIndex = XRSourceIndex(source);
	if (sourceIndex < 0 || !mirrored[sourceIndex])
		return {};
	mirrored[sourceIndex] = false;
	return { FireControlForSource(source), false };
}

void WebXR::InputRuntime::Apply(const AdaptedInputSnapshot& snapshot,
	const RuntimeInputBindings& bindings, RuntimeInputTarget& target)
{
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		HandState& previous = hands[handIndex];
		const XRHandControllerState& hand = snapshot.Controllers.Hands[handIndex];
		const InputSourceId source = SourceForHand(handIndex);
		const bool active = snapshot.Session.AcceptsInput() && hand.Connected;
		if (!active)
		{
			if (previous.Active)
				target.ReleaseSource(source);
			previous = {};
			continue;
		}

		const std::array<bool, InputButtonCount> pressed = PressedButtons(hand);
		for (size_t button = 0; button < InputButtonCount; button++)
		{
			const int32_t control = bindings.Hands[handIndex].Buttons[button];
			if (control != UnboundRuntimeInput && pressed[button] != previous.Buttons[button])
				target.SetButton(source, control, pressed[button]);
		}

		const RuntimeHandBindings& handBindings = bindings.Hands[handIndex];
		if (handBindings.ThumbstickX != UnboundRuntimeInput)
			target.SetAxis(source, handBindings.ThumbstickX, hand.Thumbstick.X);
		if (handBindings.ThumbstickY != UnboundRuntimeInput)
			target.SetAxis(source, handBindings.ThumbstickY, hand.Thumbstick.Y);
		previous.Active = true;
		previous.Buttons = pressed;
	}
}

void WebXR::InputRuntime::Reset(RuntimeInputTarget& target)
{
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		if (hands[handIndex].Active)
			target.ReleaseSource(SourceForHand(handIndex));
		hands[handIndex] = {};
	}
}
