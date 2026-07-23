#include "BotAI/BotPolicyShadow.h"

#include <iostream>
#include <string>

using namespace BotAI;

static int failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		failures++;
	}
}

static Observation CombatObservation(uint64_t tick)
{
	Observation observation;
	observation.Tick = tick;
	observation.DeltaSeconds = 0.1;
	observation.HealthFraction = 1.0;
	observation.AmmunitionFraction = 1.0;
	observation.HasUsableWeapon = true;
	EnemyObservation enemy;
	enemy.Identity = "shared-enemy";
	enemy.Distance = 256.0;
	enemy.Confidence = 1.0;
	enemy.Visible = true;
	enemy.HasLineOfSight = true;
	observation.Enemies.push_back(enemy);
	return observation;
}

static void TestAddHandlingAndStableOrder()
{
	PolicyShadowEvaluator shadow;
	Check(shadow.AddPolicy("utility-arena") == ShadowAddStatus::Added, "utility policy can be shadowed");
	Check(shadow.AddPolicy("tactical-state") == ShadowAddStatus::Added, "tactical policy can be shadowed");
	Check(shadow.AddPolicy("utility-arena") == ShadowAddStatus::AlreadyPresent, "duplicate policy is rejected");
	Check(shadow.AddPolicy("stock-botpack") == ShadowAddStatus::ScriptOwnedPolicy, "script policy is rejected by shadow evaluator");
	Check(shadow.AddPolicy("unknown") == ShadowAddStatus::UnknownPolicy, "unknown policy is rejected by shadow evaluator");

	const auto snapshots = shadow.GetSnapshots();
	Check(snapshots.size() == 2, "shadow evaluator owns two policy snapshots");
	Check(snapshots[0].PolicyId == "tactical-state" && snapshots[1].PolicyId == "utility-arena", "snapshots use deterministic policy ID order");
}

static void TestIdenticalObservationDeliveryAndTransitions()
{
	PolicyShadowEvaluator shadow;
	shadow.AddPolicy("utility-arena");
	shadow.AddPolicy("tactical-state");
	shadow.Reset(73);

	Observation observation = CombatObservation(100);
	shadow.Evaluate(observation);
	for (const ShadowPolicySnapshot& snapshot : shadow.GetSnapshots())
	{
		Check(snapshot.HasDecision, "each shadow policy receives the observation");
		Check(snapshot.LatestObservationTick == observation.Tick, "each policy records the identical observation tick");
		Check(snapshot.EvaluationCount == 1, "each policy is evaluated exactly once per observation");
		Check(snapshot.LatestDecision.Selected == Action::AttackEnemy, "both policies see the shared combat facts");
		Check(snapshot.LatestDecision.TargetIdentity == "shared-enemy", "both policies see the same target identity");
		Check(snapshot.ActionTransitionCount == 0, "first action is not a transition");
	}

	observation.Tick++;
	observation.StuckSeconds = 2.0;
	shadow.Evaluate(observation);
	shadow.Evaluate(observation);
	for (const ShadowPolicySnapshot& snapshot : shadow.GetSnapshots())
	{
		Check(snapshot.LatestDecision.Selected == Action::RecoverFromStuck, "stuck observation reaches every policy");
		Check(snapshot.EvaluationCount == 3, "evaluation count includes repeated observations");
		Check(snapshot.ActionTransitionCount == 1, "only the attack-to-recovery action change is counted");
	}
}

static void TestReset()
{
	PolicyShadowEvaluator shadow;
	shadow.AddPolicy("tactical-state");
	shadow.AddPolicy("utility-arena");
	shadow.Evaluate(CombatObservation(12));
	shadow.Reset(91);

	for (const ShadowPolicySnapshot& snapshot : shadow.GetSnapshots())
	{
		Check(!snapshot.HasDecision, "reset clears the latest decision");
		Check(snapshot.EvaluationCount == 0 && snapshot.ActionTransitionCount == 0, "reset clears shadow counters");
		Check(snapshot.LatestObservationTick == 0, "reset clears latest observation metadata");
		Check(snapshot.PolicyVersion == 1, "reset preserves policy descriptor metadata");
	}

	shadow.Evaluate(CombatObservation(12));
	for (const ShadowPolicySnapshot& snapshot : shadow.GetSnapshots())
		Check(snapshot.LatestDecision.Selected == Action::AttackEnemy, "owned policies remain usable after reset");
}

static void TestBoundedSnapshots()
{
	PolicyShadowEvaluator shadow(1);
	Check(shadow.GetMaximumPolicies() == 1, "configured snapshot capacity is exposed");
	Check(shadow.AddPolicy("utility-arena") == ShadowAddStatus::Added, "first policy fits bounded evaluator");
	Check(shadow.AddPolicy("tactical-state") == ShadowAddStatus::CapacityReached, "capacity rejects another valid policy");

	Observation observation = CombatObservation(0);
	for (uint64_t tick = 0; tick < 1000; tick++)
	{
		observation.Tick = tick;
		shadow.Evaluate(observation);
	}
	const auto snapshots = shadow.GetSnapshots();
	Check(snapshots.size() == 1 && shadow.GetPolicyCount() == 1, "evaluation history never grows the snapshot collection");
	Check(snapshots[0].EvaluationCount == 1000, "bounded snapshot retains aggregate evaluation count");
	Check(snapshots[0].LatestObservationTick == 999, "bounded snapshot retains only latest observation metadata");
}

int main()
{
	TestAddHandlingAndStableOrder();
	TestIdenticalObservationDeliveryAndTransitions();
	TestReset();
	TestBoundedSnapshots();
	if (failures == 0)
		std::cout << "All bot policy shadow tests passed.\n";
	return failures == 0 ? 0 : 1;
}
