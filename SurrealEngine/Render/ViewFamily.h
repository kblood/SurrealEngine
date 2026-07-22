#pragma once

#include "Math/coords.h"
#include "Math/mat.h"
#include "Math/vec.h"
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

// All views in a family render the same simulation state into the render
// device's currently selected target. Target selection and presentation remain
// render-backend responsibilities and are intentionally not encoded here.
// Existing PreRender, overlays, and PostRender still run once per family; a
// later presentation layer can define how those results are repeated per view.
struct ViewFamily
{
	Array<ViewDescription> Views;
};
