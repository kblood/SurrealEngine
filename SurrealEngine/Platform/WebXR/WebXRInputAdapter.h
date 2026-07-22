#pragma once

#include "Input/InputComposition.h"
#include "Platform/WebXR/WebXRInputBridge.h"
#include "XR/XRCommon.h"

#include <string>

namespace WebXR
{
	struct AdaptedInputSnapshot
	{
		double Timestamp = 0.0;
		XRSpaceSamples Spaces;
		XRControllerSnapshot Controllers;
		XRSessionState Session;
	};

	// Game-facing action names are injected by the caller. The provider does
	// not decide locomotion, weapon, menu, dominant-hand, or UI-click policy.
	struct InputActionBindings
	{
		std::string Select;
		std::string Squeeze;
		std::string Primary;
		std::string Secondary;
		std::string Menu;
		std::string ThumbstickClick;
		std::string ThumbstickX;
		std::string ThumbstickY;
	};

	AdaptedInputSnapshot AdaptInputSnapshot(const DecodedInputSnapshot& source);
	ReleasedInputActions ComposeInputSnapshot(const AdaptedInputSnapshot& source,
		const InputActionBindings& bindings, InputComposition& composition);
}
