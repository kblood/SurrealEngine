#pragma once

#include "Math/vec.h"

namespace PawnMovement
{
	enum class FallingTwoPlaneContactDecision
	{
		Unknown,
		ProbeCreaseSweep
	};

	struct FallingTwoPlaneContactInput
	{
		vec3 RequestedRemainingDelta;
		vec3 ActualDisplacement;
		vec3 FirstHitNormal;
		vec3 SecondHitNormal;
		bool AutonomousPlayerBot = false;
		bool NormalDownwardGravity = false;
		float WalkableNormalZ = 0.7071f;
		float MaximumActualDisplacement = 0.25f;
		float MaximumRequestedDelta = 256.0f;
		float MinimumPlaneCrossMagnitude = 0.05f;
		float MinimumCreaseDelta = 0.25f;
	};

	struct FallingTwoPlaneContactResult
	{
		FallingTwoPlaneContactDecision Decision = FallingTwoPlaneContactDecision::Unknown;
		vec3 CreaseSweepDelta;
	};

	FallingTwoPlaneContactResult ResolveFallingTwoPlaneContact(
		const FallingTwoPlaneContactInput& input);

	enum class FallingSeamReplanDecision
	{
		Unknown,
		ExpireMovementLatent
	};

	struct FallingSeamReplanInput
	{
		FallingTwoPlaneContactInput Contact;
		vec3 Acceleration;
		bool StockAutonomousAuthorityBot = false;
		bool ActiveMovementLatent = false;
		bool FirstHitStaticWorld = false;
		bool SecondHitStaticWorld = false;
		bool StartZoneKnown = false;
		bool StartedInPainZone = false;
		bool StartedInWaterZone = false;
		float MaximumAbsWallNormalZ = 0.1f;
	};

	struct FallingSeamReplanResult
	{
		FallingSeamReplanDecision Decision = FallingSeamReplanDecision::Unknown;
		vec3 Acceleration;
	};

	FallingSeamReplanResult EvaluateFallingSeamReplan(
		const FallingSeamReplanInput& input);
}
