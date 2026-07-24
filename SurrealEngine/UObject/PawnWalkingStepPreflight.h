#pragma once

#include "Math/vec.h"

#include <array>
#include <cstddef>

namespace PawnMovement
{
	enum class WalkingStepActorKind
	{
		Unknown,
		StockAutonomousPlayerBot,
		HumanPlayer,
		ScriptedPawn
	};

	enum class WalkingStepCollisionKind
	{
		Unknown,
		Clear,
		StaticBsp,
		Mover,
		DynamicActor
	};

	enum class WalkingStepZoneKind
	{
		Unknown,
		Safe,
		Pain,
		Water
	};

	struct WalkingStepSupportObservation
	{
		WalkingStepCollisionKind Collision = WalkingStepCollisionKind::Unknown;
		vec3 Normal = vec3(0.0f);
		WalkingStepZoneKind Zone = WalkingStepZoneKind::Unknown;
	};

	struct WalkingStepSweepObservation
	{
		WalkingStepCollisionKind Collision = WalkingStepCollisionKind::Unknown;
		vec3 Delta = vec3(0.0f);
		vec3 HitNormal = vec3(0.0f);
	};

	struct WalkingFallContinuationObservation
	{
		WalkingStepCollisionKind Collision = WalkingStepCollisionKind::Unknown;
		vec3 SegmentDelta = vec3(0.0f);
		vec3 HitNormal = vec3(0.0f);
	};

	struct WalkingFallForecastObservation
	{
		bool Complete = false;
		float TotalDrop = 0.0f;
		std::array<WalkingFallContinuationObservation, 2> Continuations;
		size_t ContinuationCount = 0;
		WalkingStepSupportObservation Landing;
		bool PainDamageImmunityKnown = false;
		bool PainDamageImmune = false;
	};

	struct WalkingStepPreflightInput
	{
		WalkingStepActorKind Actor = WalkingStepActorKind::Unknown;
		bool Walking = false;
		WalkingStepSupportObservation StartSupport;
		bool GravityKnown = false;
		vec3 Gravity = vec3(0.0f);
		bool UpwardJumpRequested = false;
		WalkingStepSweepObservation StepUp;
		WalkingStepSweepObservation Forward;
		std::array<WalkingStepSweepObservation, 1> Slides;
		size_t SlideCount = 0;
		WalkingStepSweepObservation StepDown;
		WalkingFallForecastObservation FallForecast;
		float WalkableNormalZ = 0.7f;
		float MaximumStepDelta = 96.0f;
		float MaximumFallSegmentDelta = 4096.0f;
		float MaximumForecastDrop = 4096.0f;
		float MaximumVerticalWallNormalZ = 0.2f;
	};

	enum class WalkingStepPreflightDecision
	{
		NoDecision,
		AuthorizeUnsafeStepVeto
	};

	enum class WalkingStepPreflightReason
	{
		IneligibleUnknownActor,
		IneligibleHumanPlayer,
		IneligibleScriptedPawn,
		NotWalking,
		UnknownStartSupport,
		NonStaticStartSupport,
		NonWalkableStartSupport,
		UnknownStartZone,
		UnsafeStartZone,
		UnknownGravity,
		NonAxialDownwardGravity,
		UpwardJumpRequested,
		InvalidBounds,
		InvalidStepDelta,
		UnknownStepEvidence,
		MoverStepEvidence,
		DynamicStepEvidence,
		InvalidStepSequence,
		SupportedStepEndpoint,
		IncompleteFallForecast,
		FallContinuationLimitExceeded,
		InvalidFallDelta,
		UnknownFallEvidence,
		MoverFallEvidence,
		DynamicFallEvidence,
		InvalidFallContinuation,
		InvalidFallLanding,
		UnknownLandingZone,
		SafeFallLanding,
		UnknownPainDamageImmunity,
		PainDamageImmune,
		HarmfulPainFall
	};

	struct WalkingStepPreflightResult
	{
		WalkingStepPreflightDecision Decision = WalkingStepPreflightDecision::NoDecision;
		WalkingStepPreflightReason Reason = WalkingStepPreflightReason::IneligibleUnknownActor;
	};

	WalkingStepPreflightResult EvaluateWalkingStepPreflight(
		const WalkingStepPreflightInput& input);
}
