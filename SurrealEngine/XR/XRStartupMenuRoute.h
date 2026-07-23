#pragma once

#include "XR/XRCommon.h"

#include <string>
#include <vector>

struct XRStartupLaunchPlan
{
	bool Active = false;
	bool SkipEntryMap = false;
	std::string Map;
};

// UT's Entry/CityIntro sequence can keep travelling while native XR is still
// becoming focused. Native XR starts behind one known playable map instead and
// opens the compiled UMenu only after the session can accept input.
XRStartupLaunchPlan MakeUT99OpenXRStartupLaunchPlan(bool openXRReady,
	bool isUnrealTournament, const std::string& requestedMap);

struct XRStartupMenuActions
{
	bool PrimaryFirePulse = false;
	bool EscapePulse = false;
};

class XRStartupMenuRoute
{
public:
	void Begin(bool enabled);
	XRStartupMenuActions Update(float elapsedSeconds,
		const XRSessionState& session, bool menuActive);
	bool IsActive() const { return active; }

private:
	bool active = false;
	bool fireDispatched = false;
	bool escapeDispatched = false;
	float focusedTime = 0.0f;
};

enum class XRMenuNavigationKey
{
	Up,
	Down,
	Left,
	Right,
	Enter,
	Escape
};

class XRMenuNavigationRoute
{
public:
	std::vector<XRMenuNavigationKey> Update(float elapsedSeconds,
		const XRSessionState& session, const XRControllerSnapshot& controllers,
		bool menuActive);
	void Reset();

private:
	void UpdateAxis(float value, float elapsedSeconds, int& direction,
		float& repeatTime, XRMenuNavigationKey negative,
		XRMenuNavigationKey positive, std::vector<XRMenuNavigationKey>& output);

	int verticalDirection = 0;
	int horizontalDirection = 0;
	float verticalRepeatTime = 0.0f;
	float horizontalRepeatTime = 0.0f;
	bool previousA = false;
	bool previousB = false;
	bool previousMenu[2] = {};
};

// Native OpenXR must route fire through the same console KeyEvent path as a
// physical mouse button. Writing bFire/bAltFire directly was tested on Quest
// hardware and did not advance UT99's input gates or reliably fire weapons.
enum class XRNativeKeyEventKind
{
	PrimaryFire,
	AlternateFire,
	EscapePulse
};

struct XRNativeKeyEvent
{
	XRNativeKeyEventKind Kind = XRNativeKeyEventKind::PrimaryFire;
	XRHand Hand = XRHand::Right;
	bool Pressed = false;
};

class XRNativeControllerEventRoute
{
public:
	std::vector<XRNativeKeyEvent> Update(const XRSessionState& session,
		const XRControllerSnapshot& controllers, XRHand dominantHand,
		bool gameplayInputEnabled, bool startupIntroActive, bool menuActive);
	std::vector<XRNativeKeyEvent> Release(XRHand dominantHand);

private:
	bool triggerPressed[2] = {};
	bool triggerBlocked[2] = {};
	bool previousMenu[2] = {};
};
