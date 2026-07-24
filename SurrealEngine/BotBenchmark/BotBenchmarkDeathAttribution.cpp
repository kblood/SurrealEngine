#include "BotBenchmarkDeathAttribution.h"

#include <cmath>

namespace BotBenchmarkDeathAttribution
{
	namespace
	{
		bool IsRecent(double contributionSeconds, double deathSeconds, double windowSeconds)
		{
			if (!std::isfinite(contributionSeconds) || !std::isfinite(deathSeconds) ||
				!std::isfinite(windowSeconds) || windowSeconds < 0.0)
				return false;
			const double age = deathSeconds - contributionSeconds;
			return age >= 0.0 && age <= windowSeconds;
		}
	}

	State ObserveDamage(State state, const DamageObservation& observation)
	{
		if (observation.Instigator != DamageInstigator::EnemyPlayer ||
			!std::isfinite(observation.TimeSeconds))
			return state;

		state.HasEnemyContribution = true;
		state.LastEnemyContributionSeconds = observation.TimeSeconds;
		if (observation.AppliedMomentum)
		{
			state.HasEnemyMomentumContribution = true;
			state.LastEnemyMomentumContributionSeconds = observation.TimeSeconds;
		}
		return state;
	}

	Decision ClassifyDeath(const State& state, const DeathObservation& observation,
		double recentContributionWindowSeconds)
	{
		Decision decision;
		decision.HadRecentEnemyContribution = state.HasEnemyContribution &&
			IsRecent(state.LastEnemyContributionSeconds, observation.TimeSeconds,
				recentContributionWindowSeconds);
		decision.HadRecentEnemyMomentumContribution = state.HasEnemyMomentumContribution &&
			IsRecent(state.LastEnemyMomentumContributionSeconds, observation.TimeSeconds,
				recentContributionWindowSeconds);

		if (observation.Killer == DeathKiller::SelfPlayer)
			decision.Attribution = Kind::DirectSelfKill;
		else if (observation.Killer == DeathKiller::EnemyPlayer)
			decision.Attribution = Kind::DirectEnemyKill;
		else if (!observation.HasEnvironmentalEvidence)
			decision.Attribution = Kind::AmbiguousDeath;
		else if (decision.HadRecentEnemyContribution)
			decision.Attribution = Kind::RecentEnemyContributedEnvironmentalDeathProxy;
		else
			decision.Attribution = Kind::UnassistedEnvironmentalDeath;

		// Death ends the current life. Contributions must never leak across respawn.
		decision.NextState = {};
		return decision;
	}
}
