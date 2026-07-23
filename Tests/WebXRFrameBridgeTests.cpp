#include "Platform/WebXR/WebXRFrameBridge.h"
#include "RenderDevice/ClipSpaceConversion.h"

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

	std::array<float, 16> MakeWebGLProjection(float left, float right,
		float down, float up, float nearDistance, float farDistance)
	{
		std::array<float, 16> projection = {};
		projection[0] = 2.0f / (right - left);
		projection[5] = 2.0f / (up - down);
		projection[8] = (right + left) / (right - left);
		projection[9] = (up + down) / (up - down);
		projection[10] = -(farDistance + nearDistance) / (farDistance - nearDistance);
		projection[11] = -1.0f;
		projection[14] = -(2.0f * farDistance * nearDistance) /
			(farDistance - nearDistance);
		return projection;
	}

	vec3 ProjectToNDC(const ViewDescription& view, const vec3& worldPosition)
	{
		const vec4 clip = view.Projection * view.WorldToView * vec4(worldPosition, 1.0f);
		return vec3(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
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
	if (WebXR::FrameABIVersion != 4 || Surreal_GetWebXRFrameABIVersion() != 4)
		return 15;
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
		family.Presentation.GetLayer(PresentationLayer::WeaponOverlay).Target.Slot != 1 ||
		!family.Presentation.GetLayer(PresentationLayer::WeaponOverlay).Enabled ||
		family.Presentation.GetLayer(PresentationLayer::UserInterface).Enabled)
		return 5;
	const XRWorldTransform weaponWorld = WebXR::BuildWeaponWorldTransform(
		anchor, 0.0f, unitsPerMeter, recenter);
	if (!weaponWorld.Recenter.Valid ||
		!NearlyEqual(weaponWorld.EngineOrigin.X, anchor.x) ||
		!NearlyEqual(weaponWorld.EngineOrigin.Y, anchor.y) ||
		!NearlyEqual(weaponWorld.EngineOrigin.Z,
			anchor.z - recenter.OriginMeters.z * unitsPerMeter) ||
		!NearlyEqual(weaponWorld.Recenter.HorizontalOrigin.X,
			recenter.OriginMeters.y) ||
		!NearlyEqual(weaponWorld.Recenter.HorizontalOrigin.Z,
			-recenter.OriginMeters.x) ||
		!NearlyEqual(weaponWorld.Recenter.ReferenceYawRadians,
			-recenter.YawOffset))
		return 16;

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

	invalid = bytes;
	(reinterpret_cast<WebXR::PackedFrameHeader*>(invalid.data()))->Flags = 0x80000000u;
	if (WebXR::DecodeFrame(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error) ||
		error != WebXR::FrameError::InvalidHeader)
		return 11;

	std::array<float, 16> webGLProjection = {
		2, 0, 0, 0, 0, 3, 0, 0, 0.2f, -0.3f, -1, -1, 0, 0, -0.2f, 0
	};
	const auto webGPUProjection = ConvertProjectionDepthMinusOneToOneToZeroToOne(webGLProjection);
	if (!NearlyEqual(webGPUProjection[10], -1.0f) || !NearlyEqual(webGPUProjection[11], -1.0f) ||
		!NearlyEqual(webGPUProjection[14], -0.1f) || !NearlyEqual(webGPUProjection[8], 0.2f))
		return 12;

	invalid = bytes;
	auto* atlasHeader = reinterpret_cast<WebXR::PackedFrameHeader*>(invalid.data());
	auto* atlasViews = reinterpret_cast<WebXR::PackedView*>(invalid.data() + sizeof(*atlasHeader));
	atlasHeader->Flags = WebXR::FrameProjectionDepthZeroToOne | WebXR::FrameSharedStereoAtlas;
	atlasHeader->TextureCount = 1;
	atlasViews[0].TextureIndex = atlasViews[1].TextureIndex = 0;
	atlasViews[0].TextureWidth = atlasViews[1].TextureWidth = 2048;
	atlasViews[0].ViewportWidth = atlasViews[1].ViewportWidth = 1024;
	atlasViews[1].ViewportX = 1024;
	atlasViews[0].Projection[8] = -0.25f;
	atlasViews[1].Projection[8] = 0.25f;
	if (!WebXR::DecodeFrame(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error))
		return 13;
	WebXR::RecenterState atlasRecenter;
	const ViewFamily atlasFamily = WebXR::BuildViewFamily(decoded, anchor,
		Coords::Identity(), unitsPerMeter, atlasRecenter);
	if (atlasFamily.Views.size() != 2 || atlasFamily.Views[0].Viewport.X != 0 ||
		atlasFamily.Views[0].Viewport.Width != 1024 ||
		atlasFamily.Views[1].Viewport.X != 1024 ||
		atlasFamily.Views[1].Viewport.Width != 1024 ||
		!NearlyEqual(atlasFamily.Views[0].Projection[8], 0.25f) ||
		!NearlyEqual(atlasFamily.Views[1].Projection[8], -0.25f))
		return 14;

	const std::array<float, 16> leftWebGLProjection = MakeWebGLProjection(
		-1.17f, 0.97f, -1.08f, 1.12f, 0.1f, 1000.0f);
	const std::array<float, 16> rightWebGLProjection = MakeWebGLProjection(
		-0.97f, 1.17f, -1.07f, 1.13f, 0.1f, 1000.0f);
	const std::array<float, 16> leftWebGPUProjection =
		ConvertProjectionDepthMinusOneToOneToZeroToOne(leftWebGLProjection);
	const std::array<float, 16> rightWebGPUProjection =
		ConvertProjectionDepthMinusOneToOneToZeroToOne(rightWebGLProjection);
	atlasHeader->ResetGeneration++;
	const float rotatedOrientation[4] = {
		-0.08871944f, 0.22108249f, 0.08185684f, 0.96775557f
	};
	WebXR::RecenterState canonicalRecenter;
	canonicalRecenter.Valid = true;
	const WebXR::EngineTrackedPose canonicalPose = WebXR::TransformCanonicalPose(
		vec3(0.25f, 1.6f, -0.4f),
		vec4(rotatedOrientation[0], rotatedOrientation[1],
			rotatedOrientation[2], rotatedOrientation[3]),
		vec3(0.0f), Coords::Identity(), unitsPerMeter, canonicalRecenter);
	if (!NearlyEqual(canonicalPose.Position.x, 0.4f * unitsPerMeter) ||
		!NearlyEqual(canonicalPose.Position.y, 0.25f * unitsPerMeter) ||
		!NearlyEqual(canonicalPose.Position.z, 1.6f * unitsPerMeter) ||
		!NearlyEqual(canonicalPose.Forward.x, 0.8865028f) ||
		!NearlyEqual(canonicalPose.Forward.y, -0.4133830f) ||
		!NearlyEqual(canonicalPose.Forward.z, -0.2079117f) ||
		!NearlyEqual(canonicalPose.Right.x, 0.4424322f) ||
		!NearlyEqual(canonicalPose.Right.y, 0.8888440f) ||
		!NearlyEqual(canonicalPose.Right.z, 0.1192062f) ||
		!NearlyEqual(canonicalPose.Up.x, 0.1355232f) ||
		!NearlyEqual(canonicalPose.Up.y, -0.1976635f) ||
		!NearlyEqual(canonicalPose.Up.z, 0.9708566f))
		return 22;
	for (uint32_t index = 0; index < 2; index++)
	{
		std::memcpy(atlasViews[index].Orientation, rotatedOrientation,
			sizeof(rotatedOrientation));
		const std::array<float, 16>& projection = index == 0 ?
			leftWebGPUProjection : rightWebGPUProjection;
		std::memcpy(atlasViews[index].Projection, projection.data(),
			sizeof(atlasViews[index].Projection));
	}
	if (!WebXR::DecodeFrame(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error))
		return 17;
	WebXR::RecenterState metricRecenter;
	const ViewFamily metricFamily = WebXR::BuildViewFamily(decoded, anchor,
		Coords::YawRotation(0.31f), unitsPerMeter, metricRecenter);
	if (metricFamily.Views.size() != 2)
		return 18;
	const std::array<std::array<float, 16>, 2> sourceProjections = {
		leftWebGPUProjection, rightWebGPUProjection
	};
	const auto projectionMatchesMetricSpace = [&](const ViewFamily& viewFamily)
	{
		if (viewFamily.Views.size() != 2)
			return false;
		for (size_t index = 0; index < viewFamily.Views.size(); index++)
		{
			const ViewDescription& view = viewFamily.Views[index];
			const std::array<float, 16>& projection = sourceProjections[index];
			const float nearEngineUnits = -view.Projection[14] / view.Projection[10];
			const float farEngineUnits = view.Projection[14] /
				(view.Projection[11] - view.Projection[10]);
			if (!NearlyEqual(nearEngineUnits, 0.1f * unitsPerMeter, 0.001f) ||
				!NearlyEqual(farEngineUnits, 1000.0f * unitsPerMeter,
					1000.0f * unitsPerMeter * 0.002f))
				return false;
			for (const float distanceMeters : { 0.1f, 500.0f, 1000.0f })
			{
				const vec3 worldPosition = view.Location +
					view.Rotation.XAxis * (distanceMeters * unitsPerMeter);
				const vec3 ndc = ProjectToNDC(view, worldPosition);
				const float expectedDepth =
					(projection[10] * -distanceMeters + projection[14]) /
					(projection[11] * -distanceMeters);
				if (!NearlyEqual(ndc.x, -projection[8], 0.0001f) ||
					!NearlyEqual(ndc.y, -projection[9], 0.0001f) ||
					!NearlyEqual(ndc.z, expectedDepth, 0.0001f))
					return false;
			}
		}
		return true;
	};
	if (!projectionMatchesMetricSpace(metricFamily))
		return 19;

	atlasHeader->Flags = 0;
	atlasHeader->TextureCount = 2;
	for (uint32_t index = 0; index < 2; index++)
	{
		atlasViews[index].TextureIndex = index;
		atlasViews[index].TextureWidth = 1024;
		atlasViews[index].TextureHeight = 1024;
		atlasViews[index].ViewportX = 0;
		atlasViews[index].ViewportWidth = 1024;
		const std::array<float, 16>& projection = index == 0 ?
			leftWebGLProjection : rightWebGLProjection;
		std::memcpy(atlasViews[index].Projection, projection.data(),
			sizeof(atlasViews[index].Projection));
	}
	if (!WebXR::DecodeFrame(invalid.data(), static_cast<uint32_t>(invalid.size()), decoded, error))
		return 20;
	WebXR::RecenterState directRecenter;
	const ViewFamily directFamily = WebXR::BuildViewFamily(decoded, anchor,
		Coords::YawRotation(0.31f), unitsPerMeter, directRecenter);
	if (!projectionMatchesMetricSpace(directFamily))
		return 21;

	std::cout << "WebXR packed frame and view-family tests passed\n";
	return 0;
}
