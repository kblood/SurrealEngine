#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Provider-neutral XR data and space conversion. Providers publish poses in
// canonical XR local space: metres, +X right, +Y up, and -Z forward.
enum class XRHand : uint8_t
{
	Left,
	Right
};

constexpr size_t XRHandCount = 2;

constexpr size_t XRHandIndex(XRHand hand)
{
	return hand == XRHand::Left ? 0 : 1;
}

constexpr XRHand XROppositeHand(XRHand hand)
{
	return hand == XRHand::Left ? XRHand::Right : XRHand::Left;
}

struct XRVector2
{
	float X = 0.0f;
	float Y = 0.0f;
};

struct XRVector3Meters
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

struct XREngineVector3
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

struct XRQuaternion
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float W = 1.0f;
};

struct XRPose
{
	bool Valid = false;
	XRVector3Meters Position;
	XRQuaternion Orientation;
};

struct XREnginePose
{
	bool Valid = false;
	XREngineVector3 Position;
	XRQuaternion Orientation;
};

struct XRSpaceSamples
{
	XRPose Head;
	std::array<XRPose, XRHandCount> Aim;
	std::array<XRPose, XRHandCount> Grip;

	XRPose& AimFor(XRHand hand) { return Aim[XRHandIndex(hand)]; }
	const XRPose& AimFor(XRHand hand) const { return Aim[XRHandIndex(hand)]; }
	XRPose& GripFor(XRHand hand) { return Grip[XRHandIndex(hand)]; }
	const XRPose& GripFor(XRHand hand) const { return Grip[XRHandIndex(hand)]; }
};

struct XRButtonState
{
	bool Pressed = false;
	bool Touched = false;
	float Value = 0.0f;
};

struct XRHandControllerState
{
	bool Connected = false;
	XRButtonState Select;
	XRButtonState Squeeze;
	XRButtonState Primary;
	XRButtonState Secondary;
	XRButtonState ThumbstickClick;
	XRButtonState Menu;
	XRVector2 Thumbstick;
};

struct XRControllerSnapshot
{
	std::array<XRHandControllerState, XRHandCount> Hands;

	XRHandControllerState& ForHand(XRHand hand) { return Hands[XRHandIndex(hand)]; }
	const XRHandControllerState& ForHand(XRHand hand) const { return Hands[XRHandIndex(hand)]; }
};

enum class XRSessionLifecycle : uint8_t
{
	Inactive,
	Starting,
	Running,
	Stopping,
	Lost
};

enum class XRSessionFocus : uint8_t
{
	Unavailable,
	Visible,
	Focused
};

struct XRSessionState
{
	XRSessionLifecycle Lifecycle = XRSessionLifecycle::Inactive;
	XRSessionFocus Focus = XRSessionFocus::Unavailable;

	bool IsRunning() const { return Lifecycle == XRSessionLifecycle::Running; }
	bool AcceptsInput() const { return IsRunning() && Focus == XRSessionFocus::Focused; }
};

struct XRRecenterState
{
	bool Valid = false;
	XRVector3Meters HorizontalOrigin;
	float ReferenceYawRadians = 0.0f;
};

struct XRWorldTransform
{
	XREngineVector3 EngineOrigin;
	float UnitsPerMeter = 1.0f;
	float EngineYawRadians = 0.0f;
	XRRecenterState Recenter;
};

bool IsValidXRPose(const XRPose& pose);
XRRecenterState MakeXRRecenterState(const XRPose& headPose);
XREngineVector3 TransformXRPositionToEngine(const XRVector3Meters& position, const XRWorldTransform& transform);
XRQuaternion TransformXROrientationToEngine(const XRQuaternion& orientation, const XRWorldTransform& transform);
XREnginePose TransformXRPoseToEngine(const XRPose& pose, const XRWorldTransform& transform);

enum class XRPointerClampPolicy : uint8_t
{
	RejectOutside,
	ClampToSurface
};

struct XRPointerHit
{
	bool Valid = false;
	uint64_t SurfaceId = 0;
	XRVector2 UV;
	XRVector2 Pixel;
	float DistanceMeters = 0.0f;
};

// UI UV uses a top-left origin: U increases right and V increases down.
// Pixel coordinates address pixel centres, so UV 1 maps to size - 1.
XRPointerHit MakeXRPointerHit(uint64_t surfaceId, XRVector2 uv, uint32_t pixelWidth, uint32_t pixelHeight,
	float distanceMeters, XRPointerClampPolicy clampPolicy = XRPointerClampPolicy::RejectOutside);

struct XRHapticRequest
{
	XRHand Hand = XRHand::Right;
	float Amplitude = 0.0f;
	float DurationSeconds = 0.0f;
	float FrequencyHz = 0.0f;
};

class IXRHapticSink
{
public:
	virtual ~IXRHapticSink() = default;
	virtual bool SubmitHaptic(const XRHapticRequest& request) = 0;
};

bool IsValidXRHapticRequest(const XRHapticRequest& request);
bool RouteXRHaptic(IXRHapticSink* sink, const XRHapticRequest& request);
