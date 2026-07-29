#pragma once

#include "Math/vec.h"

#include <array>
#include <cstddef>

namespace PawnMovement
{
	constexpr float FallingParityMaximumPhysicsSubstep = 0.02f;
	constexpr size_t FallingParityMaximumScheduledSubsteps = 96;
	constexpr int FallingParityMaximumNonWalkableStaticContacts = 2;
	constexpr float FallingParityWalkableNormalZ = 0.7f;
	constexpr float FallingRetailMaximumPhysicsSlice = 0.1f;

	struct FallingRetailPhysicsSlice
	{
		float Elapsed = 0.0f;
		float RemainingTime = 0.0f;
		bool Valid = false;
	};

	FallingRetailPhysicsSlice SelectFallingRetailPhysicsSlice(float timeLeft);

	struct FallingParitySubstepSchedule
	{
		std::array<float, FallingParityMaximumScheduledSubsteps> Elapsed = {};
		size_t Count = 0;
		bool Valid = false;
		bool Exhausted = false;
	};

	FallingParitySubstepSchedule BuildFallingParitySubstepSchedule(
		float elapsed,
		size_t maximumSubsteps = FallingParityMaximumScheduledSubsteps);

	enum class FallingParityCollisionKind
	{
		Unknown,
		Clear,
		StaticWorld,
		Mover,
		DynamicActor
	};

	enum class FallingParityTransitionKind
	{
		Unknown,
		ProbeDirectSweep,
		ProbeAlignedSweep,
		Continue,
		Landed
	};

	enum class FallingParityReason
	{
		None,
		InvalidState,
		InvalidEnvironment,
		WaterPhysicsUnknown,
		BouncePhysicsUnknown,
		InvalidSweepEvidence,
		MoverCollisionUnknown,
		DynamicActorCollisionUnknown,
		NonWalkableStaticContactLimitExceeded,
		ForecastHorizonExhausted
	};

	struct FallingParityState
	{
		vec3 Location = vec3(0.0f);
		vec3 Velocity = vec3(0.0f);
		int NonWalkableStaticContacts = 0;
		bool Valid = true;
	};

	struct FallingParityEnvironment
	{
		vec3 Acceleration = vec3(0.0f);
		vec3 Gravity = vec3(0.0f);
		vec3 ZoneVelocity = vec3(0.0f);
		float GroundSpeed = 0.0f;
		float TerminalVelocity = 0.0f;
		float Elapsed = 0.0f;
		bool WaterPhysics = false;
		bool Bounce = false;
	};

	struct FallingParitySweepObservation
	{
		FallingParityCollisionKind Collision = FallingParityCollisionKind::Unknown;
		float Fraction = 1.0f;
		vec3 Normal = vec3(0.0f);
	};

	struct FallingParityTransition
	{
		FallingParityTransitionKind Kind = FallingParityTransitionKind::Unknown;
		FallingParityReason Reason = FallingParityReason::InvalidState;
		FallingParityState StepStart = { .Valid = false };
		FallingParityState State = { .Valid = false };
		vec3 DirectDelta = vec3(0.0f);
		vec3 AlignedDelta = vec3(0.0f);
		float Elapsed = 0.0f;
	};

	struct FallingTwoWallAdjustment
	{
		vec3 Delta = vec3(0.0f);
		bool Ditch = false;
	};

	FallingTwoWallAdjustment BuildFallingTwoWallAdjustment(
		const vec3& desiredDir,
		const vec3& delta,
		const vec3& hitNormal,
		const vec3& oldHitNormal,
		float hitFraction);

	vec3 ReconstructFallingCollisionVelocity(
		const vec3& oldLocation,
		const vec3& location,
		float elapsed,
		float fallingVelocityZ);

	FallingParityTransition BeginFallingParityStep(
		const FallingParityState& state,
		const FallingParityEnvironment& environment);

	FallingParityTransition ResolveFallingParityDirectSweep(
		const FallingParityTransition& step,
		const FallingParitySweepObservation& observation);

	FallingParityTransition ResolveFallingParityAlignedSweep(
		const FallingParityTransition& alignedSweep,
		const FallingParitySweepObservation& observation);

	FallingParityTransition EndFallingParityForecastHorizon(
		const FallingParityState& state);
}
