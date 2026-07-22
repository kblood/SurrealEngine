#include "Platform/WebXR/WebXRFrameBridge.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
	bool NearlyEqual(float a, float b, float epsilon = 0.001f)
	{
		return std::abs(a - b) <= epsilon;
	}

	std::vector<uint8_t> MakeFrame()
	{
		WebXR::PackedFrameHeader header = {};
		header.Version = WebXR::FrameABIVersion;
		header.ViewCount = 2;
		header.TextureCount = 2;
		header.ByteSize = sizeof(header) + header.ViewCount * sizeof(WebXR::PackedView);
		header.Timestamp = 12.5;
		header.ResetGeneration = 4;

		WebXR::PackedView views[2] = {};
		for (uint32_t index = 0; index < 2; index++)
		{
			views[index].Eye = index + 1;
			views[index].TextureIndex = index;
			views[index].TextureWidth = 1024;
			views[index].TextureHeight = 1024;
			views[index].ViewportWidth = 1024;
			views[index].ViewportHeight = 1024;
			views[index].Position[0] = index == 0 ? -0.032f : 0.032f;
			views[index].Orientation[3] = 1.0f;
			views[index].Projection[0] = 1.0f;
			views[index].Projection[5] = 1.0f;
			views[index].Projection[10] = 1.0f;
			views[index].Projection[11] = -1.0f;
			views[index].Projection[15] = 1.0f;
		}

		std::vector<uint8_t> bytes(header.ByteSize);
		std::memcpy(bytes.data(), &header, sizeof(header));
		std::memcpy(bytes.data() + sizeof(header), views, sizeof(views));
		return bytes;
	}
}

int main()
{
	std::vector<uint8_t> bytes = MakeFrame();
	WebXR::DecodedFrame decoded;
	WebXR::FrameError error;
	if (!WebXR::DecodeFrame(bytes.data(), static_cast<uint32_t>(bytes.size()), decoded, error))
		return 1;
	if (decoded.Header.ViewCount != 2 || decoded.Header.TextureCount != 2 ||
		decoded.Views[1].TextureIndex != 1)
		return 2;

	WebXR::RecenterState recenter;
	const vec3 anchor(10.0f, 20.0f, 30.0f);
	const float unitsPerMeter = 1.0f / 0.0254f;
	ViewFamily family = WebXR::BuildViewFamily(decoded, anchor, Coords::Identity(), unitsPerMeter, recenter);
	if (family.Views.size() != 2 || recenter.RecenterCount != 1)
		return 3;
	const float halfIPD = 0.032f * unitsPerMeter;
	if (!NearlyEqual(family.Views[0].Location.y, anchor.y - halfIPD) ||
		!NearlyEqual(family.Views[1].Location.y, anchor.y + halfIPD) ||
		!NearlyEqual(family.Views[0].Projection[11], 1.0f))
		return 4;
	if (family.Presentation.GetLayer(PresentationLayer::World).Target.Slot != 1 ||
		family.Presentation.GetLayer(PresentationLayer::UserInterface).Enabled)
		return 5;

	ViewFamily sameGeneration = WebXR::BuildViewFamily(decoded, anchor, Coords::Identity(), unitsPerMeter, recenter);
	(void)sameGeneration;
	if (recenter.RecenterCount != 1)
		return 6;
	decoded.Header.ResetGeneration++;
	WebXR::BuildViewFamily(decoded, anchor, Coords::Identity(), unitsPerMeter, recenter);
	if (recenter.RecenterCount != 2)
		return 7;

	std::vector<uint8_t> invalid = bytes;
	reinterpret_cast<WebXR::PackedFrameHeader*>(invalid.data())->ViewCount = 3;
	if (WebXR::DecodeFrame(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) ||
		error != WebXR::FrameError::InvalidHeader)
		return 8;

	invalid = bytes;
	auto* duplicateEyes = reinterpret_cast<WebXR::PackedView*>(
		invalid.data() + sizeof(WebXR::PackedFrameHeader));
	duplicateEyes[1].Eye = static_cast<uint32_t>(WebXR::Eye::Left);
	if (WebXR::DecodeFrame(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) ||
		error != WebXR::FrameError::InvalidView)
		return 9;

	invalid = bytes;
	auto* invalidTexture = reinterpret_cast<WebXR::PackedView*>(
		invalid.data() + sizeof(WebXR::PackedFrameHeader));
	invalidTexture[1].TextureIndex = 2;
	if (WebXR::DecodeFrame(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) ||
		error != WebXR::FrameError::InvalidView)
		return 10;

	std::cout << "WebXR packed frame and view-family tests passed\n";
	return 0;
}
