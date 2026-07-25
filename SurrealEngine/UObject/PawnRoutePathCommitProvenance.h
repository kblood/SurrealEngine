#pragma once

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
		std::vector<RoutePathCommitNode> Nodes;
		std::vector<RoutePathCommitEdge> Edges;
	};
}
