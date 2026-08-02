#pragma once

#include "Math/vec.h"

// Scale is overlaid on script property data, where SheerAxis is a byte.
enum UnrealSheerAxis : uint8_t
{
	SHEER_None = 0,
	SHEER_XY = 1,
	SHEER_XZ = 2,
	SHEER_YX = 3,
	SHEER_YZ = 4,
	SHEER_ZX = 5,
	SHEER_ZY = 6,
};

class Scale
{
public:
	vec3 Scale;
	float SheerRate;
	UnrealSheerAxis SheerAxis;
};
