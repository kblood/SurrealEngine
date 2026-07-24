#include "UObject/PawnWalkingStepPreflight.h"

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

static PawnMovement::WalkingStepPreflightInput ImplicatedPainLedgeInput()
{
	using namespace PawnMovement;
	WalkingStepPreflightInput input;
	input.Actor = WalkingStepActorKind::StockAutonomousPlayerBot;
	input.Walking = true;
	input.StartSupport.Collision = WalkingStepCollisionKind::StaticBsp;
	input.StartSupport.Normal = vec3(0.0f, 0.0f, 1.0f);
	input.StartSupport.Zone = WalkingStepZoneKind::Safe;
	input.GravityKnown = true;
	input.Gravity = vec3(0.0f, 0.0f, -950.0f);
	input.StepUp.Collision = WalkingStepCollisionKind::Clear;
	input.StepUp.Delta = vec3(0.0f, 0.0f, 35.0f);
	input.Forward.Collision = WalkingStepCollisionKind::StaticBsp;
	input.Forward.Delta = vec3(64.0f, 0.0f, 0.0f);
	input.Forward.HitNormal = vec3(-1.0f, 0.0f, 0.0f);
	input.SlideCount = 1;
	input.Slides[0].Collision = WalkingStepCollisionKind::Clear;
	input.Slides[0].Delta = vec3(0.0f, 32.0f, 0.0f);
	input.StepDown.Collision = WalkingStepCollisionKind::Clear;
	input.StepDown.Delta = vec3(0.0f, 0.0f, -40.0f);
	input.FallForecast.Complete = true;
	input.FallForecast.TotalDrop = 512.0f;
	input.FallForecast.ContinuationCount = 2;
	input.FallForecast.Continuations[0].Collision = WalkingStepCollisionKind::StaticBsp;
	input.FallForecast.Continuations[0].SegmentDelta = vec3(80.0f, 0.0f, -96.0f);
	input.FallForecast.Continuations[0].HitNormal = vec3(-1.0f, 0.0f, 0.0f);
	input.FallForecast.Continuations[1].Collision = WalkingStepCollisionKind::StaticBsp;
	input.FallForecast.Continuations[1].SegmentDelta = vec3(0.0f, 64.0f, -128.0f);
	input.FallForecast.Continuations[1].HitNormal = vec3(0.0f, -1.0f, 0.0f);
	input.FallForecast.Landing.Collision = WalkingStepCollisionKind::StaticBsp;
	input.FallForecast.Landing.Normal = vec3(0.0f, 0.0f, 1.0f);
	input.FallForecast.Landing.Zone = WalkingStepZoneKind::Pain;
	input.FallForecast.PainDamageImmunityKnown = true;
	return input;
}

static void CheckNoDecision(const PawnMovement::WalkingStepPreflightInput& input,
	PawnMovement::WalkingStepPreflightReason reason, const std::string& scenario)
{
	const auto result = PawnMovement::EvaluateWalkingStepPreflight(input);
	Check(result.Decision == PawnMovement::WalkingStepPreflightDecision::NoDecision,
		scenario + " remains behavior-neutral");
	Check(result.Reason == reason, scenario + " has one exact rejection reason");
}

static void TestSupportedLedgeWallPainChainAuthorizesVeto()
{
	const auto result = PawnMovement::EvaluateWalkingStepPreflight(
		ImplicatedPainLedgeInput());
	Check(result.Decision
		== PawnMovement::WalkingStepPreflightDecision::AuthorizeUnsafeStepVeto,
		"a supported ledge-to-static-wall chain ending in harmful pain authorizes a veto");
	Check(result.Reason == PawnMovement::WalkingStepPreflightReason::HarmfulPainFall,
		"the authorization has the exact harmful-pain-fall reason");
}

static void TestSupportedAndSafeLandingsDoNotAuthorize()
{
	using namespace PawnMovement;
	auto input = ImplicatedPainLedgeInput();
	input.StepDown.Collision = WalkingStepCollisionKind::StaticBsp;
	input.StepDown.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::SupportedStepEndpoint,
		"a walkable step-down support");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.Landing.Zone = WalkingStepZoneKind::Safe;
	CheckNoDecision(input, WalkingStepPreflightReason::SafeFallLanding,
		"a complete fall forecast to a safe walkable landing");
}

static void TestDynamicAndUnknownEvidenceFailsOpen()
{
	using namespace PawnMovement;
	auto input = ImplicatedPainLedgeInput();
	input.Forward.Collision = WalkingStepCollisionKind::DynamicActor;
	CheckNoDecision(input, WalkingStepPreflightReason::DynamicStepEvidence,
		"a dynamic forward wall");

	input = ImplicatedPainLedgeInput();
	input.Forward.Collision = WalkingStepCollisionKind::Mover;
	CheckNoDecision(input, WalkingStepPreflightReason::MoverStepEvidence,
		"a mover forward wall");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.Continuations[1].Collision = WalkingStepCollisionKind::Mover;
	CheckNoDecision(input, WalkingStepPreflightReason::MoverFallEvidence,
		"a mover in the fall continuation chain");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.Landing.Collision = WalkingStepCollisionKind::DynamicActor;
	CheckNoDecision(input, WalkingStepPreflightReason::DynamicFallEvidence,
		"a dynamic fall landing");

	input = ImplicatedPainLedgeInput();
	input.StepDown.Collision = WalkingStepCollisionKind::Unknown;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownStepEvidence,
		"unknown step-down evidence");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.Landing.Zone = WalkingStepZoneKind::Unknown;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownLandingZone,
		"unknown landing-zone evidence");
}

static void TestEligibilityAndImmunityRemainBehaviorNeutral()
{
	using namespace PawnMovement;
	auto input = ImplicatedPainLedgeInput();
	input.Actor = WalkingStepActorKind::HumanPlayer;
	CheckNoDecision(input, WalkingStepPreflightReason::IneligibleHumanPlayer,
		"a human player");

	input = ImplicatedPainLedgeInput();
	input.Actor = WalkingStepActorKind::ScriptedPawn;
	CheckNoDecision(input, WalkingStepPreflightReason::IneligibleScriptedPawn,
		"a ScriptedPawn");

	input = ImplicatedPainLedgeInput();
	input.GravityKnown = false;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownGravity,
		"unknown gravity");

	input = ImplicatedPainLedgeInput();
	input.Gravity = vec3(1.0f, 0.0f, -950.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::NonAxialDownwardGravity,
		"non-axial gravity");

	input = ImplicatedPainLedgeInput();
	input.StartSupport.Zone = WalkingStepZoneKind::Unknown;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownStartZone,
		"unknown starting zone");

	input = ImplicatedPainLedgeInput();
	input.StartSupport.Zone = WalkingStepZoneKind::Water;
	CheckNoDecision(input, WalkingStepPreflightReason::UnsafeStartZone,
		"water starting support");

	input = ImplicatedPainLedgeInput();
	input.UpwardJumpRequested = true;
	CheckNoDecision(input, WalkingStepPreflightReason::UpwardJumpRequested,
		"an upward jump request");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.PainDamageImmunityKnown = false;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownPainDamageImmunity,
		"unknown pain-damage immunity");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.PainDamageImmune = true;
	CheckNoDecision(input, WalkingStepPreflightReason::PainDamageImmune,
		"pain-zone immunity");
}

static void TestStepAndForecastBoundsAreExplicit()
{
	using namespace PawnMovement;
	auto input = ImplicatedPainLedgeInput();
	input.MaximumStepDelta = 32.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a step sweep beyond the configured 32uu bound");

	input = ImplicatedPainLedgeInput();
	input.SlideCount = 2;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepSequence,
		"more than one walking slide");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.ContinuationCount = 3;
	CheckNoDecision(input, WalkingStepPreflightReason::FallContinuationLimitExceeded,
		"more than two fall wall continuations");

	input = ImplicatedPainLedgeInput();
	input.MaximumFallSegmentDelta = 100.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallDelta,
		"a fall segment beyond the configured bound");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.TotalDrop = input.MaximumForecastDrop + 1.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallDelta,
		"a total forecast drop beyond the configured bound");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.Continuations[0].HitNormal = normalize(vec3(1.0f, 0.0f, 0.3f));
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallContinuation,
		"a continuation that is not near vertical");

	input = ImplicatedPainLedgeInput();
	input.StepUp.Delta.x = std::numeric_limits<float>::quiet_NaN();
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a non-finite walking-step delta");

	input = ImplicatedPainLedgeInput();
	input.Forward.Delta = vec3(0.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a zero forward walking delta");

	input = ImplicatedPainLedgeInput();
	input.StepUp.Delta.x = 1.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a non-vertical step-up delta");

	input = ImplicatedPainLedgeInput();
	input.Forward.Delta.z = 1.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a non-horizontal forward delta");

	input = ImplicatedPainLedgeInput();
	input.FallForecast.Continuations[0].SegmentDelta = vec3(0.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallDelta,
		"a zero fall-continuation delta");

	input = ImplicatedPainLedgeInput();
	input.MaximumForecastDrop = 0.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidBounds,
		"an invalid configured forecast bound");
}

int main()
{
	TestSupportedLedgeWallPainChainAuthorizesVeto();
	TestSupportedAndSafeLandingsDoNotAuthorize();
	TestDynamicAndUnknownEvidenceFailsOpen();
	TestEligibilityAndImmunityRemainBehaviorNeutral();
	TestStepAndForecastBoundsAreExplicit();
	if (Failures == 0)
		std::cout << "Pawn walking-step preflight tests passed\n";
	return Failures == 0 ? 0 : 1;
}
