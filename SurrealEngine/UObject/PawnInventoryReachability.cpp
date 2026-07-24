#include "PawnInventoryReachability.h"

#include <cmath>

namespace PawnMovement
{
	namespace
	{
		InventoryReachabilityCorridorPlan PlanBoundedSamples(
			float distance, float maximumDistance, float maximumSpacing, int maximumSamples)
		{
			InventoryReachabilityCorridorPlan plan;
			if (!std::isfinite(distance) || !std::isfinite(maximumSpacing)
				|| distance <= 0.0f || distance > maximumDistance
				|| maximumSpacing <= 0.0f || maximumSamples <= 0)
				return plan;

			const double requiredSamples = std::ceil(
				static_cast<double>(distance) / static_cast<double>(maximumSpacing));
			if (!std::isfinite(requiredSamples) || requiredSamples < 1.0
				|| requiredSamples > static_cast<double>(maximumSamples))
				return plan;

			plan.SampleCount = static_cast<int>(requiredSamples);
			plan.SampleSpacing = distance / static_cast<float>(plan.SampleCount);
			plan.Valid = std::isfinite(plan.SampleSpacing) && plan.SampleSpacing > 0.0f
				&& plan.SampleSpacing <= maximumSpacing;
			if (!plan.Valid)
				plan = {};
			return plan;
		}
	}

	InventoryReachabilityCorridorPlan PlanInventoryReachabilityCorridor(
		float corridorDistance, float maximumSpacing, int maximumSamples)
	{
		return PlanBoundedSamples(corridorDistance, MaximumInventoryDirectReachDistance,
			maximumSpacing, maximumSamples);
	}

	InventoryReachabilityCorridorPlan PlanInventoryHazardScan(
		float scanDepth, float maximumSpacing, int maximumSamples)
	{
		return PlanBoundedSamples(scanDepth, MaximumInventoryHazardScanDepth,
			maximumSpacing, maximumSamples);
	}

	bool ShouldPreflightInventoryDirectReach(bool stockAutonomousPlayerBot,
		bool walking, bool inventoryTarget, bool ammoTarget, bool startingInHarmfulPain)
	{
		return stockAutonomousPlayerBot && walking && inventoryTarget && ammoTarget
			&& !startingInHarmfulPain;
	}

	bool InventoryReachabilitySampleObservedUnsafe(
		const InventoryReachabilitySample& sample)
	{
		if (!sample.Valid || !std::isfinite(sample.SweepProgress)
			|| sample.SweepProgress < 0.0f || sample.SweepProgress > 1.0f)
			return false;
		return sample.HarmfulFootZone || (!sample.WalkableSupport && sample.HarmfulBelow);
	}

	InventoryReachabilityDecision EvaluateInventoryReachabilitySample(
		const InventoryReachabilitySample& sample)
	{
		if (!sample.Valid || !std::isfinite(sample.SweepProgress)
			|| sample.SweepProgress < 0.0f || sample.SweepProgress > 1.0f)
			return InventoryReachabilityDecision::Continue;

		if (InventoryReachabilitySampleObservedUnsafe(sample))
			return InventoryReachabilityDecision::Reject;

		return InventoryReachabilityDecision::Continue;
	}
}
