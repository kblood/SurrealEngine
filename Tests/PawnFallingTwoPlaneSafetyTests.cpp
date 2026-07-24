#include "UObject/PawnFallingTwoPlaneSafety.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static PawnMovement::FallingTwoPlaneSafetyInput VerticalSeamInput()
{
	PawnMovement::FallingTwoPlaneSafetyInput input;
	input.Contact.RequestedRemainingDelta = vec3(-6.0f, 4.0f, -24.0f);
	input.Contact.ActualDisplacement = vec3(0.0f);
	input.Contact.FirstHitNormal = vec3(1.0f, 0.0f, 0.0f);
	input.Contact.SecondHitNormal = vec3(0.0f, 1.0f, 0.0f);
	input.Contact.AutonomousPlayerBot = true;
	input.Contact.NormalDownwardGravity = true;
	input.Contact.MaximumRequestedDelta = 64.0f;
	input.Recovery.SweepResultKnown = true;
	input.Recovery.SweepClear = true;
	input.Recovery.SupportResultKnown = true;
	input.Recovery.WalkableShortSupport = true;
	input.Recovery.PainResultKnown = true;
	input.Recovery.SupportInPainZone = false;
	input.Recovery.TargetProgressKnown = true;
	input.Recovery.TargetProgress = 12.0f;
	input.Landing.PredictionComplete = true;
	input.Landing.CollisionKind = PawnMovement::FallCollisionKind::StaticWorld;
	input.Landing.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	input.JumpZ = 325.0f;
	input.CurrentHealth = 100.0f;
	return input;
}

static float PredictImpactVelocity(float startZ, float landingZ, float gravityZ)
{
	PawnMovement::FallPredictionState state = {
		.Location = vec3(0.0f, 0.0f, startZ),
		.Velocity = vec3(0.0f)
	};
	const PawnMovement::FallPredictionStep step = {
		.Gravity = vec3(0.0f, 0.0f, gravityZ),
		.GroundSpeed = 400.0f,
		.TerminalVelocity = 2500.0f,
		.Elapsed = 1.0f / 16.0f
	};
	for (int index = 0; index < 64 && state.Location.z > landingZ; index++)
	{
		state = PawnMovement::PredictFallStep(state, step);
		if (!state.Valid)
			return std::numeric_limits<float>::quiet_NaN();
	}
	return state.Location.z <= landingZ
		? state.Velocity.z
		: std::numeric_limits<float>::quiet_NaN();
}

static void TestDeathFanDanteAndAshCreasesRequireTargetProgress()
{
	auto input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -281.415253f;
	input.Recovery.TargetProgress = -8.0f;
	auto result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::RejectTargetRegression,
		"Ash's low-speed downward DeathFan crease is rejected when it regresses the live target");
	Check(!result.LandingSafetyKnown && result.SweepDelta == vec3(0.0f),
		"target regression rejects Ash's recovery before fall speed can authorize movement");

	input.Landing.ImpactVelocityZ = -2500.0f;
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::RejectTargetRegression,
		"Dante's high-speed downward DeathFan crease is also rejected when it regresses the target");
	Check(!result.LandingSafetyKnown && result.SweepDelta == vec3(0.0f),
		"the target-progress gate is independent of Dante's high fall speed");
}

static void TestDeathFanLikeLethalDropIsRejected()
{
	auto input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = PredictImpactVelocity(1384.0f, -1350.0f, -1045.0f);
	const auto result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(input.Landing.ImpactVelocityZ < -1975.0f,
		"the bounded DeathFan-like trajectory reaches the stock lethal velocity branch");
	Check(result.LandingSafetyKnown,
		"a complete static walkable landing supplies conclusive safety evidence");
	Check(result.PredictedLandingDamage == 1000.0f,
		"the stock pawn lethal falling-damage branch is preserved");
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::RejectUnsafeLanding,
		"the vertical crease recovery is rejected before a DeathFan-like lethal landing");
}

static void TestNearbySupportedLandingIsAllowed()
{
	auto input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = PredictImpactVelocity(1384.0f, 1280.0f, -1045.0f);
	const auto result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(input.Landing.ImpactVelocityZ > -750.0f - input.JumpZ,
		"the nearby landing stays above the stock falling-damage threshold");
	Check(result.LandingSafetyKnown && result.PredictedLandingDamage == 0.0f,
		"the nearby static floor is proven safe");
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::ProbeCreaseSweep,
		"a safe nearby landing preserves the proposed crease recovery");
}

static void TestDamagingLandingRespectsHealthReserve()
{
	auto input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -1300.0f;
	input.CurrentHealth = 35.0f;
	input.MinimumHealthAfterLanding = 10.0f;
	const auto result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.PredictedLandingDamage > 25.0f,
		"the scaled stock falling-damage branch predicts a material health loss");
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::RejectUnsafeLanding,
		"a nonlethal landing is still rejected when it consumes the configured health reserve");
	input.FallingDamageDisabled = true;
	Check(PawnMovement::EvaluateFallingTwoPlaneSafety(input).Decision
		== PawnMovement::FallingTwoPlaneSafetyDecision::ProbeCreaseSweep,
		"a game-provided falling-damage immunity keeps the supported landing available");
}

static void TestInsufficientCollisionEvidenceUsesStockBehavior()
{
	auto input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -2500.0f;
	input.Landing.PredictionComplete = false;
	auto result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(!result.LandingSafetyKnown && result.SweepDelta == vec3(0.0f)
		&& result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"an incomplete forecast selects no recovery and leaves stock falling behavior active");
	input.Landing.PredictionComplete = true;
	input.Landing.CollisionKind = PawnMovement::FallCollisionKind::Mover;
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(!result.LandingSafetyKnown
		&& result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"dynamic mover support selects no recovery");
	input.Landing.CollisionKind = PawnMovement::FallCollisionKind::StaticWorld;
	input.Landing.HitNormal = vec3(1.0f, 0.0f, 0.0f);
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(!result.LandingSafetyKnown
		&& result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"a non-walkable terminal collision selects no recovery");
	input.Landing.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	input.Landing.ImpactVelocityZ = std::numeric_limits<float>::quiet_NaN();
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(!result.LandingSafetyKnown
		&& result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"non-finite landing evidence selects no recovery");

	input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -400.0f;
	input.Recovery.SweepResultKnown = false;
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"missing sweep clearance selects no recovery even for a harmless short fall");
	input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -400.0f;
	input.Recovery.WalkableShortSupport = false;
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"missing walkable short support selects no recovery");
	input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -400.0f;
	input.Recovery.SupportInPainZone = true;
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"pain-zone support selects no recovery");
	input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -400.0f;
	input.Recovery.TargetProgressKnown = false;
	result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"missing target-progress evidence selects no recovery");
}

static void TestOnlyEligibleDownwardVerticalCreasesAreVetoed()
{
	auto input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -2500.0f;
	input.Contact.AutonomousPlayerBot = false;
	Check(PawnMovement::EvaluateFallingTwoPlaneSafety(input).Decision
		== PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"an ineligible base contact does not enter the safety layer");
	input = VerticalSeamInput();
	input.Landing.ImpactVelocityZ = -2500.0f;
	input.Contact.FirstHitNormal = vec3(0.0f, 0.0f, -1.0f);
	input.Contact.SecondHitNormal = vec3(0.0f, 1.0f, 0.0f);
	const auto result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::ProbeCreaseSweep,
		"an authorized horizontal crease does not receive a vertical-fall veto");
}

static void TestEveryDownwardCreaseRequiresTargetProgress()
{
	auto input = VerticalSeamInput();
	input.Contact.RequestedRemainingDelta = vec3(0.0f, -6.0f, -24.0f);
	input.Contact.FirstHitNormal = vec3(1.0f, 0.0f, 0.0f);
	input.Contact.SecondHitNormal = vec3(0.0f, 0.8f, -0.6f);
	input.Recovery.TargetProgress = 0.0f;
	input.Landing.ImpactVelocityZ = -200.0f;
	const auto result = PawnMovement::EvaluateFallingTwoPlaneSafety(input);
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::RejectTargetRegression,
		"a diagonal downward crease below the vertical-risk ratio still requires positive progress");
}

static PawnMovement::FallingRecoveryAuthorizationEvidence AuthorizedEscapeEvidence()
{
	PawnMovement::FallingRecoveryAuthorizationEvidence evidence;
	evidence.SweepResultKnown = true;
	evidence.SweepClear = true;
	evidence.SupportResultKnown = true;
	evidence.WalkableShortSupport = true;
	evidence.PainResultKnown = true;
	evidence.TargetProgressKnown = true;
	evidence.TargetProgress = 6.0f;
	return evidence;
}

static void TestSupportedHorizontalCornerEscapeIsSelected()
{
	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = VerticalSeamInput().Contact;
	input.SweepDistance = 24.0f;
	const auto candidate = PawnMovement::BuildHorizontalCornerEscapeCandidate(input);
	Check(candidate.Valid, "two perpendicular DeathFan-like walls produce a horizontal corner candidate");
	const float expectedComponent = 24.0f / std::sqrt(2.0f);
	Check(std::abs(candidate.SweepDelta.x - expectedComponent) < 0.0001f
		&& std::abs(candidate.SweepDelta.y - expectedComponent) < 0.0001f
		&& candidate.SweepDelta.z == 0.0f,
		"the corner escape is the deterministic outward horizontal bisector");
	std::swap(input.Contact.FirstHitNormal, input.Contact.SecondHitNormal);
	const auto reversedCandidate = PawnMovement::BuildHorizontalCornerEscapeCandidate(input);
	Check(reversedCandidate.Valid && reversedCandidate.SweepDelta == candidate.SweepDelta,
		"wall callback order does not change the outward-bisector candidate");
	const auto result = PawnMovement::SelectHorizontalCornerEscape(
		candidate, AuthorizedEscapeEvidence());
	Check(result.Decision == PawnMovement::FallingTwoPlaneSafetyDecision::ProbeHorizontalCornerEscape
		&& result.SweepDelta == candidate.SweepDelta,
		"clear, supported, non-pain, target-progressing evidence selects the horizontal escape");
}

static void TestHorizontalCornerEscapeRequiresCompleteEvidence()
{
	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = VerticalSeamInput().Contact;
	const auto candidate = PawnMovement::BuildHorizontalCornerEscapeCandidate(input);
	auto evidence = AuthorizedEscapeEvidence();
	evidence.SweepClear = false;
	Check(PawnMovement::SelectHorizontalCornerEscape(candidate, evidence).Decision
		== PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"a blocked outward sweep selects no corner recovery");
	evidence = AuthorizedEscapeEvidence();
	evidence.SupportResultKnown = false;
	Check(PawnMovement::SelectHorizontalCornerEscape(candidate, evidence).Decision
		== PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"unknown short support selects no corner recovery");
	evidence = AuthorizedEscapeEvidence();
	evidence.SupportInPainZone = true;
	Check(PawnMovement::SelectHorizontalCornerEscape(candidate, evidence).Decision
		== PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"pain-zone support selects no corner recovery");
	evidence = AuthorizedEscapeEvidence();
	evidence.TargetProgress = 0.0f;
	Check(PawnMovement::SelectHorizontalCornerEscape(candidate, evidence).Decision
		== PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"non-progressing target geometry selects no corner recovery");

	input.Contact.SecondHitNormal = -input.Contact.FirstHitNormal;
	Check(!PawnMovement::BuildHorizontalCornerEscapeCandidate(input).Valid,
		"opposed wall normals have no stable outward bisector");
}

int main()
{
	TestDeathFanDanteAndAshCreasesRequireTargetProgress();
	TestDeathFanLikeLethalDropIsRejected();
	TestNearbySupportedLandingIsAllowed();
	TestDamagingLandingRespectsHealthReserve();
	TestInsufficientCollisionEvidenceUsesStockBehavior();
	TestOnlyEligibleDownwardVerticalCreasesAreVetoed();
	TestEveryDownwardCreaseRequiresTargetProgress();
	TestSupportedHorizontalCornerEscapeIsSelected();
	TestHorizontalCornerEscapeRequiresCompleteEvidence();
	if (Failures == 0)
		std::cout << "Pawn falling two-plane safety tests passed\n";
	return Failures == 0 ? 0 : 1;
}
