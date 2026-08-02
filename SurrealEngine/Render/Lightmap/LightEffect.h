#pragma once

#include <cmath>
#include "Math/vec.h"

class UActor;

// Brightness of a light at a point, as a fraction of the distance to its radius.
inline float LightDistanceFalloff(float distsqr)
{
	float v = std::sqrt(distsqr + 0.0001f);
	float v2 = v * v;
	float v3 = v2 * v;
	return std::min((1.0f + 2.0f * v3 - 3.0f * v2) / v, 1.0f);
}

class LightEffect
{
public:
	void Run(UActor* light, int width, int height, const vec3* locations, vec3 base, vec3 normal, const float* shadowmap, float* result);
};
