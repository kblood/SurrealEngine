#include "Platform/WebXR/WebXRHaptics.h"

#include <algorithm>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(int, JS_SubmitWebXRHaptic, (int hand, float amplitude, uint32_t durationMilliseconds, float frequencyHz), {
	if (typeof globalThis.surrealXRSubmitHaptic !== "function") return 0;
	return globalThis.surrealXRSubmitHaptic(hand, amplitude, durationMilliseconds, frequencyHz) ? 1 : 0;
});
#endif

namespace
{
	bool SubmitBrowserHaptic(XRHand hand, float amplitude,
		uint32_t durationMilliseconds, float frequencyHz)
	{
#ifdef __EMSCRIPTEN__
		return JS_SubmitWebXRHaptic(hand == XRHand::Left ? 0 : 1,
			amplitude, durationMilliseconds, frequencyHz) != 0;
#else
		(void)hand;
		(void)amplitude;
		(void)durationMilliseconds;
		(void)frequencyHz;
		return false;
#endif
	}
}

bool WebXR::HapticSink::SubmitHaptic(const XRHapticRequest& request)
{
	if (!IsValidXRHapticRequest(request))
		return false;

	const float boundedMilliseconds = std::clamp(request.DurationSeconds * 1000.0f,
		static_cast<float>(MinimumDurationMilliseconds),
		static_cast<float>(MaximumDurationMilliseconds));
	const uint32_t durationMilliseconds = static_cast<uint32_t>(std::lround(boundedMilliseconds));
	return (transport ? transport : SubmitBrowserHaptic)(request.Hand, request.Amplitude,
		durationMilliseconds, request.FrequencyHz);
}

WebXR::HapticSink& WebXR::BrowserHapticSink()
{
	static HapticSink sink;
	return sink;
}
