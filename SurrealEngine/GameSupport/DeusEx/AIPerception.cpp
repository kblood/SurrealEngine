#include "AIPerception.h"

#include <algorithm>
#include <cmath>

static bool IsFinite(float value) noexcept
{
	return std::isfinite(value);
}

float ComputeDXAIHearing(float volume, float radius, float hearingThreshold, float deltaX, float deltaY, float deltaZ) noexcept
{
	if (!IsFinite(volume) || !IsFinite(radius) || !IsFinite(hearingThreshold) ||
		!IsFinite(deltaX) || !IsFinite(deltaY) || !IsFinite(deltaZ))
		return 0.0f;
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

float ComputeDXAISight(float visibility, float lightVisibility, float collisionRadius, float collisionHeight, float distanceSquared, float minAngularSize, float visibilityThreshold) noexcept
{
	if (!IsFinite(visibility) || !IsFinite(lightVisibility) || !IsFinite(collisionRadius) ||
		!IsFinite(collisionHeight) || !IsFinite(distanceSquared) ||
		!IsFinite(minAngularSize) || !IsFinite(visibilityThreshold))
		return 0.0f;
	if (visibility <= 0.0f || lightVisibility <= 0.0f)
		return 0.0f;

	distanceSquared = std::max(distanceSquared, 1.0f);
	float angularSize = (collisionRadius * collisionRadius + collisionHeight * collisionHeight) / distanceSquared;
	if (angularSize < minAngularSize)
		return 0.0f;

	return std::clamp(visibility * angularSize * 64.0f * lightVisibility - visibilityThreshold, 0.0f, 1.0f);
}

bool PassesDXAISightDirection(float forward, float side, float up,
	float collisionRadius, float collisionHeight, float distanceSquared,
	float horizontalFovDegrees, float aspectRatio) noexcept
{
	if (!IsFinite(forward) || !IsFinite(side) || !IsFinite(up) ||
		!IsFinite(collisionRadius) || !IsFinite(collisionHeight) ||
		!IsFinite(distanceSquared) || !IsFinite(horizontalFovDegrees) ||
		!IsFinite(aspectRatio))
		return false;
	if (horizontalFovDegrees <= 0.0f)
		return true;
	if (aspectRatio <= 0.0f)
		aspectRatio = 1.0f;

	static constexpr double DegreesPerRadian = 57.2957795130823208768;
	const double horizontalAngle = std::atan2(
		std::abs(static_cast<double>(side)), static_cast<double>(forward)) * DegreesPerRadian;
	const double verticalAngle = std::atan2(
		std::abs(static_cast<double>(up)),
		std::hypot(static_cast<double>(forward), static_cast<double>(side))) * DegreesPerRadian;
	const double angularRadius = std::atan2(
		std::hypot(static_cast<double>(collisionRadius), static_cast<double>(collisionHeight)),
		std::sqrt(std::max(static_cast<double>(distanceSquared), 1.0))) * DegreesPerRadian;
	return horizontalAngle <= static_cast<double>(horizontalFovDegrees) * 0.5 + angularRadius &&
		verticalAngle <= static_cast<double>(horizontalFovDegrees) * 0.5 /
			static_cast<double>(aspectRatio) + angularRadius;
}

DXAISightTracePlan BuildDXAISightTracePlan(bool targetIsPawn,
	float targetEyeHeight, float collisionHeight) noexcept
{
	DXAISightTracePlan plan;
	if (!IsFinite(targetEyeHeight) || !IsFinite(collisionHeight))
		return plan;
	plan.Valid = true;
	plan.PrimaryZOffset = targetIsPawn ? targetEyeHeight : 0.0f;
	plan.TopZOffset = collisionHeight;
	plan.BottomZOffset = -collisionHeight;
	return plan;
}

float ComputeDXAIMotionVisibility(float lightVisibility, float speed, bool includeVelocity) noexcept
{
	if (!IsFinite(lightVisibility) || !IsFinite(speed))
		return 0.0f;
	if (includeVelocity)
	{
		float motion = std::clamp((speed - 30.0f) / 170.0f, 0.0f, 1.0f);
		lightVisibility += lightVisibility * motion * 0.5f;
	}
	return std::clamp(lightVisibility, 0.0f, 1.0f);
}
