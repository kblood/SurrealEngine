#pragma once

#include "PawnReachSpecEligibility.h"

#include <cstdint>
#include <string>
#include <vector>

namespace PawnMovement
{
	enum class RoutePathCommitOrigin
	{
		FindPathToward,
		FindBestInventoryPath
	};

	struct RoutePathCommitNode
	{
		std::string Name;
		std::string ClassName;
	};

	struct RoutePathCommitEdge
	{
		int32_t ReachSpecIndex = -1;
		std::string StartNode;
		std::string EndNode;
		int32_t Distance = 0;
		int32_t CollisionRadius = 0;
		int32_t CollisionHeight = 0;
		int32_t ReachFlags = 0;
		uint32_t UnknownReachFlags = 0;
		bool Pruned = false;
		ReachSpecEligibility CapabilityEligibility;
	};

	struct RoutePathCommitCapabilitySnapshot
	{
		uint64_t NativeTick = 0;
		uint64_t LifeId = 0;
		ReachSpecCapabilityProfile Profile;
		uint32_t CapabilityFlags = 0;
	};

	// This is provenance for the route cache written by native path search. It
	// deliberately captures before SpecialHandling can redirect a script target.
	struct RoutePathCommitRecord
	{
		uint64_t Sequence = 0;
		RoutePathCommitOrigin Origin = RoutePathCommitOrigin::FindPathToward;
		int32_t RawEndpointCost = 0;
		int32_t AdjustedEndpointCost = 0;
		uint32_t FailedNavigationPenaltyApplications = 0;
		bool CacheClear = false;
		bool TruncatedByRouteCache = false;
		bool CapabilitySnapshotKnown = false;
		RoutePathCommitCapabilitySnapshot CapabilitySnapshot;
		std::vector<RoutePathCommitNode> Nodes;
		std::vector<RoutePathCommitEdge> Edges;
	};
}
