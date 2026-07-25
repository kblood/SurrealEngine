#include "UObject/PawnInventoryReachability.h"

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

static bool Near(float left, float right)
{
	return std::abs(left - right) < 0.0001f;
}

static void TestCorridorPlanUsesBoundedMaximumSpacing()
{
	const auto plan = PawnMovement::PlanInventoryReachabilityCorridor(1000.0f, 34.0f, 64);
	Check(plan.Valid, "a normal direct-reach corridor produces a sampling plan");
	Check(plan.SampleCount == 30, "a 1000-unit corridor uses thirty samples at 34-unit maximum spacing");
	Check(Near(plan.SampleSpacing, 1000.0f / 30.0f), "samples are evenly spaced across the corridor");
	Check(plan.SampleSpacing <= 34.0f, "the planned spacing does not exceed the requested maximum");

	const auto shortPlan = PawnMovement::PlanInventoryReachabilityCorridor(12.0f, 34.0f, 64);
	Check(shortPlan.Valid && shortPlan.SampleCount == 1 && Near(shortPlan.SampleSpacing, 12.0f),
		"a short corridor still receives one endpoint sample");
}

static void TestCorridorPlanFailsOpenWhenItCannotHonorItsBounds()
{
	const auto exhausted = PawnMovement::PlanInventoryReachabilityCorridor(1000.0f, 10.0f, 64);
	Check(!exhausted.Valid && exhausted.SampleCount == 0 && Near(exhausted.SampleSpacing, 0.0f),
		"a corridor requiring too many samples disables the optional check");

	Check(!PawnMovement::PlanInventoryReachabilityCorridor(1000.1f, 34.0f, 64).Valid,
		"corridors beyond ActorReachable's direct-reach bound fail open");
	Check(!PawnMovement::PlanInventoryReachabilityCorridor(0.0f, 34.0f, 64).Valid,
		"an empty corridor fails open");
	Check(!PawnMovement::PlanInventoryReachabilityCorridor(100.0f, 0.0f, 64).Valid,
		"invalid spacing fails open");
	Check(!PawnMovement::PlanInventoryReachabilityCorridor(100.0f, 34.0f, 0).Valid,
		"an invalid sample bound fails open");
	Check(!PawnMovement::PlanInventoryReachabilityCorridor(
		std::numeric_limits<float>::quiet_NaN(), 34.0f, 64).Valid,
		"non-finite distance fails open");
	Check(!PawnMovement::PlanInventoryReachabilityCorridor(
		100.0f, std::numeric_limits<float>::infinity(), 64).Valid,
		"non-finite spacing fails open");
}

static void TestHazardScanPlanIsSeparatelyBounded()
{
	const auto plan = PawnMovement::PlanInventoryHazardScan(512.0f, 25.0f, 32);
	Check(plan.Valid && plan.SampleCount == 21, "the full hazard scan fits within its sample bound");
	Check(plan.SampleSpacing <= 25.0f, "hazard scan spacing does not exceed MaxStepHeight");
	Check(!PawnMovement::PlanInventoryHazardScan(512.1f, 25.0f, 32).Valid,
		"hazard scans deeper than 512 units fail open");
	Check(!PawnMovement::PlanInventoryHazardScan(512.0f, 10.0f, 32).Valid,
		"a hazard scan that exhausts its sample budget fails open");
}

static void TestDirectReachPreflightIsAmmoOnly()
{
	using PawnMovement::ShouldPreflightInventoryDirectReach;
	Check(ShouldPreflightInventoryDirectReach(true, true, true, true, false),
		"stock walking bots preflight direct ammo reachability from a safe zone");
	Check(!ShouldPreflightInventoryDirectReach(true, true, true, false, false),
		"non-ammo inventory preserves stock ActorReachable behavior");
	Check(!ShouldPreflightInventoryDirectReach(true, true, false, true, false),
		"an ammo classification without an inventory target is not eligible");
	Check(!ShouldPreflightInventoryDirectReach(false, true, true, true, false),
		"human and non-stock pawns preserve stock ActorReachable behavior");
	Check(!ShouldPreflightInventoryDirectReach(true, false, true, true, false),
		"non-walking movement preserves stock ActorReachable behavior");
	Check(!ShouldPreflightInventoryDirectReach(true, true, true, true, true),
		"bots already in harmful pain preserve their escape behavior");
}

static void TestSupportedSamplesContinue()
{
	using namespace PawnMovement;
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 1.0f,
		.WalkableSupport = true
	}) == InventoryReachabilityDecision::Continue,
		"a fully advanced supported safe sample continues the corridor check");
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 0.25f,
		.WalkableSupport = true
	}) == InventoryReachabilityDecision::Continue,
		"partial but positive sweep progress remains usable by the caller");
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 0.0f,
		.WalkableSupport = false
	}) == InventoryReachabilityDecision::Continue,
		"zero progress is left to ActorReachable rather than treated as a hazard");
}

static void TestObservedUnsafeSamplesReject()
{
	using namespace PawnMovement;
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 1.0f,
		.WalkableSupport = false
	}) == InventoryReachabilityDecision::Continue,
		"an unsupported sample with no observed pain remains eligible");
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 1.0f,
		.WalkableSupport = true,
		.HarmfulFootZone = true
	}) == InventoryReachabilityDecision::Reject,
		"a directly observed harmful foot zone rejects direct reachability");
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 1.0f,
		.WalkableSupport = false,
		.HarmfulBelow = true
	}) == InventoryReachabilityDecision::Reject,
		"an unsupported sample over observed pain rejects direct reachability");
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 1.0f,
		.WalkableSupport = true,
		.HarmfulBelow = true
	}) == InventoryReachabilityDecision::Continue,
		"a safe walkable floor shields a sample from pain observed farther below");
	Check(!InventoryReachabilitySampleObservedUnsafe({
		.SweepProgress = 1.0f,
		.WalkableSupport = false
	}), "unsupported safe space is not classified as observed unsafe");
	Check(InventoryReachabilitySampleObservedUnsafe({
		.SweepProgress = 1.0f,
		.WalkableSupport = false,
		.HarmfulBelow = true
	}), "unsupported space over pain is classified as observed unsafe");
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 1.0f,
		.WalkableSupport = true
	}) == InventoryReachabilityDecision::Continue,
		"damage-type immunity represented by no harmful observation continues");
}

static void TestInvalidSamplesFailOpen()
{
	using namespace PawnMovement;
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = 0.0f,
		.WalkableSupport = false,
		.HarmfulFootZone = true,
		.Valid = false
	}) == InventoryReachabilityDecision::Continue,
		"an unavailable observation fails open");
	Check(EvaluateInventoryReachabilitySample({
		.SweepProgress = std::numeric_limits<float>::quiet_NaN(),
		.WalkableSupport = false,
		.HarmfulFootZone = true
	}) == InventoryReachabilityDecision::Continue,
		"non-finite sweep progress fails open");
	Check(EvaluateInventoryReachabilitySample({ .SweepProgress = -0.1f })
		== InventoryReachabilityDecision::Continue,
		"negative sweep progress fails open");
	Check(EvaluateInventoryReachabilitySample({ .SweepProgress = 1.1f })
		== InventoryReachabilityDecision::Continue,
		"out-of-range sweep progress fails open");
}

static void TestPostResolutionSupportClassification()
{
	using namespace PawnMovement;
	Check(ClassifyInventoryDirectReachSupport({
		.SweepProgress = 1.0f, .WalkableSupport = true
	}) == InventoryDirectReachSupportOutcome::SafeSupported,
		"a walkable post-resolution support probe is safe");
	Check(ClassifyInventoryDirectReachSupport({
		.SweepProgress = 1.0f, .WalkableSupport = false
	}) == InventoryDirectReachSupportOutcome::SafeUnsupportedNoObservedHazard,
		"unsupported space without observed harm stays diagnostic-only");
	Check(ClassifyInventoryDirectReachSupport({
		.SweepProgress = 1.0f, .WalkableSupport = true, .HarmfulFootZone = true
	}) == InventoryDirectReachSupportOutcome::UnsafeHarmfulFootZone,
		"a harmful resolved foot zone is an unsafe witness");
	Check(ClassifyInventoryDirectReachSupport({
		.SweepProgress = 1.0f, .WalkableSupport = false, .HarmfulBelow = true
	}) == InventoryDirectReachSupportOutcome::UnsafeUnsupportedOverHarmful,
		"unsupported space above a harmful zone is an unsafe witness");
	Check(ClassifyInventoryDirectReachSupport({
		.SweepProgress = std::numeric_limits<float>::quiet_NaN()
	}) == InventoryDirectReachSupportOutcome::Unavailable,
		"missing support evidence is explicitly unavailable");
}

int main()
{
	TestCorridorPlanUsesBoundedMaximumSpacing();
	TestCorridorPlanFailsOpenWhenItCannotHonorItsBounds();
	TestHazardScanPlanIsSeparatelyBounded();
	TestDirectReachPreflightIsAmmoOnly();
	TestSupportedSamplesContinue();
	TestObservedUnsafeSamplesReject();
	TestInvalidSamplesFailOpen();
	TestPostResolutionSupportClassification();
	if (Failures == 0)
		std::cout << "Pawn inventory reachability tests passed\n";
	return Failures == 0 ? 0 : 1;
}
