#include "PawnWalkingHitWallDispatch.h"

#include <cmath>

namespace
{
	bool IsFinite(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y)
			&& std::isfinite(value.z);
	}
}

namespace PawnMovement
{
	WalkingHitWallDispatchDecision EvaluateWalkingHitWallDispatch(
		const vec3& hitNormal, const vec3& velocity, float minHitWall)
	{
		WalkingHitWallDispatchDecision decision;
		if (!IsFinite(hitNormal) || !IsFinite(velocity)
			|| !std::isfinite(minHitWall))
		{
			return decision;
		}
		const float velocityLength = length(velocity);
		if (!std::isfinite(velocityLength) || velocityLength <= 0.0001f)
			return decision;

		decision.Valid = true;
		decision.NormalVelocityDot = dot(hitNormal, velocity / velocityLength);
		decision.LegacyVerticalWallBand = hitNormal.z > -0.2f
			&& hitNormal.z < 0.2f;
		// Head-on movement has a dot product near -1.0. MinHitWall is the
		// maximum (least negative) contact angle that may issue HitWall.
		decision.MinHitWallDispatch = decision.NormalVelocityDot < minHitWall;
		return decision;
	}
}
