#include "UObject/PawnWalkingStepPreflight.h"

#include <iostream>
#include <limits>
#include <set>
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

static PawnMovement::WalkingStepPreflightInput GenericPainFallInput()
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
	input.Forward.Collision = WalkingStepCollisionKind::Clear;
	input.Forward.Delta = vec3(64.0f, 0.0f, 0.0f);
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
		GenericPainFallInput());
	Check(result.Decision
		== PawnMovement::WalkingStepPreflightDecision::AuthorizeUnsafeStepVeto,
		"a supported ledge-to-static-wall chain ending in harmful pain authorizes a veto");
	Check(result.Reason == PawnMovement::WalkingStepPreflightReason::HarmfulPainFall,
		"the authorization has the exact harmful-pain-fall reason");
}

static void TestSupportedAndSafeLandingsDoNotAuthorize()
{
	using namespace PawnMovement;
	auto input = GenericPainFallInput();
	input.StepDown.Collision = WalkingStepCollisionKind::StaticBsp;
	input.StepDown.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::SupportedStepEndpoint,
		"a walkable step-down support");

	input = GenericPainFallInput();
	input.FallForecast.Landing.Zone = WalkingStepZoneKind::Safe;
	CheckNoDecision(input, WalkingStepPreflightReason::SafeFallLanding,
		"a complete fall forecast to a safe walkable landing");
}

static void TestDynamicAndUnknownEvidenceFailsOpen()
{
	using namespace PawnMovement;
	auto input = GenericPainFallInput();
	input.Forward.Collision = WalkingStepCollisionKind::DynamicActor;
	CheckNoDecision(input, WalkingStepPreflightReason::DynamicStepEvidence,
		"a dynamic forward wall");

	input = GenericPainFallInput();
	input.Forward.Collision = WalkingStepCollisionKind::Mover;
	CheckNoDecision(input, WalkingStepPreflightReason::MoverStepEvidence,
		"a mover forward wall");

	input = GenericPainFallInput();
	input.FallForecast.Continuations[1].Collision = WalkingStepCollisionKind::Mover;
	CheckNoDecision(input, WalkingStepPreflightReason::MoverFallEvidence,
		"a mover in the fall continuation chain");

	input = GenericPainFallInput();
	input.FallForecast.Landing.Collision = WalkingStepCollisionKind::DynamicActor;
	CheckNoDecision(input, WalkingStepPreflightReason::DynamicFallEvidence,
		"a dynamic fall landing");

	input = GenericPainFallInput();
	input.StepDown.Collision = WalkingStepCollisionKind::Unknown;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownStepEvidence,
		"unknown step-down evidence");

	input = GenericPainFallInput();
	input.FallForecast.Landing.Zone = WalkingStepZoneKind::Unknown;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownLandingZone,
		"unknown landing-zone evidence");
}

static void TestEligibilityAndImmunityRemainBehaviorNeutral()
{
	using namespace PawnMovement;
	auto input = GenericPainFallInput();
	input.Actor = WalkingStepActorKind::HumanPlayer;
	CheckNoDecision(input, WalkingStepPreflightReason::IneligibleHumanPlayer,
		"a human player");

	input = GenericPainFallInput();
	input.Actor = WalkingStepActorKind::ScriptedPawn;
	CheckNoDecision(input, WalkingStepPreflightReason::IneligibleScriptedPawn,
		"a ScriptedPawn");

	input = GenericPainFallInput();
	input.GravityKnown = false;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownGravity,
		"unknown gravity");

	input = GenericPainFallInput();
	input.Gravity = vec3(1.0f, 0.0f, -950.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::NonAxialDownwardGravity,
		"non-axial gravity");

	input = GenericPainFallInput();
	input.StartSupport.Zone = WalkingStepZoneKind::Unknown;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownStartZone,
		"unknown starting zone");

	input = GenericPainFallInput();
	input.StartSupport.Zone = WalkingStepZoneKind::Water;
	CheckNoDecision(input, WalkingStepPreflightReason::UnsafeStartZone,
		"water starting support");

	input = GenericPainFallInput();
	input.UpwardJumpRequested = true;
	CheckNoDecision(input, WalkingStepPreflightReason::UpwardJumpRequested,
		"an upward jump request");

	input = GenericPainFallInput();
	input.CollisionCallbackRequired = true;
	CheckNoDecision(input,
		WalkingStepPreflightReason::CollisionCallbackRequiredScriptTransitionUnknown,
		"a walking collision whose HitWall callback has not run");

	input = GenericPainFallInput();
	input.FallForecast.PainDamageImmunityKnown = false;
	CheckNoDecision(input, WalkingStepPreflightReason::UnknownPainDamageImmunity,
		"unknown pain-damage immunity");

	input = GenericPainFallInput();
	input.FallForecast.PainDamageImmune = true;
	CheckNoDecision(input, WalkingStepPreflightReason::PainDamageImmune,
		"pain-zone immunity");
}

static void TestStepAndForecastBoundsAreExplicit()
{
	using namespace PawnMovement;
	auto input = GenericPainFallInput();
	input.MaximumStepDelta = 32.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a step sweep beyond the configured 32uu bound");

	input = GenericPainFallInput();
	input.SlideCount = 2;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepSequence,
		"more than one walking slide");

	input = GenericPainFallInput();
	input.FallForecast.ContinuationCount = 3;
	CheckNoDecision(input, WalkingStepPreflightReason::FallContinuationLimitExceeded,
		"more than two fall wall continuations");

	input = GenericPainFallInput();
	input.MaximumFallSegmentDelta = 100.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallDelta,
		"a fall segment beyond the configured bound");

	input = GenericPainFallInput();
	input.FallForecast.TotalDrop = input.MaximumForecastDrop + 1.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallDelta,
		"a total forecast drop beyond the configured bound");

	input = GenericPainFallInput();
	input.FallForecast.Continuations[0].HitNormal = normalize(vec3(1.0f, 0.0f, 0.3f));
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallContinuation,
		"a continuation that is not near vertical");

	input = GenericPainFallInput();
	input.StepUp.Delta.x = std::numeric_limits<float>::quiet_NaN();
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a non-finite walking-step delta");

	input = GenericPainFallInput();
	input.Forward.Delta = vec3(0.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a zero forward walking delta");

	input = GenericPainFallInput();
	input.StepUp.Delta.x = 1.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a non-vertical step-up delta");

	input = GenericPainFallInput();
	input.Forward.Delta.z = 1.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidStepDelta,
		"a non-horizontal forward delta");

	input = GenericPainFallInput();
	input.FallForecast.Continuations[0].SegmentDelta = vec3(0.0f);
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidFallDelta,
		"a zero fall-continuation delta");

	input = GenericPainFallInput();
	input.MaximumForecastDrop = 0.0f;
	CheckNoDecision(input, WalkingStepPreflightReason::InvalidBounds,
		"an invalid configured forecast bound");
}

static void TestReasonMetricNamesAreCompleteAndUnique()
{
	using namespace PawnMovement;
	std::set<std::string> names;
	for (size_t index = 0; index < WalkingStepPreflightReasonCount; index++)
	{
		const char* name = WalkingStepPreflightReasonMetricName(
			static_cast<WalkingStepPreflightReason>(index));
		Check(name && *name, "every preflight reason has a telemetry metric name");
		if (name)
			Check(names.insert(name).second, "preflight reason telemetry names are unique");
	}
	Check(WalkingStepPreflightReasonMetricName(WalkingStepPreflightReason::Count) == nullptr,
		"the reason sentinel is not exposed as a metric");
}

static void TestAuthorizationEpisodesDebounceByLifeOriginAndSemanticGoal()
{
	using namespace PawnMovement;
	int firstLife = 0;
	int secondLife = 0;
	int firstTarget = 0;
	int secondTarget = 0;
	WalkingStepPreflightEpisodeObservation observation;
	observation.Authorized = true;
	observation.PawnLife = &firstLife;
	observation.PawnLifeGeneration = 1;
	observation.SupportedOrigin = vec3(10.0f, 20.0f, 30.0f);
	observation.SemanticTarget = &firstTarget;
	observation.SemanticDestination = vec3(100.0f, 200.0f, 300.0f);

	auto update = UpdateWalkingStepPreflightEpisode({}, observation);
	Check(update.AuthorizationStarted, "the first authorization starts an episode");
	const auto first = update.State;

	observation.SupportedOrigin += vec3(8.0f, 0.0f, 0.0f);
	update = UpdateWalkingStepPreflightEpisode(first, observation);
	Check(!update.AuthorizationStarted, "an authorization at the inclusive 8uu origin bound is debounced");

	observation.SupportedOrigin += vec3(0.01f, 0.0f, 0.0f);
	update = UpdateWalkingStepPreflightEpisode(first, observation);
	Check(update.AuthorizationStarted, "movement beyond 8uu starts a new authorization episode");

	observation = {};
	observation.Authorized = true;
	observation.PawnLife = &firstLife;
	observation.PawnLifeGeneration = 1;
	observation.SupportedOrigin = first.SupportedOrigin;
	observation.SemanticTarget = &secondTarget;
	observation.SemanticDestination = first.SemanticDestination;
	update = UpdateWalkingStepPreflightEpisode(first, observation);
	Check(update.AuthorizationStarted, "a new semantic target starts a new episode");

	observation.SemanticTarget = &firstTarget;
	observation.PawnLife = &secondLife;
	update = UpdateWalkingStepPreflightEpisode(first, observation);
	Check(update.AuthorizationStarted, "a replacement pawn life starts a new episode");

	observation.PawnLife = &firstLife;
	observation.PawnLifeGeneration = 2;
	update = UpdateWalkingStepPreflightEpisode(first, observation);
	Check(update.AuthorizationStarted, "a new generation on a recycled pawn starts a new episode");

	observation.PawnLife = &firstLife;
	observation.PawnLifeGeneration = 1;
	observation.SemanticTarget = nullptr;
	observation.SemanticDestination = vec3(100.0f, 200.0f, 300.0f);
	update = UpdateWalkingStepPreflightEpisode({}, observation);
	Check(update.AuthorizationStarted, "a destination-only authorization starts an episode");
	auto destinationEpisode = update.State;
	observation.SemanticDestination += vec3(0.0f, 8.0f, 0.0f);
	update = UpdateWalkingStepPreflightEpisode(destinationEpisode, observation);
	Check(!update.AuthorizationStarted, "a destination shift at 8uu is the same semantic episode");
	observation.SemanticDestination += vec3(0.0f, 0.01f, 0.0f);
	update = UpdateWalkingStepPreflightEpisode(destinationEpisode, observation);
	Check(update.AuthorizationStarted, "a destination shift beyond 8uu starts a new episode");

	observation.Authorized = false;
	update = UpdateWalkingStepPreflightEpisode(destinationEpisode, observation);
	Check(!update.AuthorizationStarted && update.State.Active,
		"a behavior-neutral result preserves the current authorization episode");

	observation.Authorized = true;
	observation.PawnLife = nullptr;
	update = UpdateWalkingStepPreflightEpisode(destinationEpisode, observation);
	Check(!update.AuthorizationStarted && !update.State.Active,
		"an authorization with an unknown life key fails closed for episode counting");
}

int main()
{
	TestSupportedLedgeWallPainChainAuthorizesVeto();
	TestSupportedAndSafeLandingsDoNotAuthorize();
	TestDynamicAndUnknownEvidenceFailsOpen();
	TestEligibilityAndImmunityRemainBehaviorNeutral();
	TestStepAndForecastBoundsAreExplicit();
	TestReasonMetricNamesAreCompleteAndUnique();
	TestAuthorizationEpisodesDebounceByLifeOriginAndSemanticGoal();
	if (Failures == 0)
		std::cout << "Pawn walking-step preflight tests passed\n";
	return Failures == 0 ? 0 : 1;
}
