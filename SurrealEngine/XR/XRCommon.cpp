#include "XRCommon.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float QuaternionEpsilon = 0.000001f;

	struct Matrix3
	{
		float M[3][3] = {};
	};

	bool IsFinite(float value)
	{
		return std::isfinite(value);
	}

	bool IsFinite(const XRVector3Meters& value)
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
		if (!IsFinite(lengthSquared) || lengthSquared <= QuaternionEpsilon)
			return {};
		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		return { value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength, value.W * inverseLength };
	}

	Matrix3 Multiply(const Matrix3& left, const Matrix3& right)
	{
		Matrix3 result;
		for (size_t row = 0; row < 3; row++)
		{
			for (size_t column = 0; column < 3; column++)
			{
				for (size_t index = 0; index < 3; index++)
					result.M[row][column] += left.M[row][index] * right.M[index][column];
			}
		}
		return result;
	}

	Matrix3 Transpose(const Matrix3& value)
	{
		Matrix3 result;
		for (size_t row = 0; row < 3; row++)
			for (size_t column = 0; column < 3; column++)
				result.M[row][column] = value.M[column][row];
		return result;
	}

	Matrix3 CanonicalYaw(float radians)
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		return { {
			{ cosine, 0.0f, sine },
			{ 0.0f, 1.0f, 0.0f },
			{ -sine, 0.0f, cosine }
		} };
	}

	Matrix3 EngineYaw(float radians)
	{
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		return { {
			{ cosine, -sine, 0.0f },
			{ sine, cosine, 0.0f },
			{ 0.0f, 0.0f, 1.0f }
		} };
	}

	Matrix3 CanonicalToEngineBasis()
	{
		return { {
			{ 0.0f, 0.0f, -1.0f },
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f }
		} };
	}

	Matrix3 SpaceBasis(const XRWorldTransform& transform)
	{
		const float recenterYaw = transform.Recenter.Valid ? transform.Recenter.ReferenceYawRadians : 0.0f;
		return Multiply(EngineYaw(transform.EngineYawRadians),
			Multiply(CanonicalToEngineBasis(), CanonicalYaw(-recenterYaw)));
	}

	XRVector3Meters Transform(const Matrix3& matrix, const XRVector3Meters& value)
	{
		return {
			matrix.M[0][0] * value.X + matrix.M[0][1] * value.Y + matrix.M[0][2] * value.Z,
			matrix.M[1][0] * value.X + matrix.M[1][1] * value.Y + matrix.M[1][2] * value.Z,
			matrix.M[2][0] * value.X + matrix.M[2][1] * value.Y + matrix.M[2][2] * value.Z
		};
	}

	Matrix3 QuaternionToMatrix(const XRQuaternion& input)
	{
		const XRQuaternion value = Normalize(input);
		const float xx = value.X * value.X;
		const float yy = value.Y * value.Y;
		const float zz = value.Z * value.Z;
		const float xy = value.X * value.Y;
		const float xz = value.X * value.Z;
		const float yz = value.Y * value.Z;
		const float wx = value.W * value.X;
		const float wy = value.W * value.Y;
		const float wz = value.W * value.Z;
		return { {
			{ 1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy) },
			{ 2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx) },
			{ 2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy) }
		} };
	}

	XRQuaternion MatrixToQuaternion(const Matrix3& value)
	{
		XRQuaternion result;
		const float trace = value.M[0][0] + value.M[1][1] + value.M[2][2];
		if (trace > 0.0f)
		{
			const float scale = 2.0f * std::sqrt(trace + 1.0f);
			result.W = 0.25f * scale;
			result.X = (value.M[2][1] - value.M[1][2]) / scale;
			result.Y = (value.M[0][2] - value.M[2][0]) / scale;
			result.Z = (value.M[1][0] - value.M[0][1]) / scale;
		}
		else if (value.M[0][0] > value.M[1][1] && value.M[0][0] > value.M[2][2])
		{
			const float scale = 2.0f * std::sqrt(1.0f + value.M[0][0] - value.M[1][1] - value.M[2][2]);
			result.W = (value.M[2][1] - value.M[1][2]) / scale;
			result.X = 0.25f * scale;
			result.Y = (value.M[0][1] + value.M[1][0]) / scale;
			result.Z = (value.M[0][2] + value.M[2][0]) / scale;
		}
		else if (value.M[1][1] > value.M[2][2])
		{
			const float scale = 2.0f * std::sqrt(1.0f + value.M[1][1] - value.M[0][0] - value.M[2][2]);
			result.W = (value.M[0][2] - value.M[2][0]) / scale;
			result.X = (value.M[0][1] + value.M[1][0]) / scale;
			result.Y = 0.25f * scale;
			result.Z = (value.M[1][2] + value.M[2][1]) / scale;
		}
		else
		{
			const float scale = 2.0f * std::sqrt(1.0f + value.M[2][2] - value.M[0][0] - value.M[1][1]);
			result.W = (value.M[1][0] - value.M[0][1]) / scale;
			result.X = (value.M[0][2] + value.M[2][0]) / scale;
			result.Y = (value.M[1][2] + value.M[2][1]) / scale;
			result.Z = 0.25f * scale;
		}
		return Normalize(result);
	}
}

bool IsValidXRPose(const XRPose& pose)
{
	if (!pose.Valid || !IsFinite(pose.Position) || !IsFinite(pose.Orientation))
		return false;
	const float lengthSquared = pose.Orientation.X * pose.Orientation.X + pose.Orientation.Y * pose.Orientation.Y +
		pose.Orientation.Z * pose.Orientation.Z + pose.Orientation.W * pose.Orientation.W;
	return lengthSquared > QuaternionEpsilon;
}

XRRecenterState MakeXRRecenterState(const XRPose& headPose)
{
	if (!IsValidXRPose(headPose))
		return {};

	const Matrix3 orientation = QuaternionToMatrix(headPose.Orientation);
	const XRVector3Meters forward = Transform(orientation, { 0.0f, 0.0f, -1.0f });
	const float horizontalLengthSquared = forward.X * forward.X + forward.Z * forward.Z;
	if (horizontalLengthSquared <= QuaternionEpsilon)
		return {};

	XRRecenterState result;
	result.Valid = true;
	result.HorizontalOrigin = { headPose.Position.X, 0.0f, headPose.Position.Z };
	result.ReferenceYawRadians = std::atan2(-forward.X, -forward.Z);
	return result;
}

XREngineVector3 TransformXRPositionToEngine(const XRVector3Meters& position, const XRWorldTransform& transform)
{
	XRVector3Meters local = position;
	if (transform.Recenter.Valid)
	{
		local.X -= transform.Recenter.HorizontalOrigin.X;
		local.Z -= transform.Recenter.HorizontalOrigin.Z;
	}
	const XRVector3Meters engineAxes = Transform(SpaceBasis(transform), local);
	return {
		transform.EngineOrigin.X + engineAxes.X * transform.UnitsPerMeter,
		transform.EngineOrigin.Y + engineAxes.Y * transform.UnitsPerMeter,
		transform.EngineOrigin.Z + engineAxes.Z * transform.UnitsPerMeter
	};
}

XRQuaternion TransformXROrientationToEngine(const XRQuaternion& orientation, const XRWorldTransform& transform)
{
	const float recenterYaw = transform.Recenter.Valid ? transform.Recenter.ReferenceYawRadians : 0.0f;
	const Matrix3 canonicalToEngine = CanonicalToEngineBasis();
	const Matrix3 worldRotation = Multiply(EngineYaw(transform.EngineYawRadians),
		Multiply(canonicalToEngine, CanonicalYaw(-recenterYaw)));
	const Matrix3 engineOrientation = Multiply(worldRotation,
		Multiply(QuaternionToMatrix(orientation), Transpose(canonicalToEngine)));
	return MatrixToQuaternion(engineOrientation);
}

XREnginePose TransformXRPoseToEngine(const XRPose& pose, const XRWorldTransform& transform)
{
	if (!IsValidXRPose(pose) || !IsFinite(transform.UnitsPerMeter) || transform.UnitsPerMeter <= 0.0f ||
		!IsFinite(transform.EngineYawRadians))
		return {};

	XREnginePose result;
	result.Valid = true;
	result.Position = TransformXRPositionToEngine(pose.Position, transform);
	result.Orientation = TransformXROrientationToEngine(pose.Orientation, transform);
	return result;
}

XRPointerHit MakeXRPointerHit(uint64_t surfaceId, XRVector2 uv, uint32_t pixelWidth, uint32_t pixelHeight,
	float distanceMeters, XRPointerClampPolicy clampPolicy)
{
	if (surfaceId == 0 || pixelWidth == 0 || pixelHeight == 0 || !IsFinite(uv.X) || !IsFinite(uv.Y) ||
		!IsFinite(distanceMeters) || distanceMeters < 0.0f)
		return {};

	const bool outside = uv.X < 0.0f || uv.X > 1.0f || uv.Y < 0.0f || uv.Y > 1.0f;
	if (outside && clampPolicy == XRPointerClampPolicy::RejectOutside)
		return {};

	uv.X = std::clamp(uv.X, 0.0f, 1.0f);
	uv.Y = std::clamp(uv.Y, 0.0f, 1.0f);

	XRPointerHit result;
	result.Valid = true;
	result.SurfaceId = surfaceId;
	result.UV = uv;
	result.Pixel = { uv.X * static_cast<float>(pixelWidth - 1), uv.Y * static_cast<float>(pixelHeight - 1) };
	result.DistanceMeters = distanceMeters;
	return result;
}

bool IsValidXRHapticRequest(const XRHapticRequest& request)
{
	return IsFinite(request.Amplitude) && request.Amplitude > 0.0f && request.Amplitude <= 1.0f &&
		IsFinite(request.DurationSeconds) && request.DurationSeconds > 0.0f &&
		IsFinite(request.FrequencyHz) && request.FrequencyHz >= 0.0f;
}

bool RouteXRHaptic(IXRHapticSink* sink, const XRHapticRequest& request)
{
	return sink && IsValidXRHapticRequest(request) && sink->SubmitHaptic(request);
}
