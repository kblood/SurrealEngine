#pragma once

#include "Input/InputComposition.h"
#include "XR/XRCommon.h"

#include <string>

enum class XRTurnMode
{
	Smooth,
	Snap
};

// Provider-neutral comfort policy for the semantic turn stick. Smooth is the
// compatibility default. Snap emits one axis pulse, then requires the stick to
// return through ReleaseThreshold before another pulse can be emitted.
struct XRTurnPolicy
{
	XRTurnMode Mode = XRTurnMode::Smooth;
	XRHand Hand = XRHand::Right;
	float SmoothScale = 1.0f;
	float SnapAxisValue = 1.0f;
	float ActivationThreshold = 0.7f;
	float ReleaseThreshold = 0.3f;

	static XRTurnPolicy Smooth(XRHand hand = XRHand::Right, float scale = 1.0f);
	static XRTurnPolicy Snap(XRHand hand = XRHand::Right, float axisValue = 1.0f,
		float activationThreshold = 0.7f, float releaseThreshold = 0.3f);
};

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

	// Conventional UE1 pawn properties and exec commands only; there are no
	// game-name or provider checks. Unassigned controls remain available for a
	// later game-support or settings adapter.
	static XRInputBindings ConventionalUE1(XRHand dominantHand = XRHand::Right);
	// Physical layout retained from the first Quest-qualified native branch:
	// X/Y switch weapons, left stick click ducks, A jumps, and B goes back.
	// Trigger and raw Menu edges are handled by the native Engine event route.
	static XRInputBindings NativeOpenXR(XRHand dominantHand = XRHand::Right);
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
	explicit XRInputAdapter(XRInputBindings bindings = XRInputBindings::ConventionalUE1(),
		XRTurnPolicy turnPolicy = XRTurnPolicy::Smooth());

	// Settings layers can change comfort mode without reaching into an XR
	// provider. Entering snap mode requires a neutral stick sample before firing.
	void SetTurnPolicy(XRTurnPolicy policy);
	const XRTurnPolicy& GetTurnPolicy() const { return turnPolicy; }
	void SetBindings(XRInputBindings newBindings);

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

	void UpdateHand(int hand, bool active, const XRHandControllerState& snapshot,
		float rawStickX, XRInputTarget& target);
	void ApplyGameplayGate(int hand, bool enabled, XRHandControllerState& snapshot);
	float ApplyTurnPolicy(int hand, bool active, float value);
	void ResetTurnState();
	void UpdateButton(InputSourceId source, XRInputControl control, const std::string& command, bool down, bool& previous, XRInputTarget& target);
	static InputSourceId SourceForHand(int hand);

	XRInputBindings bindings;
	XRTurnPolicy turnPolicy;
	bool turnLatched = false;
	bool turnNeedsNeutral = false;
	HandState state[2];
};
