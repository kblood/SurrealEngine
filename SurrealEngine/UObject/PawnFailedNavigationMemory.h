#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace PawnMovement
{
	struct FailedNavigationMemoryEntry
	{
		const void* Target = nullptr;
		float AnchorX = 0.0f;
		float AnchorY = 0.0f;
		uint8_t Failures = 0;
		float RepeatWindowRemaining = 0.0f;
		float AvoidanceRemaining = 0.0f;
	};

	struct FailedNavigationMemoryState
	{
		std::array<FailedNavigationMemoryEntry, 2> Entries;
	};

	struct FailedNavigationFailureRecord
	{
		FailedNavigationMemoryState State;
		bool AvoidanceActivated = false;
	};

	struct FailedNavigationEligibility
	{
		bool TargetLive = false;
		bool LiftCenter = false;
		bool SpecialGoalRedirected = false;
		bool BasedOnLift = false;
		bool LiftInterpolating = false;
		bool LiftDelaying = false;
		bool LiftWaitingForPawn = false;
	};

	struct FailedNavigationEndpointCandidate
	{
		const void* Endpoint = nullptr;
		const void* EquivalenceGroup = nullptr;
		std::array<bool, 2> AvoidedEntries = {};
		int32_t Cost = 0;
		size_t StableOrder = 0;
	};

	using FailedNavigationEquivalenceGroups = std::array<const void*, 2>;

	struct FailedNavigationEndpointSelection
	{
		size_t CandidateIndex = 0;
		int32_t AdjustedCost = 0;
		uint32_t PenaltyApplications = 0;
		bool Found = false;
	};

	struct FailedNavigationLiftExitTopology
	{
		const void* Zone = nullptr;
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		std::span<const void* const> AdjacentLandingNodes;
	};

	bool CanRememberFailedNavigation(const FailedNavigationEligibility& eligibility);
	bool AreFailedNavigationLiftExitsOnSameLanding(
		const FailedNavigationLiftExitTopology& first,
		const FailedNavigationLiftExitTopology& second,
		float maxHorizontalSeparation, float maxVerticalSeparation);
	FailedNavigationMemoryState AdvanceFailedNavigationMemory(
		const FailedNavigationMemoryState& state, float elapsed, float currentX, float currentY,
		const std::array<bool, 2>& targetsLive, float escapeRadius);
	FailedNavigationFailureRecord RecordFailedNavigation(
		const FailedNavigationMemoryState& state, const void* target, float anchorX, float anchorY,
		float repeatWindow, float avoidanceDuration, uint8_t requiredFailures);
	int32_t FailedNavigationFirstHopPenalty(const FailedNavigationMemoryState& state,
		const void* candidate, bool reachableEndpoint, int32_t penalty);
	FailedNavigationEndpointSelection SelectFailedNavigationEndpoint(
		const FailedNavigationMemoryState& state,
		std::span<const FailedNavigationEndpointCandidate> candidates, int32_t penalty,
		FailedNavigationEquivalenceGroups avoidedTargetGroups = {});
}
