#include "UObject/PawnPainZoneFallPrediction.h"

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

static void TestStepMatchesWalkingToFallingIntegrator()
{
	PawnMovement::FallPredictionState state = { .Location = vec3(0.0f), .Velocity = vec3(100.0f, 0.0f, 0.0f) };
	PawnMovement::FallPredictionStep step = {
		.Gravity = vec3(0.0f, 0.0f, -950.0f),
		.GroundSpeed = 400.0f,
		.TerminalVelocity = 2000.0f,
		.Elapsed = 0.1f
	};
	const auto result = PawnMovement::PredictFallStep(state, step);
	Check(result.Valid, "finite prediction inputs are accepted");
	Check(Near(result.Velocity.x, 100.0f) && Near(result.Velocity.z, -95.0f), "gravity updates predicted velocity like TickFalling");
	Check(Near(result.Location.x, 10.0f) && Near(result.Location.z, -9.5f), "updated velocity advances the bounded prediction step");
}

static void TestAirControlCannotIncreaseGroundLimitedSpeed()
{
	PawnMovement::FallPredictionState state = { .Velocity = vec3(400.0f, 0.0f, 0.0f) };
	PawnMovement::FallPredictionStep step = {
		.Acceleration = vec3(2048.0f, 0.0f, 0.0f),
		.Gravity = vec3(0.0f, 0.0f, -950.0f),
		.GroundSpeed = 400.0f,
		.TerminalVelocity = 2000.0f,
		.Elapsed = 0.1f
	};
	const auto result = PawnMovement::PredictFallStep(state, step);
	Check(Near(result.Velocity.x, 400.0f), "prediction preserves TickFalling's horizontal ground-speed cap");
}

static void TestPredictionUsesSuppliedWallJumpVelocity()
{
	PawnMovement::FallPredictionState state = {
		.Velocity = vec3(140.0f, 0.0f, 357.5f)
	};
	PawnMovement::FallPredictionStep step = {
		.Gravity = vec3(0.0f, 0.0f, -950.0f),
		.GroundSpeed = 400.0f,
		.TerminalVelocity = 2000.0f,
		.Elapsed = 0.1f
	};
	const auto result = PawnMovement::PredictFallStep(state, step);
	Check(result.Valid, "finite prospective wall-jump velocity is accepted");
	Check(Near(result.Velocity.x, 140.0f) && Near(result.Velocity.z, 262.5f),
		"prediction begins with the supplied horizontal velocity and JumpZ impulse");
	Check(Near(result.Location.x, 14.0f) && Near(result.Location.z, 26.25f),
		"prospective wall-jump impulse advances the first swept segment");
}

static PawnMovement::FirstWallContinuationInput VisseFirstWallInput()
{
	const vec3 start(1860.441f, 1457.698f, -664.0f);
	const vec3 fullDelta(17.1875f, -18.154375f, 18.6328125f);
	return {
		.StepStart = { .Location = start, .Velocity = vec3(275.0f, -290.47f, 357.5f) },
		.ProposedEnd = { .Location = start + fullDelta, .Velocity = vec3(275.0f, -290.47f, 298.125f) },
		.HitNormal = vec3(0.0f, 1.0f, 0.0f),
		.HitFraction = 0.798f / -fullDelta.y,
		.Elapsed = 0.0625f,
		.CollisionKind = PawnMovement::FallCollisionKind::StaticWorld,
		.MaxWallContactCount = 24
	};
}

static void TestVisseFirstStaticWallContinuesAlongWall()
{
	const auto candidate = PawnMovement::EvaluateFirstWallContinuation(VisseFirstWallInput());
	Check(candidate.Decision == PawnMovement::FirstWallContinuationDecision::ProbeAlignedSweep,
		"Visse's first static near-vertical collision requests one aligned world sweep");
	const auto result = PawnMovement::CompleteFirstWallContinuation(candidate, true);
	Check(result.Decision == PawnMovement::FirstWallContinuationDecision::Continue,
		"Visse's first static near-vertical collision continues the bounded forecast");
	Check(Near(result.HitSegmentDelta.y, -0.798f), "first segment stops at Deck's wall plane");
	Check(Near(result.AlignedSlideDelta.y, 0.0f), "remaining movement has its into-wall component removed");
	Check(Near(result.Next.Location.x, 1877.6285f, 0.001f)
		&& Near(result.Next.Location.y, 1456.9f, 0.001f)
		&& Near(result.Next.Location.z, -645.3672f, 0.001f),
		"continued state reaches the TickFalling-compatible aligned endpoint");
	Check(Near(result.Next.Velocity.x, 275.0f, 0.01f)
		&& Near(result.Next.Velocity.y, -12.768f, 0.01f)
		&& Near(result.Next.Velocity.z, 298.125f, 0.01f),
		"continued velocity is total collision-adjusted displacement divided by the full step");
}

static void TestWallContinuationRejectsNonStaticAndUnrelatedRepeatedHits()
{
	auto input = VisseFirstWallInput();
	input.CollisionKind = PawnMovement::FallCollisionKind::Mover;
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"mover collisions remain unknown and fail open");
	input.CollisionKind = PawnMovement::FallCollisionKind::DynamicActor;
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"dynamic actor collisions remain unknown and fail open");
	input.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"dynamic actors are not treated as durable walkable support");
	input.CollisionKind = PawnMovement::FallCollisionKind::StaticWorld;
	input.HitNormal = vec3(0.0f, 1.0f, 0.0f);
	input.PriorWallContactCount = 1;
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"a second forecast wall collision without the same BSP identity remains unknown");
	input.SameStaticBspSurfaceAsFirst = true;
	input.FirstWallNormal = vec3(1.0f, 0.0f, 0.0f);
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"a second contact with a misaligned normal remains unknown");
	input.PriorWallContactCount = 0;
	input.SameStaticBspSurfaceAsFirst = false;
	const auto candidate = PawnMovement::EvaluateFirstWallContinuation(input);
	Check(PawnMovement::CompleteFirstWallContinuation(candidate, false).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"a second collision during the aligned sweep remains unknown");
}

static void TestBoundedRepeatedContactsOnSameStaticSurfaceAreAllowed()
{
	auto input = VisseFirstWallInput();
	input.PriorWallContactCount = 1;
	input.SameStaticBspSurfaceAsFirst = true;
	input.FirstWallNormal = input.HitNormal;
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::ProbeAlignedSweep,
		"the second aligned contact on the identical static BSP surface is modeled");
	input.PriorWallContactCount = 2;
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::ProbeAlignedSweep,
		"a third aligned contact on the identical static BSP surface is modeled");
	input.PriorWallContactCount = 23;
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::ProbeAlignedSweep,
		"same-surface contact at the configured forecast bound is modeled");
	input.PriorWallContactCount = 24;
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"contacts beyond the configured forecast bound remain unknown");
}

static void TestFirstWallContinuationStopsOrFailsOpenOnOtherSurfaces()
{
	auto input = VisseFirstWallInput();
	input.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::SafeLanding,
		"walkable support stops the forecast as a safe landing");
	input.HitNormal = normalize(vec3(0.0f, 1.0f, 0.5f));
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"unqualified steep slopes remain unknown");
	input.HitNormal = vec3(0.0f);
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"zero collision normals remain unknown");
	input.HitNormal = vec3(0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f);
	Check(PawnMovement::EvaluateFirstWallContinuation(input).Decision
		== PawnMovement::FirstWallContinuationDecision::Unknown,
		"non-finite collision normals remain unknown");
}

static void TestInvalidPredictionIsReportedUnknown()
{
	PawnMovement::FallPredictionState state;
	PawnMovement::FallPredictionStep step = { .Elapsed = std::numeric_limits<float>::quiet_NaN() };
	Check(!PawnMovement::PredictFallStep(state, step).Valid, "non-finite time invalidates the prediction");
	step.Elapsed = 0.0f;
	Check(!PawnMovement::PredictFallStep(state, step).Valid, "zero-sized prediction steps are rejected");
}

static void TestPainVetoIsNarrowlyScoped()
{
	Check(PawnMovement::ShouldVetoPainZoneLedge(true, true, false, true), "an AI player-bot is stopped before a newly predicted harmful pain zone");
	Check(!PawnMovement::ShouldVetoPainZoneLedge(false, true, false, true), "human-controlled pawns retain normal ledge movement");
	Check(!PawnMovement::ShouldVetoPainZoneLedge(true, true, true, true), "a bot already in pain may continue toward an escape");
	Check(!PawnMovement::ShouldVetoPainZoneLedge(true, true, false, false), "ordinary safe drops are unchanged");
	Check(!PawnMovement::ShouldVetoPainZoneLedge(true, false, false, true), "custom upward-gravity movement fails open");
}

static void TestAIPlayerClassificationCoversBothBotHierarchies()
{
	Check(PawnMovement::IsAIControlledPlayer(true, false, false), "stock UT Bot and Unreal Bots Pawn subclasses are AI-controlled players");
	Check(PawnMovement::IsAIControlledPlayer(true, true, false), "an autonomous PlayerPawn used by a mod is AI-controlled");
	Check(!PawnMovement::IsAIControlledPlayer(true, true, true), "a PlayerPawn with a local or network Player controller is human-controlled");
	Check(!PawnMovement::IsAIControlledPlayer(false, false, false), "an ordinary ScriptedPawn is not classified as a player bot");
}

static void TestDeckTerminalSegmentsAreSampledWithinStepHeight()
{
	// Deck's normal terminal-speed segment is at most 2500 / 16 = 156.25 units.
	// Seven evenly spaced probes keep its zone lookup spacing below a 25-unit MaxStepHeight.
	const int samples = PawnMovement::BoundedFallSegmentSampleCount(156.25f, 25.0f, 8);
	Check(samples == 7, "Deck terminal-speed prediction uses seven bounded zone samples per step");
	Check(156.25f / samples <= 25.0f, "Deck zone samples are no farther apart than MaxStepHeight");
	Check(PawnMovement::BoundedFallSegmentSampleCount(10000.0f, 25.0f, 8) == 8, "pathological terminal speeds retain a strict per-step sample cap");
}

int main()
{
	TestStepMatchesWalkingToFallingIntegrator();
	TestAirControlCannotIncreaseGroundLimitedSpeed();
	TestPredictionUsesSuppliedWallJumpVelocity();
	TestVisseFirstStaticWallContinuesAlongWall();
	TestWallContinuationRejectsNonStaticAndUnrelatedRepeatedHits();
	TestBoundedRepeatedContactsOnSameStaticSurfaceAreAllowed();
	TestFirstWallContinuationStopsOrFailsOpenOnOtherSurfaces();
	TestInvalidPredictionIsReportedUnknown();
	TestPainVetoIsNarrowlyScoped();
	TestAIPlayerClassificationCoversBothBotHierarchies();
	TestDeckTerminalSegmentsAreSampledWithinStepHeight();
	if (Failures == 0)
		std::cout << "Pawn pain-zone fall prediction tests passed\n";
	return Failures == 0 ? 0 : 1;
}
