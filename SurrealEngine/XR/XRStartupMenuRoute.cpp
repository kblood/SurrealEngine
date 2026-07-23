#include "XR/XRStartupMenuRoute.h"

#include <algorithm>

namespace
{
	constexpr float FireDelaySeconds = 0.25f;
	constexpr float MenuDelaySeconds = 1.0f;
	constexpr float AxisThreshold = 0.55f;
	constexpr float InitialRepeatDelay = 0.35f;
	constexpr float RepeatDelay = 0.12f;
}

XRStartupLaunchPlan MakeUT99OpenXRStartupLaunchPlan(bool openXRReady,
	bool isUnrealTournament, const std::string& requestedMap)
{
	if (!openXRReady || !isUnrealTournament || !requestedMap.empty())
		return {};

	XRStartupLaunchPlan plan;
	plan.Active = true;
	plan.SkipEntryMap = true;
	plan.Map = "DM-Deck16][";
	return plan;
}

void XRStartupMenuRoute::Begin(bool enabled)
{
	active = enabled;
	fireDispatched = false;
	escapeDispatched = false;
	focusedTime = 0.0f;
}

XRStartupMenuActions XRStartupMenuRoute::Update(float elapsedSeconds,
	const XRSessionState& session, bool menuActive)
{
	XRStartupMenuActions actions;
	if (!active)
		return actions;
	if (menuActive)
	{
		active = false;
		return actions;
	}
	if (!session.AcceptsInput())
		return actions;

	focusedTime += std::max(elapsedSeconds, 0.0f);
	if (!fireDispatched && focusedTime >= FireDelaySeconds)
	{
		fireDispatched = true;
		actions.PrimaryFirePulse = true;
	}
	if (fireDispatched && !escapeDispatched && focusedTime >= MenuDelaySeconds)
	{
		escapeDispatched = true;
		actions.EscapePulse = true;
	}
	return actions;
}

void XRMenuNavigationRoute::UpdateAxis(float value, float elapsedSeconds,
	int& direction, float& repeatTime, XRMenuNavigationKey negative,
	XRMenuNavigationKey positive, std::vector<XRMenuNavigationKey>& output)
{
	const int nextDirection = value > AxisThreshold ? 1 :
		(value < -AxisThreshold ? -1 : 0);
	if (nextDirection == 0)
	{
		direction = 0;
		repeatTime = 0.0f;
		return;
	}

	if (nextDirection != direction)
	{
		output.push_back(nextDirection < 0 ? negative : positive);
		direction = nextDirection;
		repeatTime = InitialRepeatDelay;
		return;
	}

	repeatTime -= std::max(elapsedSeconds, 0.0f);
	if (repeatTime <= 0.0f)
	{
		output.push_back(nextDirection < 0 ? negative : positive);
		repeatTime = RepeatDelay;
	}
}

std::vector<XRMenuNavigationKey> XRMenuNavigationRoute::Update(float elapsedSeconds,
	const XRSessionState& session, const XRControllerSnapshot& controllers,
	bool menuActive)
{
	std::vector<XRMenuNavigationKey> output;
	const XRHandControllerState& left = controllers.ForHand(XRHand::Left);
	const XRHandControllerState& right = controllers.ForHand(XRHand::Right);
	const bool a = right.Connected && right.Primary.Pressed;
	const bool b = right.Connected && right.Secondary.Pressed;
	const bool menu[2] = {
		left.Connected && left.Menu.Pressed,
		right.Connected && right.Menu.Pressed
	};

	if (!menuActive || !session.AcceptsInput())
	{
		verticalDirection = 0;
		horizontalDirection = 0;
		verticalRepeatTime = 0.0f;
		horizontalRepeatTime = 0.0f;
		previousA = a;
		previousB = b;
		previousMenu[0] = menu[0];
		previousMenu[1] = menu[1];
		return output;
	}

	const XRVector2 stick = left.Connected ? left.Thumbstick : XRVector2{};
	UpdateAxis(stick.Y, elapsedSeconds, verticalDirection, verticalRepeatTime,
		XRMenuNavigationKey::Down, XRMenuNavigationKey::Up, output);
	UpdateAxis(stick.X, elapsedSeconds, horizontalDirection, horizontalRepeatTime,
		XRMenuNavigationKey::Left, XRMenuNavigationKey::Right, output);
	if (a && !previousA)
		output.push_back(XRMenuNavigationKey::Enter);
	if ((b && !previousB) || (menu[0] && !previousMenu[0]) ||
		(menu[1] && !previousMenu[1]))
		output.push_back(XRMenuNavigationKey::Escape);

	previousA = a;
	previousB = b;
	previousMenu[0] = menu[0];
	previousMenu[1] = menu[1];
	return output;
}

void XRMenuNavigationRoute::Reset()
{
	verticalDirection = 0;
	horizontalDirection = 0;
	verticalRepeatTime = 0.0f;
	horizontalRepeatTime = 0.0f;
	previousA = false;
	previousB = false;
	previousMenu[0] = false;
	previousMenu[1] = false;
}
