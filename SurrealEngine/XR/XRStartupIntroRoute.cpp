#include "XR/XRStartupIntroRoute.h"

namespace
{
	int XRSourceIndex(InputSourceId source)
	{
		if (source == InputSourceId::XRLeft)
			return 0;
		if (source == InputSourceId::XRRight)
			return 1;
		return -1;
	}

	XRStartupIntroFireControl FireControlForSource(InputSourceId source,
		XRHand dominantHand)
	{
		const InputSourceId dominantSource = dominantHand == XRHand::Left ?
			InputSourceId::XRLeft : InputSourceId::XRRight;
		if (source == dominantSource)
			return XRStartupIntroFireControl::Primary;
		return source == InputSourceId::XRLeft || source == InputSourceId::XRRight ?
			XRStartupIntroFireControl::Alternate : XRStartupIntroFireControl::None;
	}
}

XRStartupIntroFireEvent XRStartupIntroTriggerRoute::Update(InputSourceId source,
	bool pressed, bool startupIntroActive, bool menuActive)
{
	const int sourceIndex = XRSourceIndex(source);
	if (sourceIndex < 0)
		return {};

	if (mirrored[sourceIndex] != XRStartupIntroFireControl::None)
	{
		if (!pressed)
		{
			const XRStartupIntroFireControl control = mirrored[sourceIndex];
			mirrored[sourceIndex] = XRStartupIntroFireControl::None;
			return { control, false };
		}
		return {};
	}

	if (pressed && startupIntroActive && !menuActive)
	{
		const XRStartupIntroFireControl control =
			FireControlForSource(source, dominantHand);
		mirrored[sourceIndex] = control;
		return { control, true };
	}
	return {};
}

XRStartupIntroFireEvent XRStartupIntroTriggerRoute::ReleaseSource(InputSourceId source)
{
	const int sourceIndex = XRSourceIndex(source);
	if (sourceIndex < 0 ||
		mirrored[sourceIndex] == XRStartupIntroFireControl::None)
		return {};
	const XRStartupIntroFireControl control = mirrored[sourceIndex];
	mirrored[sourceIndex] = XRStartupIntroFireControl::None;
	return { control, false };
}
