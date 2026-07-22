#include "DXAIPerception.h"

#include <algorithm>
#include <cmath>

float ComputeDXAIHearing(float volume, float radius, float hearingThreshold, float deltaX, float deltaY, float deltaZ)
{
	if (volume <= 0.0f)
		return 0.0f;
	if (radius <= 0.0f)
		radius = 800.0f;

	// Deus Ex makes vertical separation twice as significant as horizontal
	// separation when attenuating sound.
	deltaZ *= 2.0f;
	float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
	if (distance >= radius)
		return 0.0f;

	return std::clamp((1.0f - distance / radius) * volume - hearingThreshold, 0.0f, 1.0f);
}

float ComputeDXAISight(float visibility, float lightVisibility, float collisionRadius, float collisionHeight, float distanceSquared, float minAngularSize, float visibilityThreshold)
{
	if (visibility <= 0.0f || lightVisibility <= 0.0f)
		return 0.0f;

	distanceSquared = std::max(distanceSquared, 1.0f);
	float angularSize = (collisionRadius * collisionRadius + collisionHeight * collisionHeight) / distanceSquared;
	if (angularSize < minAngularSize)
		return 0.0f;

	return std::clamp(visibility * angularSize * 64.0f * lightVisibility - visibilityThreshold, 0.0f, 1.0f);
}

float ComputeDXAIMotionVisibility(float lightVisibility, float speed, bool includeVelocity)
{
	if (includeVelocity)
	{
		float motion = std::clamp((speed - 30.0f) / 170.0f, 0.0f, 1.0f);
		lightVisibility += lightVisibility * motion * 0.5f;
	}
	return std::clamp(lightVisibility, 0.0f, 1.0f);
}
