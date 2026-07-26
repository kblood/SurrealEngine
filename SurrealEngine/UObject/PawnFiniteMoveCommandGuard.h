#pragma once

#include "Math/vec.h"

#include <cstdint>

namespace PawnMovement
{
	enum class MoveCommandComponentClass
	{
		Finite,
		NaN,
		NegativeInfinity,
		PositiveInfinity
	};

	struct FiniteMoveCommandGuardDiagnosticRecord
	{
		uint64_t Sequence = 0;
		uint64_t ObserverTick = 0;
		uint64_t LifeId = 0;
		int32_t ActorIndex = -1;
		MoveCommandComponentClass RequestedX = MoveCommandComponentClass::Finite;
		MoveCommandComponentClass RequestedY = MoveCommandComponentClass::Finite;
		MoveCommandComponentClass RequestedZ = MoveCommandComponentClass::Finite;
		bool PriorDestinationFinite = false;
		bool PriorFocusFinite = false;
	};

	bool IsFiniteMoveCommandDestination(const vec3& destination);
	MoveCommandComponentClass ClassifyMoveCommandComponent(float value);
	const char* MoveCommandComponentClassName(MoveCommandComponentClass value);
}
