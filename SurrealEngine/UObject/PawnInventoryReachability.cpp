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

	bool ShouldPreflightInventoryMarkerDirectReach(bool stockAutonomousPlayerBot,
		bool walking, bool inventoryMarker, bool markedAmmoTarget, bool startingInHarmfulPain)
	{
		return stockAutonomousPlayerBot && walking && inventoryMarker && markedAmmoTarget
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

	InventoryDirectReachSupportOutcome ClassifyInventoryDirectReachSupport(
		const InventoryReachabilitySample& sample)
	{
		if (!sample.Valid || !std::isfinite(sample.SweepProgress)
			|| sample.SweepProgress < 0.0f || sample.SweepProgress > 1.0f)
		{
			return InventoryDirectReachSupportOutcome::Unavailable;
		}
		if (sample.HarmfulFootZone)
			return InventoryDirectReachSupportOutcome::UnsafeHarmfulFootZone;
		if (!sample.WalkableSupport && sample.HarmfulBelow)
			return InventoryDirectReachSupportOutcome::UnsafeUnsupportedOverHarmful;
		return sample.WalkableSupport
			? InventoryDirectReachSupportOutcome::SafeSupported
			: InventoryDirectReachSupportOutcome::SafeUnsupportedNoObservedHazard;
	}

	const char* InventoryDirectReachSupportOutcomeName(
		InventoryDirectReachSupportOutcome outcome)
	{
		switch (outcome)
		{
		case InventoryDirectReachSupportOutcome::SafeSupported:
			return "safe_supported";
		case InventoryDirectReachSupportOutcome::SafeUnsupportedNoObservedHazard:
			return "safe_unsupported_no_observed_hazard";
		case InventoryDirectReachSupportOutcome::UnsafeHarmfulFootZone:
			return "unsafe_harmful_foot_zone";
		case InventoryDirectReachSupportOutcome::UnsafeUnsupportedOverHarmful:
			return "unsafe_unsupported_over_harmful";
		case InventoryDirectReachSupportOutcome::Unavailable:
			return "unavailable";
		default:
			return "unavailable";
		}
	}
}
