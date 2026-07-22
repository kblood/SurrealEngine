#include "Input/XRInputAdapter.h"

#include <cmath>
#include <utility>

XRInputBindings XRInputBindings::ConventionalUE1()
{
	XRInputBindings result;
	result.Hands[0].StickX = "Axis aStrafe Speed=320.0";
	result.Hands[0].StickY = "Axis aBaseY Speed=320.0";
	result.Hands[0].Trigger = "Button bAltFire";
	result.Hands[1].StickX = "Axis aTurn Speed=200.0";
	result.Hands[1].StickY = "Axis aUp Speed=320.0";
	result.Hands[1].Trigger = "Button bFire";
	result.Hands[1].Grip = "Button bDuck";
	return result;
}

XRInputAdapter::XRInputAdapter(XRInputBindings bindings) : bindings(std::move(bindings))
{
}

InputSourceId XRInputAdapter::SourceForHand(int hand)
{
	return hand == 0 ? InputSourceId::XRLeft : InputSourceId::XRRight;
}

void XRInputAdapter::UpdateButton(InputSourceId source, XRInputControl control, const std::string& command, bool down, bool& previous, XRInputTarget& target)
{
	if (down && !previous)
	{
		if (!command.empty())
			target.InputCommand(command, { source, static_cast<int32_t>(control) }, 1.0f);
	}
	else if (!down && previous)
	{
		target.ReleaseInputControl({ source, static_cast<int32_t>(control) });
	}
	previous = down;
}

void XRInputAdapter::UpdateHand(int hand, bool active, const XRHandControllerState& snapshot, XRInputTarget& target)
{
	HandState& previous = state[hand];
	InputSourceId source = SourceForHand(hand);
	if (!snapshot.Connected)
	{
		if (previous.Connected)
			target.ReleaseInputSource(source);
		previous = {};
		return;
	}

	previous.Connected = true;
	const XRHandInputBindings& handBindings = bindings.Hands[hand];
	auto deadzone = [this](float value)
	{
		return std::fabs(value) < bindings.StickDeadzone ? 0.0f : value;
	};
	if (!handBindings.StickX.empty())
		target.InputCommand(handBindings.StickX, { source, static_cast<int32_t>(XRInputControl::StickX) }, active ? deadzone(snapshot.Thumbstick.X) : 0.0f);
	if (!handBindings.StickY.empty())
		target.InputCommand(handBindings.StickY, { source, static_cast<int32_t>(XRInputControl::StickY) }, active ? deadzone(snapshot.Thumbstick.Y) : 0.0f);

	UpdateButton(source, XRInputControl::Trigger, handBindings.Trigger, active && snapshot.Select.Value >= bindings.TriggerThreshold, previous.Buttons[0], target);
	UpdateButton(source, XRInputControl::Grip, handBindings.Grip, active && snapshot.Squeeze.Value >= bindings.GripThreshold, previous.Buttons[1], target);
	UpdateButton(source, XRInputControl::PrimaryButton, handBindings.PrimaryButton, active && snapshot.Primary.Pressed, previous.Buttons[2], target);
	UpdateButton(source, XRInputControl::SecondaryButton, handBindings.SecondaryButton, active && snapshot.Secondary.Pressed, previous.Buttons[3], target);
	UpdateButton(source, XRInputControl::MenuButton, handBindings.MenuButton, active && snapshot.Menu.Pressed, previous.Buttons[4], target);
	UpdateButton(source, XRInputControl::StickClick, handBindings.StickClick, active && snapshot.ThumbstickClick.Pressed, previous.Buttons[5], target);
}

void XRInputAdapter::Update(const XRSessionState& session, const XRControllerSnapshot& snapshot, XRInputTarget& target)
{
	// A visible but unfocused XR session publishes neutral controls so an XR
	// system overlay cannot leave gameplay input held.
	const bool active = session.AcceptsInput();
	for (size_t hand = 0; hand < XRHandCount; hand++)
		UpdateHand((int)hand, active, snapshot.Hands[hand], target);
}

void XRInputAdapter::Disconnect(XRInputTarget& target)
{
	for (int hand = 0; hand < 2; hand++)
	{
		if (state[hand].Connected)
			target.ReleaseInputSource(SourceForHand(hand));
		state[hand] = {};
	}
}
