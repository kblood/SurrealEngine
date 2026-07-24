#include "UObject/PawnFallingTwoPlaneContact.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static bool Near(float left, float right, float tolerance = 0.0001f)
{
	return std::abs(left - right) < tolerance;
}

static PawnMovement::FallingTwoPlaneContactInput DantePerpendicularSeamInput()
{
	return {
		.RequestedRemainingDelta = vec3(-6.0f, 4.0f, -24.0f),
		.ActualDisplacement = vec3(0.0f),
		.FirstHitNormal = vec3(1.0f, 0.0f, 0.0f),
		.SecondHitNormal = vec3(0.0f, 1.0f, 0.0f),
		.AutonomousPlayerBot = true,
		.NormalDownwardGravity = true,
		.MaximumActualDisplacement = 0.25f,
		.MaximumRequestedDelta = 64.0f,
		.MinimumPlaneCrossMagnitude = 0.05f,
		.MinimumCreaseDelta = 0.25f
	};
}

static void TestDantePerpendicularSeamProjectsDownward()
{
	const auto result = PawnMovement::ResolveFallingTwoPlaneContact(
		DantePerpendicularSeamInput());
	Check(result.Decision == PawnMovement::FallingTwoPlaneContactDecision::ProbeCreaseSweep,
		"Dante-like zero-progress contact with two perpendicular walls requests one crease probe");
	Check(Near(result.CreaseSweepDelta.x, 0.0f)
		&& Near(result.CreaseSweepDelta.y, 0.0f)
		&& Near(result.CreaseSweepDelta.z, -24.0f),
		"the crease projection preserves the requested downward direction");
}

static void TestEligibilityAndGravityAreRequired()
{
	auto input = DantePerpendicularSeamInput();
	input.AutonomousPlayerBot = false;
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"human and non-player actors fail open");
	input.AutonomousPlayerBot = true;
	input.NormalDownwardGravity = false;
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"custom or upward gravity fails open");
}

static void TestWalkableAndCollinearPlanesFailOpen()
{
	auto input = DantePerpendicularSeamInput();
	input.SecondHitNormal = vec3(0.0f, 0.0f, 1.0f);
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"a walkable second hit stays on the existing landing path");
	input = DantePerpendicularSeamInput();
	input.SecondHitNormal = input.FirstHitNormal;
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"parallel planes do not define a stable crease");
	input.SecondHitNormal = -input.FirstHitNormal;
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"opposed planes do not define a stable crease");
	input.SecondHitNormal = normalize(vec3(1.0f, 0.001f, 0.0f));
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"near-collinear planes below the configured cross threshold fail open");
}

static void TestProgressingSarenaSlideIsUnchanged()
{
	auto input = DantePerpendicularSeamInput();
	input.ActualDisplacement = vec3(-5.257f, 5.257f, -18.073f);
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"Sarena-like wall contact with substantial fall progress does not request recovery");
	input.ActualDisplacement = vec3(0.251f, 0.0f, 0.0f);
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"movement beyond the configured negligible-displacement bound fails open");
}

static void TestRequestedDeltaMustBeFiniteBoundedAndMeaningful()
{
	auto input = DantePerpendicularSeamInput();
	input.RequestedRemainingDelta = vec3(0.0f, 0.0f, -65.0f);
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"an over-bound requested delta fails open");
	input.RequestedRemainingDelta = vec3(2.0f, 3.0f, 0.0f);
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"a requested delta with zero crease projection fails open");
	input.RequestedRemainingDelta = vec3(2.0f, 3.0f, -0.24f);
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"a crease projection below the configured meaningful distance fails open");
	input.RequestedRemainingDelta.x = std::numeric_limits<float>::quiet_NaN();
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"a non-finite requested delta fails open");
}

static void TestInvalidNormalsAndConfigurationFailOpen()
{
	auto input = DantePerpendicularSeamInput();
	input.FirstHitNormal = vec3(2.0f, 0.0f, 0.0f);
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"a non-unit collision normal fails open");
	input = DantePerpendicularSeamInput();
	input.SecondHitNormal.y = std::numeric_limits<float>::quiet_NaN();
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"a non-finite collision normal fails open");
	input = DantePerpendicularSeamInput();
	input.MaximumRequestedDelta = 0.0f;
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"an invalid bound fails open");
	input = DantePerpendicularSeamInput();
	input.ActualDisplacement.x = std::numeric_limits<float>::infinity();
	Check(PawnMovement::ResolveFallingTwoPlaneContact(input).Decision
		== PawnMovement::FallingTwoPlaneContactDecision::Unknown,
		"non-finite observed displacement fails open");
}

static PawnMovement::FallingSeamReplanInput DanteSeamReplanInput()
{
	PawnMovement::FallingSeamReplanInput input;
	input.Contact = DantePerpendicularSeamInput();
	input.Acceleration = vec3(-120.0f, -80.0f, 15.0f);
	input.StockAutonomousAuthorityBot = true;
	input.ActiveMovementLatent = true;
	input.FirstHitStaticWorld = true;
	input.SecondHitStaticWorld = true;
	input.StartZoneKnown = true;
	return input;
}

static void TestEligibleSeamExpiresMovementAndClearsOnlyInwardAcceleration()
{
	const auto result = PawnMovement::EvaluateFallingSeamReplan(
		DanteSeamReplanInput());
	Check(result.Decision == PawnMovement::FallingSeamReplanDecision::ExpireMovementLatent,
		"an eligible no-translation static seam expires the active movement request");
	Check(Near(result.Acceleration.x, 0.0f) && Near(result.Acceleration.y, 0.0f)
		&& Near(result.Acceleration.z, 15.0f),
		"only acceleration into the two wall planes is removed");

	auto tangential = DanteSeamReplanInput();
	tangential.Acceleration = vec3(25.0f, 10.0f, -7.0f);
	const auto unchanged = PawnMovement::EvaluateFallingSeamReplan(tangential);
	Check(unchanged.Decision == PawnMovement::FallingSeamReplanDecision::ExpireMovementLatent
		&& unchanged.Acceleration == tangential.Acceleration,
		"outward and crease-tangential acceleration is preserved exactly");
}

static void TestSeamReplanRequiresNarrowKnownContext()
{
	auto input = DanteSeamReplanInput();
	input.ActiveMovementLatent = false;
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"a pawn without an active movement latent is unchanged");
	input = DanteSeamReplanInput();
	input.StockAutonomousAuthorityBot = false;
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"non-stock, human, and non-authority pawns are unchanged");
	input = DanteSeamReplanInput();
	input.FirstHitStaticWorld = false;
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"a dynamic first contact is unchanged");
	input = DanteSeamReplanInput();
	input.SecondHitStaticWorld = false;
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"a dynamic second contact is unchanged");
	input = DanteSeamReplanInput();
	input.StartZoneKnown = false;
	const auto unknownZone = PawnMovement::EvaluateFallingSeamReplan(input);
	Check(unknownZone.Decision == PawnMovement::FallingSeamReplanDecision::Unknown
		&& unknownZone.Acceleration == input.Acceleration,
		"unknown start-zone evidence fails open");
	input = DanteSeamReplanInput();
	input.StartedInPainZone = true;
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"a pain-zone start is unchanged");
	input = DanteSeamReplanInput();
	input.StartedInWaterZone = true;
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"a water-zone start is unchanged");
}

static void TestSeamReplanRequiresDistinctNearVerticalNoTranslationContact()
{
	auto input = DanteSeamReplanInput();
	input.Contact.FirstHitNormal = normalize(vec3(1.0f, 0.0f, 0.11f));
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"a wall normal outside the narrow vertical tolerance is unchanged");
	input = DanteSeamReplanInput();
	input.Contact.SecondHitNormal = input.Contact.FirstHitNormal;
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"one repeated plane is not a two-plane seam");
	input = DanteSeamReplanInput();
	input.Contact.ActualDisplacement = vec3(0.251f, 0.0f, 0.0f);
	Check(PawnMovement::EvaluateFallingSeamReplan(input).Decision
		== PawnMovement::FallingSeamReplanDecision::Unknown,
		"a translating wall slide is unchanged");
}

int main()
{
	TestDantePerpendicularSeamProjectsDownward();
	TestEligibilityAndGravityAreRequired();
	TestWalkableAndCollinearPlanesFailOpen();
	TestProgressingSarenaSlideIsUnchanged();
	TestRequestedDeltaMustBeFiniteBoundedAndMeaningful();
	TestInvalidNormalsAndConfigurationFailOpen();
	TestEligibleSeamExpiresMovementAndClearsOnlyInwardAcceleration();
	TestSeamReplanRequiresNarrowKnownContext();
	TestSeamReplanRequiresDistinctNearVerticalNoTranslationContact();
	if (Failures == 0)
		std::cout << "Pawn falling two-plane contact tests passed\n";
	return Failures == 0 ? 0 : 1;
}
