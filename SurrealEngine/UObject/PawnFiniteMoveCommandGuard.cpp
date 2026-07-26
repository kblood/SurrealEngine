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

	const char* FiniteMoveCommandGuardSourceName(FiniteMoveCommandGuardSource value)
	{
		switch (value)
		{
		case FiniteMoveCommandGuardSource::MoveToInput: return "move_to_input";
		case FiniteMoveCommandGuardSource::StrafeFacingInput: return "strafe_facing_input";
		case FiniteMoveCommandGuardSource::TickPreLatentDestination:
			return "tick_pre_latent_destination";
		case FiniteMoveCommandGuardSource::TickPostScriptDestination:
			return "tick_post_script_destination";
		}
		return "unknown";
	}

	const char* FiniteMoveCommandGuardTerminalName(FiniteMoveCommandGuardTerminal value)
	{
		switch (value)
		{
		case FiniteMoveCommandGuardTerminal::LatentContinue: return "latent_continue";
		case FiniteMoveCommandGuardTerminal::RecoveredFromFiniteLocation:
			return "recovered_from_finite_location";
		case FiniteMoveCommandGuardTerminal::UnrecoverableNonFiniteLocation:
			return "unrecoverable_nonfinite_location";
		}
		return "unknown";
	}
}
