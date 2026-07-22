#pragma once

#include "Platform/WebXR/WebXRInputAdapter.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace WebXR
{
	constexpr int32_t UnboundRuntimeInput = -1;

	enum class StartupIntroFireControl : uint8_t
	{
		None,
		Primary,
		Alternate
	};

	struct StartupIntroFireEvent
	{
		StartupIntroFireControl Control = StartupIntroFireControl::None;
		bool Pressed = false;

		explicit operator bool() const { return Control != StartupIntroFireControl::None; }
	};

	// Scripted UE1 startup maps listen for ordinary fire key events before the
	// player's configured joystick bindings are available. Keep that one policy
	// out of the controller runtime and only mirror trigger edges while the
	// engine explicitly reports that startup gate as active.
	class StartupIntroTriggerRoute
	{
	public:
		StartupIntroFireEvent Update(InputSourceId source, bool pressed,
			bool startupIntroActive, bool menuActive);
		StartupIntroFireEvent ReleaseSource(InputSourceId source);

	private:
		std::array<bool, 2> mirrored = {};
	};

	struct RuntimeHandBindings
	{
		std::array<int32_t, InputButtonCount> Buttons = {
			UnboundRuntimeInput, UnboundRuntimeInput, UnboundRuntimeInput,
			UnboundRuntimeInput, UnboundRuntimeInput, UnboundRuntimeInput
		};
		int32_t ThumbstickX = UnboundRuntimeInput;
		int32_t ThumbstickY = UnboundRuntimeInput;
	};

	struct RuntimeInputBindings
	{
		std::array<RuntimeHandBindings, XRHandCount> Hands;
	};

	// The runtime only emits ordinary engine-style controls. The target owns
	// key binding lookup and game policy; the WebXR provider owns neither.
	class RuntimeInputTarget
	{
	public:
		virtual ~RuntimeInputTarget() = default;
		virtual void SetButton(InputSourceId source, int32_t control, bool pressed) = 0;
		virtual void SetAxis(InputSourceId source, int32_t control, float value) = 0;
		virtual void ReleaseSource(InputSourceId source) = 0;
	};

	class InputRuntime
	{
	public:
		void Apply(const AdaptedInputSnapshot& snapshot, const RuntimeInputBindings& bindings,
			RuntimeInputTarget& target);
		void Reset(RuntimeInputTarget& target);

	private:
		struct HandState
		{
			bool Active = false;
			std::array<bool, InputButtonCount> Buttons = {};
		};

		std::array<HandState, XRHandCount> hands;
	};
}

extern "C"
{
	int Surreal_ApplyWebXRInputSnapshot();
}
