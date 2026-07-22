#include "Platform/WebXR/WebXRInputAdapter.h"

#include <algorithm>
#include <cmath>

namespace
{
	XRButtonState ButtonState(const WebXR::DecodedInputSource& source, WebXR::InputButton button, bool enabled)
	{
		if (!enabled)
			return {};
		const uint32_t mask = 1u << static_cast<uint32_t>(button);
		return {
			(source.PressedButtons & mask) != 0,
			(source.TouchedButtons & mask) != 0,
			source.ButtonValues[static_cast<size_t>(button)]
		};
	}

	XRPose Pose(bool valid, const std::array<float, 3>& position, const std::array<float, 4>& orientation)
	{
		return {
			valid,
			{ position[0], position[1], position[2] },
			{ orientation[0], orientation[1], orientation[2], orientation[3] }
		};
	}

	InputSourceId CompositionSource(size_t handIndex)
	{
		return handIndex == 0 ? InputSourceId::XRLeft : InputSourceId::XRRight;
	}

	void AppendReleased(ReleasedInputActions& target, ReleasedInputActions source)
	{
		target.Buttons.insert(target.Buttons.end(), source.Buttons.begin(), source.Buttons.end());
		target.Axes.insert(target.Axes.end(), source.Axes.begin(), source.Axes.end());
	}

	void SetButton(InputComposition& composition, const std::string& action,
		InputControlId control, const XRButtonState& state)
	{
		if (!action.empty() && state.Pressed)
			composition.SetButton(action, control);
	}

	void SetAxis(InputComposition& composition, const std::string& action,
		InputControlId control, float value)
	{
		if (!action.empty() && std::abs(value) > 0.000001f)
			composition.SetAxis(action, control, value);
	}
}

WebXR::AdaptedInputSnapshot WebXR::AdaptInputSnapshot(const DecodedInputSnapshot& source)
{
	AdaptedInputSnapshot result;
	result.Timestamp = source.Timestamp;
	result.Session.Lifecycle = source.SessionActive ? XRSessionLifecycle::Running : XRSessionLifecycle::Inactive;
	result.Session.Focus = !source.SessionActive ? XRSessionFocus::Unavailable :
		(source.ActionFocused ? XRSessionFocus::Focused : XRSessionFocus::Visible);
	const bool actionsEnabled = result.Session.AcceptsInput();
	for (size_t handIndex = 0; handIndex < MaxInputSources; handIndex++)
	{
		const DecodedInputSource& input = source.Sources[handIndex];
		XRHandControllerState& controller = result.Controllers.Hands[handIndex];
		controller.Connected = source.SessionActive && input.Connected;
		controller.Select = ButtonState(input, InputTrigger, actionsEnabled && controller.Connected);
		controller.Squeeze = ButtonState(input, InputSqueeze, actionsEnabled && controller.Connected);
		controller.Primary = ButtonState(input, InputPrimary, actionsEnabled && controller.Connected);
		controller.Secondary = ButtonState(input, InputSecondary, actionsEnabled && controller.Connected);
		controller.Menu = ButtonState(input, InputMenu, actionsEnabled && controller.Connected);
		controller.ThumbstickClick = ButtonState(input, InputStickClick, actionsEnabled && controller.Connected);
		if (actionsEnabled && controller.Connected)
			controller.Thumbstick = { input.Axes[2], input.Axes[3] };
		result.Spaces.Aim[handIndex] = Pose(source.SessionActive && input.AimPoseValid,
			input.AimPosition, input.AimOrientation);
		result.Spaces.Grip[handIndex] = Pose(source.SessionActive && input.GripPoseValid,
			input.GripPosition, input.GripOrientation);
	}
	return result;
}

ReleasedInputActions WebXR::ComposeInputSnapshot(const AdaptedInputSnapshot& source,
	const InputActionBindings& bindings, InputComposition& composition)
{
	ReleasedInputActions released;
	AppendReleased(released, composition.ReleaseSource(InputSourceId::XRLeft));
	AppendReleased(released, composition.ReleaseSource(InputSourceId::XRRight));

	if (source.Session.AcceptsInput())
	{
		for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
		{
			const XRHandControllerState& hand = source.Controllers.Hands[handIndex];
			if (!hand.Connected)
				continue;
			const InputSourceId inputSource = CompositionSource(handIndex);
			SetButton(composition, bindings.Select, { inputSource, InputTrigger }, hand.Select);
			SetButton(composition, bindings.Squeeze, { inputSource, InputSqueeze }, hand.Squeeze);
			SetButton(composition, bindings.Primary, { inputSource, InputPrimary }, hand.Primary);
			SetButton(composition, bindings.Secondary, { inputSource, InputSecondary }, hand.Secondary);
			SetButton(composition, bindings.Menu, { inputSource, InputMenu }, hand.Menu);
			SetButton(composition, bindings.ThumbstickClick, { inputSource, InputStickClick }, hand.ThumbstickClick);
			SetAxis(composition, bindings.ThumbstickX, { inputSource, static_cast<int32_t>(InputButtonCount) }, hand.Thumbstick.X);
			SetAxis(composition, bindings.ThumbstickY, { inputSource, static_cast<int32_t>(InputButtonCount + 1) }, hand.Thumbstick.Y);
		}
	}

	std::sort(released.Buttons.begin(), released.Buttons.end());
	released.Buttons.erase(std::unique(released.Buttons.begin(), released.Buttons.end()), released.Buttons.end());
	released.Buttons.erase(std::remove_if(released.Buttons.begin(), released.Buttons.end(),
		[&](const std::string& action) { return composition.IsButtonActive(action); }), released.Buttons.end());
	std::sort(released.Axes.begin(), released.Axes.end());
	released.Axes.erase(std::unique(released.Axes.begin(), released.Axes.end()), released.Axes.end());
	released.Axes.erase(std::remove_if(released.Axes.begin(), released.Axes.end(),
		[&](const std::string& action) { return composition.Axes().find(action) != composition.Axes().end(); }), released.Axes.end());
	return released;
}
