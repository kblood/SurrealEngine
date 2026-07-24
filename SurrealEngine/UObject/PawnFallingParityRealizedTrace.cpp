#include "PawnFallingParityRealizedTrace.h"

namespace PawnMovement
{
	namespace
	{
		FallingParityRealizedRecord MakeRecord(
			const FallingParityRealizedTraceState& state,
			FallingParityRealizedOutcome outcome)
		{
			FallingParityRealizedRecord record;
			record.Correlation = state.Correlation;
			record.StepOrdinal = state.StepOrdinal;
			record.Outcome = outcome;
			return record;
		}
	}

	FallingParityRealizedUpdate BeginFallingParityRealizedTrace(
		bool benchmarkEnabled, bool eligibleStockBot,
		bool actualUnsupportedBeginFalling,
		const FallingParityRealizedCorrelation& correlation,
		uint32_t stepBudget)
	{
		FallingParityRealizedUpdate update;
		if (!benchmarkEnabled || !eligibleStockBot
			|| !actualUnsupportedBeginFalling || stepBudget == 0
			|| correlation.SourcePawnActor.empty()
			|| correlation.InvocationToken == 0
			|| correlation.WalkingIteration < 0)
			return update;
		update.State.LifecycleActive = true;
		update.State.ModelActive = true;
		update.State.RemainingStepBudget = stepBudget;
		update.State.Correlation = correlation;
		update.Record = MakeRecord(update.State,
			FallingParityRealizedOutcome::EpisodeStarted);
		update.EmitRecord = true;
		return update;
	}

	FallingParityRealizedUpdate AdvanceFallingParityRealizedTrace(
		const FallingParityRealizedTraceState& state,
		FallingParityRealizedOutcome outcome,
		const FallingParityRealizedRecord& evidence)
	{
		FallingParityRealizedUpdate update;
		update.State = state;
		if (!state.LifecycleActive || !state.ModelActive)
			return update;
		if (state.RemainingStepBudget == 0)
		{
			update.State.ModelActive = false;
			return update;
		}

		update.Record = evidence;
		update.Record.Correlation = state.Correlation;
		update.Record.StepOrdinal = state.StepOrdinal;
		update.Record.Outcome = outcome;
		update.EmitRecord = true;
		update.State.StepOrdinal++;
		update.State.RemainingStepBudget--;
		if (update.State.RemainingStepBudget == 0
			|| outcome == FallingParityRealizedOutcome::MatchedLanding
			|| outcome == FallingParityRealizedOutcome::Unknown
			|| outcome == FallingParityRealizedOutcome::Mismatch
			|| outcome == FallingParityRealizedOutcome::CallbackBarrier)
			update.State.ModelActive = false;
		return update;
	}

	FallingParityRealizedUpdate ObserveFallingParityRealizedPain(
		const FallingParityRealizedTraceState& state)
	{
		FallingParityRealizedUpdate update;
		update.State = state;
		if (!state.LifecycleActive || state.PainObserved)
			return update;
		update.State.PainObserved = true;
		update.Record = MakeRecord(state,
			FallingParityRealizedOutcome::PainEntered);
		update.EmitRecord = true;
		return update;
	}

	FallingParityRealizedUpdate FinishFallingParityRealizedTrace(
		const FallingParityRealizedTraceState& state,
		FallingParityRealizedOutcome outcome)
	{
		FallingParityRealizedUpdate update;
		update.State = state;
		if (!state.LifecycleActive
			|| (outcome != FallingParityRealizedOutcome::Landed
				&& outcome != FallingParityRealizedOutcome::Died
				&& outcome != FallingParityRealizedOutcome::ContinuityLost))
			return update;
		update.State.LifecycleActive = false;
		update.State.ModelActive = false;
		update.Record = MakeRecord(state, outcome);
		update.EmitRecord = true;
		return update;
	}

	const char* FallingParityRealizedOutcomeName(
		FallingParityRealizedOutcome outcome)
	{
		switch (outcome)
		{
		case FallingParityRealizedOutcome::EpisodeStarted: return "episode_started";
		case FallingParityRealizedOutcome::MatchedClear: return "matched_clear";
		case FallingParityRealizedOutcome::MatchedLanding: return "matched_landing";
		case FallingParityRealizedOutcome::Mismatch: return "mismatch";
		case FallingParityRealizedOutcome::Unknown: return "unknown";
		case FallingParityRealizedOutcome::CallbackBarrier: return "callback_barrier";
		case FallingParityRealizedOutcome::PainEntered: return "pain_entered";
		case FallingParityRealizedOutcome::Landed: return "landed";
		case FallingParityRealizedOutcome::Died: return "died";
		case FallingParityRealizedOutcome::ContinuityLost: return "continuity_lost";
		default: return "unknown";
		}
	}
}
