#include "PawnFiniteMoveCommandGuard.h"

#include <cmath>

namespace PawnMovement
{
	bool IsFiniteMoveCommandDestination(const vec3& destination)
	{
		return std::isfinite(destination.x) && std::isfinite(destination.y)
			&& std::isfinite(destination.z);
	}

	MoveCommandComponentClass ClassifyMoveCommandComponent(float value)
	{
		if (std::isfinite(value))
			return MoveCommandComponentClass::Finite;
		if (std::isnan(value))
			return MoveCommandComponentClass::NaN;
		return value < 0.0f ? MoveCommandComponentClass::NegativeInfinity
			: MoveCommandComponentClass::PositiveInfinity;
	}

	const char* MoveCommandComponentClassName(MoveCommandComponentClass value)
	{
		switch (value)
		{
		case MoveCommandComponentClass::Finite: return "finite";
		case MoveCommandComponentClass::NaN: return "nan";
		case MoveCommandComponentClass::NegativeInfinity: return "negative_infinity";
		case MoveCommandComponentClass::PositiveInfinity: return "positive_infinity";
		}
		return "unknown";
	}
}
