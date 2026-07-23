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
