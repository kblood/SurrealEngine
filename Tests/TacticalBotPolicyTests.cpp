#include "BotAI/TacticalBotPolicy.h"

#include <iostream>
#include <string>
#include <utility>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	BotAI::Observation ReadyObservation(double deltaSeconds = 0.1)
	{
		BotAI::Observation observation;
		observation.DeltaSeconds = deltaSeconds;
		observation.HealthFraction = 1.0;
		observation.AmmunitionFraction = 1.0;
		observation.HasUsableWeapon = true;
		return observation;
	}

	BotAI::EnemyObservation VisibleEnemy(std::string identity)
	{
		BotAI::EnemyObservation enemy;
		enemy.Identity = std::move(identity);
		enemy.Visible = true;
		enemy.HasLineOfSight = true;
		enemy.Confidence = 1.0;
		enemy.Distance = 512.0;
		return enemy;
	}
}

int main()
{
	BotAI::TacticalBotPolicy policy;
	if (std::string(policy.GetId()) != "tactical-state" || policy.GetVersion() != 1)
		return Fail("tactical policy identity or version was incorrect");

	policy.Reset(17);
	BotAI::Observation observation = ReadyObservation();
	observation.Enemies.push_back(VisibleEnemy("enemy"));
	auto decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::AttackEnemy || decision.TargetIdentity != "enemy" ||
		policy.GetState() != BotAI::TacticalState::AcquireAttack)
		return Fail("visible enemy did not transition policy into attack");

	observation = ReadyObservation();
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::HuntEnemy || decision.TargetIdentity != "enemy" ||
		policy.GetState() != BotAI::TacticalState::HuntRememberedEnemy)
		return Fail("lost visible enemy did not transition policy into remembered hunt");

	observation = ReadyObservation(4.1);
	observation.ObjectiveUtility = 1.0;
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::Explore || !decision.TargetIdentity.empty() ||
		policy.GetState() != BotAI::TacticalState::IdleExplore)
		return Fail("expired enemy memory did not return policy to exploration");

	policy.Reset(19);
	observation = ReadyObservation();
	BotAI::EnemyObservation uncertain;
	uncertain.Identity = "footsteps";
	uncertain.Confidence = 0.7;
	uncertain.SecondsSinceObservation = 0.2;
	observation.Enemies.push_back(uncertain);
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::InvestigateSound || decision.TargetIdentity != "footsteps" ||
		policy.GetState() != BotAI::TacticalState::InvestigateUncertainObservation)
		return Fail("uncertain subjective observation did not enter investigation");

	policy.Reset(23);
	observation = ReadyObservation();
	observation.HealthFraction = 0.2;
	BotAI::ItemObservation risky;
	risky.Identity = "risky-health";
	risky.Reachable = true;
	risky.Utility = 5.0;
	risky.RouteDanger = 8.0;
	BotAI::ItemObservation safe;
	safe.Identity = "safe-health";
	safe.Reachable = true;
	safe.Utility = 4.0;
	safe.RouteDanger = 1.0;
	observation.Items = { risky, safe };
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::AcquireItem || decision.TargetIdentity != "safe-health" ||
		policy.GetState() != BotAI::TacticalState::ResupplyRetreat)
		return Fail("low resources did not select the safest useful resupply item");

	observation.Items.clear();
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::Retreat)
		return Fail("low resources without resupply did not retreat");

	policy.Reset(31);
	observation = ReadyObservation();
	observation.StuckSeconds = 1.5;
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::RecoverFromStuck ||
		policy.GetState() != BotAI::TacticalState::UnstuckRecovery)
		return Fail("stuck observation did not enter recovery");

	observation = ReadyObservation(0.1);
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::RecoverFromStuck)
		return Fail("recovery ended before movement was stably clear");
	observation.DeltaSeconds = 0.2;
	decision = policy.Tick(observation);
	if (decision.Selected != BotAI::Action::Idle || policy.GetState() != BotAI::TacticalState::IdleExplore)
		return Fail("policy did not leave recovery after stable clear movement");

	BotAI::Observation firstOrder = ReadyObservation();
	firstOrder.Enemies = { VisibleEnemy("zulu"), VisibleEnemy("alpha") };
	BotAI::Observation secondOrder = ReadyObservation();
	secondOrder.Enemies = { VisibleEnemy("alpha"), VisibleEnemy("zulu") };
	BotAI::TacticalBotPolicy firstPolicy;
	BotAI::TacticalBotPolicy secondPolicy;
	firstPolicy.Reset(41);
	secondPolicy.Reset(41);
	const auto firstDecision = firstPolicy.Tick(firstOrder);
	const auto secondDecision = secondPolicy.Tick(secondOrder);
	if (firstDecision.TargetIdentity != "alpha" || secondDecision.TargetIdentity != "alpha" ||
		firstDecision.Selected != secondDecision.Selected || firstDecision.Score != secondDecision.Score)
		return Fail("equal target selection depended on observation order");

	policy.Reset(47);
	observation = ReadyObservation();
	observation.Enemies.push_back(VisibleEnemy("forgotten"));
	policy.Tick(observation);
	policy.Reset(47);
	decision = policy.Tick(ReadyObservation());
	if (decision.Selected != BotAI::Action::Idle || !decision.TargetIdentity.empty() ||
		policy.GetState() != BotAI::TacticalState::IdleExplore)
		return Fail("reset did not clear tactical state and enemy memory");

	return 0;
}
