#pragma once

#include "PawnFallingTwoPlaneContact.h"
#include "PawnPainZoneFallPrediction.h"

namespace PawnMovement
{
	enum class FallingTwoPlaneSafetyDecision
	{
		Unknown,
		ProbeCreaseSweep,
		ProbeHorizontalCornerEscape,
		RejectTargetRegression,
		RejectUnsafeLanding
	};

	struct FallingRecoveryAuthorizationEvidence
	{
		bool SweepResultKnown = false;
		bool SweepClear = false;
		bool SupportResultKnown = false;
		bool WalkableShortSupport = false;
		bool PainResultKnown = false;
		bool SupportInPainZone = false;
		bool TargetProgressKnown = false;
		float TargetProgress = 0.0f;
	};

	struct FallingTwoPlaneLandingEvidence
	{
		bool PredictionComplete = false;
		FallCollisionKind CollisionKind = FallCollisionKind::DynamicActor;
		vec3 HitNormal = vec3(0.0f);
		float ImpactVelocityZ = 0.0f;
	};

	struct FallingTwoPlaneSafetyInput
	{
		FallingTwoPlaneContactInput Contact;
		FallingRecoveryAuthorizationEvidence Recovery;
		FallingTwoPlaneLandingEvidence Landing;
		float JumpZ = 0.0f;
		float CurrentHealth = 0.0f;
		float MinimumHealthAfterLanding = 1.0f;
		float MinimumVerticalCreaseRatio = 0.9f;
		bool FallingDamageDisabled = false;
	};

	struct FallingTwoPlaneSafetyResult
	{
		FallingTwoPlaneSafetyDecision Decision = FallingTwoPlaneSafetyDecision::Unknown;
		vec3 SweepDelta = vec3(0.0f);
		float PredictedLandingDamage = 0.0f;
		bool LandingSafetyKnown = false;
	};

	FallingTwoPlaneSafetyResult EvaluateFallingTwoPlaneSafety(
		const FallingTwoPlaneSafetyInput& input);

	struct HorizontalCornerEscapeInput
	{
		FallingTwoPlaneContactInput Contact;
		float SweepDistance = 24.0f;
		float MaximumSweepDistance = 64.0f;
		float MinimumHorizontalNormalMagnitude = 0.25f;
		float MinimumBisectorMagnitude = 0.1f;
	};

	struct HorizontalCornerEscapeCandidate
	{
		bool Valid = false;
		vec3 SweepDelta = vec3(0.0f);
	};

	HorizontalCornerEscapeCandidate BuildHorizontalCornerEscapeCandidate(
		const HorizontalCornerEscapeInput& input);
	FallingTwoPlaneSafetyResult SelectHorizontalCornerEscape(
		const HorizontalCornerEscapeCandidate& candidate,
		const FallingRecoveryAuthorizationEvidence& evidence);
}
