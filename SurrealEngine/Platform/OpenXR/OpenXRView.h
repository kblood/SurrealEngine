#pragma once

#include "Math/rotator.h"
#include "Render/ViewFamily.h"
#include "Render/XRUISurfaces.h"
#include "XR/XRCommon.h"

struct OpenXREyeView
{
	vec3 PositionMeters = vec3(0.0f);
	float OrientationX = 0.0f;
	float OrientationY = 0.0f;
	float OrientationZ = 0.0f;
	float OrientationW = 1.0f;
	float AngleLeft = 0.0f;
	float AngleRight = 0.0f;
	float AngleUp = 0.0f;
	float AngleDown = 0.0f;
};

// SDK-free description of the engine-to-LOCAL transform captured when the
// stereo views were recentered. Native composition uses this exact transform
// in reverse so independently composed UI remains registered with the world.
struct OpenXRUICompositionSpace
{
	bool Valid = false;
	vec3 AnchorLocation = vec3(0.0f);
	float YawOffset = 0.0f;
	float WorldUnitsPerMeter = 1.0f / 0.0254f;
};

struct OpenXRUIQuadPose
{
	vec3 PositionMeters = vec3(0.0f);
	float OrientationX = 0.0f;
	float OrientationY = 0.0f;
	float OrientationZ = 0.0f;
	float OrientationW = 1.0f;
};

class OpenXRViewTranslator
{
public:
	ViewFamily CreateViewFamily(const OpenXREyeView eyes[2], const vec3& anchorLocation, const Rotator& anchorRotation, const ViewRect& output);
	XRUISurfaceRay CreatePointerRay(const XRPose& pose, const vec3& anchorLocation) const;
	OpenXRUICompositionSpace CompositionSpace(const vec3& anchorLocation) const;
	void ResetRecenter() { recentered = false; yawOffset = 0.0f; }

private:
	bool recentered = false;
	float yawOffset = 0.0f;
};

bool ConvertXRUISurfacePoseToOpenXRLocal(const XRUISurfacePose& pose,
	const OpenXRUICompositionSpace& space, OpenXRUIQuadPose& output);
