#include "UObject/PawnFallingParityRealizedTrace.h"

#include <iostream>
#include <string>

namespace
{
	int Failures = 0;
	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}
}

int main()
{
	using namespace PawnMovement;
	const FallingParityRealizedCorrelation correlation = {
		.SourcePawnActor = "Necroth",
		.LifeGeneration = 2,
		.InvocationToken = 71,
		.WalkingIteration = 0
	};
	auto began = BeginFallingParityRealizedTrace(true, true, true, correlation, 3);
	Check(began.State.LifecycleActive && began.State.ModelActive && began.EmitRecord,
		"an actual benchmark unsupported BeginFalling transition arms the trace");
	Check(began.Record.Outcome == FallingParityRealizedOutcome::EpisodeStarted,
		"arming emits one correlation record independent of provisional authorization");
	Check(!BeginFallingParityRealizedTrace(false, true, true, correlation).State.LifecycleActive,
		"ordinary and spectator play cannot arm the benchmark trace");
	Check(!BeginFallingParityRealizedTrace(true, true, false, correlation).State.LifecycleActive,
		"a non-falling ledge transition does not arm the trace");

	FallingParityRealizedRecord evidence;
	evidence.Elapsed = 1.0f / 60.0f;
	evidence.CallbackBarrierMask = (1u << 3) | (1u << 5);
	auto first = AdvanceFallingParityRealizedTrace(began.State,
		FallingParityRealizedOutcome::MatchedClear, evidence);
	Check(first.State.ModelActive && first.State.StepOrdinal == 1
		&& first.State.RemainingStepBudget == 2,
		"a matched realized step advances the bounded model");
	FallingParityRealizedRecord landingEvidence;
	landingEvidence.Elapsed = 1.0f / 60.0f;
	landingEvidence.Collision = FallingParityCollisionKind::StaticWorld;
	landingEvidence.HitFraction = 0.5f;
	landingEvidence.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	auto matchedLanding = AdvanceFallingParityRealizedTrace(first.State,
		FallingParityRealizedOutcome::MatchedLanding, landingEvidence);
	Check(matchedLanding.EmitRecord && !matchedLanding.State.ModelActive
		&& matchedLanding.State.LifecycleActive
		&& matchedLanding.State.StepOrdinal == 2
		&& matchedLanding.Record.Outcome
			== FallingParityRealizedOutcome::MatchedLanding,
		"a matched direct landing is a realized step that stops the model");
	Check(std::string(FallingParityRealizedOutcomeName(
		FallingParityRealizedOutcome::MatchedLanding)) == "matched_landing",
		"a matched landing has vocabulary distinct from the terminal landing");
	auto landed = FinishFallingParityRealizedTrace(matchedLanding.State,
		FallingParityRealizedOutcome::Landed);
	Check(landed.EmitRecord && !landed.State.LifecycleActive
		&& landed.Record.Outcome == FallingParityRealizedOutcome::Landed
		&& landed.Record.StepOrdinal == 2,
		"the terminal landed record closes a matched landing lifecycle separately");
	auto barrier = AdvanceFallingParityRealizedTrace(first.State,
		FallingParityRealizedOutcome::CallbackBarrier, evidence);
	Check(!barrier.State.ModelActive && barrier.State.LifecycleActive,
		"HitWall terminates modeling but retains pain/death correlation");
	Check(barrier.Record.CallbackBarrierMask == ((1u << 3) | (1u << 5)),
		"callback evidence from the realized move is retained without another probe");
	Check(!AdvanceFallingParityRealizedTrace(barrier.State,
		FallingParityRealizedOutcome::MatchedClear, evidence).EmitRecord,
		"no model records are emitted after a callback barrier");

	auto pain = ObserveFallingParityRealizedPain(barrier.State);
	Check(pain.EmitRecord && pain.State.PainObserved
		&& pain.Record.Outcome == FallingParityRealizedOutcome::PainEntered,
		"actual pain entry remains correlated after the callback barrier");
	Check(!ObserveFallingParityRealizedPain(pain.State).EmitRecord,
		"pain entry is recorded once per trace");
	auto death = FinishFallingParityRealizedTrace(pain.State,
		FallingParityRealizedOutcome::Died);
	Check(death.EmitRecord && !death.State.LifecycleActive
		&& death.Record.Correlation.InvocationToken == correlation.InvocationToken,
		"death finalizes the original walking correlation");
	Check(!FinishFallingParityRealizedTrace(death.State,
		FallingParityRealizedOutcome::Landed).EmitRecord,
		"a synchronous fatal landing cannot be overwritten by a landed terminal");
	auto continuity = FinishFallingParityRealizedTrace(barrier.State,
		FallingParityRealizedOutcome::ContinuityLost);
	Check(continuity.EmitRecord && !continuity.State.LifecycleActive,
		"a callback that breaks falling continuity ends later attribution");
	auto replacement = BeginFallingParityRealizedTrace(true, true, true, {
		.SourcePawnActor = "Necroth",
		.LifeGeneration = 2,
		.InvocationToken = 72,
		.WalkingIteration = 1
	});
	Check(replacement.EmitRecord
		&& replacement.Record.Correlation.InvocationToken == 72
		&& continuity.Record.Correlation.InvocationToken == 71,
		"closing an active lifecycle before re-arm preserves both correlations");

	auto bounded = BeginFallingParityRealizedTrace(true, true, true, correlation, 1);
	bounded = AdvanceFallingParityRealizedTrace(bounded.State,
		FallingParityRealizedOutcome::MatchedClear, evidence);
	Check(bounded.EmitRecord && !bounded.State.ModelActive
		&& bounded.State.StepOrdinal == 1
		&& bounded.State.RemainingStepBudget == 0
		&& bounded.Record.Outcome == FallingParityRealizedOutcome::MatchedClear,
		"the last allowed actual step is retained and closes modeling immediately");
	auto exhausted = AdvanceFallingParityRealizedTrace(bounded.State,
		FallingParityRealizedOutcome::MatchedClear, evidence);
	Check(!exhausted.EmitRecord && !exhausted.State.ModelActive
		&& exhausted.State.StepOrdinal == 1,
		"an exhausted budget cannot emit a pseudo realized step");
	auto defensiveZeroBudget = began.State;
	defensiveZeroBudget.RemainingStepBudget = 0;
	auto defensiveExhausted = AdvanceFallingParityRealizedTrace(
		defensiveZeroBudget, FallingParityRealizedOutcome::MatchedClear, evidence);
	Check(!defensiveExhausted.EmitRecord
		&& !defensiveExhausted.State.ModelActive,
		"a legacy active zero-budget state closes silently");

	if (Failures == 0)
		std::cout << "Pawn falling parity realized trace tests passed\n";
	return Failures == 0 ? 0 : 1;
}
