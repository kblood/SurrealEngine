#pragma once

#include "Math/vec.h"

namespace PawnMovement
{
	struct FallPredictionState
	{
		vec3 Location;
		vec3 Velocity;
		bool Valid = true;
	};

	struct FallPredictionStep
	{
		vec3 Acceleration;
		vec3 Gravity;
		vec3 ZoneVelocity;
		float GroundSpeed = 0.0f;
		float TerminalVelocity = 0.0f;
		float Elapsed = 0.0f;
	};

	enum class FallCollisionKind
	{
		StaticWorld,
		Mover,
		DynamicActor
	};

	enum class FirstWallContinuationDecision
	{
		Unknown,
		SafeLanding,
		ProbeAlignedSweep,
		Continue
	};

	struct FirstWallContinuationInput
	{
		FallPredictionState StepStart;
		FallPredictionState ProposedEnd;
		vec3 HitNormal;
		float HitFraction = 0.0f;
		float Elapsed = 0.0f;
		FallCollisionKind CollisionKind = FallCollisionKind::DynamicActor;
		int PriorWallContactCount = 0;
		int MaxWallContactCount = 0;
		bool SameStaticBspSurfaceAsFirst = false;
		vec3 FirstWallNormal;
	};

	struct FirstWallContinuationResult
	{
		FirstWallContinuationDecision Decision = FirstWallContinuationDecision::Unknown;
		vec3 HitSegmentDelta;
		vec3 AlignedSlideDelta;
		FallPredictionState Next = { .Valid = false };
	};

	FallPredictionState PredictFallStep(const FallPredictionState& state, const FallPredictionStep& step);
	FirstWallContinuationResult EvaluateFirstWallContinuation(const FirstWallContinuationInput& input);
	FirstWallContinuationResult CompleteFirstWallContinuation(
		const FirstWallContinuationResult& candidate, bool alignedSweepClear);
	int BoundedFallSegmentSampleCount(float segmentDistance, float maxSpacing, int maxSamples);
	bool IsAIControlledPlayer(bool isPlayer, bool isPlayerPawn, bool hasPlayerController);
	bool ShouldVetoPainZoneLedge(bool aiPlayerBot, bool normalDownwardGravity,
		bool currentlyInPainZone, bool predictedHarmfulPainZone);
}
