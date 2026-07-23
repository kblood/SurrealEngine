#include "XR/XRInputDiagnostics.h"

#include <cassert>
#include <iostream>

namespace
{
	XRSessionState Focused()
	{
		return { XRSessionLifecycle::Running, XRSessionFocus::Focused };
	}

	std::array<XRInputHandAvailability, XRHandCount> Available()
	{
		std::array<XRInputHandAvailability, XRHandCount> value;
		for (auto& hand : value)
		{
			hand.ProfileOrBindingAvailable = true;
			hand.ButtonMask = XRInputButtonSelect | XRInputButtonSecondary | XRInputButtonMenu;
			hand.AxisMask = XRInputAxisThumbstick;
		}
		return value;
	}

	void TestFocusLossAndHeldButtons()
	{
		XRInputDiagnosticsAccumulator diagnostics;
		XRControllerSnapshot controllers;
		controllers.Hands[0].Connected = true;
		auto availability = Available();
		diagnostics.ObserveInput(Focused(), controllers, availability);

		controllers.Hands[0].Select.Pressed = true;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Left).PrimaryTriggerPressEdges == 1);
		controllers.Hands[0].Select.Pressed = false;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		controllers.Hands[0].Select.Pressed = true;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Left).PrimaryTriggerPressEdges == 2);

		auto events = diagnostics.ObserveSession({ XRSessionLifecycle::Running, XRSessionFocus::Visible });
		assert(events.size() == 2); // focus transition plus the cumulative activity summary
		assert(events[0].Type == XRInputDiagnosticEventType::SessionState);
		assert(events[0].PreviousSession.Focus == XRSessionFocus::Focused);
		assert(events[0].Session.Focus == XRSessionFocus::Visible);
		events = diagnostics.ObserveSession(Focused());
		assert(events.size() == 1);
		assert(events[0].PreviousSession.Focus == XRSessionFocus::Visible);
		assert(events[0].Session.Focus == XRSessionFocus::Focused);
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Left).PrimaryTriggerPressEdges == 2);

		controllers.Hands[0].Select.Pressed = false;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		controllers.Hands[0].Select.Pressed = true;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Left).PrimaryTriggerPressEdges == 3);
	}

	void TestReconnectEstablishesNewBaseline()
	{
		XRInputDiagnosticsAccumulator diagnostics;
		XRControllerSnapshot controllers;
		auto availability = Available();
		diagnostics.ObserveInput(Focused(), controllers, availability);

		controllers.Hands[1].Connected = true;
		controllers.Hands[1].Menu.Pressed = true;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Right).MenuBackPressEdges == 0);

		controllers.Hands[1].Menu.Pressed = false;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		controllers.Hands[1].Menu.Pressed = true;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Right).MenuBackPressEdges == 1);

		controllers.Hands[1].Connected = false;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		controllers.Hands[1].Connected = true;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Right).MenuBackPressEdges == 1);
	}

	void TestAvailabilityAndThumbstickCounters()
	{
		XRInputDiagnosticsAccumulator diagnostics;
		XRControllerSnapshot controllers;
		controllers.Hands[0].Connected = true;
		auto availability = Available();
		auto events = diagnostics.ObserveInput(Focused(), controllers, availability);
		bool sawAvailability = false;
		for (const auto& event : events)
			sawAvailability |= event.Type == XRInputDiagnosticEventType::HandAvailability &&
				event.Hand == XRHand::Left && event.Availability.AxisMask == XRInputAxisThumbstick;
		assert(sawAvailability);

		controllers.Hands[0].Thumbstick.X = 0.5f;
		diagnostics.ObserveInput(Focused(), controllers, availability);
		diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Left).NonzeroThumbstickSamples == 2);

		availability[0].AxisMask = 0;
		events = diagnostics.ObserveInput(Focused(), controllers, availability);
		assert(diagnostics.Counters(XRHand::Left).NonzeroThumbstickSamples == 2);
		bool sawAxisUnavailable = false;
		for (const auto& event : events)
			sawAxisUnavailable |= event.Type == XRInputDiagnosticEventType::HandAvailability &&
				event.Hand == XRHand::Left && event.Availability.AxisMask == 0;
		assert(sawAxisUnavailable);
	}
}

int main()
{
	TestFocusLossAndHeldButtons();
	TestReconnectEstablishesNewBaseline();
	TestAvailabilityAndThumbstickCounters();
	std::cout << "XR input diagnostics tests passed\n";
	return 0;
}
