#include "PawnFailedNavigationMemory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace PawnMovement
{
	bool AreFailedNavigationLiftExitsOnSameLanding(
		const FailedNavigationLiftExitTopology& first,
		const FailedNavigationLiftExitTopology& second,
		float maxHorizontalSeparation, float maxVerticalSeparation)
	{
		if (!first.Zone || first.Zone != second.Zone
			|| !std::isfinite(first.X) || !std::isfinite(first.Y) || !std::isfinite(first.Z)
			|| !std::isfinite(second.X) || !std::isfinite(second.Y) || !std::isfinite(second.Z)
			|| !std::isfinite(maxHorizontalSeparation) || maxHorizontalSeparation <= 0.0f
			|| !std::isfinite(maxVerticalSeparation) || maxVerticalSeparation < 0.0f)
			return false;

		const double deltaX = static_cast<double>(first.X) - second.X;
		const double deltaY = static_cast<double>(first.Y) - second.Y;
		const double horizontalLimit = maxHorizontalSeparation;
		if (deltaX * deltaX + deltaY * deltaY > horizontalLimit * horizontalLimit
			|| std::abs(static_cast<double>(first.Z) - second.Z) > maxVerticalSeparation)
			return false;

		for (const void* firstNeighbor : first.AdjacentLandingNodes)
		{
			if (!firstNeighbor)
				continue;
			if (std::find(second.AdjacentLandingNodes.begin(), second.AdjacentLandingNodes.end(),
				firstNeighbor) != second.AdjacentLandingNodes.end())
				return true;
		}
		return false;
	}

	bool CanRememberFailedNavigation(const FailedNavigationEligibility& eligibility)
	{
		return eligibility.TargetLive
			&& !eligibility.LiftCenter
			&& !eligibility.SpecialGoalRedirected
			&& !eligibility.BasedOnLift
			&& !eligibility.LiftInterpolating
			&& !eligibility.LiftDelaying
			&& !eligibility.LiftWaitingForPawn;
	}

	FailedNavigationMemoryState AdvanceFailedNavigationMemory(
		const FailedNavigationMemoryState& state, float elapsed, float currentX, float currentY,
		const std::array<bool, 2>& targetsLive, float escapeRadius)
	{
		if (!std::isfinite(elapsed) || elapsed < 0.0f)
			return state;

		FailedNavigationMemoryState result = state;
		for (size_t index = 0; index < result.Entries.size(); index++)
		{
			FailedNavigationMemoryEntry& entry = result.Entries[index];
			if (!entry.Target)
				continue;
			if (!targetsLive[index])
			{
				entry = {};
				continue;
			}
			if (entry.AvoidanceRemaining <= 0.0f
				&& std::isfinite(currentX) && std::isfinite(currentY)
				&& std::isfinite(escapeRadius) && escapeRadius > 0.0f)
			{
				const float deltaX = currentX - entry.AnchorX;
				const float deltaY = currentY - entry.AnchorY;
				if (deltaX * deltaX + deltaY * deltaY > escapeRadius * escapeRadius)
				{
					entry = {};
					continue;
				}
			}
			entry.RepeatWindowRemaining = std::max(entry.RepeatWindowRemaining - elapsed, 0.0f);
			entry.AvoidanceRemaining = std::max(entry.AvoidanceRemaining - elapsed, 0.0f);
			if (entry.AvoidanceRemaining <= 0.0f && entry.RepeatWindowRemaining <= 0.0f)
				entry = {};
		}
		return result;
	}

	FailedNavigationFailureRecord RecordFailedNavigation(
		const FailedNavigationMemoryState& state, const void* target, float anchorX, float anchorY,
		float repeatWindow, float avoidanceDuration, uint8_t requiredFailures)
	{
		FailedNavigationFailureRecord result = { state, false };
		if (!target || !std::isfinite(anchorX) || !std::isfinite(anchorY)
			|| !std::isfinite(repeatWindow) || repeatWindow <= 0.0f
			|| !std::isfinite(avoidanceDuration) || avoidanceDuration <= 0.0f
			|| requiredFailures == 0)
			return result;

		size_t selectedIndex = result.State.Entries.size();
		for (size_t index = 0; index < result.State.Entries.size(); index++)
		{
			const FailedNavigationMemoryEntry& entry = result.State.Entries[index];
			if (entry.Target == target
				&& (entry.RepeatWindowRemaining > 0.0f || entry.AvoidanceRemaining > 0.0f))
			{
				selectedIndex = index;
				break;
			}
		}
		if (selectedIndex == result.State.Entries.size())
		{
			for (size_t index = 0; index < result.State.Entries.size(); index++)
			{
				const FailedNavigationMemoryEntry& entry = result.State.Entries[index];
				if (!entry.Target || (entry.RepeatWindowRemaining <= 0.0f
					&& entry.AvoidanceRemaining <= 0.0f))
				{
					selectedIndex = index;
					break;
				}
			}
		}
		if (selectedIndex == result.State.Entries.size())
		{
			selectedIndex = 0;
			float leastRemaining = std::max(result.State.Entries[0].RepeatWindowRemaining,
				result.State.Entries[0].AvoidanceRemaining);
			for (size_t index = 1; index < result.State.Entries.size(); index++)
			{
				const float remaining = std::max(result.State.Entries[index].RepeatWindowRemaining,
					result.State.Entries[index].AvoidanceRemaining);
				if (remaining < leastRemaining)
				{
					selectedIndex = index;
					leastRemaining = remaining;
				}
			}
		}

		FailedNavigationMemoryEntry& selected = result.State.Entries[selectedIndex];
		const bool repeatedTarget = selected.Target == target
			&& (selected.RepeatWindowRemaining > 0.0f || selected.AvoidanceRemaining > 0.0f);
		if (!repeatedTarget)
		{
			selected = {
				.Target = target,
				.AnchorX = anchorX,
				.AnchorY = anchorY,
				.Failures = 1,
				.RepeatWindowRemaining = repeatWindow
			};
		}
		else
		{
			selected.Failures = static_cast<uint8_t>(std::min<int>(
				static_cast<int>(selected.Failures) + 1, 255));
			selected.RepeatWindowRemaining = repeatWindow;
		}

		if (selected.Failures >= requiredFailures)
		{
			result.AvoidanceActivated = selected.AvoidanceRemaining <= 0.0f;
			selected.AvoidanceRemaining = avoidanceDuration;
		}
		return result;
	}

	int32_t FailedNavigationFirstHopPenalty(const FailedNavigationMemoryState& state,
		const void* candidate, bool reachableEndpoint, int32_t penalty)
	{
		if (!reachableEndpoint || !candidate || penalty <= 0)
			return 0;
		for (const FailedNavigationMemoryEntry& entry : state.Entries)
		{
			if (entry.AvoidanceRemaining > 0.0f && entry.Target == candidate)
				return penalty;
		}
		return 0;
	}

	FailedNavigationEndpointSelection SelectFailedNavigationEndpoint(
		const FailedNavigationMemoryState& state,
		std::span<const FailedNavigationEndpointCandidate> candidates, int32_t penalty,
		FailedNavigationEquivalenceGroups avoidedTargetGroups)
	{
		FailedNavigationEndpointSelection result;
		std::vector<size_t> uniqueCandidates;
		uniqueCandidates.reserve(candidates.size());
		for (size_t index = 0; index < candidates.size(); index++)
		{
			const FailedNavigationEndpointCandidate& candidate = candidates[index];
			if (!candidate.Endpoint)
				continue;

			auto existing = std::find_if(uniqueCandidates.begin(), uniqueCandidates.end(),
				[&](size_t other) { return candidates[other].Endpoint == candidate.Endpoint; });
			if (existing == uniqueCandidates.end())
			{
				uniqueCandidates.push_back(index);
				continue;
			}

			const FailedNavigationEndpointCandidate& previous = candidates[*existing];
			if (candidate.Cost < previous.Cost
				|| (candidate.Cost == previous.Cost && candidate.StableOrder < previous.StableOrder))
				*existing = index;
		}

		for (size_t index : uniqueCandidates)
		{
			const FailedNavigationEndpointCandidate& candidate = candidates[index];
			bool avoided = false;
			for (size_t entryIndex = 0; entryIndex < state.Entries.size(); entryIndex++)
			{
				const FailedNavigationMemoryEntry& entry = state.Entries[entryIndex];
				if (entry.AvoidanceRemaining <= 0.0f)
					continue;
				const bool exactTarget = entry.Target == candidate.Endpoint;
				const bool relatedTarget = candidate.AvoidedEntries[entryIndex];
				const bool equivalentTarget = candidate.EquivalenceGroup
					&& avoidedTargetGroups[entryIndex]
					&& candidate.EquivalenceGroup == avoidedTargetGroups[entryIndex];
				if (exactTarget || relatedTarget || equivalentTarget)
				{
					avoided = true;
					break;
				}
			}
			const int32_t appliedPenalty = avoided && penalty > 0 ? penalty : 0;
			const int64_t adjusted = static_cast<int64_t>(std::max(candidate.Cost, 0))
				+ static_cast<int64_t>(appliedPenalty);
			const int32_t adjustedCost = static_cast<int32_t>(std::min<int64_t>(
				adjusted, std::numeric_limits<int32_t>::max()));
			if (appliedPenalty > 0)
				result.PenaltyApplications++;

			if (!result.Found || adjustedCost < result.AdjustedCost
				|| (adjustedCost == result.AdjustedCost
					&& candidate.StableOrder < candidates[result.CandidateIndex].StableOrder))
			{
				result.CandidateIndex = index;
				result.AdjustedCost = adjustedCost;
				result.Found = true;
			}
		}
		return result;
	}
}
