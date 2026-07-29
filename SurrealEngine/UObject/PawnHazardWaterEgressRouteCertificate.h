#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PawnMovement
{
	// These are the UE1 reach-spec flag values, kept here so the certificate can
	// operate on a detached, read-only snapshot rather than live navigation state.
	constexpr uint32_t HazardWaterEgressReachWalk = 1;
	constexpr uint32_t HazardWaterEgressReachForbidden = 2 | 4 | 8 | 16 | 32;

	struct HazardWaterEgressRouteNode
	{
		std::string Id;
		bool StaticDry = false;
		bool PlayerOnly = false;
		bool LiftOrTeleport = false;
		bool DirectFirstHopSweepClear = false;
	};

	struct HazardWaterEgressRouteEdge
	{
		size_t Source = 0;
		size_t Destination = 0;
		float Cost = 0.0f;
		float CollisionRadius = 0.0f;
		float CollisionHeight = 0.0f;
		uint32_t ReachFlags = 0;
		bool Pruned = false;
	};

	enum class HazardWaterEgressRouteCertificateResult : uint8_t
	{
		Certified,
		NoEligibleDirectFirstHop,
		NoStaticWalkContinuation,
		SearchBudgetExhausted,
	};

	struct HazardWaterEgressRouteCertificateRequest
	{
		std::vector<HazardWaterEgressRouteNode> Nodes;
		std::vector<HazardWaterEgressRouteEdge> Edges;
		float PawnCollisionRadius = 0.0f;
		float PawnCollisionHeight = 0.0f;
		size_t MaximumVisitedNodes = 64;
	};

	struct HazardWaterEgressRouteCertificate
	{
		HazardWaterEgressRouteCertificateResult Result =
			HazardWaterEgressRouteCertificateResult::NoEligibleDirectFirstHop;
		bool FirstHopKnown = false;
		size_t FirstHopNode = 0;
		bool ContinuationKnown = false;
		size_t ContinuationNode = 0;
		float StaticWalkCost = 0.0f;
		size_t StaticWalkHops = 0;
		size_t VisitedNodes = 0;

		bool IsCertified() const
		{
			return Result == HazardWaterEgressRouteCertificateResult::Certified;
		}
	};

	HazardWaterEgressRouteCertificate CertifyHazardWaterEgressStaticWalkRoute(
		const HazardWaterEgressRouteCertificateRequest& request);
	const char* HazardWaterEgressRouteCertificateResultName(
		HazardWaterEgressRouteCertificateResult result);
}
