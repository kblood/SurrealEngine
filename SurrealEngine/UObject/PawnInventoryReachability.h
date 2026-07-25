#pragma once

#include <cstdint>
#include <string>

namespace PawnMovement
{
	constexpr float MaximumInventoryDirectReachDistance = 1000.0f;
	constexpr float MaximumInventoryHazardScanDepth = 512.0f;

	struct InventoryReachabilityCorridorPlan
	{
		int SampleCount = 0;
		float SampleSpacing = 0.0f;
		bool Valid = false;
	};

	struct InventoryReachabilitySample
	{
		float SweepProgress = 0.0f;
		bool WalkableSupport = false;
		bool HarmfulFootZone = false;
		bool HarmfulBelow = false;
		bool Valid = true;
	};

	enum class InventoryReachabilityDecision
	{
		Continue,
		Reject
	};

	// This is observation-only evidence from the native ActorReachable walking
	// simulation. It deliberately describes the post-resolution position rather
	// than prescribing a different reachability result.
	enum class InventoryDirectReachSupportOutcome
	{
		SafeSupported,
		SafeUnsupportedNoObservedHazard,
		UnsafeHarmfulFootZone,
		UnsafeUnsupportedOverHarmful,
		Unavailable
	};

	struct InventoryDirectReachSupportDiagnosticRecord
	{
		std::string SourcePawnActor;
		std::string TargetActor;
		std::string TargetClass;
		uint64_t Sequence = 0;
		int WalkingSimulationIterations = 0;
		float SupportFraction = 1.0f;
		float SupportNormalZ = 0.0f;
		bool WalkableSupport = false;
		bool HarmfulFootZone = false;
		bool HarmfulBelow = false;
		InventoryDirectReachSupportOutcome Outcome =
			InventoryDirectReachSupportOutcome::Unavailable;
	};

	InventoryReachabilityCorridorPlan PlanInventoryReachabilityCorridor(
		float corridorDistance, float maximumSpacing, int maximumSamples);
	InventoryReachabilityCorridorPlan PlanInventoryHazardScan(
		float scanDepth, float maximumSpacing, int maximumSamples);
	bool ShouldPreflightInventoryDirectReach(bool stockAutonomousPlayerBot,
		bool walking, bool inventoryTarget, bool ammoTarget, bool startingInHarmfulPain);
	bool InventoryReachabilitySampleObservedUnsafe(
		const InventoryReachabilitySample& sample);
	InventoryReachabilityDecision EvaluateInventoryReachabilitySample(
		const InventoryReachabilitySample& sample);
	InventoryDirectReachSupportOutcome ClassifyInventoryDirectReachSupport(
		const InventoryReachabilitySample& sample);
	const char* InventoryDirectReachSupportOutcomeName(
		InventoryDirectReachSupportOutcome outcome);
}
