#include "Precomp.h"
#include "WebXRHaptics.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#endif

namespace
{
	uint32_t BrowserHandValue(WebXRHapticHand hand)
	{
		switch (hand)
		{
		case WebXRHapticHand::Left: return 1;
		case WebXRHapticHand::Right: return 2;
		default: return 0;
		}
	}

#ifdef __EMSCRIPTEN__

	EM_JS(int, QueueBrowserWebXRHapticPulse,
		(unsigned int hand, float intensity, unsigned int durationMs),
	{
		var handedness = hand === 1 ? 'left' : (hand === 2 ? 'right' : '');
		if (!handedness || typeof window === 'undefined' ||
			typeof window.surrealXRQueueHapticPulse !== 'function')
			return 0;

		try
		{
			return window.surrealXRQueueHapticPulse(
				handedness, intensity, durationMs) ? 1 : 0;
		}
		catch (error)
		{
			return 0;
		}
	});

	EM_JS(int, SetBrowserWebXRHapticsEnabled, (int enabled),
	{
		if (typeof window === 'undefined' ||
			typeof window.surrealXRSetHapticsEnabled !== 'function')
			return 0;

		try
		{
			var requested = !!enabled;
			return !!window.surrealXRSetHapticsEnabled(requested) === requested ? 1 : 0;
		}
		catch (error)
		{
			return 0;
		}
	});

#endif
}

bool QueueWebXRHapticPulse(WebXRHapticHand hand, float intensity, uint32_t durationMs)
{
	const uint32_t browserHand = BrowserHandValue(hand);
	if (browserHand == 0)
		return false;

#ifdef __EMSCRIPTEN__
	return QueueBrowserWebXRHapticPulse(browserHand, intensity, durationMs) != 0;
#else
	(void)intensity;
	(void)durationMs;
	return false;
#endif
}

bool SetWebXRHapticsEnabled(bool enabled)
{
#ifdef __EMSCRIPTEN__
	return SetBrowserWebXRHapticsEnabled(enabled ? 1 : 0) != 0;
#else
	(void)enabled;
	return false;
#endif
}

bool RunWebXRHapticsBridgeSelfTest()
{
	return static_cast<uint32_t>(WebXRHapticHand::Left) == 1 &&
		static_cast<uint32_t>(WebXRHapticHand::Right) == 2 &&
		BrowserHandValue(WebXRHapticHand::Left) == 1 &&
		BrowserHandValue(WebXRHapticHand::Right) == 2 &&
		BrowserHandValue(static_cast<WebXRHapticHand>(0)) == 0 &&
		BrowserHandValue(static_cast<WebXRHapticHand>(3)) == 0;
}
