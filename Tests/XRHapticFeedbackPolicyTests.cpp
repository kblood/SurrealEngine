#include "XR/XRHapticFeedbackPolicy.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
	class RecordingSink final : public IXRHapticSink
	{
	public:
		bool SubmitHaptic(const XRHapticRequest& request) override
		{
			Requests.push_back(request);
			return Accept;
		}

		bool Accept = true;
		std::vector<XRHapticRequest> Requests;
	};

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool Distinct(const XRHapticRequest& left, const XRHapticRequest& right)
	{
		return std::abs(left.Amplitude - right.Amplitude) > 0.0001f ||
			std::abs(left.DurationSeconds - right.DurationSeconds) > 0.0001f ||
			std::abs(left.FrequencyHz - right.FrequencyHz) > 0.0001f;
	}

	XRSessionState FocusedSession()
	{
		return { XRSessionLifecycle::Running, XRSessionFocus::Focused };
	}

	void SetConnected(XRControllerSnapshot& controllers, XRHand hand, bool connected)
	{
		controllers.ForHand(hand).Connected = connected;
	}

	void SetPressed(XRControllerSnapshot& controllers, XRHand hand, bool pressed)
	{
		controllers.ForHand(hand).Select.Pressed = pressed;
	}
}

int main()
{
	XRHapticFeedbackPolicy policy;
	RecordingSink sink;
	XRSessionState session = FocusedSession();
	XRControllerSnapshot controllers;
	SetConnected(controllers, XRHand::Right, true);

	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	if (!sink.Requests.empty())
		return Fail("an initially held gameplay trigger produced haptics");
	SetPressed(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	if (sink.Requests.size() != 1 || sink.Requests[0].Hand != XRHand::Right ||
		!IsValidXRHapticRequest(sink.Requests[0]) ||
		sink.Requests[0].DurationSeconds > 1.0f)
		return Fail("a fresh gameplay trigger edge did not produce exactly one bounded pulse");

	session.Focus = XRSessionFocus::Visible;
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	session.Focus = XRSessionFocus::Focused;
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	if (sink.Requests.size() != 1)
		return Fail("held-button focus recovery produced gameplay haptics");
	SetPressed(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	SetConnected(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	SetConnected(controllers, XRHand::Right, true);
	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	if (sink.Requests.size() != 1)
		return Fail("held-button controller recovery produced gameplay haptics");
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	policy.ResolveUserInterfaceHits({ false, true }, &sink);
	if (sink.Requests.size() != 1)
		return Fail("a held gameplay-to-UI handoff produced haptics");

	policy.Reset();
	SetPressed(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Disabled, &sink);
	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Disabled, &sink);
	if (sink.Requests.size() != 1)
		return Fail("disabled XR input produced haptics");

	policy.Reset();
	SetPressed(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	policy.ResolveUserInterfaceHits({ false, false }, &sink);
	policy.ResolveUserInterfaceHits({ false, true }, &sink);
	if (sink.Requests.size() != 1)
		return Fail("a UI miss or stale hit produced haptics");
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	policy.ResolveUserInterfaceHits({ false, true }, &sink);
	if (sink.Requests.size() != 1)
		return Fail("moving a held UI trigger onto a hit produced haptics");
	SetPressed(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	policy.ResolveUserInterfaceHits({ false, true }, &sink);
	if (sink.Requests.size() != 2 || sink.Requests[1].Hand != XRHand::Right ||
		!IsValidXRHapticRequest(sink.Requests[1]) ||
		sink.Requests[1].DurationSeconds > 1.0f ||
		!Distinct(sink.Requests[0], sink.Requests[1]))
		return Fail("an exact fresh UI click did not produce its distinct bounded pulse");

	policy.Reset();
	SetPressed(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	policy.UpdateInput(session, controllers, XRHapticInputContext::UserInterface, &sink);
	policy.ResolveUserInterfaceHits({ false, true }, &sink);
	if (sink.Requests.size() != 2)
		return Fail("an unresolved UI edge leaked into a later frame");

	policy.Reset();
	SetPressed(controllers, XRHand::Right, false);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	sink.Accept = false;
	SetPressed(controllers, XRHand::Right, true);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	policy.UpdateInput(session, controllers, XRHapticInputContext::Gameplay, &sink);
	if (sink.Requests.size() != 3)
		return Fail("a rejected haptic transport retried while the trigger remained held");

	return 0;
}
