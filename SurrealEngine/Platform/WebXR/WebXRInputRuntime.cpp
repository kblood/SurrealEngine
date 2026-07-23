#include "Platform/WebXR/WebXRInputRuntime.h"

#include <utility>

WebXR::InputRuntime::InputRuntime(XRInputBindings bindings)
	: adapter(std::move(bindings))
{
}

void WebXR::InputRuntime::Apply(const AdaptedInputSnapshot& snapshot,
	bool gameplayInputEnabled, XRInputTarget& target)
{
	adapter.Update(snapshot.Session, snapshot.Controllers, target, gameplayInputEnabled);
}

void WebXR::InputRuntime::Reset(XRInputTarget& target)
{
	adapter.Disconnect(target);
}

void WebXR::InputRuntime::SetDominantHand(XRHand hand, XRInputTarget& target)
{
	if (dominantHand == hand)
		return;
	adapter.Disconnect(target);
	dominantHand = hand;
	adapter.SetBindings(XRInputBindings::ConventionalUE1(hand));
}
