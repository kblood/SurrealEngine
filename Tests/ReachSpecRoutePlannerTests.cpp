#include "Automation/ReachSpecRoutePlanner.h"

#include <iostream>
#include <stdexcept>

using namespace Automation;

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	RouteNode Node(const char* identity, double x, bool start = false,
		bool endpoint = false)
	{
		return { identity, { x, 0.0, 0.0 }, start, endpoint, false };
	}

	RouteEdge Edge(const char* from, const char* to, uint32_t distance = 10,
		double radius = 48.0, double height = 80.0, bool pruned = false)
	{
		return { from, to, distance, radius, height, pruned };
	}

	RoutePlanRequest BaseRequest()
	{
		RoutePlanRequest request;
		request.Origin = {};
		request.PawnRadius = 24.0;
		request.PawnHeight = 48.0;
		request.WaypointRadius = 4.0;
		return request;
	}
}

int main()
{
	try
	{
		RoutePlanRequest request = BaseRequest();
		request.Nodes = { Node("start", 0.0, true), Node("hall", 10.0),
			Node("pickup", 20.0, false, true) };
		request.Edges = { Edge("start", "hall"), Edge("hall", "pickup") };
		RoutePlanResult result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::Found && result.Waypoints.size() == 2 &&
			result.Waypoints[0].Identity == "hall" &&
			result.Waypoints[1].Identity == "pickup",
			"reachable route did not return ordered waypoints");

		request.Edges.clear();
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::NoPath,
			"disconnected endpoint was accepted");

		request.Edges = { Edge("start", "pickup", 1, 48.0, 80.0, true),
			Edge("start", "hall"), Edge("hall", "pickup") };
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::Found &&
			result.Waypoints.front().Identity == "hall",
			"pruned shortcut was used");

		request.Edges = { Edge("start", "pickup", 1, 12.0, 80.0) };
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::NoPath,
			"undersized collision clearance was accepted");

		request = BaseRequest();
		request.Nodes = { Node("start", 0.0, true), Node("alpha", 10.0),
			Node("beta", 10.0), Node("end", 20.0, false, true) };
		request.Edges = { Edge("start", "beta"), Edge("start", "alpha"),
			Edge("beta", "end"), Edge("alpha", "end") };
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::Found &&
			result.Waypoints.front().Identity == "alpha",
			"equal-cost route tie was not deterministic");

		request.VisitedIdentities = { "alpha" };
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::Found &&
			result.Waypoints.front().Identity == "end",
			"visited waypoint was returned again");

		request.VisitedIdentities.clear();
		request.Nodes[2].Endpoint = true;
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::AmbiguousEndpoint,
			"ambiguous endpoint graph was accepted");

		request = BaseRequest();
		request.Nodes = { Node("duplicate", 0.0, true),
			Node("duplicate", 10.0, false, true) };
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::InvalidRequest,
			"duplicate node identity was accepted");

		request = BaseRequest();
		request.Nodes.reserve(MaximumRoutePlannerNodes + 1);
		for (size_t index = 0; index <= MaximumRoutePlannerNodes; index++)
			request.Nodes.push_back(Node(("node-" + std::to_string(index)).c_str(),
				static_cast<double>(index), index == 0, index == MaximumRoutePlannerNodes));
		result = PlanReachSpecRoute(request);
		Check(result.Status == RoutePlanStatus::InvalidRequest,
			"unbounded route graph was accepted");

		SpatialWaypointRequest spatial;
		spatial.Origin = {};
		spatial.Target = { 100.0, 0.0, 0.0 };
		spatial.ArrivalRadius = 4.0;
		spatial.Candidates = { Node("near", 20.0, true),
			Node("far", 70.0, true), Node("unreachable", 90.0, false),
			Node("behind", -10.0, true) };
		SpatialWaypointResult spatialResult =
			SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult && spatialResult.Waypoint.Identity == "far" &&
			spatialResult.TargetDistance == 30.0 &&
			spatialResult.OriginDistance == 70.0,
			"spatial fallback did not choose the best reachable progress waypoint");

		spatial.VisitedIdentities = { "far", "unknown-prior-nav-waypoint" };
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult && spatialResult.Waypoint.Identity == "near",
			"visited spatial waypoint was returned again");

		spatial.VisitedIdentities.clear();
		spatial.Candidates = { Node("beta", 50.0, true),
			Node("alpha", 50.0, true) };
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult && spatialResult.Waypoint.Identity == "alpha",
			"equal-score spatial waypoint tie was not deterministic");

		spatial.Candidates = { Node("duplicate", 20.0, true),
			Node("duplicate", 30.0, true) };
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult.Status == SpatialWaypointStatus::InvalidRequest,
			"duplicate spatial waypoint identity was accepted");

		spatial.Candidates = { Node("behind", -10.0, true),
			Node("unreachable", 90.0, false) };
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult.Status == SpatialWaypointStatus::NoProgressCandidate,
			"non-progress spatial waypoint was accepted");

		spatial.Candidates = { Node("high", 90.0, true) };
		spatial.Candidates.front().Location.Z = 65.0;
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult.Status == SpatialWaypointStatus::NoProgressCandidate,
			"implausibly steep spatial waypoint was accepted");

		spatial.Target = { 500.0, 0.0, 0.0 };
		spatial.Candidates = { Node("gentle-climb", 200.0, true) };
		spatial.Candidates.front().Location.Z = 80.0;
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult && spatialResult.Waypoint.Identity == "gentle-climb",
			"bounded gentle climb spatial waypoint was rejected");

		spatial.Target = { 500.0, 0.0, 100.0 };
		spatial.Candidates = { Node("stacked-high", 200.0, true),
			Node("stacked-low", 202.0, true) };
		spatial.Candidates[0].Location.Z = 80.0;
		spatial.Candidates[1].Location.Z = 10.0;
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult && spatialResult.Waypoint.Identity == "stacked-low",
			"higher stacked waypoint bypassed the reachable lower stage");

		spatial.MaximumClimbGradient = 0.0;
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult.Status == SpatialWaypointStatus::InvalidRequest,
			"invalid spatial climb gradient was accepted");
		spatial.MaximumClimbGradient = 0.5;

		spatial.Candidates.clear();
		for (size_t index = 0; index <= MaximumSpatialWaypointCandidates; index++)
			spatial.Candidates.push_back(Node(("actor-" + std::to_string(index)).c_str(),
				static_cast<double>(index + 1), true));
		spatialResult = SelectReachableSpatialWaypoint(spatial);
		Check(spatialResult.Status == SpatialWaypointStatus::InvalidRequest,
			"unbounded spatial waypoint candidate set was accepted");
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
	std::cout << "All reach-spec route planner tests passed.\n";
	return 0;
}
