#include "XR/XRStartupMenuRoute.h"

#include <cstdlib>
#include <iostream>

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

	XRSessionState Focused()
	{
		return { XRSessionLifecycle::Running, XRSessionFocus::Focused };
	}

	void TestLaunchPolicy()
	{
		Check(!MakeUT99OpenXRStartupLaunchPlan(false, true, {}).Active,
			"desktop launch unexpectedly selected the XR startup contract");
		Check(!MakeUT99OpenXRStartupLaunchPlan(true, false, {}).Active,
			"non-UT launch unexpectedly selected the UT startup contract");

		const XRStartupLaunchPlan direct =
			MakeUT99OpenXRStartupLaunchPlan(true, true, {});
		Check(direct.Active && direct.SkipEntryMap && direct.Map == "DM-Deck16][",
			"UT OpenXR did not select the known direct-map menu background");
		const XRStartupLaunchPlan explicitMap =
			MakeUT99OpenXRStartupLaunchPlan(true, true, "DM-Curse][");
		Check(!explicitMap.Active && !explicitMap.SkipEntryMap &&
			explicitMap.Map.empty(),
			"UT OpenXR auto-opened UMenu over an explicit playable map");

		for (int startupAttempt = 0; startupAttempt < 3; startupAttempt++)
		{
			const XRStartupLaunchPlan repeated =
				MakeUT99OpenXRStartupLaunchPlan(true, true, {});
			Check(repeated.SkipEntryMap && repeated.Map != "CityIntro",
				"repeated UT OpenXR startup fell back into CityIntro travel");
		}
	}

	void TestFocusedStartupTiming()
	{
		XRStartupMenuRoute route;
		route.Begin(true);
		XRSessionState visible{ XRSessionLifecycle::Running, XRSessionFocus::Visible };
		Check(!route.Update(5.0f, visible, false).PrimaryFirePulse,
			"startup timer advanced before OpenXR was focused");
		Check(!route.Update(0.24f, Focused(), false).PrimaryFirePulse,
			"startup fire was dispatched before its validated delay");
		XRStartupMenuActions fire = route.Update(0.01f, Focused(), false);
		Check(fire.PrimaryFirePulse && !fire.EscapePulse,
			"startup fire did not dispatch at 250 ms");
		Check(!route.Update(0.74f, Focused(), false).EscapePulse,
			"startup Escape was dispatched before one focused second");
		XRStartupMenuActions menu = route.Update(0.01f, Focused(), false);
		Check(menu.EscapePulse && !menu.PrimaryFirePulse,
			"startup Escape did not follow the fire gate at one second");
		XRStartupMenuActions held = route.Update(10.0f, Focused(), false);
		Check(!held.PrimaryFirePulse && !held.EscapePulse,
			"startup actions repeated while waiting for UMenu");
		route.Update(0.0f, Focused(), true);
		Check(!route.IsActive(), "observed UMenu did not complete startup routing");
	}

	void TestMenuNavigation()
	{
		XRMenuNavigationRoute route;
		XRControllerSnapshot input;
		input.ForHand(XRHand::Left).Connected = true;
		input.ForHand(XRHand::Right).Connected = true;

		input.ForHand(XRHand::Left).Menu.Pressed = true;
		Check(route.Update(0.0f, Focused(), input, false).empty(),
			"menu button navigated while gameplay owned input");
		Check(route.Update(0.0f, Focused(), input, true).empty(),
			"button held across menu opening immediately closed UMenu");
		input.ForHand(XRHand::Left).Menu.Pressed = false;
		route.Update(0.0f, Focused(), input, true);
		input.ForHand(XRHand::Left).Menu.Pressed = true;
		auto keys = route.Update(0.0f, Focused(), input, true);
		Check(keys.size() == 1 && keys[0] == XRMenuNavigationKey::Escape,
			"fresh controller menu edge did not route through Escape");

		input.ForHand(XRHand::Left).Menu.Pressed = false;
		input.ForHand(XRHand::Left).Thumbstick.Y = 0.8f;
		keys = route.Update(0.0f, Focused(), input, true);
		Check(keys.size() == 1 && keys[0] == XRMenuNavigationKey::Up,
			"left stick did not navigate UMenu upward");
		Check(route.Update(0.34f, Focused(), input, true).empty(),
			"menu stick repeated before its initial delay");
		keys = route.Update(0.01f, Focused(), input, true);
		Check(keys.size() == 1 && keys[0] == XRMenuNavigationKey::Up,
			"menu stick did not repeat after its initial delay");

		input.ForHand(XRHand::Left).Thumbstick = {};
		input.ForHand(XRHand::Right).Primary.Pressed = true;
		keys = route.Update(0.0f, Focused(), input, true);
		Check(keys.size() == 1 && keys[0] == XRMenuNavigationKey::Enter,
			"controller A did not select the focused UMenu item");
		XRSessionState visible{ XRSessionLifecycle::Running, XRSessionFocus::Visible };
		Check(route.Update(1.0f, visible, input, true).empty(),
			"unfocused OpenXR session navigated UMenu");
		Check(route.Update(0.0f, Focused(), input, true).empty(),
			"button held through focus recovery selected a UMenu item");
	}
}

int main()
{
	TestLaunchPolicy();
	TestFocusedStartupTiming();
	TestMenuNavigation();
	std::cout << "XR startup menu route tests passed\n";
	return 0;
}
