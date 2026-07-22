#pragma once

#include "Math/rotator.h"
#include "Render/ViewFamily.h"

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

class OpenXRViewTranslator
{
public:
	ViewFamily CreateViewFamily(const OpenXREyeView eyes[2], const vec3& anchorLocation, const Rotator& anchorRotation, const ViewRect& output);
	void ResetRecenter() { recentered = false; yawOffset = 0.0f; }

private:
	bool recentered = false;
	float yawOffset = 0.0f;
};
