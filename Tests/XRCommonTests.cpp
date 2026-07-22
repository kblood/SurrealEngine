#include "XR/XRCommon.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool NearlyEqual(float a, float b, float tolerance = 0.0001f)
	{
		return std::abs(a - b) < tolerance;
	}

	XRQuaternion YawQuaternion(float radians)
	{
		return { 0.0f, std::sin(radians * 0.5f), 0.0f, std::cos(radians * 0.5f) };
	}

	class RecordingHapticSink : public IXRHapticSink
	{
	public:
		bool SubmitHaptic(const XRHapticRequest& request) override
		{
			Calls++;
			LastRequest = request;
			return Result;
		}

		int Calls = 0;
		bool Result = true;
		XRHapticRequest LastRequest;
	};
}

int main()
{
	XRWorldTransform world;
	world.EngineOrigin = { 10.0f, 20.0f, 30.0f };
	world.UnitsPerMeter = 100.0f;

	XREngineVector3 position = TransformXRPositionToEngine({ 1.0f, 2.0f, -3.0f }, world);
	if (!NearlyEqual(position.X, 310.0f) || !NearlyEqual(position.Y, 120.0f) || !NearlyEqual(position.Z, 230.0f))
		return Fail("canonical axes or metre scale were converted incorrectly");

	world.EngineOrigin = {};
	world.EngineYawRadians = Pi * 0.5f;
	position = TransformXRPositionToEngine({ 0.0f, 0.0f, -2.0f }, world);
	if (!NearlyEqual(position.X, 0.0f) || !NearlyEqual(position.Y, 200.0f) || !NearlyEqual(position.Z, 0.0f))
		return Fail("engine world yaw was not applied after the canonical axis conversion");

	world.EngineYawRadians = 0.0f;
	const XRQuaternion engineYaw = TransformXROrientationToEngine(YawQuaternion(Pi * 0.5f), world);
	if (!NearlyEqual(engineYaw.X, 0.0f) || !NearlyEqual(engineYaw.Y, 0.0f) ||
		!NearlyEqual(std::abs(engineYaw.Z), std::sin(Pi * 0.25f)) || engineYaw.Z >= 0.0f ||
		!NearlyEqual(std::abs(engineYaw.W), std::cos(Pi * 0.25f)))
		return Fail("orientation handedness was not converted with the coordinate basis");

	XRPose invalidPose;
	if (IsValidXRPose(invalidPose) || TransformXRPoseToEngine(invalidPose, world).Valid)
		return Fail("a pose without provider validity was accepted");
	invalidPose.Valid = true;
	invalidPose.Orientation = {};
	invalidPose.Orientation.W = 0.0f;
	if (IsValidXRPose(invalidPose) || TransformXRPoseToEngine(invalidPose, world).Valid)
		return Fail("a zero-length pose orientation was accepted");
	invalidPose.Orientation.W = 1.0f;
	invalidPose.Position.X = std::numeric_limits<float>::quiet_NaN();
	if (IsValidXRPose(invalidPose))
		return Fail("a non-finite pose was accepted");

	XRPose head;
	head.Valid = true;
	head.Position = { 1.0f, 1.7f, 2.0f };
	head.Orientation = YawQuaternion(Pi * 0.5f);
	const XRRecenterState recenter = MakeXRRecenterState(head);
	if (!recenter.Valid || !NearlyEqual(recenter.HorizontalOrigin.X, 1.0f) ||
		!NearlyEqual(recenter.HorizontalOrigin.Y, 0.0f) || !NearlyEqual(recenter.HorizontalOrigin.Z, 2.0f) ||
		!NearlyEqual(recenter.ReferenceYawRadians, Pi * 0.5f))
		return Fail("head pose did not produce a horizontal recenter state");

	world.Recenter = recenter;
	const XREnginePose recenteredHead = TransformXRPoseToEngine(head, world);
	if (!recenteredHead.Valid || !NearlyEqual(recenteredHead.Position.X, 0.0f) ||
		!NearlyEqual(recenteredHead.Position.Y, 0.0f) || !NearlyEqual(recenteredHead.Position.Z, 170.0f) ||
		!NearlyEqual(recenteredHead.Orientation.X, 0.0f) || !NearlyEqual(recenteredHead.Orientation.Y, 0.0f) ||
		!NearlyEqual(recenteredHead.Orientation.Z, 0.0f) || !NearlyEqual(std::abs(recenteredHead.Orientation.W), 1.0f))
		return Fail("recentering did not preserve height while resetting horizontal position and heading");

	XRSpaceSamples samples;
	samples.AimFor(XRHand::Left).Valid = true;
	samples.AimFor(XRHand::Left).Position.X = -0.2f;
	samples.AimFor(XRHand::Right).Valid = true;
	samples.AimFor(XRHand::Right).Position.X = 0.3f;
	if (!NearlyEqual(samples.AimFor(XRHand::Left).Position.X, -0.2f) ||
		!NearlyEqual(samples.AimFor(XRHand::Right).Position.X, 0.3f) ||
		XROppositeHand(XRHand::Left) != XRHand::Right)
		return Fail("left and right pose samples were not independent");

	XRControllerSnapshot controllers;
	controllers.ForHand(XRHand::Left).Connected = true;
	controllers.ForHand(XRHand::Left).Select.Pressed = true;
	controllers.ForHand(XRHand::Right).Thumbstick.X = 0.75f;
	if (!controllers.ForHand(XRHand::Left).Select.Pressed ||
		controllers.ForHand(XRHand::Right).Select.Pressed ||
		!NearlyEqual(controllers.ForHand(XRHand::Right).Thumbstick.X, 0.75f))
		return Fail("left and right semantic controls were not independent");

	XRPointerHit hit = MakeXRPointerHit(42, { 0.25f, 0.75f }, 800, 600, 1.5f);
	if (!hit.Valid || hit.SurfaceId != 42 || !NearlyEqual(hit.Pixel.X, 199.75f) ||
		!NearlyEqual(hit.Pixel.Y, 449.25f) || !NearlyEqual(hit.DistanceMeters, 1.5f))
		return Fail("valid pointer hit was not converted to pixel coordinates");
	if (MakeXRPointerHit(42, { -0.1f, 1.2f }, 800, 600, 1.0f).Valid ||
		MakeXRPointerHit(0, { 0.5f, 0.5f }, 800, 600, 1.0f).Valid ||
		MakeXRPointerHit(42, { 0.5f, 0.5f }, 0, 600, 1.0f).Valid)
		return Fail("invalid pointer hit was accepted");
	hit = MakeXRPointerHit(7, { -0.1f, 1.2f }, 800, 600, 2.0f, XRPointerClampPolicy::ClampToSurface);
	if (!hit.Valid || !NearlyEqual(hit.UV.X, 0.0f) || !NearlyEqual(hit.UV.Y, 1.0f) ||
		!NearlyEqual(hit.Pixel.X, 0.0f) || !NearlyEqual(hit.Pixel.Y, 599.0f))
		return Fail("pointer clamping policy did not clamp to the addressable surface");

	RecordingHapticSink sink;
	const XRHapticRequest haptic = { XRHand::Left, 0.6f, 0.15f, 120.0f };
	if (!RouteXRHaptic(&sink, haptic) || sink.Calls != 1 || sink.LastRequest.Hand != XRHand::Left ||
		!NearlyEqual(sink.LastRequest.Amplitude, 0.6f))
		return Fail("valid haptic request was not routed unchanged to the sink");
	if (RouteXRHaptic(nullptr, haptic) || RouteXRHaptic(&sink, { XRHand::Right, 1.1f, 0.1f, 0.0f }) || sink.Calls != 1)
		return Fail("missing sinks or invalid haptic requests reached the provider");
	sink.Result = false;
	if (RouteXRHaptic(&sink, { XRHand::Right, 0.5f, 0.1f, 0.0f }) || sink.Calls != 2)
		return Fail("haptic sink failure was not returned to the caller");

	XRSessionState session;
	session.Lifecycle = XRSessionLifecycle::Running;
	session.Focus = XRSessionFocus::Visible;
	if (!session.IsRunning() || session.AcceptsInput())
		return Fail("visible but unfocused session accepted input");
	session.Focus = XRSessionFocus::Focused;
	if (!session.AcceptsInput())
		return Fail("focused running session did not accept input");

	return 0;
}
