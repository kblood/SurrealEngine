#include "BotAI/UtilityBotPolicy.h"

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

static EnemyObservation Enemy(std::string identity, bool visible, bool lineOfSight, double confidence = 1.0)
{
	EnemyObservation enemy;
	enemy.Identity = std::move(identity);
	enemy.Visible = visible;
	enemy.HasLineOfSight = lineOfSight;
	enemy.Confidence = confidence;
	enemy.Distance = 300.0;
	enemy.EstimatedHealthFraction = 1.0;
	return enemy;
}

static ItemObservation Item(std::string identity, double utility, double danger = 0.0)
{
	ItemObservation item;
	item.Identity = std::move(identity);
	item.Utility = utility;
	item.RouteDanger = danger;
	item.Distance = 100.0;
	item.Reachable = true;
	return item;
}

static Observation ArmedObservation(uint64_t tick = 1)
{
	Observation observation;
	observation.Tick = tick;
	observation.HealthFraction = 1.0;
	observation.HasUsableWeapon = true;
	return observation;
}

static bool SameDecision(const Decision& left, const Decision& right)
{
	if (left.Selected != right.Selected || left.TargetIdentity != right.TargetIdentity
		|| left.Score != right.Score || left.Reason != right.Reason
		|| left.Alternatives.size() != right.Alternatives.size())
		return false;
	for (size_t index = 0; index < left.Alternatives.size(); index++)
	{
		const Alternative& leftAlternative = left.Alternatives[index];
		const Alternative& rightAlternative = right.Alternatives[index];
		if (leftAlternative.Candidate != rightAlternative.Candidate
			|| leftAlternative.Score != rightAlternative.Score
			|| leftAlternative.Reason != rightAlternative.Reason)
			return false;
	}
	return true;
}

static void TestIdentityVersionResetAndDeterminism()
{
	UtilityBotPolicy policy;
	Check(std::string(policy.GetId()) == "utility-arena", "policy has stable utility-arena ID");
	Check(policy.GetVersion() == 1, "policy has version one");

	Observation observation = ArmedObservation();
	observation.Enemies.push_back(Enemy("enemy", true, true));
	policy.Reset(11);
	Decision first = policy.Tick(observation);
	policy.Reset(999);
	Decision second = policy.Tick(observation);
	Check(SameDecision(first, second), "reset clears policy history and equivalent observations are deterministic");
}

static void TestStuckRecoveryPreemptsEverything()
{
	UtilityBotPolicy policy;
	Observation observation = ArmedObservation();
	observation.Items.push_back(Item("armor", 0.2, 0.53));
	Check(policy.Tick(observation).Selected == Action::AcquireItem, "policy has an action to suspend");

	observation.Tick++;
	observation.StuckSeconds = 1.5;
	observation.Enemies.push_back(Enemy("enemy", true, true));
	observation.Items.push_back(Item("super-item", 10.0));
	Decision decision = policy.Tick(observation);
	Check(decision.Selected == Action::RecoverFromStuck, "stuck recovery preempts combat and items");
	Check(decision.Score >= 1000.0, "stuck recovery has an inspectable priority score");

	observation.Tick++;
	observation.StuckSeconds = 0.0;
	observation.Enemies.clear();
	observation.Items.resize(1);
	observation.Items[0].RouteDanger = 0.65;
	Decision resumed = policy.Tick(observation);
	Check(resumed.Selected == Action::AcquireItem, "stuck recovery resumes the suspended viable action");
}

static void TestVisibleCombat()
{
	UtilityBotPolicy policy;
	Observation observation = ArmedObservation();
	observation.Enemies.push_back(Enemy("visible-enemy", true, true));
	Decision decision = policy.Tick(observation);
	Check(decision.Selected == Action::AttackEnemy, "armed bot attacks a visible line-of-sight enemy");
	Check(decision.TargetIdentity == "visible-enemy", "attack names its selected enemy");
	Check(decision.Alternatives.size() == 6, "decision reports every scored behavior");
}

static void TestLowHealthRetreat()
{
	UtilityBotPolicy policy;
	Observation observation = ArmedObservation();
	observation.HealthFraction = 0.1;
	observation.RecentIncomingDamage = 0.4;
	observation.Enemies.push_back(Enemy("threat", true, true));
	Decision decision = policy.Tick(observation);
	Check(decision.Selected == Action::Retreat, "low health and incoming damage outweigh attack");
	Check(decision.TargetIdentity == "threat", "retreat identifies the deterministic threat");
}

static void TestMemoryHunt()
{
	UtilityBotPolicy policy;
	Observation observation = ArmedObservation();
	EnemyObservation memory = Enemy("remembered", false, false, 0.9);
	memory.SecondsSinceObservation = 2.0;
	observation.Enemies.push_back(memory);
	Decision decision = policy.Tick(observation);
	Check(decision.Selected == Action::HuntEnemy, "recent confident enemy memory produces a hunt");
	Check(decision.TargetIdentity == "remembered", "hunt names its remembered enemy");
}

static void TestItemChoiceAndTieOrdering()
{
	UtilityBotPolicy policy;
	Observation observation = ArmedObservation();
	observation.Items.push_back(Item("zeta", 0.8));
	observation.Items.push_back(Item("alpha", 0.8));
	observation.Items.push_back(Item("dangerous", 1.0, 1.0));
	Decision decision = policy.Tick(observation);
	Check(decision.Selected == Action::AcquireItem, "reachable high-utility item beats exploration");
	Check(decision.TargetIdentity == "alpha", "equal item scores use lexical identity ordering");
}

static void TestHysteresisAndReset()
{
	UtilityBotPolicy policy;
	Observation observation = ArmedObservation(10);
	observation.Items.push_back(Item("armor", 0.2, 0.53));
	Decision first = policy.Tick(observation);
	Check(first.Selected == Action::AcquireItem, "item initially wins by a narrow margin");

	observation.Tick++;
	observation.Items[0].RouteDanger = 0.65;
	Decision persisted = policy.Tick(observation);
	Check(persisted.Selected == Action::AcquireItem, "close score change does not thrash to exploration");
	Check(persisted.Reason.find("hysteresis") != std::string::npos, "persisted action explains hysteresis");

	policy.Reset(0);
	Decision afterReset = policy.Tick(observation);
	Check(afterReset.Selected == Action::Explore, "reset clears persisted action state");
}

int main()
{
	TestIdentityVersionResetAndDeterminism();
	TestStuckRecoveryPreemptsEverything();
	TestVisibleCombat();
	TestLowHealthRetreat();
	TestMemoryHunt();
	TestItemChoiceAndTieOrdering();
	TestHysteresisAndReset();
	if (failures == 0)
		std::cout << "All utility bot policy tests passed.\n";
	return failures == 0 ? 0 : 1;
}
