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

	vec3 ToOpenXRVector(const vec3& value)
	{
		return { value.y, value.z, -value.x };
	}

	bool IsFinite(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
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
		XRPose pose;
		pose.Valid = true;
		pose.Orientation = { source.OrientationX, source.OrientationY,
			source.OrientationZ, source.OrientationW };
		if (!CreateEngineRotation(pose, anchorRotation, eyeRotations[eye]))
			return {};

		Coords recenter = Coords::YawRotation(yawOffset);
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
		// OpenXR's up/down angles use +Y up. Render-device view space uses
		// +Y down, so its frustum bottom/top bounds are the negated up/down
		// extents rather than OpenXR's down/up order.
		view.Projection = mat4::frustum(
			std::tan(eyes[eye].AngleLeft), std::tan(eyes[eye].AngleRight),
			-std::tan(eyes[eye].AngleUp), -std::tan(eyes[eye].AngleDown),
			1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		view.ApplyGameViewport = false;
		family.Views.push_back(view);
	}
	return family;
}

bool OpenXRViewTranslator::CreateEngineRotation(const XRPose& pose,
	const Rotator& anchorRotation, Coords& output)
{
	if (!IsValidXRPose(pose))
		return false;
	quaternion orientation(pose.Orientation.X, pose.Orientation.Y,
		pose.Orientation.Z, pose.Orientation.W);
	const vec3 forward = ToUnrealVector(orientation * vec3(0.0f, 0.0f, -1.0f));
	const vec3 right = ToUnrealVector(orientation * vec3(1.0f, 0.0f, 0.0f));
	const vec3 up = ToUnrealVector(orientation * vec3(0.0f, 1.0f, 0.0f));
	if (!IsFinite(forward) || !IsFinite(right) || !IsFinite(up))
		return false;

	if (!recentered)
	{
		// Coords::YawRotation and Rotator yaw use opposite signs. Keep the
		// tracked-space offset in Coords convention so a neutral headset
		// begins at the pawn's Rotator-space facing.
		yawOffset = -anchorRotation.YawRadians() - std::atan2(-forward.y, forward.x);
		recentered = true;
	}

	const Coords recenter = Coords::YawRotation(yawOffset);
	output.Origin = vec3(0.0f);
	output.XAxis = RotateLocalToWorld(recenter, forward);
	output.YAxis = RotateLocalToWorld(recenter, right);
	output.ZAxis = RotateLocalToWorld(recenter, up);
	return true;
}

bool OpenXRViewTranslator::CreateHeadRotation(const XRPose& pose,
	const Rotator& anchorRotation, Rotator& output)
{
	Coords rotation;
	if (!CreateEngineRotation(pose, anchorRotation, rotation))
		return false;
	output = normalize(Rotator::FromVector(rotation.XAxis));
	return true;
}

bool OpenXRViewTranslator::ApplyYawTurn(float radians)
{
	if (!recentered || !std::isfinite(radians))
		return false;
	yawOffset += radians;
	return true;
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

OpenXRUICompositionSpace OpenXRViewTranslator::CompositionSpace(
	const vec3& anchorLocation) const
{
	return { recentered, anchorLocation, yawOffset, UnrealUnitsPerMeter };
}

bool OpenXRViewTranslator::CreateWeaponWorldTransform(const vec3& anchorLocation,
	XRWorldTransform& output) const
{
	output = {};
	if (!recentered || !IsFinite(anchorLocation))
		return false;
	output.EngineOrigin = { anchorLocation.x, anchorLocation.y, anchorLocation.z };
	output.UnitsPerMeter = UnrealUnitsPerMeter;
	output.EngineYawRadians = yawOffset;
	return true;
}

bool ConvertXRUISurfacePoseToOpenXRLocal(const XRUISurfacePose& pose,
	const OpenXRUICompositionSpace& space, OpenXRUIQuadPose& output)
{
	output = {};
	if (!space.Valid || !IsFinite(space.AnchorLocation) ||
		!std::isfinite(space.YawOffset) || !std::isfinite(space.WorldUnitsPerMeter) ||
		space.WorldUnitsPerMeter <= 0.0f || !IsFinite(pose.Center) ||
		!IsFinite(pose.Right) || !IsFinite(pose.Up) || !IsFinite(pose.Normal))
		return false;

	const vec3 right = normalize(pose.Right);
	const vec3 up = normalize(pose.Up);
	const vec3 normal = normalize(pose.Normal);
	if (length(right) <= 0.00001f || length(up) <= 0.00001f ||
		length(normal) <= 0.00001f || std::abs(dot(right, up)) > 0.001f ||
		std::abs(dot(right, normal)) > 0.001f || std::abs(dot(up, normal)) > 0.001f)
		return false;

	const Coords inverseRecenter = Coords::YawRotation(-space.YawOffset);
	output.PositionMeters = ToOpenXRVector(RotateLocalToWorld(inverseRecenter,
		pose.Center - space.AnchorLocation)) / space.WorldUnitsPerMeter;

	// The surface basis is expressed relative to the shared UI's canonical
	// (-Y right, +Z up, -X front) basis. Conjugating that relative rotation
	// through the handedness-changing XR/UE axis map produces a proper
	// right-handed OpenXR rotation. The fixed canonical U-axis reflection is
	// handled once by the provider's final image copy.
	const vec3 localRight = RotateLocalToWorld(inverseRecenter, right);
	const vec3 localUp = RotateLocalToWorld(inverseRecenter, up);
	const vec3 localNormal = RotateLocalToWorld(inverseRecenter, normal);
	auto rotateSurface = [&](const vec3& unrealVector)
	{
		return localRight * dot(unrealVector, vec3(0.0f, -1.0f, 0.0f)) +
			localUp * dot(unrealVector, vec3(0.0f, 0.0f, 1.0f)) +
			localNormal * dot(unrealVector, vec3(-1.0f, 0.0f, 0.0f));
	};

	mat4 rotation = mat4::identity();
	const vec3 columns[3] = {
		ToOpenXRVector(rotateSurface(ToUnrealVector(vec3(1.0f, 0.0f, 0.0f)))),
		ToOpenXRVector(rotateSurface(ToUnrealVector(vec3(0.0f, 1.0f, 0.0f)))),
		ToOpenXRVector(rotateSurface(ToUnrealVector(vec3(0.0f, 0.0f, 1.0f))))
	};
	for (int column = 0; column < 3; column++)
	{
		rotation[column * 4 + 0] = columns[column].x;
		rotation[column * 4 + 1] = columns[column].y;
		rotation[column * 4 + 2] = columns[column].z;
	}
	const quaternion orientation = normalize(quaternion::rotation_matrix(rotation));
	if (!std::isfinite(orientation.x) || !std::isfinite(orientation.y) ||
		!std::isfinite(orientation.z) || !std::isfinite(orientation.w))
		return false;
	output.OrientationX = orientation.x;
	output.OrientationY = orientation.y;
	output.OrientationZ = orientation.z;
	output.OrientationW = orientation.w;
	return true;
}
