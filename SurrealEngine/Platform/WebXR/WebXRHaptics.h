#pragma once

#include "XR/XRCommon.h"

#include <cstdint>

namespace WebXR
{
	using HapticTransport = bool (*)(XRHand hand, float amplitude,
		uint32_t durationMilliseconds, float frequencyHz);

	class HapticSink final : public IXRHapticSink
	{
	public:
		static constexpr uint32_t MinimumDurationMilliseconds = 1;
		static constexpr uint32_t MaximumDurationMilliseconds = 1000;

		explicit HapticSink(HapticTransport transport = nullptr) : transport(transport) {}
		bool SubmitHaptic(const XRHapticRequest& request) override;

	private:
		HapticTransport transport = nullptr;
	};

	HapticSink& BrowserHapticSink();
}
