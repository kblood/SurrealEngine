#include "Platform/WebXR/WebXRInputRuntime.h"

#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
	bool Expect(bool condition, const char* message)
	{
		if (!condition) std::cerr << message << '\n';
		return condition;
	}

	struct ButtonEvent
	{
		InputSourceId Source;
		int32_t Control;
		bool Pressed;
	};

	class CompositionTarget final : public WebXR::RuntimeInputTarget
	{
	public:
		void SetButton(InputSourceId source, int32_t control, bool pressed) override
		{
			Buttons.push_back({ source, control, pressed });
			const auto binding = ButtonBindings.find(control);
			if (binding == ButtonBindings.end()) return;
			if (pressed)
				Composition.SetButton(binding->second, { source, control });
			else
				Composition.ReleaseControl({ source, control });
		}

		void SetAxis(InputSourceId source, int32_t control, float value) override
		{
			const auto binding = AxisBindings.find(control);
			if (binding != AxisBindings.end())
				Composition.SetAxis(binding->second, { source, control }, value);
		}

		void ReleaseSource(InputSourceId source) override
		{
			ReleasedSources.push_back(source);
			Composition.ReleaseSource(source);
		}

		InputComposition Composition;
		std::map<int32_t, std::string> ButtonBindings;
		std::map<int32_t, std::string> AxisBindings;
		std::vector<ButtonEvent> Buttons;
		std::vector<InputSourceId> ReleasedSources;
	};

	WebXR::DecodedInputSnapshot ActiveSnapshot()
	{
		WebXR::DecodedInputSnapshot snapshot;
		snapshot.SessionActive = true;
		snapshot.ActionFocused = true;
		snapshot.Sources[0].Connected = true;
		snapshot.Sources[0].PressedButtons = 1u << WebXR::InputTrigger;
		snapshot.Sources[0].Axes[2] = -0.75f;
		snapshot.Sources[1].Connected = true;
		snapshot.Sources[1].PressedButtons = 1u << WebXR::InputPrimary;
		snapshot.Sources[1].Axes[3] = 0.5f;
		return snapshot;
	}
}

int main()
{
	WebXR::StartupIntroTriggerRoute introRoute;
	if (!Expect(!introRoute.Update(InputSourceId::XRRight, true, false, false),
		"trigger was remapped outside the startup intro")) return 1;
	WebXR::StartupIntroFireEvent introPress = introRoute.Update(InputSourceId::XRRight, true, true, false);
	if (!Expect(introPress && introPress.Control == WebXR::StartupIntroFireControl::Primary && introPress.Pressed,
		"right startup trigger did not emit primary fire")) return 1;
	if (!Expect(!introRoute.Update(InputSourceId::XRRight, true, true, false),
		"held startup trigger emitted a duplicate press")) return 1;
	WebXR::StartupIntroFireEvent introRelease = introRoute.Update(InputSourceId::XRRight, false, false, true);
	if (!Expect(introRelease && introRelease.Control == WebXR::StartupIntroFireControl::Primary && !introRelease.Pressed,
		"startup trigger did not release after the menu transition")) return 1;
	if (!Expect(!introRoute.Update(InputSourceId::XRRight, true, true, true),
		"menu trigger was stolen by the startup intro route")) return 1;
	WebXR::StartupIntroFireEvent alternatePress = introRoute.Update(InputSourceId::XRLeft, true, true, false);
	if (!Expect(alternatePress && alternatePress.Control == WebXR::StartupIntroFireControl::Alternate,
		"left startup trigger did not emit alternate fire")) return 1;
	WebXR::StartupIntroFireEvent disconnectRelease = introRoute.ReleaseSource(InputSourceId::XRLeft);
	if (!Expect(disconnectRelease && !disconnectRelease.Pressed &&
		disconnectRelease.Control == WebXR::StartupIntroFireControl::Alternate,
		"controller disconnect did not release mirrored startup fire")) return 1;

	WebXR::RuntimeInputBindings bindings;
	bindings.Hands[0].Buttons[WebXR::InputTrigger] = 10;
	bindings.Hands[0].ThumbstickX = 11;
	bindings.Hands[1].Buttons[WebXR::InputPrimary] = 20;
	bindings.Hands[1].ThumbstickY = 21;

	CompositionTarget target;
	target.ButtonBindings = { { 10, "bFire" }, { 20, "bUse" } };
	target.AxisBindings = { { 11, "aStrafe" }, { 21, "aLookUp" } };
	target.Composition.SetButton("bFire", { InputSourceId::KeyboardMouse, 1 });
	target.Composition.SetButton("bUse", { InputSourceId::Gamepad, 2 });

	WebXR::InputRuntime runtime;
	WebXR::DecodedInputSnapshot decoded = ActiveSnapshot();
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.Buttons.size() == 2 && target.Buttons[0].Source == InputSourceId::XRLeft &&
		target.Buttons[1].Source == InputSourceId::XRRight,
		"focused controllers did not enter the per-hand runtime sources")) return 1;
	if (!Expect(target.Composition.IsButtonActive("bFire") && target.Composition.IsButtonActive("bUse") &&
		std::abs(target.Composition.GetAxisValue("aStrafe") + 0.75f) < 0.0001f &&
		std::abs(target.Composition.GetAxisValue("aLookUp") - 0.5f) < 0.0001f,
		"focused controller state did not reach ordinary input composition")) return 1;

	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.Buttons.size() == 2, "unchanged held buttons emitted duplicate presses")) return 1;
	decoded.Sources[0].PressedButtons = 0;
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.Buttons.size() == 3 && !target.Buttons.back().Pressed &&
		target.Composition.IsButtonActive("bFire"),
		"button release did not preserve the keyboard contributor")) return 1;
	decoded.Sources[0].PressedButtons = 1u << WebXR::InputTrigger;
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.Buttons.size() == 4 && target.Buttons.back().Pressed,
		"button re-press did not emit a new edge")) return 1;

	decoded.Sources[0] = {};
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.ReleasedSources.size() == 1 && target.ReleasedSources[0] == InputSourceId::XRLeft,
		"left disconnect did not release only the left XR source")) return 1;
	if (!Expect(target.Composition.IsButtonActive("bFire") && target.Composition.IsButtonActive("bUse") &&
		std::abs(target.Composition.GetAxisValue("aStrafe")) < 0.0001f &&
		std::abs(target.Composition.GetAxisValue("aLookUp") - 0.5f) < 0.0001f,
		"left disconnect removed desktop/gamepad/right-hand contributors")) return 1;

	decoded.ActionFocused = false;
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.ReleasedSources.size() == 2 && target.ReleasedSources[1] == InputSourceId::XRRight,
		"blur did not neutralize the remaining XR source")) return 1;
	if (!Expect(target.Composition.IsButtonActive("bFire") && target.Composition.IsButtonActive("bUse") &&
		target.Composition.Axes().empty(),
		"blur disturbed non-XR input contributors")) return 1;

	decoded.ActionFocused = true;
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.Buttons.size() == 5 && target.Buttons.back().Pressed,
		"focus recovery did not restore a held controller button")) return 1;

	decoded = {};
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), bindings, target);
	if (!Expect(target.ReleasedSources.size() == 3 && target.ReleasedSources.back() == InputSourceId::XRRight,
		"session end did not release the active XR source")) return 1;
	if (!Expect(target.Composition.IsButtonActive("bFire") && target.Composition.IsButtonActive("bUse"),
		"session end removed keyboard or gamepad input")) return 1;

	std::cout << "WebXR runtime input lifecycle tests passed\n";
	return 0;
}
