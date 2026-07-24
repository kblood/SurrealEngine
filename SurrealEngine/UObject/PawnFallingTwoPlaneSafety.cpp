#include "PawnFallingTwoPlaneSafety.h"

#include <algorithm>
#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool IsFinite(const vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		float EstimateFallingDamage(float impactVelocityZ, float jumpZ,
			bool fallingDamageDisabled)
		{
			if (fallingDamageDisabled || impactVelocityZ >= -1.4f * jumpZ
				|| impactVelocityZ > -750.0f - jumpZ)
				return 0.0f;
			if (impactVelocityZ < -1650.0f - jumpZ)
				return 1000.0f;
			return std::max(0.0f, -0.15f * (impactVelocityZ + 700.0f + jumpZ));
		}

		bool HasAuthorizedRecoveryEvidence(const FallingRecoveryAuthorizationEvidence& evidence)
		{
			return evidence.SweepResultKnown && evidence.SweepClear
				&& evidence.SupportResultKnown && evidence.WalkableShortSupport
				&& evidence.PainResultKnown && !evidence.SupportInPainZone
				&& evidence.TargetProgressKnown && std::isfinite(evidence.TargetProgress)
				&& evidence.TargetProgress > 0.0f;
		}
	}

	FallingTwoPlaneSafetyResult EvaluateFallingTwoPlaneSafety(
		const FallingTwoPlaneSafetyInput& input)
	{
		FallingTwoPlaneSafetyResult result;
		const FallingTwoPlaneContactResult contact = ResolveFallingTwoPlaneContact(input.Contact);
		if (contact.Decision != FallingTwoPlaneContactDecision::ProbeCreaseSweep)
			return result;

		const float creaseLengthSquared = dot(contact.CreaseSweepDelta, contact.CreaseSweepDelta);
		if (!IsFinite(contact.CreaseSweepDelta) || creaseLengthSquared <= 0.0f
			|| !std::isfinite(input.MinimumVerticalCreaseRatio)
			|| input.MinimumVerticalCreaseRatio <= 0.0f
			|| input.MinimumVerticalCreaseRatio > 1.0f)
			return result;

		const float downwardRatio = -contact.CreaseSweepDelta.z / std::sqrt(creaseLengthSquared);
		if (!std::isfinite(downwardRatio))
			return result;
		const bool downwardCrease = contact.CreaseSweepDelta.z < 0.0f;
		if (downwardCrease && input.Recovery.TargetProgressKnown
			&& (!std::isfinite(input.Recovery.TargetProgress)
				|| input.Recovery.TargetProgress <= 0.0f))
		{
			result.Decision = FallingTwoPlaneSafetyDecision::RejectTargetRegression;
			return result;
		}
		if (!HasAuthorizedRecoveryEvidence(input.Recovery))
			return result;

		if (!downwardCrease || downwardRatio < input.MinimumVerticalCreaseRatio)
		{
			result.Decision = FallingTwoPlaneSafetyDecision::ProbeCreaseSweep;
			result.SweepDelta = contact.CreaseSweepDelta;
			return result;
		}

		const float normalLengthSquared = dot(input.Landing.HitNormal, input.Landing.HitNormal);
		if (!input.Landing.PredictionComplete
			|| input.Landing.CollisionKind != FallCollisionKind::StaticWorld
			|| !IsFinite(input.Landing.HitNormal)
			|| normalLengthSquared < 0.99f || normalLengthSquared > 1.01f
			|| input.Landing.HitNormal.z < input.Contact.WalkableNormalZ
			|| !std::isfinite(input.Landing.ImpactVelocityZ)
			|| input.Landing.ImpactVelocityZ > 0.0f
			|| !std::isfinite(input.JumpZ) || input.JumpZ <= 0.0f
			|| !std::isfinite(input.CurrentHealth) || input.CurrentHealth <= 0.0f
			|| !std::isfinite(input.MinimumHealthAfterLanding)
			|| input.MinimumHealthAfterLanding < 0.0f
			|| input.MinimumHealthAfterLanding > input.CurrentHealth)
			return result;

		result.LandingSafetyKnown = true;
		result.PredictedLandingDamage = EstimateFallingDamage(
			input.Landing.ImpactVelocityZ, input.JumpZ, input.FallingDamageDisabled);
		if (result.PredictedLandingDamage
			> input.CurrentHealth - input.MinimumHealthAfterLanding)
			result.Decision = FallingTwoPlaneSafetyDecision::RejectUnsafeLanding;
		else
		{
			result.Decision = FallingTwoPlaneSafetyDecision::ProbeCreaseSweep;
			result.SweepDelta = contact.CreaseSweepDelta;
		}
		return result;
	}

	HorizontalCornerEscapeCandidate BuildHorizontalCornerEscapeCandidate(
		const HorizontalCornerEscapeInput& input)
	{
		HorizontalCornerEscapeCandidate result;
		if (ResolveFallingTwoPlaneContact(input.Contact).Decision
			!= FallingTwoPlaneContactDecision::ProbeCreaseSweep
			|| !std::isfinite(input.SweepDistance) || input.SweepDistance <= 0.0f
			|| !std::isfinite(input.MaximumSweepDistance) || input.MaximumSweepDistance <= 0.0f
			|| input.SweepDistance > input.MaximumSweepDistance
			|| !std::isfinite(input.MinimumHorizontalNormalMagnitude)
			|| input.MinimumHorizontalNormalMagnitude <= 0.0f
			|| input.MinimumHorizontalNormalMagnitude > 1.0f
			|| !std::isfinite(input.MinimumBisectorMagnitude)
			|| input.MinimumBisectorMagnitude <= 0.0f
			|| input.MinimumBisectorMagnitude > 2.0f)
			return result;

		vec2 firstHorizontal = input.Contact.FirstHitNormal.xy();
		vec2 secondHorizontal = input.Contact.SecondHitNormal.xy();
		const float firstMagnitudeSquared = dot(firstHorizontal, firstHorizontal);
		const float secondMagnitudeSquared = dot(secondHorizontal, secondHorizontal);
		const float minimumHorizontalSquared = input.MinimumHorizontalNormalMagnitude
			* input.MinimumHorizontalNormalMagnitude;
		if (firstMagnitudeSquared < minimumHorizontalSquared
			|| secondMagnitudeSquared < minimumHorizontalSquared)
			return result;

		firstHorizontal = normalize(firstHorizontal);
		secondHorizontal = normalize(secondHorizontal);
		const vec2 outwardBisector = firstHorizontal + secondHorizontal;
		const float bisectorMagnitudeSquared = dot(outwardBisector, outwardBisector);
		if (!std::isfinite(bisectorMagnitudeSquared)
			|| bisectorMagnitudeSquared < input.MinimumBisectorMagnitude
				* input.MinimumBisectorMagnitude)
			return result;

		const vec2 horizontalDelta = normalize(outwardBisector) * input.SweepDistance;
		result.SweepDelta = vec3(horizontalDelta, 0.0f);
		result.Valid = IsFinite(result.SweepDelta)
			&& dot(result.SweepDelta, result.SweepDelta) > 0.0f;
		return result;
	}

	FallingTwoPlaneSafetyResult SelectHorizontalCornerEscape(
		const HorizontalCornerEscapeCandidate& candidate,
		const FallingRecoveryAuthorizationEvidence& evidence)
	{
		FallingTwoPlaneSafetyResult result;
		if (!candidate.Valid || !IsFinite(candidate.SweepDelta)
			|| dot(candidate.SweepDelta, candidate.SweepDelta) <= 0.0f
			|| candidate.SweepDelta.z != 0.0f
			|| !HasAuthorizedRecoveryEvidence(evidence))
			return result;

		result.Decision = FallingTwoPlaneSafetyDecision::ProbeHorizontalCornerEscape;
		result.SweepDelta = candidate.SweepDelta;
		return result;
	}
}
