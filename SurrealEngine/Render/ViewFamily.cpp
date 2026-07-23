#include "Render/ViewFamily.h"

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
