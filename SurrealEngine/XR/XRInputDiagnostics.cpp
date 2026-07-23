#include "Precomp.h"
#include "XR/XRInputDiagnostics.h"

#include <cmath>

namespace
{
	bool SessionEquals(const XRSessionState& a, const XRSessionState& b)
	{
		return a.Lifecycle == b.Lifecycle && a.Focus == b.Focus;
	}

	bool CountersEqual(const XRInputDiagnosticCounters& a, const XRInputDiagnosticCounters& b)
	{
		return a.PrimaryTriggerPressEdges == b.PrimaryTriggerPressEdges &&
			a.MenuBackPressEdges == b.MenuBackPressEdges &&
			a.NonzeroThumbstickSamples == b.NonzeroThumbstickSamples;
	}
}

std::vector<XRInputDiagnosticEvent> XRInputDiagnosticsAccumulator::ObserveSession(const XRSessionState& session)
{
	std::vector<XRInputDiagnosticEvent> events;
	if (!SessionInitialized || !SessionEquals(LastSession, session))
	{
		if (SessionEvents < MaxSessionEvents)
		{
			XRInputDiagnosticEvent event;
			event.Type = XRInputDiagnosticEventType::SessionState;
			event.Initial = !SessionInitialized;
			event.PreviousSession = LastSession;
			event.Session = session;
			events.push_back(event);
			SessionEvents++;
		}

		if (SessionInitialized && LastSession.Focus == XRSessionFocus::Focused &&
			session.Focus != XRSessionFocus::Focused)
		{
			AppendActivitySummary(events, XRHand::Left);
			AppendActivitySummary(events, XRHand::Right);
		}

		LastSession = session;
		SessionInitialized = true;
	}
	return events;
}

std::vector<XRInputDiagnosticEvent> XRInputDiagnosticsAccumulator::ObserveInput(
	const XRSessionState& session,
	const XRControllerSnapshot& controllers,
	const std::array<XRInputHandAvailability, XRHandCount>& availability)
{
	std::vector<XRInputDiagnosticEvent> events = ObserveSession(session);
	for (size_t index = 0; index < XRHandCount; index++)
	{
		const XRHand hand = index == 0 ? XRHand::Left : XRHand::Right;
		const XRHandControllerState& controller = controllers.Hands[index];
		HandState& state = Hands[index];
		const bool availabilityChanged = !state.Initialized ||
			state.Connected != controller.Connected ||
			!(state.Availability == availability[index]);

		if (availabilityChanged && state.AvailabilityEvents < MaxAvailabilityEventsPerHand)
		{
			XRInputDiagnosticEvent event;
			event.Type = XRInputDiagnosticEventType::HandAvailability;
			event.Initial = !state.Initialized;
			event.Session = session;
			event.Hand = hand;
			event.Connected = controller.Connected;
			event.Availability = availability[index];
			events.push_back(event);
			state.AvailabilityEvents++;
		}

		const bool wasInitialized = state.Initialized;
		const bool wasConnected = state.Connected;
		const bool newlyConnected = !wasInitialized || (!wasConnected && controller.Connected);
		if (controller.Connected && session.AcceptsInput())
		{
			const bool selectAvailable = (availability[index].ButtonMask & XRInputButtonSelect) != 0;
			const bool menuBackAvailable = (availability[index].ButtonMask &
				(XRInputButtonMenu | XRInputButtonSecondary)) != 0;
			const bool selectPressed = selectAvailable && controller.Select.Pressed;
			const bool menuBackPressed = menuBackAvailable &&
				(controller.Menu.Pressed || controller.Secondary.Pressed);

			// A newly connected controller establishes a baseline. A held button is
			// not reported as a press edge until a release/press is observed.
			if (!newlyConnected)
			{
				if (selectPressed && !state.SelectPressed)
					state.Counters.PrimaryTriggerPressEdges++;
				if (menuBackPressed && !state.MenuBackPressed)
					state.Counters.MenuBackPressEdges++;
			}
			state.SelectPressed = selectPressed;
			state.MenuBackPressed = menuBackPressed;

			if ((availability[index].AxisMask & XRInputAxisThumbstick) != 0 &&
				(std::abs(controller.Thumbstick.X) > 0.001f || std::abs(controller.Thumbstick.Y) > 0.001f))
			{
				state.Counters.NonzeroThumbstickSamples++;
			}
		}

		state.Initialized = true;
		state.Connected = controller.Connected;
		state.Availability = availability[index];

		const bool firstTrigger = state.Counters.PrimaryTriggerPressEdges == 1 &&
			state.LastReportedCounters.PrimaryTriggerPressEdges == 0;
		const bool firstMenu = state.Counters.MenuBackPressEdges == 1 &&
			state.LastReportedCounters.MenuBackPressEdges == 0;
		const bool firstStick = state.Counters.NonzeroThumbstickSamples == 1 &&
			state.LastReportedCounters.NonzeroThumbstickSamples == 0;
		if (firstTrigger || firstMenu || firstStick || (wasConnected && !controller.Connected))
			AppendActivitySummary(events, hand);
	}
	return events;
}

const XRInputDiagnosticCounters& XRInputDiagnosticsAccumulator::Counters(XRHand hand) const
{
	return Hands[XRHandIndex(hand)].Counters;
}

void XRInputDiagnosticsAccumulator::AppendActivitySummary(
	std::vector<XRInputDiagnosticEvent>& events, XRHand hand)
{
	HandState& state = Hands[XRHandIndex(hand)];
	if (state.ActivityEvents >= MaxActivityEventsPerHand ||
		CountersEqual(state.Counters, state.LastReportedCounters))
		return;
	XRInputDiagnosticEvent event;
	event.Type = XRInputDiagnosticEventType::ActivitySummary;
	event.Session = LastSession;
	event.Hand = hand;
	event.Connected = state.Connected;
	event.Counters = state.Counters;
	events.push_back(event);
	state.LastReportedCounters = state.Counters;
	state.ActivityEvents++;
}
