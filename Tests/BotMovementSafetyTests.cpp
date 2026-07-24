#include "BotAI/BotMovementSafety.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace BotAI;

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static MovementSafetyContext Context()
{
	MovementSafetyContext context;
	context.DesiredDirection = { 1.0, 0.0, 0.0 };
	context.Velocity = { 300.0, 0.0, 0.0 };
	context.CollisionRadius = 20.0;
	context.SafeDropDistance = 48.0;
	return context;
}

static MovementProbe Probe(uint32_t index, double x, double y)
{
	MovementProbe probe;
	probe.Index = index;
	probe.Direction = { x, y, 0.0 };
	probe.ForwardClearance = 256.0;
	return probe;
}

static void TestSafeForwardRouteContinues()
{
	MovementSafetyAdvisor advisor;
	MovementAdvice advice = advisor.Evaluate(Context(), { Probe(4, 0.0, 1.0), Probe(2, 1.0, 0.0) });
	Check(advice.ValidInput, "safe probes are valid");
	Check(advice.Action == MovementAdviceAction::Continue, "aligned safe route continues");
	Check(advice.SelectedProbeIndex == 2, "aligned route is selected");
}

static void TestPainAndLandingHazardsAreRejected()
{
	MovementProbe pain = Probe(1, 1.0, 0.0);
	pain.EntersPainZone = true;
	pain.ExpectedDamage = 40.0;
	MovementProbe noFloor = Probe(2, 0.9, 0.1);
	noFloor.HasLanding = false;
	MovementProbe drop = Probe(3, 0.8, -0.2);
	drop.LandingDrop = 96.0;
	MovementProbe safe = Probe(4, 0.0, 1.0);

	MovementSafetyAdvisor advisor;
	MovementAdvice advice = advisor.Evaluate(Context(), { pain, noFloor, drop, safe });
	Check(advice.Action == MovementAdviceAction::Steer, "advisor steers around hazards");
	Check(advice.SelectedProbeIndex == 4, "only safe supported route is selected");
	Check(advice.Alternatives.back().Status != MovementProbeStatus::Viable, "rejected probes remain inspectable");
}

static void TestCurrentPainZonePrioritizesEscape()
{
	MovementSafetyContext context = Context();
	context.InPainZone = true;
	MovementProbe remainsInPain = Probe(1, 1.0, 0.0);
	remainsInPain.EntersPainZone = true;
	MovementProbe exits = Probe(2, 0.0, 1.0);

	MovementSafetyAdvisor advisor;
	MovementAdvice advice = advisor.Evaluate(context, { remainsInPain, exits });
	Check(advice.Action == MovementAdviceAction::EscapeHazard, "pain escape has an explicit action");
	Check(advice.SelectedProbeIndex == 2, "leaving pain beats desired-direction alignment");
}

static void TestUnsafeJumpIsRejected()
{
	MovementProbe jump = Probe(1, 1.0, 0.0);
	jump.RequiresJump = true;
	jump.LandingReachable = false;
	MovementProbe side = Probe(2, 0.0, -1.0);

	MovementSafetyAdvisor advisor;
	MovementAdvice advice = advisor.Evaluate(Context(), { jump, side });
	Check(advice.SelectedProbeIndex == 2, "unreachable predicted jump is rejected");
	const auto rejected = std::find_if(advice.Alternatives.begin(), advice.Alternatives.end(), [](const MovementAlternative& item) { return item.ProbeIndex == 1; });
	Check(rejected != advice.Alternatives.end() && rejected->Status == MovementProbeStatus::UnsafeJump, "unsafe jump reason is inspectable");
}

static void TestStuckRecoveryCyclesBoundedCandidates()
{
	MovementSafetyContext context = Context();
	context.StuckSeconds = 1.5;
	MovementSafetyAdvisor advisor;
	const std::vector<MovementProbe> probes = { Probe(1, 1.0, 0.0), Probe(2, 0.0, 1.0), Probe(3, 0.0, -1.0) };
	MovementAdvice first = advisor.Evaluate(context, probes);
	MovementAdvice second = advisor.Evaluate(context, probes);
	Check(first.Action == MovementAdviceAction::RecoverFromStuck, "stuck context selects recovery action");
	Check(first.SelectedProbeIndex == 2, "stable index breaks equal lateral tie");
	Check(second.SelectedProbeIndex == 3, "previous recovery direction is penalized on retry");
	advisor.Reset();
	Check(advisor.Evaluate(context, probes).SelectedProbeIndex == 2, "reset clears recovery history");
}

static void TestBoundsValidationAndPermutationDeterminism()
{
	MovementSafetyAdvisor advisor;
	std::vector<MovementProbe> tooMany;
	for (uint32_t index = 0; index <= MaxMovementSafetyProbes; index++)
		tooMany.push_back(Probe(index, 1.0, 0.0));
	Check(!advisor.Evaluate(Context(), tooMany).ValidInput, "probe count is strictly bounded");

	MovementProbe invalid = Probe(1, 1.0, 0.0);
	invalid.ForwardClearance = std::numeric_limits<double>::quiet_NaN();
	MovementAdvice invalidAdvice = advisor.Evaluate(Context(), { invalid });
	Check(invalidAdvice.ValidInput && invalidAdvice.Action == MovementAdviceAction::Hold, "invalid probe data fails closed");

	const std::vector<MovementProbe> ordered = { Probe(8, 0.0, -1.0), Probe(3, 1.0, 0.0), Probe(5, 0.0, 1.0) };
	std::vector<MovementProbe> reversed = ordered;
	std::reverse(reversed.begin(), reversed.end());
	MovementAdvice left = advisor.Evaluate(Context(), ordered);
	MovementAdvice right = advisor.Evaluate(Context(), reversed);
	Check(left.SelectedProbeIndex == right.SelectedProbeIndex && left.Score == right.Score, "input order does not change selection");

	MovementProbe duplicate = Probe(3, 0.0, 1.0);
	Check(!advisor.Evaluate(Context(), { Probe(3, 1.0, 0.0), duplicate }).ValidInput, "duplicate stable indices are rejected");
}

int main()
{
	TestSafeForwardRouteContinues();
	TestPainAndLandingHazardsAreRejected();
	TestCurrentPainZonePrioritizesEscape();
	TestUnsafeJumpIsRejected();
	TestStuckRecoveryCyclesBoundedCandidates();
	TestBoundsValidationAndPermutationDeterminism();
	if (Failures == 0)
		std::cout << "Bot movement safety tests passed\n";
	return Failures == 0 ? 0 : 1;
}
