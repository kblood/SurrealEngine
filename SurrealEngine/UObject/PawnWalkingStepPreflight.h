#pragma once

#include "Math/vec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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
		bool CollisionCallbackRequired = false;
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
		CollisionCallbackRequiredScriptTransitionUnknown,
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
		HarmfulPainFall,
		Count
	};

	constexpr size_t WalkingStepPreflightReasonCount =
		static_cast<size_t>(WalkingStepPreflightReason::Count);

	const char* WalkingStepPreflightReasonMetricName(WalkingStepPreflightReason reason);

	struct WalkingStepPreflightResult
	{
		WalkingStepPreflightDecision Decision = WalkingStepPreflightDecision::NoDecision;
		WalkingStepPreflightReason Reason = WalkingStepPreflightReason::IneligibleUnknownActor;
	};

	WalkingStepPreflightResult EvaluateWalkingStepPreflight(
		const WalkingStepPreflightInput& input);

	struct WalkingStepPreflightEpisodeState
	{
		bool Active = false;
		const void* PawnLife = nullptr;
		uint64_t PawnLifeGeneration = 0;
		vec3 SupportedOrigin = vec3(0.0f);
		const void* SemanticTarget = nullptr;
		vec3 SemanticDestination = vec3(0.0f);
	};

	struct WalkingStepPreflightEpisodeObservation
	{
		bool Authorized = false;
		const void* PawnLife = nullptr;
		uint64_t PawnLifeGeneration = 0;
		vec3 SupportedOrigin = vec3(0.0f);
		const void* SemanticTarget = nullptr;
		vec3 SemanticDestination = vec3(0.0f);
		float OriginRadius = 8.0f;
		float DestinationRadius = 8.0f;
	};

	struct WalkingStepPreflightEpisodeUpdate
	{
		WalkingStepPreflightEpisodeState State;
		bool AuthorizationStarted = false;
	};

	WalkingStepPreflightEpisodeUpdate UpdateWalkingStepPreflightEpisode(
		const WalkingStepPreflightEpisodeState& state,
		const WalkingStepPreflightEpisodeObservation& observation);

	struct WalkingStepPreflightProbeDiagnostic
	{
		WalkingStepCollisionKind Collision = WalkingStepCollisionKind::Unknown;
		float Fraction = 1.0f;
		vec3 Delta = vec3(0.0f);
		vec3 Normal = vec3(0.0f);
	};

	struct WalkingStepPreflightDiagnosticRecord
	{
		std::string SourcePawnActor;
		uint64_t Sequence = 0;
		uint64_t LifeGeneration = 0;
		uint64_t InvocationToken = 0;
		int WalkingIteration = 0;
		std::string Phase = "precommit_provisional";
		std::string TransitionOutcome;
		WalkingStepPreflightReason Reason = WalkingStepPreflightReason::IneligibleUnknownActor;
		vec3 PrecommitOrigin = vec3(0.0f);
		vec3 PredictedUnsupportedEndpoint = vec3(0.0f);
		vec3 ActualUnsupportedEndpoint = vec3(0.0f);
		std::string SemanticTarget;
		vec3 SemanticDestination = vec3(0.0f);
		WalkingStepPreflightProbeDiagnostic StartSupport;
		WalkingStepPreflightProbeDiagnostic StepUp;
		WalkingStepPreflightProbeDiagnostic Forward;
		WalkingStepPreflightProbeDiagnostic ActualStepDown;
		WalkingStepPreflightProbeDiagnostic SupportProbe;
		bool FallForecastAttempted = false;
		vec3 FallForecastOrigin = vec3(0.0f);
		vec3 FallForecastVelocity = vec3(0.0f);
		vec3 FallForecastAcceleration = vec3(0.0f);
		bool FallForecastGravityKnown = false;
		vec3 FallForecastGravity = vec3(0.0f);
		WalkingFallForecastObservation FallForecast;
		std::array<float, 3> FallHitFractions = {};
		size_t FallHitCount = 0;
	};
}
