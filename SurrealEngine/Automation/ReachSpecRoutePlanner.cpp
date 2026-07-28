#include "ReachSpecRoutePlanner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace Automation
{
	namespace
	{
		bool IsFinitePoint(const WorldPoint& point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y) &&
				std::isfinite(point.Z) &&
				std::abs(point.X) <= MaximumCoordinateMagnitude &&
				std::abs(point.Y) <= MaximumCoordinateMagnitude &&
				std::abs(point.Z) <= MaximumCoordinateMagnitude;
		}

		bool IsBoundedIdentity(const std::string& identity)
		{
			if (identity.empty() || identity.size() > MaximumTargetIdentityBytes)
				return false;
			return std::all_of(identity.begin(), identity.end(), [](unsigned char value)
			{
				return value >= 0x20 && value <= 0x7e;
			});
		}

		double Distance(const WorldPoint& left, const WorldPoint& right)
		{
			return std::hypot(std::hypot(left.X - right.X, left.Y - right.Y),
				left.Z - right.Z);
		}

		RoutePlanResult Reject(RoutePlanStatus status, const RoutePlanRequest& request,
			std::string error)
		{
			RoutePlanResult result;
			result.Status = status;
			result.NodeCount = request.Nodes.size();
			result.EdgeCount = request.Edges.size();
			result.Error = std::move(error);
			return result;
		}
	}

	RoutePlanResult PlanReachSpecRoute(const RoutePlanRequest& request)
	{
		if (!IsFinitePoint(request.Origin) || !std::isfinite(request.PawnRadius) ||
			!std::isfinite(request.PawnHeight) || !std::isfinite(request.WaypointRadius) ||
			request.PawnRadius <= 0.0 || request.PawnHeight <= 0.0 ||
			request.WaypointRadius <= 0.0 || request.WaypointRadius > MaximumArrivalRadius ||
			request.Nodes.empty() || request.Nodes.size() > MaximumRoutePlannerNodes ||
			request.Edges.size() > MaximumRoutePlannerEdges ||
			request.VisitedIdentities.size() > MaximumRoutePlannerNodes)
			return Reject(RoutePlanStatus::InvalidRequest, request,
				"route request is invalid or exceeds deterministic bounds");

		std::map<std::string, const RouteNode*> nodes;
		const RouteNode* endpoint = nullptr;
		size_t endpointCount = 0;
		size_t reachableStartCount = 0;
		for (const RouteNode& node : request.Nodes)
		{
			if (!IsBoundedIdentity(node.Identity) || !IsFinitePoint(node.Location) ||
				!nodes.emplace(node.Identity, &node).second)
				return Reject(RoutePlanStatus::InvalidRequest, request,
					"route nodes contain an invalid or duplicate identity");
			if (!node.Deleted && node.DirectlyReachable)
				reachableStartCount++;
			if (!node.Deleted && node.Endpoint)
			{
				endpoint = &node;
				endpointCount++;
			}
		}
		if (endpointCount == 0)
			return Reject(RoutePlanStatus::MissingEndpoint, request,
				"route graph has no live endpoint");
		if (endpointCount != 1)
			return Reject(RoutePlanStatus::AmbiguousEndpoint, request,
				"route graph has more than one live endpoint");
		if (reachableStartCount == 0)
			return Reject(RoutePlanStatus::NoReachableStart, request,
				"route graph has no directly reachable start node");

		std::set<std::string> visited;
		for (const std::string& identity : request.VisitedIdentities)
		{
			if (!IsBoundedIdentity(identity) || nodes.find(identity) == nodes.end() ||
				!visited.insert(identity).second)
				return Reject(RoutePlanStatus::InvalidRequest, request,
					"visited waypoint identities are invalid, unknown, or duplicated");
		}

		std::map<std::string, std::vector<const RouteEdge*>> adjacency;
		for (const RouteEdge& edge : request.Edges)
		{
			if (!IsBoundedIdentity(edge.FromIdentity) || !IsBoundedIdentity(edge.ToIdentity) ||
				edge.FromIdentity == edge.ToIdentity || edge.Distance == 0 ||
				!std::isfinite(edge.ClearanceRadius) || !std::isfinite(edge.ClearanceHeight) ||
				edge.ClearanceRadius < 0.0 || edge.ClearanceHeight < 0.0 ||
				nodes.find(edge.FromIdentity) == nodes.end() ||
				nodes.find(edge.ToIdentity) == nodes.end())
				return Reject(RoutePlanStatus::InvalidRequest, request,
					"route edges contain an invalid endpoint or bound");
			adjacency[edge.FromIdentity].push_back(&edge);
		}
		for (auto& entry : adjacency)
		{
			std::sort(entry.second.begin(), entry.second.end(), [](const RouteEdge* left,
				const RouteEdge* right)
			{
				if (left->ToIdentity != right->ToIdentity)
					return left->ToIdentity < right->ToIdentity;
				return left->Distance < right->Distance;
			});
		}

		struct QueueEntry
		{
			double Distance = 0.0;
			std::string Identity;
		};
		struct QueueOrder
		{
			bool operator()(const QueueEntry& left, const QueueEntry& right) const
			{
				if (left.Distance != right.Distance)
					return left.Distance > right.Distance;
				return left.Identity > right.Identity;
			}
		};

		std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueOrder> open;
		std::map<std::string, double> distances;
		std::map<std::string, std::string> previous;
		for (const RouteNode& node : request.Nodes)
		{
			if (node.Deleted || !node.DirectlyReachable)
				continue;
			const double distance = Distance(request.Origin, node.Location);
			auto found = distances.find(node.Identity);
			if (found == distances.end() || distance < found->second)
			{
				distances[node.Identity] = distance;
				previous[node.Identity] = {};
				open.push({ distance, node.Identity });
			}
		}

		while (!open.empty())
		{
			QueueEntry current = open.top();
			open.pop();
			auto currentDistance = distances.find(current.Identity);
			if (currentDistance == distances.end() || current.Distance != currentDistance->second)
				continue;
			if (current.Identity == endpoint->Identity)
				break;

			for (const RouteEdge* edge : adjacency[current.Identity])
			{
				const RouteNode* next = nodes[edge->ToIdentity];
				if (edge->Pruned || next->Deleted ||
					edge->ClearanceRadius < request.PawnRadius ||
					edge->ClearanceHeight < request.PawnHeight)
					continue;
				const double candidate = current.Distance + edge->Distance;
				auto known = distances.find(next->Identity);
				const bool shorter = known == distances.end() || candidate < known->second;
				const bool deterministicTie = known != distances.end() && candidate == known->second &&
					current.Identity < previous[next->Identity];
				if (!shorter && !deterministicTie)
					continue;
				distances[next->Identity] = candidate;
				previous[next->Identity] = current.Identity;
				open.push({ candidate, next->Identity });
			}
		}

		auto endpointDistance = distances.find(endpoint->Identity);
		if (endpointDistance == distances.end())
		{
			RoutePlanResult result = Reject(RoutePlanStatus::NoPath, request,
				"no clearance-compatible path connects a reachable start to the endpoint");
			result.ReachableStartCount = reachableStartCount;
			return result;
		}

		std::vector<const RouteNode*> path;
		for (std::string identity = endpoint->Identity; !identity.empty();
			identity = previous[identity])
			path.push_back(nodes[identity]);
		std::reverse(path.begin(), path.end());

		RoutePlanResult result;
		result.Status = RoutePlanStatus::Found;
		result.TotalDistance = endpointDistance->second;
		result.NodeCount = request.Nodes.size();
		result.EdgeCount = request.Edges.size();
		result.ReachableStartCount = reachableStartCount;
		for (const RouteNode* node : path)
		{
			if (Distance(request.Origin, node->Location) <= request.WaypointRadius ||
				visited.find(node->Identity) != visited.end())
				continue;
			result.Waypoints.push_back(*node);
		}
		if (result.Waypoints.empty())
		{
			result.Status = RoutePlanStatus::AlreadyAtEndpoint;
			result.Error = "route endpoint is already inside the waypoint envelope";
		}
		return result;
	}

	SpatialWaypointResult SelectReachableSpatialWaypoint(
		const SpatialWaypointRequest& request)
	{
		if (!IsFinitePoint(request.Origin) || !IsFinitePoint(request.Target) ||
			!std::isfinite(request.ArrivalRadius) || request.ArrivalRadius <= 0.0 ||
			request.ArrivalRadius > MaximumArrivalRadius ||
			!std::isfinite(request.MaximumVerticalDelta) ||
			request.MaximumVerticalDelta <= 0.0 ||
			request.MaximumVerticalDelta > MaximumArrivalRadius ||
			!std::isfinite(request.MaximumClimbGradient) ||
			request.MaximumClimbGradient <= 0.0 ||
			request.MaximumClimbGradient > 16.0 ||
			!std::isfinite(request.MinimumProgress) || request.MinimumProgress <= 0.0 ||
			request.Candidates.empty() ||
			request.Candidates.size() > MaximumSpatialWaypointCandidates ||
			request.VisitedIdentities.size() > MaximumSpatialWaypointCandidates)
			return { SpatialWaypointStatus::InvalidRequest, {}, 0.0, 0.0,
				"spatial waypoint request is invalid or exceeds deterministic bounds" };

		std::set<std::string> identities;
		for (const RouteNode& candidate : request.Candidates)
		{
			if (!IsBoundedIdentity(candidate.Identity) ||
				!IsFinitePoint(candidate.Location) ||
				!identities.insert(candidate.Identity).second)
				return { SpatialWaypointStatus::InvalidRequest, {}, 0.0, 0.0,
					"spatial waypoint candidates contain an invalid or duplicate identity" };
		}

		std::set<std::string> visited;
		for (const std::string& identity : request.VisitedIdentities)
		{
			if (!IsBoundedIdentity(identity) || !visited.insert(identity).second)
				return { SpatialWaypointStatus::InvalidRequest, {}, 0.0, 0.0,
					"visited spatial waypoint identities are invalid or duplicated" };
		}

		const double originTargetDistance = Distance(request.Origin, request.Target);
		const RouteNode* best = nullptr;
		double bestScore = std::numeric_limits<double>::infinity();
		double bestTargetDistance = 0.0;
		double bestOriginDistance = 0.0;
		for (const RouteNode& candidate : request.Candidates)
		{
			if (candidate.Deleted || !candidate.DirectlyReachable ||
				visited.find(candidate.Identity) != visited.end())
				continue;
			const double originDistance = Distance(request.Origin, candidate.Location);
			const double targetDistance = Distance(request.Target, candidate.Location);
			const double deltaX = candidate.Location.X - request.Origin.X;
			const double deltaY = candidate.Location.Y - request.Origin.Y;
			const double horizontalDistance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
			const double verticalDistance =
				std::abs(candidate.Location.Z - request.Origin.Z);
			bool shadowedByLowerWaypoint = false;
			for (const RouteNode& lower : request.Candidates)
			{
				if (&lower == &candidate || lower.Deleted || !lower.DirectlyReachable ||
					visited.find(lower.Identity) != visited.end())
					continue;
				const double separationX = lower.Location.X - candidate.Location.X;
				const double separationY = lower.Location.Y - candidate.Location.Y;
				const double horizontalSeparationSquared =
					separationX * separationX + separationY * separationY;
				const double lowerVerticalDistance =
					std::abs(lower.Location.Z - request.Origin.Z);
				const double lowerTargetDistance = Distance(request.Target, lower.Location);
				if (horizontalSeparationSquared <= request.ArrivalRadius * request.ArrivalRadius &&
					lowerVerticalDistance + request.MinimumProgress < verticalDistance &&
					lowerTargetDistance + request.MinimumProgress < originTargetDistance)
				{
					shadowedByLowerWaypoint = true;
					break;
				}
			}
			const bool implausibleClimb = verticalDistance > request.MaximumVerticalDelta &&
				(horizontalDistance == 0.0 ||
					verticalDistance > horizontalDistance * request.MaximumClimbGradient);
			if (originDistance <= request.ArrivalRadius || shadowedByLowerWaypoint ||
				implausibleClimb ||
				targetDistance + request.MinimumProgress >= originTargetDistance)
				continue;
			const double score = targetDistance + 0.25 * originDistance;
			if (score > bestScore ||
				(score == bestScore && best && candidate.Identity >= best->Identity))
				continue;
			best = &candidate;
			bestScore = score;
			bestTargetDistance = targetDistance;
			bestOriginDistance = originDistance;
		}

		if (!best)
			return { SpatialWaypointStatus::NoProgressCandidate, {}, 0.0, 0.0,
				"no unvisited directly reachable actor makes bounded target progress" };
		return { SpatialWaypointStatus::Found, *best, bestTargetDistance,
			bestOriginDistance, {} };
	}
}
