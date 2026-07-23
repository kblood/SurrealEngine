#include "Platform/OpenXR/OpenXRView.h"
#include "Math/quaternion.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << '\n';
		std::exit(1);
	}
}

static bool Near(float a, float b)
{
	return std::abs(a - b) < 0.001f;
}

static float Radians(float degrees)
{
	return degrees * 3.14159265359f / 180.0f;
}

static bool Near(const vec4& a, const vec4& b)
{
	return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z) && Near(a.w, b.w);
}

int main()
{
	OpenXREyeView eyes[2];
	eyes[0].PositionMeters = { -0.032f, 0.0f, 0.0f };
	eyes[1].PositionMeters = { 0.032f, 0.0f, 0.0f };
	for (OpenXREyeView& eye : eyes)
	{
		eye.AngleLeft = -0.7f;
		eye.AngleRight = 0.8f;
		eye.AngleDown = -0.6f;
		eye.AngleUp = 0.65f;
	}

	OpenXRViewTranslator translator;
	ViewFamily family = translator.CreateViewFamily(eyes, { 100.0f, 200.0f, 300.0f }, Rotator(0, 0, 0), { 4, 6, 101, 80 });
	Check(family.Views.size() == 2, "OpenXR translation must produce two views");
	Check(family.Views[0].Viewport.X == 4 && family.Views[0].Viewport.Width == 50, "left viewport is incorrect");
	Check(family.Views[1].Viewport.X == 54 && family.Views[1].Viewport.Width == 51, "right viewport is incorrect");
	Check(family.Views[0].HasProjection && family.Views[1].HasProjection, "OpenXR projections must be explicit");
	Check(!family.Views[0].ApplyGameViewport, "OpenXR view must not inherit script viewport cropping");
	Check(Near(family.Views[0].Location.y, 200.0f - 0.032f / 0.0254f), "left-eye meter scale or axis mapping is incorrect");
	Check(Near(family.Views[1].Location.y, 200.0f + 0.032f / 0.0254f), "right-eye meter scale or axis mapping is incorrect");
	Check(!Near(family.Views[0].Projection[0], family.Views[0].Projection[8]), "asymmetric projection was not retained");

	OpenXREyeView questEyes[2];
	questEyes[0].AngleLeft = Radians(-54.0f);
	questEyes[0].AngleRight = Radians(40.0f);
	questEyes[0].AngleUp = Radians(44.0f);
	questEyes[0].AngleDown = Radians(-55.0f);
	questEyes[1].AngleLeft = Radians(-40.0f);
	questEyes[1].AngleRight = Radians(54.0f);
	questEyes[1].AngleUp = Radians(44.0f);
	questEyes[1].AngleDown = Radians(-55.0f);
	auto questAtlas = CreateStereoAtlasLayout(2112, 2304);
	Check(questAtlas.has_value(), "Quest runtime extent did not produce a stereo atlas");
	OpenXRViewTranslator questTranslator;
	ViewFamily questFamily = questTranslator.CreateViewFamily(questEyes, {},
		Rotator(), questAtlas->Atlas);
	for (int eye = 0; eye < 2; eye++)
	{
		Check(questFamily.Views[eye].Viewport.X == questAtlas->EyeSources[eye].X &&
			questFamily.Views[eye].Viewport.Y == questAtlas->EyeSources[eye].Y &&
			questFamily.Views[eye].Viewport.Width == questAtlas->EyeSources[eye].Width &&
			questFamily.Views[eye].Viewport.Height == questAtlas->EyeSources[eye].Height,
			"OpenXR view does not fill its exact runtime-sized atlas half");
		const mat4 expected = mat4::frustum(
			std::tan(questEyes[eye].AngleLeft), std::tan(questEyes[eye].AngleRight),
			-std::tan(questEyes[eye].AngleUp), -std::tan(questEyes[eye].AngleDown),
			1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		const mat4 verticallyFlipped = mat4::frustum(
			std::tan(questEyes[eye].AngleLeft), std::tan(questEyes[eye].AngleRight),
			std::tan(questEyes[eye].AngleDown), std::tan(questEyes[eye].AngleUp),
			1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		for (size_t component = 0; component < 16; component++)
			Check(Near(questFamily.Views[eye].Projection[component], expected[component]),
			"Quest asymmetric vertical FOV was not converted to render-device Y-down coordinates");
		Check(!Near(questFamily.Views[eye].Projection[9], verticallyFlipped[9]),
			"Quest asymmetric vertical FOV remained vertically flipped");
	}

	OpenXREyeView rotatedEyes[2] = { questEyes[0], questEyes[1] };
	const quaternion trackedOrientation = quaternion::euler(
		Radians(17.0f), Radians(-23.0f), Radians(11.0f), EulerOrder::yxz);
	for (OpenXREyeView& eye : rotatedEyes)
	{
		eye.PositionMeters = { 0.04f, 1.65f, -0.12f };
		eye.OrientationX = trackedOrientation.x;
		eye.OrientationY = trackedOrientation.y;
		eye.OrientationZ = trackedOrientation.z;
		eye.OrientationW = trackedOrientation.w;
	}
	OpenXRViewTranslator rotatedTranslator;
	ViewFamily rotatedFamily = rotatedTranslator.CreateViewFamily(rotatedEyes,
		{ 123.0f, -456.0f, 789.0f }, Rotator(2800, -6100, 1900),
		questAtlas->Atlas);
	for (const ViewDescription& view : rotatedFamily.Views)
	{
		Check(Near(view.WorldToView * vec4(view.Location, 1.0f),
			vec4(0.0f, 0.0f, 0.0f, 1.0f)),
			"rotated OpenXR camera location did not map to view-space origin");
		Check(Near(view.WorldToView * vec4(view.Location + view.Rotation.XAxis, 1.0f),
			vec4(0.0f, 0.0f, 1.0f, 1.0f)),
			"rotated OpenXR forward axis did not map to render-device forward");
		Check(Near(view.WorldToView * vec4(view.Location + view.Rotation.YAxis, 1.0f),
			vec4(1.0f, 0.0f, 0.0f, 1.0f)),
			"rotated OpenXR right axis did not map to render-device right");
		Check(Near(view.WorldToView * vec4(view.Location + view.Rotation.ZAxis, 1.0f),
			vec4(0.0f, -1.0f, 0.0f, 1.0f)),
			"rotated OpenXR up axis did not map to render-device Y-down");
	}

	OpenXREyeView neutralEyes[2] = { questEyes[0], questEyes[1] };
	const Rotator spawnFacing(0, 13289, 0);
	OpenXRViewTranslator spawnTranslator;
	ViewFamily spawnFamily = spawnTranslator.CreateViewFamily(neutralEyes, {},
		spawnFacing, { 0, 0, 4224, 2304 });
	const Coords expectedSpawnFacing = Coords::Rotation(spawnFacing);
	for (const ViewDescription& view : spawnFamily.Views)
	{
		Check(Near(vec4(view.Rotation.XAxis, 0.0f),
			vec4(expectedSpawnFacing.XAxis, 0.0f)),
			"OpenXR recenter mirrored a nonzero pawn spawn facing");
	}
	XRPose neutralHead;
	neutralHead.Valid = true;
	Rotator headRotation;
	Check(spawnTranslator.CreateHeadRotation(neutralHead, spawnFacing, headRotation) &&
		Near(headRotation.YawRadians(), spawnFacing.YawRadians()),
		"neutral OpenXR head rotation did not preserve pawn facing");
	const float smoothTurn = Radians(-30.0f);
	Check(spawnTranslator.ApplyYawTurn(smoothTurn) &&
		spawnTranslator.CreateHeadRotation(neutralHead, spawnFacing, headRotation) &&
		Near(headRotation.YawRadians(), spawnFacing.YawRadians() - smoothTurn),
		"OpenXR smooth turn did not compose with tracked head yaw");
	OpenXRViewTranslator inactiveTranslator;
	Check(!inactiveTranslator.ApplyYawTurn(Radians(10.0f)),
		"OpenXR yaw turn was accepted before a tracked pose established recentering");
	XRPose rightAim;
	rightAim.Valid = true;
	rightAim.Position = { 0.1f, 0.0f, 0.0f };
	rightAim.Orientation.W = 1.0f;
	XRUISurfaceRay pointer = translator.CreatePointerRay(rightAim, { 100.0f, 200.0f, 300.0f });
	Check(Near(pointer.Origin.x, 100.0f) &&
		Near(pointer.Origin.y, 200.0f + 0.1f / 0.0254f) &&
		Near(pointer.Direction.x, 1.0f),
		"OpenXR aim pose did not share the view transform");
	XRWorldTransform weaponWorld;
	Check(translator.CreateWeaponWorldTransform({ 100.0f, 200.0f, 300.0f },
		weaponWorld), "OpenXR weapon transform was unavailable after recenter");
	XRSpaceSamples weaponSpaces;
	weaponSpaces.AimFor(XRHand::Right) = rightAim;
	XREnginePose weaponPose = TransformXRPoseToEngine(
		weaponSpaces.AimFor(XRHand::Right), weaponWorld);
	Check(weaponPose.Valid && Near(weaponPose.Position.X, pointer.Origin.x) &&
		Near(weaponPose.Position.Y, pointer.Origin.y) &&
		Near(weaponPose.Position.Z, pointer.Origin.z),
		"OpenXR weapon pose did not share the view/pointer transform");
	OpenXRUICompositionSpace compositionSpace = translator.CompositionSpace(
		{ 100.0f, 200.0f, 300.0f });
	XRUISurfacePose surface;
	surface.Center = { 100.0f + 1.5f / 0.0254f, 200.0f, 300.0f };
	OpenXRUIQuadPose quad;
	Check(ConvertXRUISurfacePoseToOpenXRLocal(surface, compositionSpace, quad),
		"canonical UI surface did not convert to LOCAL space");
	Check(Near(quad.PositionMeters.z, -1.5f) && Near(quad.PositionMeters.x, 0.0f) &&
		Near(quad.OrientationX, 0.0f) && Near(quad.OrientationY, 0.0f) &&
		Near(quad.OrientationZ, 0.0f) && Near(std::abs(quad.OrientationW), 1.0f),
		"canonical UI surface conversion changed its position or orientation");
	OpenXRUIQuadPose invalidQuad;
	Check(!ConvertXRUISurfacePoseToOpenXRLocal(surface, {}, invalidQuad),
		"UI surface converted without a valid recenter transform");
	const float quarterTurn = 3.14159265359f * 0.5f;
	const Coords recenter = Coords::YawRotation(quarterTurn);
	auto rotate = [&](const vec3& value)
	{
		return recenter.XAxis * value.x + recenter.YAxis * value.y +
			recenter.ZAxis * value.z;
	};
	XRUISurfacePose yawedSurface;
	yawedSurface.Center = vec3(10.0f, 20.0f, 30.0f) +
		rotate(vec3(2.0f * 40.0f, 0.0f, 0.0f));
	yawedSurface.Right = rotate(vec3(0.0f, -1.0f, 0.0f));
	yawedSurface.Up = rotate(vec3(0.0f, 0.0f, 1.0f));
	yawedSurface.Normal = rotate(vec3(-1.0f, 0.0f, 0.0f));
	OpenXRUIQuadPose yawedQuad;
	Check(ConvertXRUISurfacePoseToOpenXRLocal(yawedSurface,
		{ true, vec3(10.0f, 20.0f, 30.0f), quarterTurn, 40.0f }, yawedQuad),
		"recentered UI surface did not convert to LOCAL space");
	Check(Near(yawedQuad.PositionMeters.z, -2.0f) &&
		Near(yawedQuad.OrientationX, 0.0f) && Near(yawedQuad.OrientationY, 0.0f) &&
		Near(yawedQuad.OrientationZ, 0.0f) && Near(std::abs(yawedQuad.OrientationW), 1.0f),
		"UI surface did not reverse the shared recenter transform");

	translator.ResetRecenter();
	Check(!translator.CreateWeaponWorldTransform({}, weaponWorld),
		"OpenXR weapon transform remained active after recenter reset");
	Check(Near(length(translator.CreatePointerRay(rightAim, {}).Direction), 0.0f),
		"OpenXR pointer remained active after recenter state reset");
	ViewFamily invalid = translator.CreateViewFamily(eyes, {}, Rotator(), { 0, 0, 1, 1 });
	Check(invalid.Views.empty(), "invalid output must not create unusable views");
	return 0;
}
