#include "XRWeaponPoseSolver.h"

#include <cmath>

namespace
{
	constexpr float VectorEpsilon = 0.000001f;

	bool IsFinite(float value)
	{
		return std::isfinite(value);
	}

	bool IsFinite(const XREngineVector3& value)
	{
		return IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z);
	}

	bool IsFinite(const XRQuaternion& value)
	{
		return IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z) && IsFinite(value.W);
	}

	XRQuaternion Normalize(const XRQuaternion& value)
	{
		const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z + value.W * value.W;
		if (!IsFinite(lengthSquared) || lengthSquared <= VectorEpsilon)
			return { 0.0f, 0.0f, 0.0f, 0.0f };

		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		return { value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength, value.W * inverseLength };
	}

	XRQuaternion Multiply(const XRQuaternion& left, const XRQuaternion& right)
	{
		return {
			left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
			left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
			left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
			left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z
		};
	}

	XREngineVector3 Cross(const XREngineVector3& left, const XREngineVector3& right)
	{
		return {
			left.Y * right.Z - left.Z * right.Y,
			left.Z * right.X - left.X * right.Z,
			left.X * right.Y - left.Y * right.X
		};
	}

	XREngineVector3 Rotate(const XRQuaternion& orientation, const XREngineVector3& value)
	{
		const XREngineVector3 imaginary = { orientation.X, orientation.Y, orientation.Z };
		const XREngineVector3 firstCross = Cross(imaginary, value);
		const XREngineVector3 secondCross = Cross(imaginary, firstCross);
		return {
			value.X + 2.0f * (orientation.W * firstCross.X + secondCross.X),
			value.Y + 2.0f * (orientation.W * firstCross.Y + secondCross.Y),
			value.Z + 2.0f * (orientation.W * firstCross.Z + secondCross.Z)
		};
	}

	XREngineVector3 Normalize(const XREngineVector3& value)
	{
		const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
		if (!IsFinite(lengthSquared) || lengthSquared <= VectorEpsilon)
			return {};

		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		return { value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength };
	}

	bool IsValid(const XREnginePose& pose)
	{
		if (!pose.Valid || !IsFinite(pose.Position) || !IsFinite(pose.Orientation))
			return false;
		const XRQuaternion normalized = Normalize(pose.Orientation);
		return normalized.X != 0.0f || normalized.Y != 0.0f || normalized.Z != 0.0f || normalized.W != 0.0f;
	}
}

XRWeaponPoseResult SolveXRWeaponPose(const XREnginePose& gripPose, const XREnginePose& aimPose,
	XRHand dominantHand, const XRWeaponPoseOptions& options)
{
	if (!IsValid(gripPose) || !IsValid(aimPose) || !IsFinite(options.LocalOffset) ||
		!IsFinite(options.LocalRotation) || !IsFinite(options.Scale) || options.Scale <= 0.0f)
		return {};

	const XRQuaternion gripOrientation = Normalize(gripPose.Orientation);
	const XRQuaternion aimOrientation = Normalize(aimPose.Orientation);
	const XRQuaternion localRotation = Normalize(options.LocalRotation);
	if (localRotation.X == 0.0f && localRotation.Y == 0.0f &&
		localRotation.Z == 0.0f && localRotation.W == 0.0f)
		return {};

	const XREngineVector3 worldOffset = Rotate(gripOrientation, options.LocalOffset);
	const XREngineVector3 aimDirection = Normalize(Rotate(aimOrientation, { 1.0f, 0.0f, 0.0f }));
	if (!IsFinite(aimDirection) ||
		(aimDirection.X == 0.0f && aimDirection.Y == 0.0f && aimDirection.Z == 0.0f))
		return {};

	XRWeaponPoseResult result;
	result.Valid = true;
	result.Hand = dominantHand;
	result.VisualPose.Valid = true;
	result.VisualPose.Position = {
		gripPose.Position.X + worldOffset.X,
		gripPose.Position.Y + worldOffset.Y,
		gripPose.Position.Z + worldOffset.Z
	};
	result.VisualPose.Orientation = Normalize(Multiply(gripOrientation, localRotation));
	result.AimDirection = aimDirection;
	result.Scale = options.Scale;
	result.Mirror = options.Mirror;
	return result;
}

XRWeaponPoseResult SolveXRWeaponPose(const XRSpaceSamples& spaces, const XRWorldTransform& worldTransform,
	XRHand dominantHand, const XRWeaponPoseOptions& options)
{
	return SolveXRWeaponPose(
		TransformXRPoseToEngine(spaces.GripFor(dominantHand), worldTransform),
		TransformXRPoseToEngine(spaces.AimFor(dominantHand), worldTransform),
		dominantHand, options);
}
