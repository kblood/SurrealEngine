#pragma once

#include "AutomationProtocol.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Automation
{
	static constexpr size_t MaximumRoutePlannerNodes = 4096;
	static constexpr size_t MaximumRoutePlannerEdges = 16384;
	static constexpr size_t MaximumSpatialWaypointCandidates = 64;

	struct RouteNode
	{
		std::string Identity;
		WorldPoint Location;
		bool DirectlyReachable = false;
		bool Endpoint = false;
		bool Deleted = false;
	};

	struct RouteEdge
	{
		std::string FromIdentity;
		std::string ToIdentity;
		uint32_t Distance = 0;
		double ClearanceRadius = 0.0;
		double ClearanceHeight = 0.0;
		bool Pruned = false;
	};

	struct RoutePlanRequest
	{
		WorldPoint Origin;
		double PawnRadius = 0.0;
		double PawnHeight = 0.0;
		double WaypointRadius = 32.0;
		std::vector<RouteNode> Nodes;
		std::vector<RouteEdge> Edges;
		std::vector<std::string> VisitedIdentities;
	};

	enum class RoutePlanStatus
	{
		Found,
		AlreadyAtEndpoint,
		InvalidRequest,
		MissingEndpoint,
		AmbiguousEndpoint,
		NoReachableStart,
		NoPath
	};

	struct RoutePlanResult
	{
		RoutePlanStatus Status = RoutePlanStatus::InvalidRequest;
		std::vector<RouteNode> Waypoints;
		double TotalDistance = 0.0;
		size_t NodeCount = 0;
		size_t EdgeCount = 0;
		size_t ReachableStartCount = 0;
		std::string Error;

		explicit operator bool() const
		{
			return Status == RoutePlanStatus::Found ||
				Status == RoutePlanStatus::AlreadyAtEndpoint;
		}
	};

	RoutePlanResult PlanReachSpecRoute(const RoutePlanRequest& request);

	struct SpatialWaypointRequest
	{
		WorldPoint Origin;
		WorldPoint Target;
		double ArrivalRadius = 32.0;
		double MaximumVerticalDelta = 64.0;
		double MaximumClimbGradient = 0.5;
		double MinimumProgress = 1.0;
		std::vector<RouteNode> Candidates;
		std::vector<std::string> VisitedIdentities;
	};

	enum class SpatialWaypointStatus
	{
		Found,
		NoProgressCandidate,
		InvalidRequest
	};

	struct SpatialWaypointResult
	{
		SpatialWaypointStatus Status = SpatialWaypointStatus::InvalidRequest;
		RouteNode Waypoint;
		double TargetDistance = 0.0;
		double OriginDistance = 0.0;
		std::string Error;

		explicit operator bool() const
		{
			return Status == SpatialWaypointStatus::Found;
		}
	};

	SpatialWaypointResult SelectReachableSpatialWaypoint(
		const SpatialWaypointRequest& request);
}
