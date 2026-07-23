#include "Platform/WebXR/WebXRFrameBridge.h"

#include "Math/quaternion.h"

#include <cmath>
#include <cstring>

namespace
{
	WebXR::FrameError LastFrameError = WebXR::FrameError::None;

	bool IsFiniteArray(const float* values, size_t count)
	{
		for (size_t index = 0; index < count; index++)
		{
			if (!std::isfinite(values[index]))
				return false;
		}
		return true;
	}

	vec3 WebXRVectorToUE1(const vec3& value)
	{
		// WebXR: right-handed, +X right, +Y up, -Z forward.
		// UE1: left-handed, +X forward, +Y right, +Z up.
		return vec3(-value.z, value.x, value.y);
	}

	vec3 Rotate(const Coords& rotation, const vec3& value)
	{
		return rotation.XAxis * value.x + rotation.YAxis * value.y + rotation.ZAxis * value.z;
	}

	struct PoseAxes
	{
		vec3 Forward;
		vec3 Right;
		vec3 Up;
		vec3 PositionMeters;
	};

	PoseAxes DecodePose(const WebXR::PackedView& view)
	{
		quaternion orientation(view.Orientation[0], view.Orientation[1],
			view.Orientation[2], view.Orientation[3]);
		orientation = normalize(orientation);
		return {
			WebXRVectorToUE1(orientation * vec3(0.0f, 0.0f, -1.0f)),
			WebXRVectorToUE1(orientation * vec3(1.0f, 0.0f, 0.0f)),
			WebXRVectorToUE1(orientation * vec3(0.0f, 1.0f, 0.0f)),
			WebXRVectorToUE1(vec3(view.Position[0], view.Position[1], view.Position[2]))
		};
	}

	mat4 DecodeProjection(const WebXR::PackedView& view)
	{
		float values[16];
		std::memcpy(values, view.Projection, sizeof(values));
		mat4 projection = mat4::from_values(values);
		// WebXR looks down -Z. SurrealEngine's render path looks down +Z.
		if (view.Projection[11] < 0.0f)
			projection = projection * mat4::scale(1.0f, 1.0f, -1.0f);
		return projection;
	}
}

WebXR::EngineTrackedPose WebXR::TransformCanonicalPose(const vec3& positionMeters,
	const vec4& orientationValue, const vec3& cameraLocation, const Coords& bodyRotation,
	float worldUnitsPerMeter, const RecenterState& recenter)
{
	quaternion orientation(orientationValue.x, orientationValue.y,
		orientationValue.z, orientationValue.w);
	orientation = normalize(orientation);
	const Coords recenterRotation = Coords::YawRotation(recenter.YawOffset);
	EngineTrackedPose result;
	result.Position = cameraLocation + Rotate(bodyRotation, Rotate(recenterRotation,
		(WebXRVectorToUE1(positionMeters) - recenter.OriginMeters) * worldUnitsPerMeter));
	result.Forward = Rotate(bodyRotation, Rotate(recenterRotation,
		WebXRVectorToUE1(orientation * vec3(0.0f, 0.0f, -1.0f))));
	result.Right = Rotate(bodyRotation, Rotate(recenterRotation,
		WebXRVectorToUE1(orientation * vec3(1.0f, 0.0f, 0.0f))));
	result.Up = Rotate(bodyRotation, Rotate(recenterRotation,
		WebXRVectorToUE1(orientation * vec3(0.0f, 1.0f, 0.0f))));
	return result;
}

bool WebXR::DecodeFrame(const void* frameData, uint32_t bufferBytes, DecodedFrame& result, FrameError& error)
{
	result = {};
	if (!frameData || bufferBytes < sizeof(PackedFrameHeader))
	{
		error = FrameError::InvalidHeader;
		return false;
	}

	std::memcpy(&result.Header, frameData, sizeof(PackedFrameHeader));
	const PackedFrameHeader& header = result.Header;
	if (header.Version != FrameABIVersion || (header.Flags & ~FrameKnownFlags) != 0 ||
		header.ViewCount != MaxViews || header.TextureCount == 0 ||
		header.TextureCount > header.ViewCount ||
		header.ByteSize != bufferBytes ||
		header.ByteSize != sizeof(PackedFrameHeader) + header.ViewCount * sizeof(PackedView) ||
		!std::isfinite(header.Timestamp))
	{
		error = FrameError::InvalidHeader;
		return false;
	}

	bool texturesUsed[MaxViews] = {};
	bool eyesUsed[static_cast<uint32_t>(Eye::Right) + 1] = {};
	const uint8_t* bytes = static_cast<const uint8_t*>(frameData) + sizeof(PackedFrameHeader);
	for (uint32_t index = 0; index < header.ViewCount; index++)
	{
		PackedView& view = result.Views[index];
		std::memcpy(&view, bytes + index * sizeof(PackedView), sizeof(PackedView));
		const float lengthSquared = view.Orientation[0] * view.Orientation[0] +
			view.Orientation[1] * view.Orientation[1] + view.Orientation[2] * view.Orientation[2] +
			view.Orientation[3] * view.Orientation[3];
		if (view.Eye < static_cast<uint32_t>(Eye::Left) ||
			view.Eye > static_cast<uint32_t>(Eye::Right) || eyesUsed[view.Eye] ||
			view.TextureIndex >= header.TextureCount || view.TextureWidth == 0 || view.TextureHeight == 0 ||
			view.ViewportX < 0 || view.ViewportY < 0 ||
			view.ViewportWidth <= 0 || view.ViewportHeight <= 0 ||
			static_cast<uint32_t>(view.ViewportWidth) > view.TextureWidth ||
			static_cast<uint32_t>(view.ViewportHeight) > view.TextureHeight ||
			static_cast<uint32_t>(view.ViewportX) > view.TextureWidth - static_cast<uint32_t>(view.ViewportWidth) ||
			static_cast<uint32_t>(view.ViewportY) > view.TextureHeight - static_cast<uint32_t>(view.ViewportHeight) ||
			!IsFiniteArray(view.Position, 3) || !IsFiniteArray(view.Orientation, 4) ||
			!IsFiniteArray(view.Projection, 16) || !std::isfinite(lengthSquared) || lengthSquared < 0.000001f)
		{
			error = FrameError::InvalidView;
			return false;
		}
		eyesUsed[view.Eye] = true;
		texturesUsed[view.TextureIndex] = true;
	}
	if ((header.Flags & FrameSharedStereoAtlas) != 0 &&
		(header.TextureCount != 1 || result.Views[0].TextureIndex != 0 ||
		 result.Views[1].TextureIndex != 0 || result.Views[0].ArrayLayer != 0 ||
		 result.Views[1].ArrayLayer != 0 ||
		 result.Views[0].TextureWidth != result.Views[1].TextureWidth ||
		 result.Views[0].TextureHeight != result.Views[1].TextureHeight))
	{
		error = FrameError::InvalidView;
		return false;
	}
	for (uint32_t index = 0; index < header.TextureCount; index++)
	{
		if (!texturesUsed[index])
		{
			error = FrameError::InvalidView;
			return false;
		}
	}

	error = FrameError::None;
	return true;
}

void WebXR::SetLastFrameError(FrameError error)
{
	LastFrameError = error;
}

WebXR::FrameError WebXR::GetLastFrameError()
{
	return LastFrameError;
}

XRWorldTransform WebXR::BuildWeaponWorldTransform(const vec3& cameraLocation,
	float engineYawRadians, float worldUnitsPerMeter,
	const RecenterState& recenter)
{
	XRWorldTransform result;
	result.EngineOrigin = { cameraLocation.x, cameraLocation.y,
		cameraLocation.z - recenter.OriginMeters.z * worldUnitsPerMeter };
	result.UnitsPerMeter = worldUnitsPerMeter;
	result.EngineYawRadians = engineYawRadians;
	result.Recenter.Valid = recenter.Valid;
	result.Recenter.HorizontalOrigin = {
		recenter.OriginMeters.y, 0.0f, -recenter.OriginMeters.x };
	result.Recenter.ReferenceYawRadians = -recenter.YawOffset;
	return result;
}

ViewFamily WebXR::BuildViewFamily(const DecodedFrame& frame, const vec3& cameraLocation,
	const Coords& bodyRotation, float worldUnitsPerMeter, RecenterState& recenter)
{
	std::array<PoseAxes, MaxViews> poses = {};
	vec3 center(0.0f);
	vec3 averageForward(0.0f);
	for (uint32_t index = 0; index < frame.Header.ViewCount; index++)
	{
		poses[index] = DecodePose(frame.Views[index]);
		center += poses[index].PositionMeters;
		averageForward += poses[index].Forward;
	}
	center /= static_cast<float>(frame.Header.ViewCount);
	averageForward = normalize(averageForward);

	if (!recenter.Valid || recenter.ResetGeneration != frame.Header.ResetGeneration)
	{
		const float horizontalLength = std::sqrt(averageForward.x * averageForward.x +
			averageForward.y * averageForward.y);
		const float rawYaw = horizontalLength > 0.0001f ?
			std::atan2(-averageForward.y, averageForward.x) : 0.0f;
		recenter.OriginMeters = center;
		recenter.YawOffset = -rawYaw;
		recenter.ResetGeneration = frame.Header.ResetGeneration;
		recenter.Valid = true;
		recenter.RecenterCount++;
	}

	ViewFamily family;
	for (uint32_t index = 0; index < frame.Header.ViewCount; index++)
	{
		const PackedView& source = frame.Views[index];
		const EngineTrackedPose pose = TransformCanonicalPose(
			vec3(source.Position[0], source.Position[1], source.Position[2]),
			vec4(source.Orientation[0], source.Orientation[1], source.Orientation[2], source.Orientation[3]),
			cameraLocation, bodyRotation, worldUnitsPerMeter, recenter);
		ViewDescription view;
		view.Viewport = { source.ViewportX, source.ViewportY, source.ViewportWidth, source.ViewportHeight };
		view.Location = pose.Position;
		view.Rotation.Origin = vec3(0.0f);
		view.Rotation.XAxis = pose.Forward;
		view.Rotation.YAxis = pose.Right;
		view.Rotation.ZAxis = pose.Up;
		view.WorldToView = Coords::ViewToRenderDev().ToMatrix() * view.Rotation.Inverse().ToMatrix() *
			Coords::Location(view.Location).ToMatrix();
		view.HasProjection = true;
		view.Projection = DecodeProjection(source);
		view.ApplyGameViewport = false;
		family.Views.push_back(view);
	}

	// The first-person weapon shares the projection target and is rendered
	// contiguously with each world eye. UI and cinematic surfaces remain owned
	// by the dedicated XR UI compositor.
	family.Presentation.SetLayer(PresentationLayer::World, { 1 }, true);
	family.Presentation.SetLayer(PresentationLayer::WeaponOverlay, { 1 }, true);
	family.Presentation.SetLayer(PresentationLayer::UserInterface, { 1 }, false);
	family.Presentation.SetLayer(PresentationLayer::Cinematic, { 1 }, false);
	return family;
}

extern "C"
{
	uint32_t Surreal_GetWebXRFrameABIVersion() { return WebXR::FrameABIVersion; }
	uint32_t Surreal_GetWebXRFrameHeaderSize() { return sizeof(WebXR::PackedFrameHeader); }
	uint32_t Surreal_GetWebXRViewSize() { return sizeof(WebXR::PackedView); }
	uint32_t Surreal_GetWebXRFrameMaxViews() { return WebXR::MaxViews; }

	int Surreal_ValidateWebXRFrame(const void* frameData, uint32_t bufferBytes)
	{
		WebXR::DecodedFrame frame;
		const bool valid = WebXR::DecodeFrame(frameData, bufferBytes, frame, LastFrameError);
		return valid ? 1 : 0;
	}

	int Surreal_GetWebXRFrameLastError() { return static_cast<int>(WebXR::GetLastFrameError()); }
}
