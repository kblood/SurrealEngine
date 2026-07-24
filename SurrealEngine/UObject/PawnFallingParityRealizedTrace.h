#pragma once

#include "PawnFallingParityForecast.h"

#include <cstdint>
#include <string>

namespace PawnMovement
{
	enum class FallingParityRealizedOutcome
	{
		EpisodeStarted,
		MatchedClear,
		Mismatch,
		Unknown,
		CallbackBarrier,
		PainEntered,
		Landed,
		Died,
		ContinuityLost
	};

	struct FallingParityRealizedCorrelation
	{
		std::string SourcePawnActor;
		uint64_t LifeGeneration = 0;
		uint64_t InvocationToken = 0;
		int WalkingIteration = 0;
	};

	struct FallingParityRealizedTraceState
	{
		bool LifecycleActive = false;
		bool ModelActive = false;
		bool PainObserved = false;
		uint32_t StepOrdinal = 0;
		uint32_t RemainingStepBudget = 0;
		FallingParityRealizedCorrelation Correlation;
	};

	struct FallingParityRealizedRecord
	{
		FallingParityRealizedCorrelation Correlation;
		uint32_t StepOrdinal = 0;
		FallingParityRealizedOutcome Outcome =
			FallingParityRealizedOutcome::Unknown;
		float Elapsed = 0.0f;
		FallingParityCollisionKind Collision = FallingParityCollisionKind::Unknown;
		float HitFraction = 1.0f;
		vec3 HitNormal = vec3(0.0f);
		float VelocityError = 0.0f;
		float RequestedDeltaError = 0.0f;
		float EndpointError = 0.0f;
		uint32_t CallbackBarrierMask = 0;
	};

	struct FallingParityRealizedUpdate
	{
		FallingParityRealizedTraceState State;
		FallingParityRealizedRecord Record;
		bool EmitRecord = false;
	};

	FallingParityRealizedUpdate BeginFallingParityRealizedTrace(
		bool benchmarkEnabled, bool eligibleStockBot,
		bool actualUnsupportedBeginFalling,
		const FallingParityRealizedCorrelation& correlation,
		uint32_t stepBudget = 96);

	FallingParityRealizedUpdate AdvanceFallingParityRealizedTrace(
		const FallingParityRealizedTraceState& state,
		FallingParityRealizedOutcome outcome,
		const FallingParityRealizedRecord& evidence);

	FallingParityRealizedUpdate ObserveFallingParityRealizedPain(
		const FallingParityRealizedTraceState& state);

	FallingParityRealizedUpdate FinishFallingParityRealizedTrace(
		const FallingParityRealizedTraceState& state,
		FallingParityRealizedOutcome outcome);

	const char* FallingParityRealizedOutcomeName(
		FallingParityRealizedOutcome outcome);
}
