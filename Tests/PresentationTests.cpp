#include "Render/Presentation.h"
#include "Render/ViewFamily.h"

#include <climits>
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

int main()
{
	auto runtimeAtlas = CreateStereoAtlasLayout(2112, 2304);
	Check(runtimeAtlas.has_value(), "runtime eye extent did not create a stereo atlas");
	Check(runtimeAtlas->Atlas.X == 0 && runtimeAtlas->Atlas.Y == 0 &&
		runtimeAtlas->Atlas.Width == 4224 && runtimeAtlas->Atlas.Height == 2304,
		"stereo atlas extent does not match two runtime eye images");
	Check(runtimeAtlas->EyeSources[0].X == 0 &&
		runtimeAtlas->EyeSources[0].Width == 2112 &&
		runtimeAtlas->EyeSources[1].X == 2112 &&
		runtimeAtlas->EyeSources[1].Width == 2112,
		"stereo atlas eye layout is not contiguous");
	Check(runtimeAtlas->CopiesExactlyTo(2112, 2304),
		"runtime atlas unexpectedly requires eye-copy scaling");
	Check(!runtimeAtlas->CopiesExactlyTo(2048, 2304),
		"mismatched destination extent was accepted as an exact copy");
	Check(!CreateStereoAtlasLayout(0, 2304) &&
		!CreateStereoAtlasLayout(2112, 0),
		"invalid runtime extent created a stereo atlas");
	Check(!CreateStereoAtlasLayout(INT_MAX, 2304),
		"overflowing runtime extent created a stereo atlas");

	PresentationPlan plan;
	auto defaultWorld = plan.GetLayer(PresentationLayer::World);
	Check(defaultWorld.Enabled, "unconfigured layers must be enabled");
	Check(defaultWorld.Target.IsDefault(), "unconfigured layers must use the window target");

	plan.SetLayer(PresentationLayer::UserInterface, { 2 });
	plan.SetLayer(PresentationLayer::World, { 1 }, false);
	plan.SetLayer(PresentationLayer::UserInterface, { 3 });
	Check(plan.Layers.size() == 2, "updating a layer must not add a duplicate");
	Check(plan.GetLayer(PresentationLayer::UserInterface).Target.Slot == 3, "layer target update was lost");
	Check(!plan.GetLayer(PresentationLayer::World).Enabled, "disabled layer was re-enabled");
	PresentationTargetBinding binding;
	binding.Target = { 7 };
	binding.Images.push_back({ reinterpret_cast<void*>(1), 1200, 1200 });
	binding.Images.push_back({ reinterpret_cast<void*>(2), 1200, 1200 });
	Check(binding.Target.Slot == 7 && binding.Images.size() == 2, "opaque presentation target binding was not retained");
	Check(binding.Images[1].Width == 1200 && binding.Images[1].NativeHandle == reinterpret_cast<void*>(2), "presentation image metadata was not retained");

	ViewFamily hudFamily;
	hudFamily.Hud.Enabled = true;
	hudFamily.Views.resize(2);
	for (int eye = 0; eye < 2; eye++)
	{
		hudFamily.Views[eye].Viewport = { eye * 2112, 0, 2112, 2304 };
		hudFamily.Views[eye].HasProjectionTangents = true;
		hudFamily.Views[eye].ProjectionTangents = {
			std::tan((eye == 0 ? -54.0f : -40.0f) * 3.14159265359f / 180.0f),
			std::tan((eye == 0 ? 40.0f : 54.0f) * 3.14159265359f / 180.0f),
			std::tan(44.0f * 3.14159265359f / 180.0f),
			std::tan(-55.0f * 3.14159265359f / 180.0f)
		};
		hudFamily.Views[eye].Location = { 0.0f, eye == 0 ? -1.26f : 1.26f, 0.0f };
	}
	const auto leftHud = CreatePerViewHudRect(hudFamily, 0);
	const auto rightHud = CreatePerViewHudRect(hudFamily, 1);
	Check(leftHud && rightHud, "qualified per-eye HUD rectangles were not created");
	Check(leftHud->Width == rightHud->Width && leftHud->Height == rightHud->Height,
		"symmetric headset FOV produced mismatched HUD extents");
	Check(leftHud->X >= hudFamily.Views[0].Viewport.X &&
		leftHud->X + leftHud->Width <= hudFamily.Views[0].Viewport.X + hudFamily.Views[0].Viewport.Width &&
		rightHud->X >= hudFamily.Views[1].Viewport.X &&
		rightHud->X + rightHud->Width <= hudFamily.Views[1].Viewport.X + hudFamily.Views[1].Viewport.Width,
		"compact HUD escaped an eye viewport");
	const float expectedAspect = 4.0f / 3.0f;
	Check(std::abs(static_cast<float>(leftHud->Width) / leftHud->Height - expectedAspect) < 0.12f,
		"qualified HUD no longer approximates its 4:3 virtual screen");
	Check(leftHud->Width < hudFamily.Views[0].Viewport.Width &&
		leftHud->Height < hudFamily.Views[0].Viewport.Height,
		"HUD expanded back to the full lens edges");

	// Reproduce the physical OpenXR failure: the desktop mirror is 16:9 but
	// each direct HUD is approximately 4:3 inside the 4224x2304 atlas. A Canvas
	// frame that keeps the mirror projection maps its bottom edge outside clip
	// space, leaving only part of UT's status doll visible. The HUD projection
	// must instead be derived from the HUD frame itself.
	const mat4 mirrorCanvasProjection = CreateCanvasProjection(2560, 1440, 90.0f);
	const mat4 hudCanvasProjection = CreateCanvasProjection(leftHud->Width,
		leftHud->Height, 90.0f);
	const float hudAspect = static_cast<float>(leftHud->Height) /
		static_cast<float>(leftHud->Width);
	Check(std::abs(hudCanvasProjection[0] / hudCanvasProjection[5] - hudAspect) < 0.0001f,
		"per-eye Canvas projection does not match the HUD frame aspect");
	Check(std::abs(hudCanvasProjection[5] - mirrorCanvasProjection[5]) > 0.1f,
		"per-eye Canvas accidentally retained the desktop mirror projection");
	const float projectionZ = std::tan(45.0f * 3.14159265359f / 180.0f);
	const vec4 bottomHudPoint(0.0f, projectionZ * hudAspect, 1.0f, 1.0f);
	const vec4 wronglyClipped = mirrorCanvasProjection * bottomHudPoint;
	const vec4 correctlyFitted = hudCanvasProjection * bottomHudPoint;
	Check(std::abs(wronglyClipped.y / wronglyClipped.w) > 1.1f,
		"regression fixture no longer demonstrates mirror-projection clipping");
	Check(std::abs(std::abs(correctlyFitted.y / correctlyFitted.w) - 1.0f) < 0.0001f,
		"per-eye Canvas projection does not fit the HUD edge to clip space");
	const mat4 invalidCanvasProjection = CreateCanvasProjection(0, 2304, 90.0f);
	Check(invalidCanvasProjection[0] == 1.0f && invalidCanvasProjection[5] == 1.0f,
		"invalid Canvas extent did not fail closed to identity");
	ViewFamily invalidHud = hudFamily;
	invalidHud.Views[0].HasProjectionTangents = false;
	Check(!CreatePerViewHudRect(invalidHud, 0),
		"direct HUD accepted a view without optical tangent bounds");

	ViewDescription center;
	center.Location = vec3(10.0f, 20.0f, 30.0f);
	center.Rotation = Coords::Identity();
	center.Viewport = { 7, 9, 101, 60 };
	center.ApplyGameViewport = true;
	ViewFamily stereo = CreateSideBySideDiagnosticViewFamily(center, 4.0f);
	Check(stereo.Views.size() == 2, "diagnostic must create two valid views");
	Check(stereo.Views[0].Viewport.X == 7 && stereo.Views[0].Viewport.Width == 50, "left diagnostic viewport is incorrect");
	Check(stereo.Views[1].Viewport.X == 57 && stereo.Views[1].Viewport.Width == 51, "right diagnostic viewport is incorrect");
	Check(stereo.Views[0].Location == vec3(10.0f, 18.0f, 30.0f), "left eye offset is incorrect");
	Check(stereo.Views[1].Location == vec3(10.0f, 22.0f, 30.0f), "right eye offset is incorrect");
	Check(!stereo.Views[0].ApplyGameViewport && !stereo.Views[1].ApplyGameViewport, "game viewport crop must be disabled for diagnostic views");
	Check(ShouldRenderWeaponPerView(stereo),
		"stereo layers on the default target must keep world and weapon contiguous");
	stereo.Presentation.SetLayer(PresentationLayer::World, { 1 }, true);
	stereo.Presentation.SetLayer(PresentationLayer::WeaponOverlay, { 2 }, true);
	Check(!ShouldRenderWeaponPerView(stereo),
		"separate weapon targets must retain their own presentation pass");
	stereo.Presentation.SetLayer(PresentationLayer::WeaponOverlay, { 1 }, false);
	Check(!ShouldRenderWeaponPerView(stereo),
		"a disabled weapon layer must not be rendered per view");

	center.Viewport.Width = 1;
	ViewFamily fallback = CreateSideBySideDiagnosticViewFamily(center);
	Check(fallback.Views.size() == 1, "invalid split must safely retain one view");
	Check(!ShouldRenderWeaponPerView(fallback),
		"single-view desktop rendering must keep its established overlay pass");

	return 0;
}
