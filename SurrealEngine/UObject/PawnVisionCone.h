#pragma once

#include "Math/vec.h"

namespace PawnMovement
{
	bool IsWithinPawnVisionCone(const vec3& observerLocation, const vec3& forward,
		const vec3& targetLocation, float peripheralVision);
}
