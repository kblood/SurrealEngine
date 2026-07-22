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
	if (header.Version != FrameABIVersion || header.Flags != 0 ||
		header.ViewCount == 0 || header.ViewCount > MaxViews ||
		header.ByteSize != bufferBytes ||
		header.ByteSize != sizeof(PackedFrameHeader) + header.ViewCount * sizeof(PackedView) ||
		header.TextureWidth == 0 || header.TextureHeight == 0 || !std::isfinite(header.Timestamp))
	{
		error = FrameError::InvalidHeader;
		return false;
	}

	const uint8_t* bytes = static_cast<const uint8_t*>(frameData) + sizeof(PackedFrameHeader);
	for (uint32_t index = 0; index < header.ViewCount; index++)
	{
		PackedView& view = result.Views[index];
		std::memcpy(&view, bytes + index * sizeof(PackedView), sizeof(PackedView));
		const float lengthSquared = view.Orientation[0] * view.Orientation[0] +
			view.Orientation[1] * view.Orientation[1] + view.Orientation[2] * view.Orientation[2] +
			view.Orientation[3] * view.Orientation[3];
		if (view.Eye > static_cast<uint32_t>(Eye::Right) || view.ViewportX < 0 || view.ViewportY < 0 ||
			view.ViewportWidth <= 0 || view.ViewportHeight <= 0 ||
			static_cast<uint32_t>(view.ViewportWidth) > header.TextureWidth ||
			static_cast<uint32_t>(view.ViewportHeight) > header.TextureHeight ||
			static_cast<uint32_t>(view.ViewportX) > header.TextureWidth - static_cast<uint32_t>(view.ViewportWidth) ||
			static_cast<uint32_t>(view.ViewportY) > header.TextureHeight - static_cast<uint32_t>(view.ViewportHeight) ||
			!IsFiniteArray(view.Position, 3) || !IsFiniteArray(view.Orientation, 4) ||
			!IsFiniteArray(view.Projection, 16) || !std::isfinite(lengthSquared) || lengthSquared < 0.000001f)
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

	const Coords recenterRotation = Coords::YawRotation(recenter.YawOffset);
	ViewFamily family;
	for (uint32_t index = 0; index < frame.Header.ViewCount; index++)
	{
		const PackedView& source = frame.Views[index];
		const PoseAxes& pose = poses[index];
		ViewDescription view;
		view.Viewport = { source.ViewportX, source.ViewportY, source.ViewportWidth, source.ViewportHeight };
		view.Location = cameraLocation + Rotate(bodyRotation,
			Rotate(recenterRotation, (pose.PositionMeters - recenter.OriginMeters) * worldUnitsPerMeter));
		view.Rotation.Origin = vec3(0.0f);
		view.Rotation.XAxis = Rotate(bodyRotation, Rotate(recenterRotation, pose.Forward));
		view.Rotation.YAxis = Rotate(bodyRotation, Rotate(recenterRotation, pose.Right));
		view.Rotation.ZAxis = Rotate(bodyRotation, Rotate(recenterRotation, pose.Up));
		view.WorldToView = Coords::ViewToRenderDev().ToMatrix() * view.Rotation.Inverse().ToMatrix() *
			Coords::Location(view.Location).ToMatrix();
		view.HasProjection = true;
		view.Projection = DecodeProjection(source);
		view.ApplyGameViewport = false;
		family.Views.push_back(view);
	}

	// This provider skeleton presents only the world. Controller, weapon, HUD,
	// menu, and cinematic policies deliberately remain separate follow-ups.
	family.Presentation.SetLayer(PresentationLayer::World, { 1 }, true);
	family.Presentation.SetLayer(PresentationLayer::WeaponOverlay, { 1 }, false);
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
