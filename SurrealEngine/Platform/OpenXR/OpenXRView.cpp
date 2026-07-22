#include "Platform/OpenXR/OpenXRView.h"

#include "Math/quaternion.h"

#include <cmath>

namespace
{
	constexpr float UnrealUnitsPerMeter = 1.0f / 0.0254f;

	vec3 ToUnrealVector(const vec3& value)
	{
		// OpenXR: +X right, +Y up, -Z forward. UE1: +X forward,
		// +Y right, +Z up.
		return { -value.z, value.x, value.y };
	}

	vec3 RotateLocalToWorld(const Coords& rotation, const vec3& value)
	{
		return rotation.XAxis * value.x + rotation.YAxis * value.y + rotation.ZAxis * value.z;
	}
}

ViewFamily OpenXRViewTranslator::CreateViewFamily(const OpenXREyeView eyes[2], const vec3& anchorLocation, const Rotator& anchorRotation, const ViewRect& output)
{
	ViewFamily family;
	if (output.Width < 2 || output.Height <= 0)
		return family;

	Coords eyeRotations[2];
	vec3 unrealPositions[2];
	for (int eye = 0; eye < 2; eye++)
	{
		const OpenXREyeView& source = eyes[eye];
		quaternion orientation(source.OrientationX, source.OrientationY, source.OrientationZ, source.OrientationW);
		vec3 forward = ToUnrealVector(orientation * vec3(0.0f, 0.0f, -1.0f));
		vec3 right = ToUnrealVector(orientation * vec3(1.0f, 0.0f, 0.0f));
		vec3 up = ToUnrealVector(orientation * vec3(0.0f, 1.0f, 0.0f));

		if (!recentered && eye == 0)
		{
			yawOffset = anchorRotation.YawRadians() - std::atan2(-forward.y, forward.x);
			recentered = true;
		}

		Coords recenter = Coords::YawRotation(yawOffset);
		eyeRotations[eye].Origin = vec3(0.0f);
		eyeRotations[eye].XAxis = RotateLocalToWorld(recenter, forward);
		eyeRotations[eye].YAxis = RotateLocalToWorld(recenter, right);
		eyeRotations[eye].ZAxis = RotateLocalToWorld(recenter, up);
		unrealPositions[eye] = RotateLocalToWorld(recenter, ToUnrealVector(source.PositionMeters) * UnrealUnitsPerMeter);
	}

	int leftWidth = output.Width / 2;
	for (int eye = 0; eye < 2; eye++)
	{
		ViewDescription view;
		view.Location = anchorLocation + unrealPositions[eye];
		view.Rotation = eyeRotations[eye];
		view.WorldToView = Coords::ViewToRenderDev().ToMatrix() * view.Rotation.Inverse().ToMatrix() * Coords::Location(view.Location).ToMatrix();
		view.Viewport = { output.X + (eye == 0 ? 0 : leftWidth), output.Y, eye == 0 ? leftWidth : output.Width - leftWidth, output.Height };
		view.HasProjection = true;
		view.Projection = mat4::frustum(
			std::tan(eyes[eye].AngleLeft), std::tan(eyes[eye].AngleRight),
			std::tan(eyes[eye].AngleDown), std::tan(eyes[eye].AngleUp),
			1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		view.ApplyGameViewport = false;
		family.Views.push_back(view);
	}
	return family;
}

XRUISurfaceRay OpenXRViewTranslator::CreatePointerRay(const XRPose& pose,
	const vec3& anchorLocation) const
{
	if (!recentered || !IsValidXRPose(pose))
		return {};
	quaternion orientation(pose.Orientation.X, pose.Orientation.Y,
		pose.Orientation.Z, pose.Orientation.W);
	const Coords recenter = Coords::YawRotation(yawOffset);
	const vec3 positionMeters(pose.Position.X, pose.Position.Y, pose.Position.Z);
	const vec3 forward = RotateLocalToWorld(recenter,
		ToUnrealVector(orientation * vec3(0.0f, 0.0f, -1.0f)));
	return {
		anchorLocation + RotateLocalToWorld(recenter,
			ToUnrealVector(positionMeters) * UnrealUnitsPerMeter),
		normalize(forward)
	};
}
