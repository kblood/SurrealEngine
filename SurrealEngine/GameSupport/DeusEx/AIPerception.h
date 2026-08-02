#pragma once

#include <cstdint>

// Bounded Deus Ex AI perception kernels. They have no actor, property-offset,
// or world-state dependency; the LOS adapter accepts an injected trace callback
// so native runtime code and data-free tests share the exact short-circuit order.
float ComputeDXAIHearing(float volume, float radius, float hearingThreshold, float deltaX, float deltaY, float deltaZ) noexcept;
// Converts the renderer's lightmap value at a spot into the light level Deus Ex
// reports there. Measured against retail over a level's worth of NPC positions,
// its scale is a constant fraction of the lightmap's and it never falls below a
// floor even where no light reaches.
float ComputeDXAILightLevel(float lightmapLevel) noexcept;
float ComputeDXAISight(float visibility, float lightVisibility, float collisionRadius, float collisionHeight, float distanceSquared, float minAngularSize, float visibilityThreshold) noexcept;
bool PassesDXAISightDirection(float forward, float side, float up,
	float collisionRadius, float collisionHeight, float distanceSquared,
	float horizontalFovDegrees, float aspectRatio) noexcept;

enum class DXAISightTraceEndpoint
{
	Primary,
	Top,
	Bottom
};

struct DXAISightLineOfSightResult
{
	bool Visible = false;
	bool PrimaryVisible = false;
	bool TopTested = false;
	bool TopVisible = false;
	bool BottomTested = false;
	bool BottomVisible = false;
	uint8_t TraceCount = 0;
};

struct DXAISightTracePlan
{
	bool Valid = false;
	float PrimaryZOffset = 0.0f;
	float TopZOffset = 0.0f;
	float BottomZOffset = 0.0f;
};

DXAISightTracePlan BuildDXAISightTracePlan(bool targetIsPawn,
	float targetEyeHeight, float collisionHeight) noexcept;

template<typename TraceCallback>
DXAISightLineOfSightResult TraceDXAISightLineOfSight(
	bool checkCylinder, TraceCallback&& trace)
{
	DXAISightLineOfSightResult result;
	result.TraceCount++;
	result.PrimaryVisible = trace(DXAISightTraceEndpoint::Primary);
	if (result.PrimaryVisible)
	{
		result.Visible = true;
		return result;
	}
	if (!checkCylinder)
		return result;

	result.TopTested = true;
	result.TraceCount++;
	result.TopVisible = trace(DXAISightTraceEndpoint::Top);
	if (result.TopVisible)
	{
		result.Visible = true;
		return result;
	}

	result.BottomTested = true;
	result.TraceCount++;
	result.BottomVisible = trace(DXAISightTraceEndpoint::Bottom);
	result.Visible = result.BottomVisible;
	return result;
}

float ComputeDXAIMotionVisibility(float lightVisibility, float speed, bool includeVelocity) noexcept;
