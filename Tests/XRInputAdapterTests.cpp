#include "Input/XRInputAdapter.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

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

	class FakeTarget : public XRInputTarget
	{
	public:
		void InputCommand(const std::string& command, InputControlId control, float delta) override
		{
			commandCounts[command]++;
			if (command.starts_with("Button "))
			{
				input.SetButton(command.substr(7), control);
			}
			else if (command.starts_with("Axis "))
			{
				size_t end = command.find(' ', 5);
				input.SetAxis(command.substr(5, end == std::string::npos ? end : end - 5), control, delta);
			}
		}

		void ReleaseInputControl(InputControlId control) override
		{
			controlReleases++;
			input.ReleaseControl(control);
		}

		void ReleaseInputSource(InputSourceId source) override
		{
			sourceReleases[source]++;
			input.ReleaseSource(source);
		}

		InputComposition input;
		std::map<std::string, int> commandCounts;
		std::map<InputSourceId, int> sourceReleases;
		int controlReleases = 0;
	};
}

int main()
{
	XRInputBindings bindings;
	bindings.Hands[0].StickX = "Axis MoveX";
	bindings.Hands[0].StickY = "Axis MoveY";
	bindings.Hands[0].Trigger = "Button Fire";
	bindings.Hands[0].PrimaryButton = "Button Use";
	bindings.Hands[1].Trigger = "Button Fire";
	bindings.Hands[1].Grip = "Button Duck";
	XRInputAdapter adapter(bindings);
	FakeTarget target;

	const InputControlId desktopFire{ InputSourceId::KeyboardMouse, 1 };
	const InputControlId desktopMove{ InputSourceId::KeyboardMouse, 2 };
	target.input.SetButton("Fire", desktopFire);
	target.input.SetAxis("MoveX", desktopMove, 2.0f);

	OpenXRInputSnapshot snapshot;
	snapshot.Controllers[0].Connected = true;
	snapshot.Controllers[0].ActionsActive = true;
	snapshot.Controllers[0].StickX = 0.5f;
	snapshot.Controllers[0].Trigger = 1.0f;
	snapshot.Controllers[1].Connected = true;
	snapshot.Controllers[1].ActionsActive = true;
	snapshot.Controllers[1].Trigger = 1.0f;
	adapter.Update(snapshot, target);
	Check(target.commandCounts["Button Fire"] == 2, "both hands must publish independent press edges");
	Check(target.input.IsButtonActive("Fire"), "XR press did not compose with desktop input");
	Check(Near(target.input.GetAxisValue("MoveX"), 2.5f), "XR and desktop axes did not compose");

	adapter.Update(snapshot, target);
	Check(target.commandCounts["Button Fire"] == 2, "held buttons must not emit another press");

	snapshot.Controllers[0].Trigger = 0.0f;
	adapter.Update(snapshot, target);
	Check(target.controlReleases == 1 && target.input.IsButtonActive("Fire"), "one hand releasing must preserve other contributors");
	snapshot.Controllers[1].Trigger = 0.0f;
	adapter.Update(snapshot, target);
	Check(target.input.IsButtonActive("Fire"), "XR release removed the desktop button contributor");

	snapshot.Controllers[0].StickX = 0.05f;
	adapter.Update(snapshot, target);
	Check(Near(target.input.GetAxisValue("MoveX"), 2.0f), "deadzone must publish an explicit zero axis contribution");

	snapshot.Controllers[0].PrimaryButton = true;
	adapter.Update(snapshot, target);
	Check(target.input.IsButtonActive("Use"), "configured semantic button did not publish");
	snapshot.Controllers[0].Connected = false;
	adapter.Update(snapshot, target);
	Check(target.sourceReleases[InputSourceId::XRLeft] == 1, "left disconnect must release the complete XRLeft source");
	Check(!target.input.IsButtonActive("Use"), "disconnect left a held control active");
	Check(Near(target.input.GetAxisValue("MoveX"), 2.0f), "disconnect removed the desktop axis contributor");

	snapshot.Controllers[1].Grip = 1.0f;
	adapter.Update(snapshot, target);
	Check(target.input.IsButtonActive("Duck"), "right-hand grip did not publish");
	adapter.Disconnect(target);
	Check(target.sourceReleases[InputSourceId::XRRight] == 1, "session stop must release XRRight");
	Check(!target.input.IsButtonActive("Duck"), "session stop left a held button active");
	Check(target.input.IsButtonActive("Fire"), "session stop removed simultaneous keyboard input");
	Check(Near(target.input.GetAxisValue("MoveX"), 2.0f), "session stop removed simultaneous desktop axis input");

	adapter.Disconnect(target);
	Check(target.sourceReleases[InputSourceId::XRLeft] == 1 && target.sourceReleases[InputSourceId::XRRight] == 1,
		"repeated session-stop cleanup must be idempotent");
	return 0;
}
