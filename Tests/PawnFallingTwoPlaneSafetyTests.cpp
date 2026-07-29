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

static bool Near(float left, float right, float tolerance = 0.0001f)
{
	return std::abs(left - right) <= tolerance;
}

static bool Near(const vec3& left, const vec3& right, float tolerance = 0.0001f)
{
	return Near(left.x, right.x, tolerance)
		&& Near(left.y, right.y, tolerance)
		&& Near(left.z, right.z, tolerance);
}

static void CheckCandidateDistances(
	const PawnMovement::HorizontalCornerEscapeCandidates& candidates,
	const std::string& scenario)
{
	Check(candidates.Count <= candidates.Candidates.size(),
		scenario + " produces at most three candidates");
	for (size_t index = 0; index < candidates.Count; index++)
	{
		const auto& candidate = candidates.Candidates[index];
		Check(candidate.Valid && std::isfinite(candidate.SweepDelta.x)
			&& std::isfinite(candidate.SweepDelta.y)
			&& candidate.SweepDelta.z == 0.0f
			&& Near(length(candidate.SweepDelta), 24.0f),
			scenario + " candidate " + std::to_string(index) + " is a finite bounded 24uu sweep");
	}
}

static void TestHorizontalCornerEscapeCandidateSequence()
{
	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = VerticalSeamInput().Contact;
	const auto perpendicular = PawnMovement::BuildHorizontalCornerEscapeCandidates(input);
	Check(perpendicular.Count == 3,
		"perpendicular walls produce the bisector and both individual wall-normal candidates");
	const float diagonalComponent = 24.0f / std::sqrt(2.0f);
	Check(Near(perpendicular.Candidates[0].SweepDelta,
		vec3(diagonalComponent, diagonalComponent, 0.0f)),
		"the outward bisector is always the first perpendicular-wall candidate");
	Check(Near(perpendicular.Candidates[1].SweepDelta, vec3(0.0f, 24.0f, 0.0f))
		&& Near(perpendicular.Candidates[2].SweepDelta, vec3(24.0f, 0.0f, 0.0f)),
		"individual normals follow the bisector in lexicographic direction order");
	CheckCandidateDistances(perpendicular, "perpendicular walls");

	input.Contact.FirstHitNormal = vec3(1.0f, 0.0f, 0.0f);
	input.Contact.SecondHitNormal = vec3(0.8f, 0.6f, 0.0f);
	const auto acute = PawnMovement::BuildHorizontalCornerEscapeCandidates(input);
	Check(acute.Count == 3, "acute walls retain three distinct escape directions");
	Check(Near(acute.Candidates[0].SweepDelta,
		vec3(normalize(vec2(1.8f, 0.6f)) * 24.0f, 0.0f)),
		"an acute corner still starts with its normalized outward bisector");
	Check(Near(acute.Candidates[1].SweepDelta, vec3(19.2f, 14.4f, 0.0f))
		&& Near(acute.Candidates[2].SweepDelta, vec3(24.0f, 0.0f, 0.0f)),
		"acute individual normals use the same stable ordering");
	CheckCandidateDistances(acute, "acute walls");

	input.Contact.SecondHitNormal = normalize(vec3(-1.0f, 0.075f, 0.0f));
	const auto nearOpposed = PawnMovement::BuildHorizontalCornerEscapeCandidates(input);
	Check(nearOpposed.Count == 2,
		"near-opposed walls omit the unstable bisector but retain both wall-normal alternatives");
	Check(nearOpposed.Candidates[0].SweepDelta.x < 0.0f
		&& Near(nearOpposed.Candidates[1].SweepDelta, vec3(24.0f, 0.0f, 0.0f)),
		"near-opposed alternatives remain in stable direction order");
	CheckCandidateDistances(nearOpposed, "near-opposed walls");

	input.Contact.SecondHitNormal = input.Contact.FirstHitNormal;
	const auto duplicate = PawnMovement::BuildHorizontalCornerEscapeCandidates(input);
	Check(duplicate.Count == 1
		&& Near(duplicate.Candidates[0].SweepDelta, vec3(24.0f, 0.0f, 0.0f)),
		"duplicate walls collapse the bisector and individual normals into one direction");
	CheckCandidateDistances(duplicate, "duplicate walls");
}

static void TestHorizontalCornerEscapeCandidateOrderIsCallbackInvariant()
{
	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = VerticalSeamInput().Contact;
	input.Contact.FirstHitNormal = vec3(0.8f, 0.6f, 0.0f);
	input.Contact.SecondHitNormal = vec3(1.0f, 0.0f, 0.0f);
	const auto forward = PawnMovement::BuildHorizontalCornerEscapeCandidates(input);
	std::swap(input.Contact.FirstHitNormal, input.Contact.SecondHitNormal);
	const auto reversed = PawnMovement::BuildHorizontalCornerEscapeCandidates(input);
	Check(forward.Count == reversed.Count, "swapped callbacks preserve the candidate count");
	for (size_t index = 0; index < forward.Count && index < reversed.Count; index++)
	{
		Check(forward.Candidates[index].Valid == reversed.Candidates[index].Valid
			&& forward.Candidates[index].SweepDelta == reversed.Candidates[index].SweepDelta,
			"swapped callbacks preserve candidate " + std::to_string(index) + " exactly");
	}
}

static void TestHorizontalCornerEscapeCandidateGenerationFailsClosed()
{
	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = VerticalSeamInput().Contact;
	input.Contact.FirstHitNormal = normalize(vec3(0.2f, 0.0f, -0.9797959f));
	Check(PawnMovement::BuildHorizontalCornerEscapeCandidates(input).Count == 0,
		"a low-horizontal contact produces no candidate list");

	input.Contact = VerticalSeamInput().Contact;
	input.SweepDistance = 65.0f;
	Check(PawnMovement::BuildHorizontalCornerEscapeCandidates(input).Count == 0,
		"an over-bound sweep produces no candidates");
	input = {};
	input.Contact = VerticalSeamInput().Contact;
	input.Contact.FirstHitNormal.x = std::numeric_limits<float>::quiet_NaN();
	Check(PawnMovement::BuildHorizontalCornerEscapeCandidates(input).Count == 0,
		"a non-finite wall normal produces no candidates");
	input = {};
	input.Contact = VerticalSeamInput().Contact;
	input.Contact.AutonomousPlayerBot = false;
	Check(PawnMovement::BuildHorizontalCornerEscapeCandidates(input).Count == 0,
		"an ineligible contact produces no candidates");
	input = {};
	input.Contact = VerticalSeamInput().Contact;
	input.DuplicateDirectionTolerance = std::numeric_limits<float>::infinity();
	Check(PawnMovement::BuildHorizontalCornerEscapeCandidates(input).Count == 0,
		"an invalid direction tolerance produces no candidates");
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
	evidence.PainResultKnown = false;
	Check(PawnMovement::SelectHorizontalCornerEscape(candidate, evidence).Decision
		== PawnMovement::FallingTwoPlaneSafetyDecision::Unknown,
		"unknown pain-zone evidence selects no corner recovery");
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
	input = {};
	input.Contact = VerticalSeamInput().Contact;
	input.Contact.SecondHitNormal = normalize(vec3(-1.0f, 0.075f, 0.0f));
	Check(!PawnMovement::BuildHorizontalCornerEscapeCandidate(input).Valid,
		"a near-opposed pair below the bisector threshold is rejected");
	input = {};
	input.Contact = VerticalSeamInput().Contact;
	input.Contact.FirstHitNormal = normalize(vec3(0.2f, 0.0f, -0.9797959f));
	Check(!PawnMovement::BuildHorizontalCornerEscapeCandidate(input).Valid,
		"a contact without enough horizontal wall normal is rejected");
	input = {};
	input.Contact = VerticalSeamInput().Contact;
	input.SweepDistance = 65.0f;
	Check(!PawnMovement::BuildHorizontalCornerEscapeCandidate(input).Valid,
		"an over-bound horizontal sweep is rejected");
}

static void TestHorizontalCornerEscapeShadowClassificationIsDisjoint()
{
	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = VerticalSeamInput().Contact;
	const auto candidate = PawnMovement::BuildHorizontalCornerEscapeCandidate(input);
	auto evidence = AuthorizedEscapeEvidence();
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::Authorized,
		"complete supported evidence is classified as an authorized shadow escape");

	evidence.SweepClear = false;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::UnknownOrUnsafeSupport,
		"a blocked dry-run sweep is classified as unknown or unsafe support");
	evidence = AuthorizedEscapeEvidence();
	evidence.WalkableShortSupport = false;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::UnknownOrUnsafeSupport,
		"missing walkable support is classified before target progress");
	evidence = AuthorizedEscapeEvidence();
	evidence.SupportInPainZone = true;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::UnknownOrUnsafeSupport,
		"pain support is classified as unsafe");
	evidence = AuthorizedEscapeEvidence();
	evidence.PainResultKnown = false;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::UnknownOrUnsafeSupport,
		"unknown pain evidence is classified as unknown support");

	evidence = AuthorizedEscapeEvidence();
	evidence.TargetProgressKnown = false;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::TargetProgressRejected,
		"unknown target progress rejects an otherwise supported candidate");
	evidence.TargetProgressKnown = true;
	evidence.TargetProgress = 0.0f;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::TargetProgressRejected,
		"non-positive target progress rejects an otherwise supported candidate");
	evidence.TargetProgress = std::numeric_limits<float>::quiet_NaN();
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence)
		== PawnMovement::HorizontalCornerEscapeShadowClassification::TargetProgressRejected,
		"non-finite target progress rejects an otherwise supported candidate");

	PawnMovement::HorizontalCornerEscapeCandidate invalid;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(invalid, AuthorizedEscapeEvidence())
		== PawnMovement::HorizontalCornerEscapeShadowClassification::CandidateInvalid,
		"invalid geometry is distinct from a candidate probe rejection");
	invalid.Valid = true;
	invalid.SweepDelta = vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(invalid, AuthorizedEscapeEvidence())
		== PawnMovement::HorizontalCornerEscapeShadowClassification::CandidateInvalid,
		"a non-finite valid-marked candidate remains structurally invalid");
	invalid.SweepDelta = vec3(0.0f);
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(invalid, AuthorizedEscapeEvidence())
		== PawnMovement::HorizontalCornerEscapeShadowClassification::CandidateInvalid,
		"a zero valid-marked candidate remains structurally invalid");
	invalid.SweepDelta = vec3(1.0f, 0.0f, 1.0f);
	Check(PawnMovement::ClassifyHorizontalCornerEscapeShadow(invalid, AuthorizedEscapeEvidence())
		== PawnMovement::HorizontalCornerEscapeShadowClassification::CandidateInvalid,
		"a vertical valid-marked candidate remains structurally invalid");
}

static void TestFallingSeamEpisodesAreSpatialAndPairStable()
{
	PawnMovement::FallingSeamEpisodeState state;
	PawnMovement::FallingSeamEpisodeObservation observation;
	observation.Eligible = true;
	observation.Position = vec2(10.0f, 20.0f);
	observation.FirstNormal = vec3(1.0f, 0.0f, 0.0f);
	observation.SecondNormal = vec3(0.0f, 1.0f, 0.0f);
	auto update = PawnMovement::UpdateFallingSeamEpisode(state, observation);
	Check(update.Started && update.State.Active, "the first eligible seam starts an episode");
	state = update.State;
	state.AuthorizationCounted = true;
	std::swap(observation.FirstNormal, observation.SecondNormal);
	observation.Position = vec2(18.0f, 20.0f);
	update = PawnMovement::UpdateFallingSeamEpisode(state, observation);
	Check(!update.Started && update.State.AuthorizationCounted,
		"callback order and an eight-unit XY displacement preserve the episode");
	observation.Position = vec2(18.01f, 20.0f);
	update = PawnMovement::UpdateFallingSeamEpisode(state, observation);
	Check(update.Started && !update.State.AuthorizationCounted,
		"an XY displacement beyond the episode anchor starts a fresh episode");
	state = update.State;
	observation.Position = state.Anchor;
	observation.SecondNormal = vec3(-1.0f, 0.0f, 0.0f);
	update = PawnMovement::UpdateFallingSeamEpisode(state, observation);
	Check(update.Started, "a different unordered wall-normal pair starts a fresh episode");
	update = PawnMovement::UpdateFallingSeamEpisode(update.State, {});
	Check(!update.Started && !update.State.Active,
		"an ineligible observation resets the per-life episode state");
}

static void TestDetailedHorizontalCornerClassificationsAreDisjoint()
{
	using Classification = PawnMovement::HorizontalCornerEscapeDetailedClassification;
	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = VerticalSeamInput().Contact;
	const auto candidate = PawnMovement::BuildHorizontalCornerEscapeCandidate(input);
	auto evidence = AuthorizedEscapeEvidence();
	Check(PawnMovement::ClassifyHorizontalCornerEscapeDetailed(candidate, evidence, true)
		== Classification::Authorized, "positive target progress authorizes a detailed candidate");
	evidence.SweepClear = false;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeDetailed(candidate, evidence, true)
		== Classification::BlockedSweep, "a blocked sweep has a distinct detailed outcome");
	evidence = AuthorizedEscapeEvidence();
	evidence.WalkableShortSupport = false;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeDetailed(candidate, evidence, true)
		== Classification::NoStaticWalkableSupport,
		"missing static walkable support has a distinct detailed outcome");
	evidence = AuthorizedEscapeEvidence();
	evidence.SupportInPainZone = true;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeDetailed(candidate, evidence, true)
		== Classification::PainSupport, "pain support has a distinct detailed outcome");
	evidence = AuthorizedEscapeEvidence();
	Check(PawnMovement::ClassifyHorizontalCornerEscapeDetailed(candidate, evidence, false)
		== Classification::NoActiveMovementIntentOrTarget,
		"Sleep or a missing required target is not mislabeled as target regression");
	evidence.TargetProgress = 0.0f;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeDetailed(candidate, evidence, true)
		== Classification::TrueTargetRegression,
		"known non-positive progress is the only true target regression");
	evidence = AuthorizedEscapeEvidence();
	evidence.TargetProgressKnown = false;
	Check(PawnMovement::ClassifyHorizontalCornerEscapeDetailed(candidate, evidence, true)
		== Classification::UnknownEvidence,
		"missing evidence during active movement remains explicitly unknown");
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
	TestHorizontalCornerEscapeCandidateSequence();
	TestHorizontalCornerEscapeCandidateOrderIsCallbackInvariant();
	TestHorizontalCornerEscapeCandidateGenerationFailsClosed();
	TestSupportedHorizontalCornerEscapeIsSelected();
	TestHorizontalCornerEscapeRequiresCompleteEvidence();
	TestHorizontalCornerEscapeShadowClassificationIsDisjoint();
	TestFallingSeamEpisodesAreSpatialAndPairStable();
	TestDetailedHorizontalCornerClassificationsAreDisjoint();
	if (Failures == 0)
		std::cout << "Pawn falling two-plane safety tests passed\n";
	return Failures == 0 ? 0 : 1;
}
