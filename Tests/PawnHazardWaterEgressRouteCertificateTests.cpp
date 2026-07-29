#include "UObject/PawnHazardWaterEgressRouteCertificate.h"

#include <cstdlib>
#include <iostream>

namespace
{
	using namespace PawnMovement;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}

	HazardWaterEgressRouteNode Node(const char* id, bool firstHop = false)
	{
		HazardWaterEgressRouteNode node;
		node.Id = id;
		node.StaticDry = true;
		node.DirectFirstHopSweepClear = firstHop;
		return node;
	}

	HazardWaterEgressRouteEdge Edge(size_t source, size_t destination, float cost = 10.0f)
	{
		HazardWaterEgressRouteEdge edge;
		edge.Source = source;
		edge.Destination = destination;
		edge.Cost = cost;
		edge.CollisionRadius = 20.0f;
		edge.CollisionHeight = 40.0f;
		edge.ReachFlags = HazardWaterEgressReachWalk;
		return edge;
	}

	HazardWaterEgressRouteCertificateRequest BaseRequest()
	{
		HazardWaterEgressRouteCertificateRequest request;
		request.PawnCollisionRadius = 16.0f;
		request.PawnCollisionHeight = 39.0f;
		request.MaximumVisitedNodes = 8;
		return request;
	}
}

int main()
{
	auto request = BaseRequest();
	request.Nodes = { Node("first", true), Node("dry-end") };
	request.Edges = { Edge(0, 1, 17.0f) };
	auto certificate = CertifyHazardWaterEgressStaticWalkRoute(request);
	Check(certificate.IsCertified() && certificate.FirstHopNode == 0
		&& certificate.ContinuationNode == 1 && certificate.StaticWalkCost == 17.0f
		&& certificate.StaticWalkHops == 1 && certificate.VisitedNodes == 2,
		"a collision-clear first hop and static walk continuation must certify");

	request.Nodes[0].DirectFirstHopSweepClear = false;
	certificate = CertifyHazardWaterEgressStaticWalkRoute(request);
	Check(certificate.Result == HazardWaterEgressRouteCertificateResult::NoEligibleDirectFirstHop,
		"a blocked first hop must not certify");

	request.Nodes[0].DirectFirstHopSweepClear = true;
	request.Edges[0].Pruned = true;
	certificate = CertifyHazardWaterEgressStaticWalkRoute(request);
	Check(certificate.Result == HazardWaterEgressRouteCertificateResult::NoStaticWalkContinuation,
		"pruned links must not certify");
	request.Edges[0].Pruned = false;
	request.Edges[0].CollisionRadius = 15.0f;
	certificate = CertifyHazardWaterEgressStaticWalkRoute(request);
	Check(certificate.Result == HazardWaterEgressRouteCertificateResult::NoStaticWalkContinuation,
		"undersized reachspecs must not certify");
	request.Edges[0].CollisionRadius = 20.0f;
	request.Edges[0].ReachFlags = HazardWaterEgressReachWalk | 8;
	certificate = CertifyHazardWaterEgressStaticWalkRoute(request);
	Check(certificate.Result == HazardWaterEgressRouteCertificateResult::NoStaticWalkContinuation,
		"jump links must not certify a static walking continuation");

	request.Edges[0].ReachFlags = HazardWaterEgressReachWalk;
	request.Nodes[1].StaticDry = false;
	certificate = CertifyHazardWaterEgressStaticWalkRoute(request);
	Check(certificate.Result == HazardWaterEgressRouteCertificateResult::NoStaticWalkContinuation,
		"unsafe endpoints must not certify");

	request.Nodes = { Node("first", true), Node("middle"), Node("end") };
	request.Edges = { Edge(0, 1), Edge(1, 2) };
	request.MaximumVisitedNodes = 1;
	certificate = CertifyHazardWaterEgressStaticWalkRoute(request);
	Check(certificate.Result == HazardWaterEgressRouteCertificateResult::SearchBudgetExhausted,
		"bounded graph searches must report exhaustion rather than infer a route");
	return 0;
}
