#pragma once

#include "Math/coords.h"
#include "Math/mat.h"
#include "Math/vec.h"
#include "Render/Presentation.h"
#include "Utils/Array.h"

struct ViewRect
{
	int X = 0;
	int Y = 0;
	int Width = 0;
	int Height = 0;
};

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
	// The desktop view retains Deus Ex's script-controlled render viewport.
	// Provider-created views normally leave this false because their viewport
	// is authoritative.
	bool ApplyGameViewport = false;
};

// All views in a family render the same simulation state. Presentation maps
// the family's logical layers to output slots without exposing OpenXR, WebXR,
// or backend-native image types to engine code. PreRender, overlays, and
// PostRender continue to run once per family.
struct ViewFamily
{
	Array<ViewDescription> Views;
	PresentationPlan Presentation;
};

// Creates a provider-free multi-view diagnostic. It splits the supplied
// viewport horizontally and offsets the two cameras along the view's lateral
// axis. Invalid or one-pixel viewports safely remain a single view.
ViewFamily CreateSideBySideDiagnosticViewFamily(const ViewDescription& centerView, float eyeSeparation = 4.0f);

// Stereo targets render the first-person weapon inside each world view when
// both layers share ownership. This prevents a backend target re-selection
// from clearing an already rendered eye. The ordinary one-view desktop path
// intentionally remains a separate overlay pass.
bool ShouldRenderWeaponPerView(const ViewFamily& family);
