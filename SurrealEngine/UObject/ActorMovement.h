#pragma once

constexpr bool HasHorizontalMovement(float x, float y)
{
	return x != 0.0f || y != 0.0f;
}

constexpr bool HasSpatialMovement(float x, float y, float z)
{
	return x != 0.0f || y != 0.0f || z != 0.0f;
}

// A walking collision retry is useful only if it moved the actor or consumed
// part of the remaining physics time. Exact equality is intentional: this
// guard stops the zero-fraction corner case without suppressing arbitrarily
// small but legitimate multi-plane movement.
constexpr bool MadeWalkingIterationProgress(
	float oldX, float oldY, float oldZ, float oldTimeLeft,
	float newX, float newY, float newZ, float newTimeLeft)
{
	return oldX != newX || oldY != newY || oldZ != newZ || newTimeLeft < oldTimeLeft;
}
