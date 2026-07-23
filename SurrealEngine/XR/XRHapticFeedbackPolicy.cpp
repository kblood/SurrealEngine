#include "XRHapticFeedbackPolicy.h"

XRHapticRequest MakeXRHapticOutcomeRequest(XRHapticOutcome outcome, XRHand hand)
{
	switch (outcome)
	{
	case XRHapticOutcome::GameplaySelect:
		return { hand, 0.35f, 0.035f, 0.0f };
	case XRHapticOutcome::UserInterfaceClick:
		return { hand, 0.18f, 0.018f, 0.0f };
	default:
		return {};
	}
}

void XRHapticFeedbackPolicy::UpdateInput(const XRSessionState& session,
	const XRControllerSnapshot& controllers, XRHapticInputContext context,
	IXRHapticSink* sink)
{
	pendingUserInterfaceClick.fill(false);
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		const XRHandControllerState& controller = controllers.Hands[handIndex];
		if (!session.AcceptsInput() || !controller.Connected ||
			context == XRHapticInputContext::Disabled)
		{
			gameplayArmed[handIndex] = false;
			userInterfaceArmed[handIndex] = false;
			continue;
		}

		const bool pressed = controller.Select.Pressed;
		const XRHand hand = handIndex == 0 ? XRHand::Left : XRHand::Right;
		if (context == XRHapticInputContext::Gameplay)
		{
			userInterfaceArmed[handIndex] = false;
			if (!pressed)
				gameplayArmed[handIndex] = true;
			else if (gameplayArmed[handIndex])
			{
				gameplayArmed[handIndex] = false;
				RouteXRHaptic(sink,
					MakeXRHapticOutcomeRequest(XRHapticOutcome::GameplaySelect, hand));
			}
		}
		else
		{
			gameplayArmed[handIndex] = false;
			if (!pressed)
				userInterfaceArmed[handIndex] = true;
			else if (userInterfaceArmed[handIndex])
			{
				userInterfaceArmed[handIndex] = false;
				pendingUserInterfaceClick[handIndex] = true;
			}
		}
	}
}

void XRHapticFeedbackPolicy::ResolveUserInterfaceHits(
	const std::array<bool, XRHandCount>& exactHits, IXRHapticSink* sink)
{
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		if (pendingUserInterfaceClick[handIndex] && exactHits[handIndex])
		{
			const XRHand hand = handIndex == 0 ? XRHand::Left : XRHand::Right;
			RouteXRHaptic(sink,
				MakeXRHapticOutcomeRequest(XRHapticOutcome::UserInterfaceClick, hand));
		}
		pendingUserInterfaceClick[handIndex] = false;
	}
}

void XRHapticFeedbackPolicy::Reset()
{
	gameplayArmed.fill(false);
	userInterfaceArmed.fill(false);
	pendingUserInterfaceClick.fill(false);
}
