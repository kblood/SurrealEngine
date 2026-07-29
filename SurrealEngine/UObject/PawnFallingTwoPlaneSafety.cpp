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

		bool IsUnitNormal(const vec3& value)
		{
			const float lengthSquared = dot(value, value);
			return IsFinite(value) && lengthSquared >= 0.99f && lengthSquared <= 1.01f;
		}

		bool IsValidHorizontalCornerEscapeInput(
			const HorizontalCornerEscapeInput& input)
		{
			if (!input.Contact.AutonomousPlayerBot || !input.Contact.NormalDownwardGravity
				|| !IsFinite(input.Contact.RequestedRemainingDelta)
				|| !IsFinite(input.Contact.ActualDisplacement)
				|| !IsUnitNormal(input.Contact.FirstHitNormal)
				|| !IsUnitNormal(input.Contact.SecondHitNormal)
				|| !std::isfinite(input.Contact.WalkableNormalZ)
				|| input.Contact.WalkableNormalZ <= 0.0f
				|| input.Contact.WalkableNormalZ > 1.0f
				|| input.Contact.FirstHitNormal.z >= input.Contact.WalkableNormalZ
				|| input.Contact.SecondHitNormal.z >= input.Contact.WalkableNormalZ
				|| !std::isfinite(input.Contact.MaximumActualDisplacement)
				|| input.Contact.MaximumActualDisplacement < 0.0f
				|| !std::isfinite(input.Contact.MaximumRequestedDelta)
				|| input.Contact.MaximumRequestedDelta <= 0.0f
				|| !std::isfinite(input.SweepDistance) || input.SweepDistance <= 0.0f
				|| !std::isfinite(input.MaximumSweepDistance)
				|| input.MaximumSweepDistance <= 0.0f
				|| input.SweepDistance > input.MaximumSweepDistance
				|| !std::isfinite(input.MinimumHorizontalNormalMagnitude)
				|| input.MinimumHorizontalNormalMagnitude <= 0.0f
				|| input.MinimumHorizontalNormalMagnitude > 1.0f
				|| !std::isfinite(input.MinimumBisectorMagnitude)
				|| input.MinimumBisectorMagnitude <= 0.0f
				|| input.MinimumBisectorMagnitude > 2.0f
				|| !std::isfinite(input.DuplicateDirectionTolerance)
				|| input.DuplicateDirectionTolerance < 0.0f
				|| input.DuplicateDirectionTolerance >= 2.0f)
				return false;

			const float actualDistanceSquared = dot(
				input.Contact.ActualDisplacement, input.Contact.ActualDisplacement);
			const float requestedDistanceSquared = dot(
				input.Contact.RequestedRemainingDelta, input.Contact.RequestedRemainingDelta);
			return actualDistanceSquared <= input.Contact.MaximumActualDisplacement
					* input.Contact.MaximumActualDisplacement
				&& requestedDistanceSquared > 0.0f
				&& requestedDistanceSquared <= input.Contact.MaximumRequestedDelta
					* input.Contact.MaximumRequestedDelta;
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

		bool IsValidHorizontalCornerEscapeCandidate(
			const HorizontalCornerEscapeCandidate& candidate)
		{
			return candidate.Valid && IsFinite(candidate.SweepDelta)
				&& dot(candidate.SweepDelta, candidate.SweepDelta) > 0.0f
				&& candidate.SweepDelta.z == 0.0f;
		}

		bool NormalLess(const vec3& first, const vec3& second)
		{
			return first.x < second.x
				|| (first.x == second.x && (first.y < second.y
					|| (first.y == second.y && first.z < second.z)));
		}

		void CanonicalizeNormalPair(vec3& first, vec3& second)
		{
			if (NormalLess(second, first))
				std::swap(first, second);
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

	HorizontalCornerEscapeCandidates BuildHorizontalCornerEscapeCandidates(
		const HorizontalCornerEscapeInput& input)
	{
		HorizontalCornerEscapeCandidates result;
		if (!IsValidHorizontalCornerEscapeInput(input))
			return result;

		vec2 firstHorizontal = input.Contact.FirstHitNormal.xy();
		vec2 secondHorizontal = input.Contact.SecondHitNormal.xy();
		const float minimumHorizontalSquared = input.MinimumHorizontalNormalMagnitude
			* input.MinimumHorizontalNormalMagnitude;
		if (dot(firstHorizontal, firstHorizontal) < minimumHorizontalSquared
			|| dot(secondHorizontal, secondHorizontal) < minimumHorizontalSquared)
			return result;

		firstHorizontal = normalize(firstHorizontal);
		secondHorizontal = normalize(secondHorizontal);
		if (secondHorizontal.x < firstHorizontal.x
			|| (secondHorizontal.x == firstHorizontal.x
				&& secondHorizontal.y < firstHorizontal.y))
			std::swap(firstHorizontal, secondHorizontal);

		const float duplicateToleranceSquared = input.DuplicateDirectionTolerance
			* input.DuplicateDirectionTolerance;
		std::array<vec2, 3> acceptedDirections;
		auto appendDirection = [&](const vec2& direction)
		{
			if (!std::isfinite(direction.x) || !std::isfinite(direction.y)
				|| result.Count >= result.Candidates.size())
				return;
			for (size_t index = 0; index < result.Count; index++)
			{
				const vec2 difference = direction - acceptedDirections[index];
				if (dot(difference, difference) <= duplicateToleranceSquared)
					return;
			}

			HorizontalCornerEscapeCandidate& candidate = result.Candidates[result.Count];
			candidate.SweepDelta = vec3(direction * input.SweepDistance, 0.0f);
			candidate.Valid = IsFinite(candidate.SweepDelta)
				&& dot(candidate.SweepDelta, candidate.SweepDelta) > 0.0f
				&& dot(candidate.SweepDelta, candidate.SweepDelta)
					<= input.MaximumSweepDistance * input.MaximumSweepDistance;
			if (candidate.Valid)
			{
				acceptedDirections[result.Count] = direction;
				result.Count++;
			}
			else
				candidate = {};
		};

		const vec2 outwardBisector = firstHorizontal + secondHorizontal;
		const float bisectorMagnitudeSquared = dot(outwardBisector, outwardBisector);
		if (std::isfinite(bisectorMagnitudeSquared)
			&& bisectorMagnitudeSquared >= input.MinimumBisectorMagnitude
				* input.MinimumBisectorMagnitude)
			appendDirection(normalize(outwardBisector));
		appendDirection(firstHorizontal);
		appendDirection(secondHorizontal);
		return result;
	}

	FallingSeamEpisodeUpdate UpdateFallingSeamEpisode(
		const FallingSeamEpisodeState& state,
		const FallingSeamEpisodeObservation& observation)
	{
		FallingSeamEpisodeUpdate result;
		if (!observation.Eligible)
			return result;
		if (!std::isfinite(observation.Position.x)
			|| !std::isfinite(observation.Position.y)
			|| !IsUnitNormal(observation.FirstNormal)
			|| !IsUnitNormal(observation.SecondNormal)
			|| !std::isfinite(observation.MaximumAnchorDistance)
			|| observation.MaximumAnchorDistance < 0.0f
			|| !std::isfinite(observation.MinimumNormalAlignment)
			|| observation.MinimumNormalAlignment < -1.0f
			|| observation.MinimumNormalAlignment > 1.0f)
			return result;

		vec3 firstNormal = observation.FirstNormal;
		vec3 secondNormal = observation.SecondNormal;
		CanonicalizeNormalPair(firstNormal, secondNormal);
		const vec2 anchorDelta = observation.Position - state.Anchor;
		const bool sameAnchor = state.Active
			&& dot(anchorDelta, anchorDelta) <= observation.MaximumAnchorDistance
				* observation.MaximumAnchorDistance;
		const bool samePair = state.Active
			&& dot(firstNormal, state.FirstNormal) >= observation.MinimumNormalAlignment
			&& dot(secondNormal, state.SecondNormal) >= observation.MinimumNormalAlignment;
		if (sameAnchor && samePair)
		{
			result.State = state;
			return result;
		}

		result.State.Active = true;
		result.State.Anchor = observation.Position;
		result.State.FirstNormal = firstNormal;
		result.State.SecondNormal = secondNormal;
		result.Started = true;
		return result;
	}

	FallingTwoPlaneSafetyResult SelectHorizontalCornerEscape(
		const HorizontalCornerEscapeCandidate& candidate,
		const FallingRecoveryAuthorizationEvidence& evidence)
	{
		FallingTwoPlaneSafetyResult result;
		if (!IsValidHorizontalCornerEscapeCandidate(candidate)
			|| !HasAuthorizedRecoveryEvidence(evidence))
			return result;

		result.Decision = FallingTwoPlaneSafetyDecision::ProbeHorizontalCornerEscape;
		result.SweepDelta = candidate.SweepDelta;
		return result;
	}

	HorizontalCornerEscapeShadowClassification ClassifyHorizontalCornerEscapeShadow(
		const HorizontalCornerEscapeCandidate& candidate,
		const FallingRecoveryAuthorizationEvidence& evidence)
	{
		if (!IsValidHorizontalCornerEscapeCandidate(candidate))
			return HorizontalCornerEscapeShadowClassification::CandidateInvalid;

		const FallingTwoPlaneSafetyResult selection = SelectHorizontalCornerEscape(
			candidate, evidence);
		if (selection.Decision
			== FallingTwoPlaneSafetyDecision::ProbeHorizontalCornerEscape)
			return HorizontalCornerEscapeShadowClassification::Authorized;

		if (!evidence.SweepResultKnown || !evidence.SweepClear
			|| !evidence.SupportResultKnown || !evidence.WalkableShortSupport
			|| !evidence.PainResultKnown || evidence.SupportInPainZone)
			return HorizontalCornerEscapeShadowClassification::UnknownOrUnsafeSupport;

		return HorizontalCornerEscapeShadowClassification::TargetProgressRejected;
	}

	HorizontalCornerEscapeDetailedClassification ClassifyHorizontalCornerEscapeDetailed(
		const HorizontalCornerEscapeCandidate& candidate,
		const FallingRecoveryAuthorizationEvidence& evidence,
		bool activeMovementIntentAndTarget)
	{
		if (!IsValidHorizontalCornerEscapeCandidate(candidate))
			return HorizontalCornerEscapeDetailedClassification::CandidateInvalid;
		if (!evidence.SweepResultKnown)
			return HorizontalCornerEscapeDetailedClassification::UnknownEvidence;
		if (!evidence.SweepClear)
			return HorizontalCornerEscapeDetailedClassification::BlockedSweep;
		if (!evidence.SupportResultKnown)
			return HorizontalCornerEscapeDetailedClassification::UnknownEvidence;
		if (!evidence.WalkableShortSupport)
			return HorizontalCornerEscapeDetailedClassification::NoStaticWalkableSupport;
		if (!evidence.PainResultKnown)
			return HorizontalCornerEscapeDetailedClassification::UnknownEvidence;
		if (evidence.SupportInPainZone)
			return HorizontalCornerEscapeDetailedClassification::PainSupport;
		if (!activeMovementIntentAndTarget)
			return HorizontalCornerEscapeDetailedClassification::NoActiveMovementIntentOrTarget;
		if (!evidence.TargetProgressKnown || !std::isfinite(evidence.TargetProgress))
			return HorizontalCornerEscapeDetailedClassification::UnknownEvidence;
		if (evidence.TargetProgress <= 0.0f)
			return HorizontalCornerEscapeDetailedClassification::TrueTargetRegression;
		return HorizontalCornerEscapeDetailedClassification::Authorized;
	}
}
