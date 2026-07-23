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
	const XRInputBindings rightDominant = XRInputBindings::ConventionalUE1(XRHand::Right);
	Check(rightDominant.Hands[1].PrimaryButton == "Jump",
		"dominant primary face button must provide the conventional jump action");
	Check(rightDominant.Hands[0].PrimaryButton == "NextWeapon" &&
		rightDominant.Hands[0].SecondaryButton == "KeyPulse Escape" &&
		rightDominant.Hands[1].SecondaryButton == "KeyPulse Escape",
		"controller back buttons must use the physical Escape route");
	Check(rightDominant.Hands[0].MenuButton == "KeyPulse Escape" &&
		rightDominant.Hands[1].MenuButton == "KeyPulse Escape",
		"provider-reported menu controls must use Escape from either hand");
	const XRInputBindings leftDominant = XRInputBindings::ConventionalUE1(XRHand::Left);
	Check(leftDominant.Hands[0].PrimaryButton == "Jump" &&
		leftDominant.Hands[1].SecondaryButton == "KeyPulse Escape",
		"dominant-hand selection must swap face-button action policy");
	XRInputAdapter conventional(rightDominant);
	FakeTarget conventionalTarget;
	XRSessionState conventionalSession{ XRSessionLifecycle::Running, XRSessionFocus::Focused };
	XRControllerSnapshot conventionalSnapshot;
	conventionalSnapshot.Hands[0].Connected = true;
	conventionalSnapshot.Hands[0].Primary.Pressed = true;
	conventionalSnapshot.Hands[0].Secondary.Pressed = true;
	conventionalSnapshot.Hands[1].Connected = true;
	conventionalSnapshot.Hands[1].Primary.Pressed = true;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(conventionalTarget.commandCounts["Jump"] == 1 &&
		conventionalTarget.commandCounts["NextWeapon"] == 1 &&
		conventionalTarget.commandCounts["KeyPulse Escape"] == 1,
		"conventional face-button actions did not reach the engine command target");

	// Compatibility remains smooth continuous turn until settings explicitly
	// select snap. The adapter policy can be changed without provider changes.
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.5f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(conventional.GetTurnPolicy().Mode == XRTurnMode::Smooth &&
		Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.5f),
		"default right-hand turning must preserve continuous stick output");
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.5f),
		"smooth turn must remain continuous while held");

	conventional.SetTurnPolicy(XRTurnPolicy::Snap(XRHand::Right, 0.8f, 0.7f, 0.3f));
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.0f),
		"entering snap mode must require a neutral sample");
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.0f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.9f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.8f),
		"snap turn did not emit its configured positive pulse");
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.0f),
		"held snap input emitted more than one pulse");
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.5f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	conventionalSnapshot.Hands[1].Thumbstick.X = -0.9f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.0f),
		"snap latch rearmed without crossing the release threshold");
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.2f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	conventionalSnapshot.Hands[1].Thumbstick.X = -0.9f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), -0.8f),
		"snap turn did not rearm and preserve direction after recentering");

	conventionalSession.Focus = XRSessionFocus::Visible;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	conventionalSession.Focus = XRSessionFocus::Focused;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.0f),
		"stick held through focus loss emitted an unsafe snap");
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.0f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.9f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.8f),
		"snap turn did not rearm after focus recovery and recentering");
	conventionalSnapshot.Hands[1].Connected = false;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	conventionalSnapshot.Hands[1].Connected = true;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.0f),
		"reconnecting with a held stick emitted an unsafe snap");
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.0f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	conventionalSnapshot.Hands[1].Thumbstick.X = 0.9f;
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.8f),
		"snap turn did not rearm after reconnecting and recentering");

	conventional.SetTurnPolicy(XRTurnPolicy::Smooth(XRHand::Right, 0.5f));
	conventional.Update(conventionalSession, conventionalSnapshot, conventionalTarget);
	Check(Near(conventionalTarget.input.GetAxisValue("aTurn"), 0.45f),
		"runtime smooth-turn scale was not applied");

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

	XRSessionState session{ XRSessionLifecycle::Running, XRSessionFocus::Focused };
	XRControllerSnapshot snapshot;
	snapshot.Hands[0].Connected = true;
	snapshot.Hands[0].Thumbstick.X = 0.5f;
	snapshot.Hands[0].Select.Value = 1.0f;
	snapshot.Hands[1].Connected = true;
	snapshot.Hands[1].Select.Value = 1.0f;
	adapter.Update(session, snapshot, target);
	Check(target.commandCounts["Button Fire"] == 2, "both hands must publish independent press edges");
	Check(target.input.IsButtonActive("Fire"), "XR press did not compose with desktop input");
	Check(Near(target.input.GetAxisValue("MoveX"), 2.5f), "XR and desktop axes did not compose");

	adapter.Update(session, snapshot, target);
	Check(target.commandCounts["Button Fire"] == 2, "held buttons must not emit another press");

	snapshot.Hands[0].Select.Value = 0.0f;
	adapter.Update(session, snapshot, target);
	Check(target.controlReleases == 1 && target.input.IsButtonActive("Fire"), "one hand releasing must preserve other contributors");
	snapshot.Hands[1].Select.Value = 0.0f;
	adapter.Update(session, snapshot, target);
	Check(target.input.IsButtonActive("Fire"), "XR release removed the desktop button contributor");

	snapshot.Hands[0].Thumbstick.X = 0.05f;
	adapter.Update(session, snapshot, target);
	Check(Near(target.input.GetAxisValue("MoveX"), 2.0f), "deadzone must publish an explicit zero axis contribution");

	snapshot.Hands[0].Primary.Pressed = true;
	adapter.Update(session, snapshot, target);
	Check(target.input.IsButtonActive("Use"), "configured semantic button did not publish");
	snapshot.Hands[0].Connected = false;
	adapter.Update(session, snapshot, target);
	Check(target.sourceReleases[InputSourceId::XRLeft] == 1, "left disconnect must release the complete XRLeft source");
	Check(!target.input.IsButtonActive("Use"), "disconnect left a held control active");
	Check(Near(target.input.GetAxisValue("MoveX"), 2.0f), "disconnect removed the desktop axis contributor");

	snapshot.Hands[1].Squeeze.Value = 1.0f;
	adapter.Update(session, snapshot, target);
	Check(target.input.IsButtonActive("Duck"), "right-hand grip did not publish");
	adapter.Disconnect(target);
	Check(target.sourceReleases[InputSourceId::XRRight] == 1, "session stop must release XRRight");
	Check(!target.input.IsButtonActive("Duck"), "session stop left a held button active");
	Check(target.input.IsButtonActive("Fire"), "session stop removed simultaneous keyboard input");
	Check(Near(target.input.GetAxisValue("MoveX"), 2.0f), "session stop removed simultaneous desktop axis input");

	// Runtime focus is provider-neutral. Losing focus must neutralize XR controls
	// while retaining simultaneous keyboard/mouse contributors.
	snapshot.Hands[1].Connected = true;
	snapshot.Hands[1].Select.Value = 1.0f;
	adapter.Update(session, snapshot, target);
	Check(target.commandCounts["Button Fire"] == 3, "focused session did not restore XR input");
	session.Focus = XRSessionFocus::Visible;
	adapter.Update(session, snapshot, target);
	Check(target.input.IsButtonActive("Fire"), "focus loss removed the desktop button contributor");
	Check(target.controlReleases == 4, "focus loss did not release active XR buttons");
	session.Focus = XRSessionFocus::Focused;
	adapter.Update(session, snapshot, target);
	Check(target.commandCounts["Button Fire"] == 3,
		"a button held through focus loss became a new XR press");
	snapshot.Hands[1].Select.Value = 0.0f;
	adapter.Update(session, snapshot, target);
	snapshot.Hands[1].Select.Value = 1.0f;
	adapter.Update(session, snapshot, target);
	Check(target.commandCounts["Button Fire"] == 4,
		"a fresh press after focus recovery was not restored");

	adapter.Disconnect(target);
	adapter.Disconnect(target);
	Check(target.sourceReleases[InputSourceId::XRLeft] == 1 && target.sourceReleases[InputSourceId::XRRight] == 2,
		"repeated session-stop cleanup must be idempotent");
	return 0;
}
