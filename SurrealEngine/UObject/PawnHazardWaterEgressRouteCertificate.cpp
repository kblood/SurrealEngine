#include "PawnHazardWaterEgressRouteCertificate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

namespace
{
	using namespace PawnMovement;

	bool IsEligibleNode(const HazardWaterEgressRouteNode& node)
	{
		return !node.Id.empty() && node.StaticDry && !node.PlayerOnly
			&& !node.LiftOrTeleport;
	}

	bool IsEligibleEdge(const HazardWaterEgressRouteEdge& edge,
		const HazardWaterEgressRouteCertificateRequest& request)
	{
		return std::isfinite(edge.Cost) && edge.Cost >= 0.0f && !edge.Pruned
			&& edge.CollisionRadius >= request.PawnCollisionRadius
			&& edge.CollisionHeight >= request.PawnCollisionHeight
			&& (edge.ReachFlags & HazardWaterEgressReachWalk) != 0
			&& (edge.ReachFlags & HazardWaterEgressReachForbidden) == 0;
	}
}

namespace PawnMovement
{
	HazardWaterEgressRouteCertificate CertifyHazardWaterEgressStaticWalkRoute(
		const HazardWaterEgressRouteCertificateRequest& request)
	{
		HazardWaterEgressRouteCertificate certificate;
		if (request.MaximumVisitedNodes == 0 || !std::isfinite(request.PawnCollisionRadius)
			|| !std::isfinite(request.PawnCollisionHeight)
			|| request.PawnCollisionRadius < 0.0f || request.PawnCollisionHeight < 0.0f)
		{
			return certificate;
		}

		std::vector<size_t> firstHops;
		for (size_t index = 0; index < request.Nodes.size(); index++)
		{
			const auto& node = request.Nodes[index];
			if (node.DirectFirstHopSweepClear && IsEligibleNode(node))
				firstHops.push_back(index);
		}
		if (firstHops.empty())
			return certificate;

		const size_t nodeCount = request.Nodes.size();
		std::vector<std::vector<size_t>> outgoing(nodeCount);
		for (size_t index = 0; index < request.Edges.size(); index++)
		{
			const auto& edge = request.Edges[index];
			if (edge.Source < nodeCount && edge.Destination < nodeCount)
				outgoing[edge.Source].push_back(index);
		}

		using WorkItem = std::tuple<float, size_t, size_t, size_t>;
		std::priority_queue<WorkItem, std::vector<WorkItem>, std::greater<WorkItem>> queue;
		std::vector<float> costs(nodeCount, std::numeric_limits<float>::infinity());
		std::vector<size_t> hops(nodeCount, 0);
		std::vector<size_t> origin(nodeCount, nodeCount);
		for (size_t firstHop : firstHops)
		{
			costs[firstHop] = 0.0f;
			origin[firstHop] = firstHop;
			queue.emplace(0.0f, 0, firstHop, firstHop);
		}

		while (!queue.empty())
		{
			auto [cost, hopCount, node, firstHop] = queue.top();
			queue.pop();
			if (cost != costs[node] || hopCount != hops[node] || firstHop != origin[node])
				continue;
			if (certificate.VisitedNodes >= request.MaximumVisitedNodes)
			{
				certificate.Result = HazardWaterEgressRouteCertificateResult::SearchBudgetExhausted;
				certificate.FirstHopKnown = true;
				certificate.FirstHopNode = firstHop;
				return certificate;
			}
			certificate.VisitedNodes++;

			for (size_t edgeIndex : outgoing[node])
			{
				const auto& edge = request.Edges[edgeIndex];
				if (!IsEligibleEdge(edge, request) || !IsEligibleNode(request.Nodes[edge.Destination]))
					continue;
				if (certificate.VisitedNodes >= request.MaximumVisitedNodes)
				{
					certificate.Result = HazardWaterEgressRouteCertificateResult::SearchBudgetExhausted;
					certificate.FirstHopKnown = true;
					certificate.FirstHopNode = firstHop;
					return certificate;
				}
				const float nextCost = cost + edge.Cost;
				const size_t nextHops = hopCount + 1;
				const bool improves = nextCost < costs[edge.Destination]
					|| (nextCost == costs[edge.Destination]
						&& std::tie(nextHops, firstHop) < std::tie(hops[edge.Destination], origin[edge.Destination]));
				if (!improves)
					continue;
				costs[edge.Destination] = nextCost;
				hops[edge.Destination] = nextHops;
				origin[edge.Destination] = firstHop;
				if (nextHops >= 1)
				{
					certificate.Result = HazardWaterEgressRouteCertificateResult::Certified;
					certificate.FirstHopKnown = true;
					certificate.FirstHopNode = firstHop;
					certificate.ContinuationKnown = true;
					certificate.ContinuationNode = edge.Destination;
					certificate.StaticWalkCost = nextCost;
					certificate.StaticWalkHops = nextHops;
					certificate.VisitedNodes++;
					return certificate;
				}
				queue.emplace(nextCost, nextHops, edge.Destination, firstHop);
			}
		}

		certificate.Result = HazardWaterEgressRouteCertificateResult::NoStaticWalkContinuation;
		return certificate;
	}

	const char* HazardWaterEgressRouteCertificateResultName(
		HazardWaterEgressRouteCertificateResult result)
	{
		switch (result)
		{
		case HazardWaterEgressRouteCertificateResult::Certified:
			return "certified_static_walk_continuation";
		case HazardWaterEgressRouteCertificateResult::NoStaticWalkContinuation:
			return "no_static_walk_continuation";
		case HazardWaterEgressRouteCertificateResult::SearchBudgetExhausted:
			return "search_budget_exhausted";
		default:
			return "no_eligible_direct_first_hop";
		}
	}
}
