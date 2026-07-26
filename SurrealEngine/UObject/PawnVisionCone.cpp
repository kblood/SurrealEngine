#include "PawnVisionCone.h"

#include <cmath>

namespace
{
	bool IsFinite(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}
}

namespace PawnMovement
{
	bool IsWithinPawnVisionCone(const vec3& observerLocation, const vec3& forward,
		const vec3& targetLocation, float peripheralVision)
	{
		if (!IsFinite(observerLocation) || !IsFinite(forward) || !IsFinite(targetLocation)
			|| !std::isfinite(peripheralVision))
		{
			return false;
		}

		if (peripheralVision <= 0.0f)
			return true;

		const vec3 toTarget = targetLocation - observerLocation;
		const float targetLengthSquared = dot(toTarget, toTarget);
		if (targetLengthSquared <= 0.00000001f)
			return true;

		const float forwardLengthSquared = dot(forward, forward);
		if (forwardLengthSquared <= 0.00000001f)
			return false;

		return dot(forward, toTarget)
			>= peripheralVision * std::sqrt(forwardLengthSquared * targetLengthSquared);
	}
}
