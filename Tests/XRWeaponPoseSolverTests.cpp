#include "XR/XRWeaponPoseSolver.h"
#include "XR/XRHandedness.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool NearlyEqual(float left, float right, float epsilon = 0.0001f)
	{
		return std::abs(left - right) <= epsilon;
	}

	bool NearlyEqual(const XREngineVector3& left, const XREngineVector3& right, float epsilon = 0.0001f)
	{
		return NearlyEqual(left.X, right.X, epsilon) && NearlyEqual(left.Y, right.Y, epsilon) &&
			NearlyEqual(left.Z, right.Z, epsilon);
	}

	bool Equivalent(const XRQuaternion& left, const XRQuaternion& right, float epsilon = 0.0001f)
	{
		const bool same = NearlyEqual(left.X, right.X, epsilon) && NearlyEqual(left.Y, right.Y, epsilon) &&
			NearlyEqual(left.Z, right.Z, epsilon) && NearlyEqual(left.W, right.W, epsilon);
		const bool negated = NearlyEqual(left.X, -right.X, epsilon) && NearlyEqual(left.Y, -right.Y, epsilon) &&
			NearlyEqual(left.Z, -right.Z, epsilon) && NearlyEqual(left.W, -right.W, epsilon);
		return same || negated;
	}

	float DegreesFromUnits(int value)
	{
		return value * (360.0f / 65536.0f);
	}

	float ShortestDegrees(float left, float right)
	{
		float delta = std::fmod(right - left, 360.0f);
		if (delta > 180.0f) delta -= 360.0f;
		if (delta < -180.0f) delta += 360.0f;
		return delta;
	}

	float Dot(const XREngineVector3& left, const XREngineVector3& right)
	{
		return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
	}

	XREngineVector3 Cross(const XREngineVector3& left, const XREngineVector3& right)
	{
		return {
			left.Y * right.Z - left.Z * right.Y,
			left.Z * right.X - left.X * right.Z,
			left.X * right.Y - left.Y * right.X
		};
	}

	float Length(const XREngineVector3& value)
	{
		return std::sqrt(Dot(value, value));
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

	XRQuaternion AxisAngle(float x, float y, float z, float radians)
	{
		const float halfAngle = radians * 0.5f;
		const float sine = std::sin(halfAngle);
		return { x * sine, y * sine, z * sine, std::cos(halfAngle) };
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

	XREnginePose Pose(XREngineVector3 position = {}, XRQuaternion orientation = {})
	{
		return { true, position, orientation };
	}

	void TestDefaultsAndAimGripSeparation()
	{
		const XREnginePose grip = Pose({ 10.0f, 20.0f, 30.0f }, AxisAngle(0.0f, 0.0f, 1.0f, Pi * 0.5f));
		const XREnginePose aim = Pose({ -40.0f, -50.0f, -60.0f }, AxisAngle(0.0f, 1.0f, 0.0f, Pi * 0.5f));
		const XRWeaponPoseResult result = SolveXRWeaponPose(grip, aim, XRHand::Left);

		Require(result.Valid && result.VisualPose.Valid, "valid grip and aim poses did not produce a weapon pose");
		Require(result.Hand == XRHand::Left && !result.Mirror, "default hand or mirror metadata was incorrect");
		Require(NearlyEqual(result.Scale, 5.0f), "default weapon scale was not the Farantir 5.0 baseline");
		Require(result.VisualAnchor == XRWeaponVisualAnchor::Aim &&
			NearlyEqual(result.VisualPose.Position, aim.Position) &&
			Equivalent(result.VisualPose.Orientation, aim.Orientation),
			"default visual transform did not follow the aim pose");
		Require(NearlyEqual(result.AimDirection, { 0.0f, 0.0f, -1.0f }) &&
			NearlyEqual(Length(result.AimDirection), 1.0f),
			"aim direction did not follow the independent aim pose");
	}

	void TestCanonicalProviderParity()
	{
		XRSpaceSamples openXRSamples;
		openXRSamples.GripFor(XRHand::Right) = { true, { 0.25f, 1.1f, -0.4f },
			AxisAngle(0.0f, 1.0f, 0.0f, -0.35f) };
		openXRSamples.AimFor(XRHand::Right) = { true, { 0.28f, 1.13f, -0.46f },
			AxisAngle(1.0f, 0.0f, 0.0f, 0.2f) };
		const XRSpaceSamples webXRSamples = openXRSamples;

		XRWorldTransform world;
		world.EngineOrigin = { 100.0f, -20.0f, 35.0f };
		world.UnitsPerMeter = 64.0f;
		world.EngineYawRadians = 0.3f;

		XRWeaponPoseOptions options;
		options.LocalOffset = { 2.0f, -1.0f, 0.5f };
		options.LocalRotation = AxisAngle(1.0f, 0.0f, 0.0f, 0.15f);
		options.Scale = 4.25f;
		options.Mirror = true;

		const XRWeaponPoseResult openXR = SolveXRWeaponPose(openXRSamples, world, XRHand::Right, options);
		const XRWeaponPoseResult webXR = SolveXRWeaponPose(webXRSamples, world, XRHand::Right, options);
		Require(openXR.Valid && webXR.Valid, "canonical provider samples were rejected");
		Require(NearlyEqual(openXR.VisualPose.Position, webXR.VisualPose.Position) &&
			Equivalent(openXR.VisualPose.Orientation, webXR.VisualPose.Orientation) &&
			NearlyEqual(openXR.VisualForward, webXR.VisualForward) &&
			NearlyEqual(openXR.VisualRight, webXR.VisualRight) &&
			NearlyEqual(openXR.VisualUp, webXR.VisualUp) &&
			NearlyEqual(openXR.AimDirection, webXR.AimDirection) &&
			NearlyEqual(openXR.Scale, webXR.Scale) && openXR.Mirror == webXR.Mirror,
			"identical OpenXR and WebXR canonical samples produced different weapon transforms");
	}

	void TestRenderedWeaponFollowsControllerAndReferenceTurns()
	{
		const XREnginePose neutral = Pose({ 12.0f, -7.0f, 3.0f });
		const XREnginePose controllerTurned = Pose(neutral.Position,
			AxisAngle(0.0f, 0.0f, 1.0f, Pi * 0.5f));
		const XRWeaponActorTransform neutralActor = BuildXRWeaponActorTransform(
			SolveXRWeaponPose(neutral, neutral, XRHand::Right));
		const XRWeaponActorTransform controllerActor = BuildXRWeaponActorTransform(
			SolveXRWeaponPose(controllerTurned, controllerTurned, XRHand::Right));
		Require(neutralActor.Valid && controllerActor.Valid &&
			NearlyEqual(neutralActor.Position, controllerActor.Position),
			"controller rotation invalidated or translated the rendered weapon");
		Require(NearlyEqual(std::abs(ShortestDegrees(
			DegreesFromUnits(neutralActor.Yaw), DegreesFromUnits(controllerActor.Yaw))), 90.0f, 0.01f),
			"rendered weapon yaw did not follow a 90-degree controller rotation");

		XRSpaceSamples spaces;
		spaces.GripFor(XRHand::Right) = { true, { 0.25f, 1.1f, -0.4f }, {} };
		spaces.AimFor(XRHand::Right) = spaces.GripFor(XRHand::Right);
		XRWorldTransform initialWorld;
		initialWorld.UnitsPerMeter = 64.0f;
		XRWorldTransform turnedWorld = initialWorld;
		turnedWorld.EngineYawRadians = Pi * 0.5f;
		const XRWeaponPoseResult initialPose = SolveXRWeaponPose(
			spaces, initialWorld, XRHand::Right);
		const XRWeaponPoseResult turnedPose = SolveXRWeaponPose(
			spaces, turnedWorld, XRHand::Right);
		const XRWeaponActorTransform initialActor =
			BuildXRWeaponActorTransform(initialPose);
		const XRWeaponActorTransform turnedActor =
			BuildXRWeaponActorTransform(turnedPose);
		Require(initialActor.Valid && turnedActor.Valid &&
			!NearlyEqual(initialActor.Position, turnedActor.Position),
			"player reference turn left the rendered weapon at its old world position");
		Require(NearlyEqual(std::abs(ShortestDegrees(
			DegreesFromUnits(initialActor.Yaw), DegreesFromUnits(turnedActor.Yaw))), 90.0f, 0.01f),
			"rendered weapon yaw did not follow a 90-degree player reference turn");
		Require(NearlyEqual(turnedPose.VisualForward, turnedPose.AimDirection),
			"reference turn split the rendered weapon direction from ballistics");
	}

	void TestYawPitchRollBasis()
	{
		const XRQuaternion roll = AxisAngle(1.0f, 0.0f, 0.0f, 15.0f * Pi / 180.0f);
		const XRQuaternion pitch = AxisAngle(0.0f, 1.0f, 0.0f, -20.0f * Pi / 180.0f);
		const XRQuaternion yaw = AxisAngle(0.0f, 0.0f, 1.0f, 40.0f * Pi / 180.0f);
		XRQuaternion orientation = Multiply(yaw, Multiply(pitch, roll));
		orientation.X *= 3.0f;
		orientation.Y *= 3.0f;
		orientation.Z *= 3.0f;
		orientation.W *= 3.0f;

		const XRWeaponPoseResult result = SolveXRWeaponPose(Pose({}, orientation), Pose({}, orientation), XRHand::Right);
		Require(result.Valid, "finite non-unit yaw/pitch/roll pose was rejected");

		const XREngineVector3 forward = result.VisualForward;
		const XREngineVector3 right = result.VisualRight;
		const XREngineVector3 up = result.VisualUp;
		Require(NearlyEqual(Length(forward), 1.0f) && NearlyEqual(Length(right), 1.0f) &&
			NearlyEqual(Length(up), 1.0f), "weapon orientation basis was not normalized");
		Require(NearlyEqual(Dot(forward, right), 0.0f) && NearlyEqual(Dot(forward, up), 0.0f) &&
			NearlyEqual(Dot(right, up), 0.0f), "weapon orientation basis was not orthogonal");
		Require(NearlyEqual(Dot(Cross(forward, right), up), 1.0f),
			"weapon orientation basis was reflected or left handed");
		Require(NearlyEqual(forward, Rotate(result.VisualPose.Orientation, { 1.0f, 0.0f, 0.0f })) &&
			NearlyEqual(right, Rotate(result.VisualPose.Orientation, { 0.0f, 1.0f, 0.0f })) &&
			NearlyEqual(up, Rotate(result.VisualPose.Orientation, { 0.0f, 0.0f, 1.0f })),
			"exposed visual basis did not match the solved orientation");
		Require(NearlyEqual(Length(result.AimDirection), 1.0f) && NearlyEqual(result.AimDirection, forward),
			"yaw/pitch/roll aim direction was not normalized from the aim pose");
	}

	void TestLocalRollPreservation()
	{
		XRWeaponPoseOptions options;
		options.LocalRotation = AxisAngle(1.0f, 0.0f, 0.0f, Pi * 0.5f);
		const XRWeaponPoseResult result = SolveXRWeaponPose(Pose(), Pose(), XRHand::Right, options);
		Require(result.Valid && NearlyEqual(result.VisualForward, { 1.0f, 0.0f, 0.0f }),
			"local roll changed the visual forward axis");
		Require(NearlyEqual(result.VisualRight, { 0.0f, 0.0f, 1.0f }) &&
			NearlyEqual(result.VisualUp, { 0.0f, -1.0f, 0.0f }),
			"local roll was not preserved in the exposed visual right/up basis");
		Require(NearlyEqual(result.AimDirection, { 1.0f, 0.0f, 0.0f }),
			"local visual roll changed the independent aim direction");
	}

	void TestLocalPoseOffsetsAndMirrorMetadata()
	{
		const XRQuaternion gripRotation = AxisAngle(0.0f, 0.0f, 1.0f, Pi * 0.5f);
		const XRQuaternion localRotation = AxisAngle(1.0f, 0.0f, 0.0f, Pi * 0.25f);
		XRWeaponPoseOptions options;
		options.LocalOffset = { 2.0f, 3.0f, 4.0f };
		options.LocalRotation = localRotation;
		options.Scale = 6.0f;
		options.Mirror = true;
		options.VisualAnchor = XRWeaponVisualAnchor::Grip;

		const XRWeaponPoseResult result = SolveXRWeaponPose(
			Pose({ 10.0f, 20.0f, 30.0f }, gripRotation), Pose({}, {}), XRHand::Left, options);
		Require(result.Valid && NearlyEqual(result.VisualPose.Position, { 7.0f, 22.0f, 34.0f }),
			"local offset was not transformed through the grip pose basis");
		Require(Equivalent(result.VisualPose.Orientation, Multiply(gripRotation, localRotation)),
			"local rotation was not composed in grip pose space");
		Require(result.Mirror && NearlyEqual(result.Scale, 6.0f) &&
			result.VisualAnchor == XRWeaponVisualAnchor::Grip,
			"mirror, scale, or explicit grip anchor metadata was not preserved");
		Require(NearlyEqual(result.AimDirection, { 1.0f, 0.0f, 0.0f }),
			"visual offset, rotation, or mirror metadata changed the firing direction");
	}

	void TestDominantHandPolicySelectsPoseAndMirrorMetadata()
	{
		XRSpaceSamples spaces;
		spaces.AimFor(XRHand::Left).Valid = true;
		spaces.GripFor(XRHand::Left).Valid = true;
		spaces.AimFor(XRHand::Right).Valid = true;
		spaces.GripFor(XRHand::Right).Valid = true;
		XRWorldTransform world;
		world.UnitsPerMeter = 1.0f;
		for (XRHand hand : { XRHand::Right, XRHand::Left })
		{
			const XRHandedness policy{ hand };
			XRWeaponPoseOptions options;
			options.Mirror = policy.MirrorWeaponPresentation();
			const XRWeaponPoseResult result = SolveXRWeaponPose(
				spaces, world, policy.Dominant, options);
			Require(result.Valid && result.Hand == hand &&
				result.Mirror == (hand == XRHand::Left),
				"dominant-hand policy did not select matching pose/mirror metadata");
		}
	}

	void RequireNoOp(const XRWeaponPoseResult& result, const char* message)
	{
		Require(!result.Valid && !result.VisualPose.Valid && NearlyEqual(result.VisualPose.Position, {}) &&
			NearlyEqual(result.VisualForward, {}) && NearlyEqual(result.VisualRight, {}) &&
			NearlyEqual(result.VisualUp, {}) && NearlyEqual(result.AimDirection, {}), message);
	}

	void TestInvalidInputsAreNoOp()
	{
		const XREnginePose valid = Pose();
		XREnginePose invalidPose = valid;
		invalidPose.Valid = false;
		Require(SolveXRWeaponPose(invalidPose, valid, XRHand::Right).Valid,
			"default aim anchor incorrectly required a valid grip pose");
		XRWeaponPoseOptions options;
		options.VisualAnchor = XRWeaponVisualAnchor::Grip;
		RequireNoOp(SolveXRWeaponPose(invalidPose, valid, XRHand::Right, options),
			"explicit grip anchor accepted an invalid grip pose");
		RequireNoOp(SolveXRWeaponPose(valid, invalidPose, XRHand::Right),
			"invalid aim pose did not return a no-op result");

		options = {};
		options.Scale = 0.0f;
		RequireNoOp(SolveXRWeaponPose(valid, valid, XRHand::Right, options),
			"zero visual scale did not return a no-op result");
		options = {};
		options.LocalRotation = { 0.0f, 0.0f, 0.0f, 0.0f };
		RequireNoOp(SolveXRWeaponPose(valid, valid, XRHand::Right, options),
			"zero local rotation did not return a no-op result");
		options = {};
		options.LocalOffset.X = std::numeric_limits<float>::quiet_NaN();
		RequireNoOp(SolveXRWeaponPose(valid, valid, XRHand::Right, options),
			"non-finite local offset did not return a no-op result");
		options = {};
		options.LocalOffset.X = std::numeric_limits<float>::max();
		XREnginePose maximumPosition = valid;
		maximumPosition.Position.X = std::numeric_limits<float>::max();
		RequireNoOp(SolveXRWeaponPose(valid, maximumPosition, XRHand::Right, options),
			"overflowing visual transform did not return a no-op result");

		XRSpaceSamples spaces;
		spaces.GripFor(XRHand::Left).Valid = true;
		XRWorldTransform world;
		RequireNoOp(SolveXRWeaponPose(spaces, world, XRHand::Left),
			"missing canonical aim sample did not return a no-op result");
	}

	void TestPrimaryAndSecondaryActorsSelectIndependentHandPoses()
	{
		int primaryActor = 1;
		int secondaryActor = 2;
		int unrelatedActor = 3;
		XRWeaponPoseResult primaryPose;
		primaryPose.Valid = true;
		primaryPose.Hand = XRHand::Right;
		XRWeaponPoseResult secondaryPose;
		secondaryPose.Valid = true;
		secondaryPose.Hand = XRHand::Left;

		Require(SelectXRWeaponActorPose(&primaryActor, &primaryActor, primaryPose,
			&secondaryActor, secondaryPose) == &primaryPose,
			"primary weapon actor did not select the dominant-hand pose");
		Require(SelectXRWeaponActorPose(&secondaryActor, &primaryActor, primaryPose,
			&secondaryActor, secondaryPose) == &secondaryPose,
			"secondary weapon actor did not select the off-hand pose");
		Require(!SelectXRWeaponActorPose(&unrelatedActor, &primaryActor, primaryPose,
			&secondaryActor, secondaryPose),
			"unrelated actor incorrectly received an XR weapon pose");
		secondaryPose.Valid = false;
		Require(!SelectXRWeaponActorPose(&secondaryActor, &primaryActor, primaryPose,
			&secondaryActor, secondaryPose),
			"secondary actor received an invalid off-hand pose");
	}
}

int main()
{
	try
	{
		TestDefaultsAndAimGripSeparation();
		TestCanonicalProviderParity();
		TestRenderedWeaponFollowsControllerAndReferenceTurns();
		TestYawPitchRollBasis();
		TestLocalRollPreservation();
		TestLocalPoseOffsetsAndMirrorMetadata();
		TestDominantHandPolicySelectsPoseAndMirrorMetadata();
		TestInvalidInputsAreNoOp();
		TestPrimaryAndSecondaryActorsSelectIndependentHandPoses();
		std::cout << "XR weapon pose solver tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "XR weapon pose solver test failed: " << error.what() << '\n';
		return 1;
	}
}
