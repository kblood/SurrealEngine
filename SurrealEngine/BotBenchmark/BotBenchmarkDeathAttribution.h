#pragma once

namespace BotBenchmarkDeathAttribution
{
	enum class DamageInstigator
	{
		NoneOrNonPlayer,
		SelfPlayer,
		EnemyPlayer,
	};

	enum class DeathKiller
	{
		None,
		SelfPlayer,
		EnemyPlayer,
		NonPlayer,
	};

	enum class Kind
	{
		DirectSelfKill,
		DirectEnemyKill,
		UnassistedEnvironmentalDeath,
		RecentEnemyContributedEnvironmentalDeathProxy,
		AmbiguousDeath,
	};

	struct State
	{
		bool HasEnemyContribution = false;
		double LastEnemyContributionSeconds = 0.0;
		bool HasEnemyMomentumContribution = false;
		double LastEnemyMomentumContributionSeconds = 0.0;
	};

	struct DamageObservation
	{
		double TimeSeconds = 0.0;
		DamageInstigator Instigator = DamageInstigator::NoneOrNonPlayer;
		bool AppliedMomentum = false;
	};

	struct DeathObservation
	{
		double TimeSeconds = 0.0;
		DeathKiller Killer = DeathKiller::None;
		bool HasEnvironmentalEvidence = false;
	};

	struct Decision
	{
		Kind Attribution = Kind::AmbiguousDeath;
		bool HadRecentEnemyContribution = false;
		bool HadRecentEnemyMomentumContribution = false;
		State NextState;
	};

	State ObserveDamage(State state, const DamageObservation& observation);
	Decision ClassifyDeath(const State& state, const DeathObservation& observation,
		double recentContributionWindowSeconds);
}
