#include "Platform/WebXR/WebXRHaptics.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	struct RecordedPulse
	{
		XRHand Hand = XRHand::Right;
		float Amplitude = 0.0f;
		uint32_t DurationMilliseconds = 0;
		float FrequencyHz = 0.0f;
	};

	RecordedPulse LastPulse;
	int Calls = 0;
	bool TransportResult = true;

	bool RecordPulse(XRHand hand, float amplitude, uint32_t durationMilliseconds, float frequencyHz)
	{
		Calls++;
		LastPulse = { hand, amplitude, durationMilliseconds, frequencyHz };
		return TransportResult;
	}

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool NearlyEqual(float left, float right)
	{
		return std::abs(left - right) < 0.0001f;
	}
}

int main()
{
	WebXR::HapticSink sink(RecordPulse);
	if (!RouteXRHaptic(&sink, { XRHand::Left, 0.6f, 0.125f, 80.0f }) || Calls != 1 ||
		LastPulse.Hand != XRHand::Left || !NearlyEqual(LastPulse.Amplitude, 0.6f) ||
		LastPulse.DurationMilliseconds != 125 || !NearlyEqual(LastPulse.FrequencyHz, 80.0f))
		return Fail("XRCommon did not route the semantic haptic request to the WebXR transport");

	if (!RouteXRHaptic(&sink, { XRHand::Right, 0.4f, 0.0001f, 0.0f }) ||
		LastPulse.DurationMilliseconds != WebXR::HapticSink::MinimumDurationMilliseconds)
		return Fail("short WebXR haptic duration was not bounded");
	if (!RouteXRHaptic(&sink, { XRHand::Right, 0.4f, 12.0f, 0.0f }) ||
		LastPulse.DurationMilliseconds != WebXR::HapticSink::MaximumDurationMilliseconds)
		return Fail("long WebXR haptic duration was not bounded");

	const int callsBeforeInvalid = Calls;
	if (RouteXRHaptic(&sink, { XRHand::Left, 0.0f, 0.1f, 0.0f }) ||
		RouteXRHaptic(&sink, { XRHand::Left, 0.5f, 0.0f, 0.0f }) ||
		RouteXRHaptic(&sink, { XRHand::Left, 0.5f, 0.1f,
			std::numeric_limits<float>::quiet_NaN() }) || Calls != callsBeforeInvalid)
		return Fail("invalid XRCommon haptic requests reached the WebXR transport");

	TransportResult = false;
	if (RouteXRHaptic(&sink, { XRHand::Right, 0.2f, 0.02f, 0.0f }) || Calls != callsBeforeInvalid + 1)
		return Fail("WebXR transport rejection was not returned through XRCommon");

	return 0;
}
