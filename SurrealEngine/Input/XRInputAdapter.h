#pragma once

#include "Input/InputComposition.h"
#include "XR/XRCommon.h"

#include <string>

// Engine-facing command strings are policy, so the OpenXR provider only
// reports semantic controls. A launcher or game-support module can replace
// any of these bindings without changing the runtime integration.
struct XRHandInputBindings
{
	std::string StickX;
	std::string StickY;
	std::string Trigger;
	std::string Grip;
	std::string PrimaryButton;
	std::string SecondaryButton;
	std::string MenuButton;
	std::string StickClick;
};

struct XRInputBindings
{
	XRHandInputBindings Hands[2];
	float StickDeadzone = 0.15f;
	float TriggerThreshold = 0.5f;
	float GripThreshold = 0.5f;

	// Conventional UE1 pawn properties only; there are no game-name checks.
	// Unassigned face/menu controls remain available in the snapshot for a
	// later game-support or UI adapter.
	static XRInputBindings ConventionalUE1(XRHand dominantHand = XRHand::Right);
};

class XRInputTarget
{
public:
	virtual ~XRInputTarget() = default;
	virtual void InputCommand(const std::string& command, InputControlId control, float delta) = 0;
	virtual void ReleaseInputControl(InputControlId control) = 0;
	virtual void ReleaseInputSource(InputSourceId source) = 0;
};

// Stable logical control numbers within each independent XR source. The
// source id distinguishes identical left/right control numbers.
enum class XRInputControl : int32_t
{
	StickX = 1,
	StickY,
	Trigger,
	Grip,
	PrimaryButton,
	SecondaryButton,
	MenuButton,
	StickClick
};

class XRInputAdapter
{
public:
	explicit XRInputAdapter(XRInputBindings bindings = XRInputBindings::ConventionalUE1());

	// Disabling gameplay publishes neutral controls and blocks held buttons until
	// they are released, allowing an XR UI layer to own the same physical input.
	void Update(const XRSessionState& session, const XRControllerSnapshot& snapshot,
		XRInputTarget& target, bool gameplayInputEnabled = true);
	void Disconnect(XRInputTarget& target);

private:
	struct HandState
	{
		bool Connected = false;
		bool Buttons[6] = {};
		bool BlockedButtons[6] = {};
	};

	void UpdateHand(int hand, bool active, const XRHandControllerState& snapshot, XRInputTarget& target);
	void ApplyGameplayGate(int hand, bool enabled, XRHandControllerState& snapshot);
	void UpdateButton(InputSourceId source, XRInputControl control, const std::string& command, bool down, bool& previous, XRInputTarget& target);
	static InputSourceId SourceForHand(int hand);

	XRInputBindings bindings;
	HandState state[2];
};
