#pragma once

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
}
