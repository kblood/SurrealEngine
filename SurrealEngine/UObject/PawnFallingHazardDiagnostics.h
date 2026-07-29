#pragma once

#include "PawnFallingHazardTrajectoryModel.h"

#include <cstdint>
#include <string>

namespace PawnMovement
{
	enum class FallingHazardDiagnosticKind
	{
		Start,
		Terminal,
		GenerationCapacityExceeded
	};

	// This is command-continuity evidence only. In particular, an intact command
	// still has no same-slice opportunity to change an aligned fall after HitWall.
	enum class FallingHazardAlignedCommandProvenance
	{
		NotAlignedContinuation,
		NoCommandWitness,
		NonStaticCollision,
		NoLiveMovementCommand,
		CommandTokenChanged,
		LatentStateChanged,
		MoveTargetChanged,
		DestinationChanged,
		AccelerationChanged,
		IntactCommandButNoActionLead,
	};

	struct FallingHazardDiagnosticRecord
	{
		FallingHazardDiagnosticKind Kind = FallingHazardDiagnosticKind::Start;
		std::string SourcePawnActor;
		uint64_t Sequence = 0;
		float PrechargedElapsed = 0.0f;
		FallingHazardAlignedCommandProvenance AlignedCommandProvenance =
			FallingHazardAlignedCommandProvenance::NotAlignedContinuation;
		FallingHazardGenerationState Generation;
		FallingHazardCorrelation Correlation = FallingHazardCorrelation::Pending;
	};

	inline const char* FallingHazardDiagnosticKindName(
		FallingHazardDiagnosticKind kind)
	{
		switch (kind)
		{
		case FallingHazardDiagnosticKind::Start: return "start";
		case FallingHazardDiagnosticKind::Terminal: return "terminal";
		case FallingHazardDiagnosticKind::GenerationCapacityExceeded:
			return "generation_capacity_exceeded";
		}
		return "unknown";
	}

	inline const char* FallingHazardAlignedCommandProvenanceName(
		FallingHazardAlignedCommandProvenance provenance)
	{
		switch (provenance)
		{
		case FallingHazardAlignedCommandProvenance::NotAlignedContinuation:
			return "not_aligned_continuation";
		case FallingHazardAlignedCommandProvenance::NoCommandWitness:
			return "no_command_witness";
		case FallingHazardAlignedCommandProvenance::NonStaticCollision:
			return "nonstatic_collision";
		case FallingHazardAlignedCommandProvenance::NoLiveMovementCommand:
			return "no_live_movement_command";
		case FallingHazardAlignedCommandProvenance::CommandTokenChanged:
			return "command_token_changed";
		case FallingHazardAlignedCommandProvenance::LatentStateChanged:
			return "latent_state_changed";
		case FallingHazardAlignedCommandProvenance::MoveTargetChanged:
			return "move_target_changed";
		case FallingHazardAlignedCommandProvenance::DestinationChanged:
			return "destination_changed";
		case FallingHazardAlignedCommandProvenance::AccelerationChanged:
			return "acceleration_changed";
		case FallingHazardAlignedCommandProvenance::IntactCommandButNoActionLead:
			return "intact_command_but_no_action_lead";
		}
		return "not_aligned_continuation";
	}

	inline const char* FallingHazardForecastSourceName(
		FallingHazardForecastSource source)
	{
		switch (source)
		{
		case FallingHazardForecastSource::Unknown: return "unknown";
		case FallingHazardForecastSource::UnsupportedWalkCommit:
			return "unsupported_walk_commit";
		case FallingHazardForecastSource::ExistingFallingCommit:
			return "existing_falling_commit";
		case FallingHazardForecastSource::PostWallDeflectionCommit:
			return "post_wall_deflection_commit";
		case FallingHazardForecastSource::AlignedContinuationCommit:
			return "aligned_continuation_commit";
		case FallingHazardForecastSource::ThirdMoveContinuationCommit:
			return "third_move_continuation_commit";
		case FallingHazardForecastSource::CallbackReturnCommit:
			return "callback_return_commit";
		case FallingHazardForecastSource::ScriptTickTransitionCommit:
			return "script_tick_transition_commit";
		case FallingHazardForecastSource::ExternalImpulseCommit:
			return "external_impulse_commit";
		case FallingHazardForecastSource::HorizonContinuationCommit:
			return "horizon_continuation_commit";
		}
		return "unknown";
	}

	inline const char* FallingHazardForecastName(FallingHazardForecast forecast)
	{
		switch (forecast)
		{
		case FallingHazardForecast::Unknown: return "unknown";
		case FallingHazardForecast::NoHarmfulPainObserved:
			return "no_harmful_pain_observed";
		case FallingHazardForecast::HarmfulPainObserved:
			return "harmful_pain_observed";
		}
		return "unknown";
	}

	inline const char* FallingHazardTerminalName(FallingHazardTerminal terminal)
	{
		switch (terminal)
		{
		case FallingHazardTerminal::Active: return "active";
		case FallingHazardTerminal::HarmfulPainEntered:
			return "harmful_pain_entered";
		case FallingHazardTerminal::Landed: return "landed";
		case FallingHazardTerminal::Died: return "died";
		case FallingHazardTerminal::CallbackBoundary: return "callback_boundary";
		case FallingHazardTerminal::ExternalImpulseBoundary:
			return "external_impulse_boundary";
		case FallingHazardTerminal::ContinuityLost: return "continuity_lost";
		case FallingHazardTerminal::WaterPhysicsBoundary:
			return "water_physics_boundary";
		case FallingHazardTerminal::ObservationHorizonExhausted:
			return "observation_horizon_exhausted";
		case FallingHazardTerminal::SupersededByCommittedSource:
			return "superseded_by_committed_source";
		case FallingHazardTerminal::SweptSegmentBudgetExceeded:
			return "swept_segment_budget_exceeded";
		case FallingHazardTerminal::InvalidObservation:
			return "invalid_observation";
		}
		return "unknown";
	}

	inline const char* FallingHazardCorrelationName(
		FallingHazardCorrelation correlation)
	{
		switch (correlation)
		{
		case FallingHazardCorrelation::Pending: return "pending";
		case FallingHazardCorrelation::Unknown: return "unknown";
		case FallingHazardCorrelation::Ambiguous: return "ambiguous";
		case FallingHazardCorrelation::ConfirmedHarmfulForecast:
			return "confirmed_harmful_forecast";
		case FallingHazardCorrelation::ForecastOnly: return "forecast_only";
		case FallingHazardCorrelation::ActualOnly: return "actual_only";
		case FallingHazardCorrelation::ConfirmedNoHarmfulObservation:
			return "confirmed_no_harmful_observation";
		}
		return "unknown";
	}

	inline const char* FallingHazardCollisionName(
		FallingHazardCollisionKind collision)
	{
		switch (collision)
		{
		case FallingHazardCollisionKind::Unknown: return "unknown";
		case FallingHazardCollisionKind::Clear: return "clear";
		case FallingHazardCollisionKind::StaticWorld: return "static_world";
		case FallingHazardCollisionKind::Mover: return "mover";
		case FallingHazardCollisionKind::DynamicActor: return "dynamic_actor";
		}
		return "unknown";
	}
}
