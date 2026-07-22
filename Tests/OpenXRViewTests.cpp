#include "Platform/OpenXR/OpenXRView.h"

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
	XRPose rightAim;
	rightAim.Valid = true;
	rightAim.Position = { 0.1f, 0.0f, 0.0f };
	rightAim.Orientation.W = 1.0f;
	XRUISurfaceRay pointer = translator.CreatePointerRay(rightAim, { 100.0f, 200.0f, 300.0f });
	Check(Near(pointer.Origin.x, 100.0f) &&
		Near(pointer.Origin.y, 200.0f + 0.1f / 0.0254f) &&
		Near(pointer.Direction.x, 1.0f),
		"OpenXR aim pose did not share the view transform");

	translator.ResetRecenter();
	Check(Near(length(translator.CreatePointerRay(rightAim, {}).Direction), 0.0f),
		"OpenXR pointer remained active after recenter state reset");
	ViewFamily invalid = translator.CreateViewFamily(eyes, {}, Rotator(), { 0, 0, 1, 1 });
	Check(invalid.Views.empty(), "invalid output must not create unusable views");
	return 0;
}
