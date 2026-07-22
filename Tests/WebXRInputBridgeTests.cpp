#include "Platform/WebXR/WebXRInputBridge.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	std::vector<uint8_t> MakePacket(bool focused = true)
	{
		WebXR::PackedInputHeader header = {};
		header.Version = WebXR::InputABIVersion;
		header.SourceCount = 2;
		header.ByteSize = sizeof(header) + header.SourceCount * sizeof(WebXR::PackedInputSource);
		header.Flags = WebXR::InputSessionActive | (focused ? WebXR::InputActionFocused : 0);
		header.Timestamp = 42.5;

		WebXR::PackedInputSource sources[2] = {};
		for (uint32_t index = 0; index < 2; index++)
		{
			sources[index].Hand = index + 1;
			sources[index].Flags = WebXR::InputSourceConnected | WebXR::InputAimPoseValid |
				WebXR::InputGripPoseValid;
			sources[index].AimOrientation[3] = 1.0f;
			sources[index].GripOrientation[3] = 1.0f;
			sources[index].AimPosition[0] = index == 0 ? -0.2f : 0.2f;
			sources[index].GripPosition[1] = 1.1f;
		}
		sources[0].PressedButtons = (1u << WebXR::InputTrigger) | (1u << WebXR::InputPrimary);
		sources[0].TouchedButtons = 1u << WebXR::InputStickClick;
		sources[0].ButtonValues[WebXR::InputTrigger] = 0.75f;
		sources[0].ButtonValues[WebXR::InputPrimary] = 1.0f;
		sources[0].Axes[2] = -0.5f;
		sources[0].Axes[3] = 0.25f;

		std::vector<uint8_t> bytes(header.ByteSize);
		std::memcpy(bytes.data(), &header, sizeof(header));
		std::memcpy(bytes.data() + sizeof(header), sources, sizeof(sources));
		return bytes;
	}

	bool Expect(bool condition, const char* message)
	{
		if (!condition) std::cerr << message << '\n';
		return condition;
	}
}

int main()
{
	auto bytes = MakePacket();
	WebXR::DecodedInputSnapshot decoded;
	WebXR::InputError error;
	if (!Expect(WebXR::DecodeInputSnapshot(bytes.data(), static_cast<uint32_t>(bytes.size()), decoded, error),
		"valid two-controller packet rejected")) return 1;
	if (!Expect(error == WebXR::InputError::None && decoded.SessionActive && decoded.ActionFocused && decoded.Timestamp == 42.5,
		"header state decoded incorrectly")) return 1;
	const auto& left = decoded.Sources[0];
	const auto& right = decoded.Sources[1];
	if (!Expect(left.Connected && right.Connected && left.AimPoseValid && left.GripPoseValid,
		"independent controller connection/pose state missing")) return 1;
	if (!Expect(left.ButtonValues[WebXR::InputTrigger] == 0.75f && left.Axes[2] == -0.5f &&
		(left.PressedButtons & (1u << WebXR::InputPrimary)) != 0 && right.PressedButtons == 0,
		"semantic button or axis state decoded incorrectly")) return 1;

	if (!Expect(Surreal_SubmitWebXRInputSnapshot(bytes.data(), static_cast<uint32_t>(bytes.size())) == 1,
		"valid snapshot submission failed")) return 1;
	const auto stored = WebXR::GetInputSnapshot();
	if (!Expect(stored.Sources[0].Connected && stored.Sources[1].Connected,
		"submitted snapshot was not stored")) return 1;

	WebXR::PackedInputHeader neutral = {};
	neutral.Version = WebXR::InputABIVersion;
	neutral.ByteSize = sizeof(neutral);
	neutral.Timestamp = 43.0;
	if (!Expect(Surreal_SubmitWebXRInputSnapshot(&neutral, sizeof(neutral)) == 1,
		"neutral replacement snapshot rejected")) return 1;
	const auto cleared = WebXR::GetInputSnapshot();
	if (!Expect(!cleared.SessionActive && !cleared.ActionFocused && !cleared.Sources[0].Connected && !cleared.Sources[1].Connected,
		"neutral snapshot did not clear both hands")) return 1;

	auto invalid = MakePacket();
	reinterpret_cast<WebXR::PackedInputHeader*>(invalid.data())->Version++;
	if (!Expect(!WebXR::DecodeInputSnapshot(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) &&
		error == WebXR::InputError::InvalidHeader, "invalid version was accepted")) return 1;

	invalid = MakePacket();
	auto* sources = reinterpret_cast<WebXR::PackedInputSource*>(invalid.data() + sizeof(WebXR::PackedInputHeader));
	sources[1].Hand = sources[0].Hand;
	if (!Expect(!WebXR::DecodeInputSnapshot(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) &&
		error == WebXR::InputError::DuplicateHand, "duplicate handedness was accepted")) return 1;

	invalid = MakePacket();
	sources = reinterpret_cast<WebXR::PackedInputSource*>(invalid.data() + sizeof(WebXR::PackedInputHeader));
	sources[0].Axes[0] = std::numeric_limits<float>::quiet_NaN();
	if (!Expect(!WebXR::DecodeInputSnapshot(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) &&
		error == WebXR::InputError::InvalidSource, "non-finite axis was accepted")) return 1;

	invalid = MakePacket();
	sources = reinterpret_cast<WebXR::PackedInputSource*>(invalid.data() + sizeof(WebXR::PackedInputHeader));
	sources[0].Axes[0] = 1.01f;
	if (!Expect(!WebXR::DecodeInputSnapshot(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) &&
		error == WebXR::InputError::InvalidSource, "out-of-range axis was accepted")) return 1;

	invalid = MakePacket();
	sources = reinterpret_cast<WebXR::PackedInputSource*>(invalid.data() + sizeof(WebXR::PackedInputHeader));
	sources[0].Flags &= ~WebXR::InputSourceConnected;
	if (!Expect(!WebXR::DecodeInputSnapshot(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) &&
		error == WebXR::InputError::InvalidSource, "included disconnected source was accepted")) return 1;

	std::cout << "WebXR packed controller snapshot tests passed\n";
	return 0;
}
