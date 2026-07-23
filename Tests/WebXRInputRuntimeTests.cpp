#include "Platform/WebXR/WebXRInputRuntime.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	bool Near(float a, float b)
	{
		return std::fabs(a - b) < 0.0001f;
	}

	struct CommandEvent
	{
		std::string Command;
		InputControlId Control;
		float Delta = 0.0f;
	};

	class SemanticTarget final : public XRInputTarget
	{
	public:
		void InputCommand(const std::string& command, InputControlId control, float delta) override
		{
			Commands.push_back({ command, control, delta });
			if (command.starts_with("Button "))
			{
				Composition.SetButton(command.substr(7), control);
			}
			else if (command.starts_with("Axis "))
			{
				const size_t end = command.find(' ', 5);
				const std::string action = command.substr(5,
					end == std::string::npos ? end : end - 5);
				float speed = 1.0f;
				if (end != std::string::npos)
				{
					const size_t speedAt = command.find("Speed=", end);
					if (speedAt != std::string::npos)
						speed = std::strtof(command.c_str() + speedAt + 6, nullptr);
				}
				Composition.SetAxis(action, control, speed * delta);
			}
		}

		void ReleaseInputControl(InputControlId control) override
		{
			ReleasedControls.push_back(control);
			Composition.ReleaseControl(control);
		}

		void ReleaseInputSource(InputSourceId source) override
		{
			ReleasedSources.push_back(source);
			Composition.ReleaseSource(source);
		}

		int CountCommand(const std::string& command) const
		{
			int count = 0;
			for (const CommandEvent& event : Commands)
				count += event.Command == command;
			return count;
		}

		InputComposition Composition;
		std::vector<CommandEvent> Commands;
		std::vector<InputControlId> ReleasedControls;
		std::vector<InputSourceId> ReleasedSources;
	};

	class StartupIntroTarget final : public XRInputTarget
	{
	public:
		void InputCommand(const std::string&, InputControlId control, float) override
		{
			if (control.Control == static_cast<int32_t>(XRInputControl::Trigger))
			{
				const WebXR::StartupIntroFireEvent event = route.Update(control.Source,
					true, StartupActive, MenuActive);
				if (event)
					Events.push_back(event);
			}
		}

		void ReleaseInputControl(InputControlId control) override
		{
			if (control.Control == static_cast<int32_t>(XRInputControl::Trigger))
			{
				const WebXR::StartupIntroFireEvent event = route.Update(control.Source,
					false, StartupActive, MenuActive);
				if (event)
					Events.push_back(event);
			}
		}

		void ReleaseInputSource(InputSourceId source) override
		{
			const WebXR::StartupIntroFireEvent event = route.ReleaseSource(source);
			if (event)
				Events.push_back(event);
		}

		bool StartupActive = true;
		bool MenuActive = false;
		std::vector<WebXR::StartupIntroFireEvent> Events;

	private:
		WebXR::StartupIntroTriggerRoute route;
	};

	WebXR::DecodedInputSnapshot ActiveSnapshot()
	{
		WebXR::DecodedInputSnapshot snapshot;
		snapshot.SessionActive = true;
		snapshot.ActionFocused = true;
		for (WebXR::DecodedInputSource& source : snapshot.Sources)
			source.Connected = true;
		snapshot.Sources[0].PressedButtons = 1u << WebXR::InputTrigger;
		snapshot.Sources[0].ButtonValues[WebXR::InputTrigger] = 0.0f;
		snapshot.Sources[0].Axes[2] = -0.75f;
		snapshot.Sources[0].Axes[3] = 0.5f;
		snapshot.Sources[1].PressedButtons = 1u << WebXR::InputTrigger;
		snapshot.Sources[1].ButtonValues[WebXR::InputTrigger] = 0.0f;
		snapshot.Sources[1].Axes[2] = 0.25f;
		snapshot.Sources[1].Axes[3] = -0.5f;
		return snapshot;
	}

	void SetTrigger(WebXR::DecodedInputSnapshot& snapshot, size_t hand, bool pressed)
	{
		const uint32_t mask = 1u << WebXR::InputTrigger;
		if (pressed)
			snapshot.Sources[hand].PressedButtons |= mask;
		else
			snapshot.Sources[hand].PressedButtons &= ~mask;
		snapshot.Sources[hand].ButtonValues[WebXR::InputTrigger] = 0.0f;
	}
}

int main()
{
	WebXR::StartupIntroTriggerRoute introRoute;
	Check(!introRoute.Update(InputSourceId::XRRight, true, false, false),
		"trigger was remapped outside the startup intro");
	WebXR::StartupIntroFireEvent introPress = introRoute.Update(
		InputSourceId::XRRight, true, true, false);
	Check(introPress && introPress.Control == WebXR::StartupIntroFireControl::Primary && introPress.Pressed,
		"right startup trigger did not emit primary fire");
	WebXR::StartupIntroFireEvent introRelease = introRoute.Update(
		InputSourceId::XRRight, false, false, true);
	Check(introRelease && !introRelease.Pressed,
		"startup trigger was not balanced across the intro/menu transition");

	// WebXR exposes a primary-action pressed bit independently from the analog
	// trigger value. The intro and gameplay route must honor that edge once,
	// while retaining the held-button gate across UI ownership.
	WebXR::InputRuntime introRuntime;
	StartupIntroTarget introTarget;
	WebXR::DecodedInputSnapshot digitalTrigger = ActiveSnapshot();
	SetTrigger(digitalTrigger, 0, false);
	introRuntime.Apply(WebXR::AdaptInputSnapshot(digitalTrigger), true, introTarget);
	introRuntime.Apply(WebXR::AdaptInputSnapshot(digitalTrigger), true, introTarget);
	Check(introTarget.Events.size() == 1 && introTarget.Events[0].Pressed &&
		introTarget.Events[0].Control == WebXR::StartupIntroFireControl::Primary,
		"a digital WebXR trigger with zero analog value did not advance the startup intro exactly once");
	SetTrigger(digitalTrigger, 1, false);
	introRuntime.Apply(WebXR::AdaptInputSnapshot(digitalTrigger), true, introTarget);
	Check(introTarget.Events.size() == 2 && !introTarget.Events[1].Pressed,
		"the digital startup trigger release was not balanced");
	SetTrigger(digitalTrigger, 1, true);
	introRuntime.Apply(WebXR::AdaptInputSnapshot(digitalTrigger), false, introTarget);
	introRuntime.Apply(WebXR::AdaptInputSnapshot(digitalTrigger), true, introTarget);
	Check(introTarget.Events.size() == 2,
		"a trigger held across UI ownership became a phantom startup click");
	SetTrigger(digitalTrigger, 1, false);
	introRuntime.Apply(WebXR::AdaptInputSnapshot(digitalTrigger), true, introTarget);
	SetTrigger(digitalTrigger, 1, true);
	introRuntime.Apply(WebXR::AdaptInputSnapshot(digitalTrigger), true, introTarget);
	Check(introTarget.Events.size() == 3 && introTarget.Events.back().Pressed,
		"a fresh digital trigger edge after UI ownership did not reach the startup intro");

	SemanticTarget target;
	const InputControlId keyboardFire{ InputSourceId::KeyboardMouse, 1 };
	target.Composition.SetButton("bFire", keyboardFire);
	WebXR::InputRuntime runtime;
	WebXR::DecodedInputSnapshot decoded = ActiveSnapshot();
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), true, target);

	Check(target.CountCommand("Button bAltFire") == 1 &&
		target.CountCommand("Button bFire") == 1,
		"right-dominant conventional triggers did not emit direct fire semantics");
	Check(target.Composition.IsButtonActive("bFire") &&
		target.Composition.IsButtonActive("bAltFire"),
		"semantic fire commands did not enter shared input composition");
	Check(Near(target.Composition.GetAxisValue("aStrafe"), -5250.0f) &&
		Near(target.Composition.GetAxisValue("aBaseY"), 3500.0f) &&
		Near(target.Composition.GetAxisValue("aTurn"), 50.0f) &&
		Near(target.Composition.GetAxisValue("aUp"), -3500.0f),
		"conventional sticks did not emit movement and turn semantics");
	for (const CommandEvent& event : target.Commands)
		Check(event.Command.find("Joy") == std::string::npos,
			"WebXR gameplay input leaked through host Joy mappings");

	// The menu owns the raw trigger for pointer clicks. Entering the menu must
	// release gameplay controls, and leaving while held must not synthesize fire.
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), false, target);
	Check(!target.Composition.IsButtonActive("bAltFire"),
		"menu ownership left alternate fire held");
	Check(target.Composition.IsButtonActive("bFire"),
		"menu ownership removed the simultaneous keyboard fire contributor");
	Check(Near(target.Composition.GetAxisValue("aStrafe"), 0.0f) &&
		Near(target.Composition.GetAxisValue("aTurn"), 0.0f),
		"menu ownership left XR locomotion active");
	const int firePressesBeforeHandoff = target.CountCommand("Button bFire");
	const int altPressesBeforeHandoff = target.CountCommand("Button bAltFire");
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), true, target);
	Check(target.CountCommand("Button bFire") == firePressesBeforeHandoff &&
		target.CountCommand("Button bAltFire") == altPressesBeforeHandoff,
		"a held menu trigger became a gameplay press after handoff");

	SetTrigger(decoded, 0, false);
	SetTrigger(decoded, 1, false);
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), true, target);
	SetTrigger(decoded, 1, true);
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), true, target);
	Check(target.CountCommand("Button bFire") == firePressesBeforeHandoff + 1,
		"a fresh dominant-hand trigger press did not fire after menu handoff");

	// A source disconnect releases only that hand. Keyboard and the other XR
	// hand remain independent contributors.
	SetTrigger(decoded, 0, true);
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), true, target);
	decoded.Sources[0] = {};
	runtime.Apply(WebXR::AdaptInputSnapshot(decoded), true, target);
	Check(!target.Composition.IsButtonActive("bAltFire"),
		"left source disconnect left its alternate fire active");
	Check(target.Composition.IsButtonActive("bFire"),
		"left source disconnect removed right-hand or keyboard fire");
	const auto& fireContributors = target.Composition.Buttons().at("bFire");
	Check(fireContributors.contains(keyboardFire) &&
		fireContributors.contains({ InputSourceId::XRRight,
			static_cast<int32_t>(XRInputControl::Trigger) }),
		"left source disconnect did not preserve independent fire contributors");
	Check(!target.ReleasedSources.empty() &&
		target.ReleasedSources.back() == InputSourceId::XRLeft,
		"left disconnect did not release the XRLeft source");

	// Dominant-hand policy is provider-neutral and can be changed without host
	// key maps or WebXR control numbers.
	SemanticTarget leftDominantTarget;
	WebXR::InputRuntime leftDominant(XRInputBindings::ConventionalUE1(XRHand::Left));
	WebXR::DecodedInputSnapshot leftDominantSnapshot = ActiveSnapshot();
	leftDominant.Apply(WebXR::AdaptInputSnapshot(leftDominantSnapshot), true,
		leftDominantTarget);
	Check(leftDominantTarget.Commands[0].Command.find("aStrafe") != std::string::npos,
		"left-dominant test did not publish conventional movement first");
	Check(leftDominantTarget.CountCommand("Button bFire") == 1 &&
		leftDominantTarget.CountCommand("Button bAltFire") == 1,
		"left-dominant bindings did not preserve both fire actions");
	bool leftFires = false;
	bool rightAltFires = false;
	for (const CommandEvent& event : leftDominantTarget.Commands)
	{
		leftFires |= event.Command == "Button bFire" &&
			event.Control.Source == InputSourceId::XRLeft;
		rightAltFires |= event.Command == "Button bAltFire" &&
			event.Control.Source == InputSourceId::XRRight;
	}
	Check(leftFires && rightAltFires,
		"dominant-hand selection did not swap semantic trigger actions");

	runtime.Reset(target);
	Check(target.Composition.IsButtonActive("bFire"),
		"WebXR reset removed the keyboard fire contributor");
	std::cout << "WebXR semantic gameplay input tests passed\n";
	return 0;
}
