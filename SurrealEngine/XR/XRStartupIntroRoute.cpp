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

	XRStartupIntroFireControl FireControlForSource(InputSourceId source)
	{
		return source == InputSourceId::XRRight ? XRStartupIntroFireControl::Primary :
			source == InputSourceId::XRLeft ? XRStartupIntroFireControl::Alternate :
			XRStartupIntroFireControl::None;
	}
}

XRStartupIntroFireEvent XRStartupIntroTriggerRoute::Update(InputSourceId source,
	bool pressed, bool startupIntroActive, bool menuActive)
{
	const int sourceIndex = XRSourceIndex(source);
	if (sourceIndex < 0)
		return {};

	if (mirrored[sourceIndex])
	{
		if (!pressed)
		{
			mirrored[sourceIndex] = false;
			return { FireControlForSource(source), false };
		}
		return {};
	}

	if (pressed && startupIntroActive && !menuActive)
	{
		mirrored[sourceIndex] = true;
		return { FireControlForSource(source), true };
	}
	return {};
}

XRStartupIntroFireEvent XRStartupIntroTriggerRoute::ReleaseSource(InputSourceId source)
{
	const int sourceIndex = XRSourceIndex(source);
	if (sourceIndex < 0 || !mirrored[sourceIndex])
		return {};
	mirrored[sourceIndex] = false;
	return { FireControlForSource(source), false };
}
