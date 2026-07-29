#include "PawnFallingTwoPlaneContact.h"

#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsUnitNormal(const vec3& value)
		{
			const float lengthSquared = dot(value, value);
			return IsFinite(value) && lengthSquared >= 0.99f && lengthSquared <= 1.01f;
		}
	}

	FallingTwoPlaneContactResult ResolveFallingTwoPlaneContact(
		const FallingTwoPlaneContactInput& input)
	{
		FallingTwoPlaneContactResult result;
		if (!input.AutonomousPlayerBot || !input.NormalDownwardGravity
			|| !IsFinite(input.RequestedRemainingDelta) || !IsFinite(input.ActualDisplacement)
			|| !IsUnitNormal(input.FirstHitNormal) || !IsUnitNormal(input.SecondHitNormal)
			|| !std::isfinite(input.WalkableNormalZ)
			|| !std::isfinite(input.MaximumActualDisplacement)
			|| !std::isfinite(input.MaximumRequestedDelta)
			|| !std::isfinite(input.MinimumPlaneCrossMagnitude)
			|| !std::isfinite(input.MinimumCreaseDelta)
			|| input.WalkableNormalZ <= 0.0f || input.WalkableNormalZ > 1.0f
			|| input.MaximumActualDisplacement < 0.0f
			|| input.MaximumRequestedDelta <= 0.0f
			|| input.MinimumPlaneCrossMagnitude <= 0.0f
			|| input.MinimumPlaneCrossMagnitude > 1.0f
			|| input.MinimumCreaseDelta <= 0.0f
			|| input.MinimumCreaseDelta > input.MaximumRequestedDelta)
			return result;

		if (input.FirstHitNormal.z >= input.WalkableNormalZ
			|| input.SecondHitNormal.z >= input.WalkableNormalZ)
			return result;

		const float actualDistanceSquared = dot(input.ActualDisplacement, input.ActualDisplacement);
		const float maximumActualDistanceSquared = input.MaximumActualDisplacement
			* input.MaximumActualDisplacement;
		if (actualDistanceSquared > maximumActualDistanceSquared)
			return result;

		const float requestedDistanceSquared = dot(
			input.RequestedRemainingDelta, input.RequestedRemainingDelta);
		const float maximumRequestedDistanceSquared = input.MaximumRequestedDelta
			* input.MaximumRequestedDelta;
		if (requestedDistanceSquared <= 0.0f
			|| requestedDistanceSquared > maximumRequestedDistanceSquared)
			return result;

		vec3 creaseDirection = cross(input.FirstHitNormal, input.SecondHitNormal);
		const float planeCrossMagnitudeSquared = dot(creaseDirection, creaseDirection);
		const float minimumPlaneCrossMagnitudeSquared = input.MinimumPlaneCrossMagnitude
			* input.MinimumPlaneCrossMagnitude;
		if (!IsFinite(creaseDirection)
			|| planeCrossMagnitudeSquared < minimumPlaneCrossMagnitudeSquared)
			return result;

		creaseDirection = normalize(creaseDirection);
		float projectedMagnitude = dot(input.RequestedRemainingDelta, creaseDirection);
		if (!std::isfinite(projectedMagnitude))
			return result;
		if (projectedMagnitude < 0.0f)
		{
			creaseDirection = -creaseDirection;
			projectedMagnitude = -projectedMagnitude;
		}
		if (projectedMagnitude < input.MinimumCreaseDelta)
			return result;

		result.CreaseSweepDelta = creaseDirection * projectedMagnitude;
		if (!IsFinite(result.CreaseSweepDelta)
			|| dot(input.RequestedRemainingDelta, result.CreaseSweepDelta) <= 0.0f)
			return FallingTwoPlaneContactResult();

		result.Decision = FallingTwoPlaneContactDecision::ProbeCreaseSweep;
		return result;
	}

	FallingSeamReplanResult EvaluateFallingSeamReplan(
		const FallingSeamReplanInput& input)
	{
		FallingSeamReplanResult result;
		result.Acceleration = input.Acceleration;
		if (!input.StockAutonomousAuthorityBot || !input.ActiveMovementLatent
			|| !input.FirstHitStaticWorld || !input.SecondHitStaticWorld
			|| !input.StartZoneKnown || input.StartedInPainZone || input.StartedInWaterZone
			|| !IsFinite(input.Acceleration)
			|| !std::isfinite(input.MaximumAbsWallNormalZ)
			|| input.MaximumAbsWallNormalZ < 0.0f
			|| input.MaximumAbsWallNormalZ >= input.Contact.WalkableNormalZ
			|| std::abs(input.Contact.FirstHitNormal.z) > input.MaximumAbsWallNormalZ
			|| std::abs(input.Contact.SecondHitNormal.z) > input.MaximumAbsWallNormalZ
			|| ResolveFallingTwoPlaneContact(input.Contact).Decision
				!= FallingTwoPlaneContactDecision::ProbeCreaseSweep)
			return result;

		const vec3& firstNormal = input.Contact.FirstHitNormal;
		const vec3& secondNormal = input.Contact.SecondHitNormal;
		const float firstInward = dot(input.Acceleration, firstNormal);
		const float secondInward = dot(input.Acceleration, secondNormal);
		vec3 adjusted = input.Acceleration;
		if (firstInward < 0.0f || secondInward < 0.0f)
		{
			const float normalDot = dot(firstNormal, secondNormal);
			const vec3 firstProjection = input.Acceleration
				- firstNormal * firstInward;
			const vec3 secondProjection = input.Acceleration
				- secondNormal * secondInward;
			if (firstInward < 0.0f && dot(firstProjection, secondNormal) >= 0.0f)
				adjusted = firstProjection;
			else if (secondInward < 0.0f && dot(secondProjection, firstNormal) >= 0.0f)
				adjusted = secondProjection;
			else
			{
				const float denominator = 1.0f - normalDot * normalDot;
				if (!std::isfinite(denominator) || denominator <= 0.0f)
					return result;
				const float firstScale = (-firstInward + normalDot * secondInward)
					/ denominator;
				const float secondScale = (-secondInward + normalDot * firstInward)
					/ denominator;
				if (!std::isfinite(firstScale) || !std::isfinite(secondScale)
					|| firstScale < 0.0f || secondScale < 0.0f)
					return result;
				adjusted += firstNormal * firstScale + secondNormal * secondScale;
			}
		}

		if (!IsFinite(adjusted)
			|| dot(adjusted, firstNormal) < -0.0001f
			|| dot(adjusted, secondNormal) < -0.0001f)
			return result;

		result.Decision = FallingSeamReplanDecision::ExpireMovementLatent;
		result.Acceleration = adjusted;
		return result;
	}
}
