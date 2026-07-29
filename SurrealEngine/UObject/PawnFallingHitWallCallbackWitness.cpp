#include "PawnFallingHitWallCallbackWitness.h"

#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z);
		}

		bool Near(float left, float right)
		{
			return std::isfinite(left) && std::isfinite(right)
				&& std::abs(left - right) <= FallingHitWallCallbackWitnessTolerance;
		}

		bool Near(const vec3& left, const vec3& right)
		{
			return Near(left.x, right.x) && Near(left.y, right.y)
				&& Near(left.z, right.z);
		}

		bool Valid(const FallingHitWallCallbackState& state)
		{
			return state.HasStateFrame && IsFinite(state.Location) && IsFinite(state.Velocity)
				&& IsFinite(state.Acceleration) && IsFinite(state.Destination)
				&& IsFinite(state.Focus) && std::isfinite(state.MoveTimer);
		}
	}

	FallingHitWallCallbackWitnessDecision EvaluateFallingHitWallCallbackWitness(
		bool staticWorldCollision, const FallingHitWallCallbackState& before,
		const FallingHitWallCallbackState& after)
	{
		if (!staticWorldCollision || !Valid(before) || !Valid(after)
			|| before.Physics != FallingHitWallCallbackWitnessFallingPhysics
			|| after.Physics != FallingHitWallCallbackWitnessFallingPhysics)
		{
			return FallingHitWallCallbackWitnessDecision::Ineligible;
		}
		const bool unchanged = before.JustTeleported == after.JustTeleported
			&& Near(before.Location, after.Location)
			&& Near(before.Velocity, after.Velocity)
			&& Near(before.Acceleration, after.Acceleration)
			&& Near(before.Destination, after.Destination)
			&& Near(before.Focus, after.Focus)
			&& before.MoveTarget == after.MoveTarget
			&& Near(before.MoveTimer, after.MoveTimer)
			&& before.LatentState == after.LatentState;
		return unchanged ? FallingHitWallCallbackWitnessDecision::ExactNoOp
			: FallingHitWallCallbackWitnessDecision::MutationObserved;
	}
}
