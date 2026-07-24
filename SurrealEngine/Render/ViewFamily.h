#pragma once

#include "Math/coords.h"
#include "Math/mat.h"
#include "Math/vec.h"
#include "Render/Presentation.h"
#include "Utils/Array.h"

#include <array>
#include <optional>

struct ViewRect
{
	int X = 0;
	int Y = 0;
	int Width = 0;
	int Height = 0;
};

// Tangent-space extents of a provider's asymmetric projection. Left/down are
// normally negative and right/up positive. Keeping these beside the projection
// matrix lets 2D presentation use the real optical centre without reverse-
// engineering a backend-specific matrix convention.
struct ProjectionTangentBounds
{
	float Left = 0.0f;
	float Right = 0.0f;
	float Up = 0.0f;
	float Down = 0.0f;
};

// A side-by-side source atlas whose halves match two equal-sized destination
// images. Providers which render directly into independent eye images do not
// use this layout.
struct StereoAtlasLayout
{
	ViewRect Atlas;
	ViewRect EyeSources[2];

	bool CopiesExactlyTo(int eyeWidth, int eyeHeight) const;
};

std::optional<StereoAtlasLayout> CreateStereoAtlasLayout(int eyeWidth,
	int eyeHeight);

// Creates the symmetric projection used by a 2D Canvas frame. The projection
// must be rebuilt whenever the frame's width/height changes; retaining a
// desktop projection for a differently-shaped XR HUD frame clips and distorts
// its contents.
mat4 CreateCanvasProjection(int width, int height, float fovAngleDegrees);

// Describes one camera view of an already-advanced game frame. The transform
// and viewport are always explicit. Projection is optional so the ordinary
// desktop view can retain its FOV-derived projection while stereo providers can
// later supply an asymmetric matrix without introducing provider-specific data.
struct ViewDescription
{
	vec3 Location = vec3(0.0f);
	mat4 WorldToView = mat4::identity();
	Coords Rotation = Coords::Identity();
	ViewRect Viewport;
	float FovAngle = 95.0f;
	bool HasProjection = false;
	mat4 Projection = mat4::identity();
	bool HasProjectionTangents = false;
	ProjectionTangentBounds ProjectionTangents;
	// The desktop view retains Deus Ex's script-controlled render viewport.
	// Provider-created views normally leave this false because their viewport
	// is authoritative.
	bool ApplyGameViewport = false;
};

// Some legacy HUD blend modes depend on the already-rendered world and cannot
// be faithfully captured into a transparent compositor texture. Providers can
// request a compact virtual HUD screen drawn directly into every projection
// view. The defaults match the headset-qualified first native VR release.
struct PerViewHudPresentation
{
	bool Enabled = false;
	float HalfFovDegrees = 25.0f;
	float HeightToWidth = 0.75f;
	float ConvergenceDepth = 68.9f;
};

// All views in a family render the same simulation state. Presentation maps
// the family's logical layers to output slots without exposing OpenXR, WebXR,
// or backend-native image types to engine code. PreRender, overlays, and
// PostRender continue to run once per family.
struct ViewFamily
{
	Array<ViewDescription> Views;
	PresentationPlan Presentation;
	PerViewHudPresentation Hud;
};

// Maps the virtual HUD screen into one asymmetric eye viewport. Eye separation
// is measured from the family view locations, so the returned rectangles fuse
// at Hud.ConvergenceDepth rather than at infinity.
std::optional<ViewRect> CreatePerViewHudRect(const ViewFamily& family,
	size_t viewIndex);
std::optional<std::array<ViewRect, 2>> CreateStereoPerViewHudRects(
	const ViewFamily& family);

// Creates a provider-free multi-view diagnostic. It splits the supplied
// viewport horizontally and offsets the two cameras along the view's lateral
// axis. Invalid or one-pixel viewports safely remain a single view.
ViewFamily CreateSideBySideDiagnosticViewFamily(const ViewDescription& centerView, float eyeSeparation = 4.0f);

// Stereo targets render the first-person weapon inside each world view when
// both layers share ownership. This prevents a backend target re-selection
// from clearing an already rendered eye. The ordinary one-view desktop path
// intentionally remains a separate overlay pass.
bool ShouldRenderWeaponPerView(const ViewFamily& family);
