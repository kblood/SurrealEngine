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

	enum class FiniteMoveCommandGuardSource
	{
		MoveToInput,
		StrafeFacingInput,
		TickPreLatentDestination,
		TickPostScriptDestination
	};

	enum class FiniteMoveCommandGuardTerminal
	{
		LatentContinue,
		RecoveredFromFiniteLocation,
		UnrecoverableNonFiniteLocation
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
		FiniteMoveCommandGuardSource Source = FiniteMoveCommandGuardSource::MoveToInput;
		FiniteMoveCommandGuardTerminal Terminal = FiniteMoveCommandGuardTerminal::LatentContinue;
		bool PriorDestinationFinite = false;
		bool PriorFocusFinite = false;
	};

	bool IsFiniteMoveCommandDestination(const vec3& destination);
	MoveCommandComponentClass ClassifyMoveCommandComponent(float value);
	const char* MoveCommandComponentClassName(MoveCommandComponentClass value);
	const char* FiniteMoveCommandGuardSourceName(FiniteMoveCommandGuardSource value);
	const char* FiniteMoveCommandGuardTerminalName(FiniteMoveCommandGuardTerminal value);
}
