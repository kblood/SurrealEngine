#include "Precomp.h"
#include "WebXRHaptics.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

WebXRHapticPulse GetWebXRHapticPulse(WebXRHapticEvent event, float magnitude)
{
	switch (event)
	{
	case WebXRHapticEvent::Fire:
		return { 0.55f, 35 };
	case WebXRHapticEvent::Damage:
	{
		// Damage is the only magnitude-scaled event. Treat non-finite/negative
		// input as the weakest confirmed hit and saturate at 100 health.
		const float healthLoss = std::isfinite(magnitude) ? std::max(magnitude, 0.0f) : 0.0f;
		const float normalized = std::min(healthLoss / 100.0f, 1.0f);
		return { 0.35f + normalized * 0.45f,
			static_cast<uint32_t>(std::lround(45.0f + normalized * 65.0f)) };
	}
	case WebXRHapticEvent::Pickup:
		return { 0.32f, 45 };
	case WebXRHapticEvent::UIConfirm:
		return { 0.25f, 30 };
	default:
		return {};
	}
}

bool QueueWebXRHapticEvent(WebXRHapticEvent event, WebXRHapticHand hand, float magnitude)
{
	const WebXRHapticPulse pulse = GetWebXRHapticPulse(event, magnitude);
	return pulse.Intensity > 0.0f && pulse.Intensity <= 1.0f &&
		pulse.DurationMs > 0 && QueueWebXRHapticPulse(hand, pulse.Intensity, pulse.DurationMs);
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
	const WebXRHapticPulse fire = GetWebXRHapticPulse(WebXRHapticEvent::Fire);
	const WebXRHapticPulse damageMinimum = GetWebXRHapticPulse(WebXRHapticEvent::Damage, -25.0f);
	const WebXRHapticPulse damageMedium = GetWebXRHapticPulse(WebXRHapticEvent::Damage, 50.0f);
	const WebXRHapticPulse damageMaximum = GetWebXRHapticPulse(WebXRHapticEvent::Damage, 500.0f);
	const WebXRHapticPulse damageNaN = GetWebXRHapticPulse(WebXRHapticEvent::Damage,
		std::numeric_limits<float>::quiet_NaN());
	const WebXRHapticPulse pickup = GetWebXRHapticPulse(WebXRHapticEvent::Pickup);
	const WebXRHapticPulse ui = GetWebXRHapticPulse(WebXRHapticEvent::UIConfirm);
	const WebXRHapticPulse invalid = GetWebXRHapticPulse(
		static_cast<WebXRHapticEvent>(static_cast<uint32_t>(WebXRHapticEvent::Count)));
	auto bounded = [](const WebXRHapticPulse& pulse)
	{
		return std::isfinite(pulse.Intensity) && pulse.Intensity > 0.0f &&
			pulse.Intensity <= 1.0f && pulse.DurationMs > 0 && pulse.DurationMs <= 250;
	};

	return static_cast<uint32_t>(WebXRHapticHand::Left) == 1 &&
		static_cast<uint32_t>(WebXRHapticHand::Right) == 2 &&
		static_cast<uint32_t>(WebXRHapticEvent::Fire) == 0 &&
		static_cast<uint32_t>(WebXRHapticEvent::Damage) == 1 &&
		static_cast<uint32_t>(WebXRHapticEvent::Pickup) == 2 &&
		static_cast<uint32_t>(WebXRHapticEvent::UIConfirm) == 3 &&
		static_cast<uint32_t>(WebXRHapticEvent::Count) == 4 &&
		BrowserHandValue(WebXRHapticHand::Left) == 1 &&
		BrowserHandValue(WebXRHapticHand::Right) == 2 &&
		BrowserHandValue(static_cast<WebXRHapticHand>(0)) == 0 &&
		BrowserHandValue(static_cast<WebXRHapticHand>(3)) == 0 &&
		bounded(fire) && bounded(damageMinimum) && bounded(damageMedium) &&
		bounded(damageMaximum) && bounded(damageNaN) && bounded(pickup) && bounded(ui) &&
		damageMinimum.Intensity == damageNaN.Intensity &&
		damageMinimum.DurationMs == damageNaN.DurationMs &&
		damageMinimum.Intensity < damageMedium.Intensity &&
		damageMedium.Intensity < damageMaximum.Intensity &&
		damageMinimum.DurationMs < damageMedium.DurationMs &&
		damageMedium.DurationMs < damageMaximum.DurationMs &&
		invalid.Intensity == 0.0f && invalid.DurationMs == 0;
}
