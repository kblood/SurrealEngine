#include "Render/ViewFamily.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool StereoAtlasLayout::CopiesExactlyTo(int eyeWidth, int eyeHeight) const
{
	return eyeWidth > 0 && eyeHeight > 0 &&
		eyeWidth <= std::numeric_limits<int>::max() / 2 &&
		Atlas.X == 0 && Atlas.Y == 0 && Atlas.Width == eyeWidth * 2 &&
		Atlas.Height == eyeHeight &&
		EyeSources[0].X == 0 && EyeSources[0].Y == 0 &&
		EyeSources[0].Width == eyeWidth && EyeSources[0].Height == eyeHeight &&
		EyeSources[1].X == eyeWidth && EyeSources[1].Y == 0 &&
		EyeSources[1].Width == eyeWidth && EyeSources[1].Height == eyeHeight;
}

std::optional<StereoAtlasLayout> CreateStereoAtlasLayout(int eyeWidth,
	int eyeHeight)
{
	if (eyeWidth <= 0 || eyeHeight <= 0 ||
		eyeWidth > std::numeric_limits<int>::max() / 2)
		return {};

	StereoAtlasLayout layout;
	layout.Atlas = { 0, 0, eyeWidth * 2, eyeHeight };
	layout.EyeSources[0] = { 0, 0, eyeWidth, eyeHeight };
	layout.EyeSources[1] = { eyeWidth, 0, eyeWidth, eyeHeight };
	return layout;
}

mat4 CreateCanvasProjection(int width, int height, float fovAngleDegrees)
{
	if (width <= 0 || height <= 0 || !std::isfinite(fovAngleDegrees) ||
		fovAngleDegrees <= 0.0f || fovAngleDegrees >= 180.0f)
		return mat4::identity();

	constexpr float Pi = 3.14159265359f;
	const float aspect = static_cast<float>(height) /
		static_cast<float>(width);
	const float projectionZ = std::tan(fovAngleDegrees * Pi / 360.0f);
	return mat4::frustum(-projectionZ, projectionZ,
		-aspect * projectionZ, aspect * projectionZ, 1.0f, 32768.0f,
		handedness::left, clipzrange::zero_positive_w);
}

std::optional<ViewRect> CreatePerViewHudRect(const ViewFamily& family,
	size_t viewIndex)
{
	if (!family.Hud.Enabled || family.Views.size() != 2 || viewIndex >= 2 ||
		!std::isfinite(family.Hud.HalfFovDegrees) ||
		!std::isfinite(family.Hud.HeightToWidth) ||
		!std::isfinite(family.Hud.ConvergenceDepth) ||
		family.Hud.HalfFovDegrees <= 0.0f ||
		family.Hud.HalfFovDegrees >= 89.0f ||
		family.Hud.HeightToWidth <= 0.0f ||
		family.Hud.ConvergenceDepth <= 0.0f)
		return {};

	const ViewDescription& view = family.Views[viewIndex];
	if (!view.HasProjectionTangents || view.Viewport.Width <= 0 ||
		view.Viewport.Height <= 0)
		return {};

	const ProjectionTangentBounds& bounds = view.ProjectionTangents;
	const float horizontalRange = bounds.Right - bounds.Left;
	const float verticalRange = bounds.Up - bounds.Down;
	if (!std::isfinite(horizontalRange) || !std::isfinite(verticalRange) ||
		horizontalRange <= 0.0f || verticalRange <= 0.0f)
		return {};

	constexpr float Pi = 3.14159265359f;
	const float halfTanX = std::tan(family.Hud.HalfFovDegrees * Pi / 180.0f);
	const float halfTanY = halfTanX * family.Hud.HeightToWidth;
	const float eyeSeparation = length(family.Views[1].Location -
		family.Views[0].Location);
	const float shift = (viewIndex == 0 ? 1.0f : -1.0f) *
		(eyeSeparation * 0.5f) / family.Hud.ConvergenceDepth;

	const int x0 = static_cast<int>(std::lround(view.Viewport.Width *
		((-halfTanX + shift) - bounds.Left) / horizontalRange));
	const int x1 = static_cast<int>(std::lround(view.Viewport.Width *
		((halfTanX + shift) - bounds.Left) / horizontalRange));
	const int y0 = static_cast<int>(std::lround(view.Viewport.Height *
		(bounds.Up - halfTanY) / verticalRange));
	const int y1 = static_cast<int>(std::lround(view.Viewport.Height *
		(bounds.Up + halfTanY) / verticalRange));

	return ViewRect{ view.Viewport.X + x0, view.Viewport.Y + y0,
		std::max(x1 - x0, 1), std::max(y1 - y0, 1) };
}

std::optional<std::array<ViewRect, 2>> CreateStereoPerViewHudRects(
	const ViewFamily& family, int pixelAlignment)
{
	const std::optional<ViewRect> left = CreatePerViewHudRect(family, 0);
	const std::optional<ViewRect> right = CreatePerViewHudRect(family, 1);
	if (!left || !right)
		return {};

	const int alignment = std::max(pixelAlignment, 1);
	const int width = (std::min(left->Width, right->Width) / alignment) * alignment;
	const int height = (std::min(left->Height, right->Height) / alignment) * alignment;
	if (width <= 0 || height <= 0)
		return {};
	std::array<ViewRect, 2> result = { *left, *right };
	for (ViewRect& rect : result)
	{
		rect.X += (rect.Width - width) / 2;
		rect.Y += (rect.Height - height) / 2;
		rect.Width = width;
		rect.Height = height;
	}
	return result;
}

bool ShouldRenderWeaponPerView(const ViewFamily& family)
{
	const PresentationLayerDescription world =
		family.Presentation.GetLayer(PresentationLayer::World);
	const PresentationLayerDescription weapon =
		family.Presentation.GetLayer(PresentationLayer::WeaponOverlay);
	return family.Views.size() > 1 && world.Enabled && weapon.Enabled &&
		world.Target == weapon.Target;
}

ViewFamily CreateSideBySideDiagnosticViewFamily(const ViewDescription& centerView, float eyeSeparation)
{
	ViewFamily family;
	if (centerView.Viewport.Width < 2 || centerView.Viewport.Height <= 0)
	{
		family.Views.push_back(centerView);
		return family;
	}

	ViewDescription left = centerView;
	ViewDescription right = centerView;

	int leftWidth = centerView.Viewport.Width / 2;
	left.Viewport.Width = leftWidth;
	right.Viewport.X = centerView.Viewport.X + leftWidth;
	right.Viewport.Width = centerView.Viewport.Width - leftWidth;

	vec3 eyeOffset = centerView.Rotation.YAxis * (eyeSeparation * 0.5f);
	left.Location = centerView.Location - eyeOffset;
	right.Location = centerView.Location + eyeOffset;
	left.WorldToView = Coords::ViewToRenderDev().ToMatrix() * left.Rotation.Inverse().ToMatrix() * Coords::Location(left.Location).ToMatrix();
	right.WorldToView = Coords::ViewToRenderDev().ToMatrix() * right.Rotation.Inverse().ToMatrix() * Coords::Location(right.Location).ToMatrix();

	// Script-controlled flat-screen viewport cropping cannot be applied twice
	// inside the side-by-side output rectangles.
	left.ApplyGameViewport = false;
	right.ApplyGameViewport = false;

	family.Views.push_back(left);
	family.Views.push_back(right);
	return family;
}
