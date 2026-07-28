#include "PawnVisionCone.h"

#include <cmath>

namespace
{
	// Retail UT99 widens the angular cone for nearby targets. Use a conservative
	// positional margin that covers every retail-positive cone sample in the
	// retained Deck16 corpus (the measured maximum was 234.428 units). This is a
	// deliberate one-sided compatibility policy: prefer seeing slightly more to
	// missing a target retail would see.
	constexpr float RetailVisionConeCompatibilitySlack = 240.0f;

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

		const vec3 toTarget = targetLocation - observerLocation;
		const float targetLengthSquared = dot(toTarget, toTarget);
		if (targetLengthSquared <= 0.00000001f)
			return true;

		const float forwardLengthSquared = dot(forward, forward);
		if (forwardLengthSquared <= 0.00000001f)
			return false;

		const float forwardLength = std::sqrt(forwardLengthSquared);
		const float targetLength = std::sqrt(targetLengthSquared);
		return dot(forward, toTarget) >= forwardLength *
			(peripheralVision * targetLength - RetailVisionConeCompatibilitySlack);
	}
}
