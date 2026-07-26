#pragma once

#include "Math/vec.h"

#include <cstdint>
#include <string>

namespace PawnMovement
{
	// Pure reconstruction of the two observable walking callback predicates.
	// The MinHitWall predicate intentionally does not normalize HitNormal: UE1
	// collision supplies that normal, and the Pawn contract is its dot product
	// with Velocity.Normal.
	struct WalkingHitWallDispatchDecision
	{
		bool Valid = false;
		float NormalVelocityDot = 0.0f;
		bool LegacyVerticalWallBand = false;
		bool MinHitWallDispatch = false;
	};

	enum class WalkingHitWallBlockerKind
	{
		Unknown,
		StaticWorld,
		Mover,
		DynamicActor,
	};

	enum class WalkingHitWallContactPhase
	{
		PrimaryForward,
		AlignedSlide,
		ForwardRetryResult,
	};

	struct WalkingHitWallDispatchDiagnosticRecord
	{
		std::string SourcePawnActor;
		uint64_t Sequence = 0;
		WalkingHitWallContactPhase ContactPhase = WalkingHitWallContactPhase::PrimaryForward;
		vec3 HitNormal;
		vec3 Velocity;
		float MinHitWall = 0.0f;
		WalkingHitWallDispatchDecision Decision;
		WalkingHitWallBlockerKind Blocker = WalkingHitWallBlockerKind::Unknown;
		bool CallbackDispatched = false;
		bool CallbackSelectedByMinHitWallCandidate = false;
		bool PhysicsChangedByCallback = false;
		bool PawnDeletedByCallback = false;
	};

	inline const char* WalkingHitWallContactPhaseName(
		WalkingHitWallContactPhase phase)
	{
		switch (phase)
		{
		case WalkingHitWallContactPhase::PrimaryForward: return "primary_forward";
		case WalkingHitWallContactPhase::AlignedSlide: return "aligned_slide";
		case WalkingHitWallContactPhase::ForwardRetryResult: return "forward_retry_result";
		default: return "primary_forward";
		}
	}

	inline const char* WalkingHitWallBlockerKindName(
		WalkingHitWallBlockerKind kind)
	{
		switch (kind)
		{
		case WalkingHitWallBlockerKind::Unknown: return "unknown";
		case WalkingHitWallBlockerKind::StaticWorld: return "static_world";
		case WalkingHitWallBlockerKind::Mover: return "mover";
		case WalkingHitWallBlockerKind::DynamicActor: return "dynamic_actor";
		}
		return "unknown";
	}

	WalkingHitWallDispatchDecision EvaluateWalkingHitWallDispatch(
		const vec3& hitNormal, const vec3& velocity, float minHitWall);

	bool SelectWalkingHitWallDispatch(const WalkingHitWallDispatchDecision& decision,
		bool minHitWallCandidateEnabled);
}
