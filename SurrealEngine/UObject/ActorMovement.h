#pragma once

constexpr bool HasHorizontalMovement(float x, float y)
{
	return x != 0.0f || y != 0.0f;
}

constexpr bool HasSpatialMovement(float x, float y, float z)
{
	return x != 0.0f || y != 0.0f || z != 0.0f;
}
