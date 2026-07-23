#pragma once

#include "XRCommon.h"

#include <array>

enum class XRHapticInputContext : uint8_t
{
	Disabled,
	Gameplay,
	UserInterface
};

enum class XRHapticOutcome : uint8_t
{
	GameplaySelect,
	UserInterfaceClick
};

XRHapticRequest MakeXRHapticOutcomeRequest(XRHapticOutcome outcome, XRHand hand);

class XRHapticFeedbackPolicy
{
public:
	void UpdateInput(const XRSessionState& session,
		const XRControllerSnapshot& controllers, XRHapticInputContext context,
		IXRHapticSink* sink);
	void ResolveUserInterfaceHits(const std::array<bool, XRHandCount>& exactHits,
		IXRHapticSink* sink);
	void Reset();

private:
	std::array<bool, XRHandCount> gameplayArmed = {};
	std::array<bool, XRHandCount> userInterfaceArmed = {};
	std::array<bool, XRHandCount> pendingUserInterfaceClick = {};
};
