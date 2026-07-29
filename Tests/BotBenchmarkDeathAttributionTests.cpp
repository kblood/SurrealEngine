#include "BotBenchmark/BotBenchmarkDeathAttribution.h"

#include <iostream>

namespace
{
	using namespace BotBenchmarkDeathAttribution;

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool IsCleared(const State& state)
	{
		return !state.HasEnemyContribution && !state.HasEnemyMomentumContribution;
	}
}

int main()
{
	constexpr double windowSeconds = 2.0;

	State priorEnemyDamage = ObserveDamage({}, { 9.5, DamageInstigator::EnemyPlayer, true });
	Decision selfSplash = ClassifyDeath(priorEnemyDamage,
		{ 10.0, DeathKiller::SelfPlayer, false }, windowSeconds);
	if (selfSplash.Attribution != Kind::DirectSelfKill || !IsCleared(selfSplash.NextState))
		return Fail("self splash was not a direct self kill with cleared life state");

	Decision voluntaryEnvironment = ClassifyDeath({},
		{ 10.0, DeathKiller::None, true }, windowSeconds);
	if (voluntaryEnvironment.Attribution != Kind::UnassistedEnvironmentalDeath)
		return Fail("environmental death without player contribution was not unassisted");

	State knockback = ObserveDamage({}, { 9.25, DamageInstigator::EnemyPlayer, true });
	Decision enemyKnockback = ClassifyDeath(knockback,
		{ 10.0, DeathKiller::NonPlayer, true }, windowSeconds);
	if (enemyKnockback.Attribution != Kind::RecentEnemyContributedEnvironmentalDeathProxy ||
		!enemyKnockback.HadRecentEnemyContribution ||
		!enemyKnockback.HadRecentEnemyMomentumContribution)
		return Fail("recent enemy knockback was not retained as environmental contribution");

	State recentDamage = ObserveDamage({}, { 9.0, DamageInstigator::EnemyPlayer, false });
	Decision enemyContribution = ClassifyDeath(recentDamage,
		{ 10.0, DeathKiller::None, true }, windowSeconds);
	if (enemyContribution.Attribution != Kind::RecentEnemyContributedEnvironmentalDeathProxy ||
		!enemyContribution.HadRecentEnemyContribution ||
		enemyContribution.HadRecentEnemyMomentumContribution)
		return Fail("recent enemy damage contribution was not distinguished from knockback");

	State expiredDamage = ObserveDamage({}, { 7.0, DamageInstigator::EnemyPlayer, true });
	Decision delayedEnvironment = ClassifyDeath(expiredDamage,
		{ 10.0, DeathKiller::None, true }, windowSeconds);
	if (delayedEnvironment.Attribution != Kind::UnassistedEnvironmentalDeath ||
		delayedEnvironment.HadRecentEnemyContribution ||
		delayedEnvironment.HadRecentEnemyMomentumContribution)
		return Fail("expired enemy contribution affected environmental attribution");

	Decision ambiguousNull = ClassifyDeath({},
		{ 10.0, DeathKiller::None, false }, windowSeconds);
	Decision ambiguousNonPlayer = ClassifyDeath({},
		{ 10.0, DeathKiller::NonPlayer, false }, windowSeconds);
	if (ambiguousNull.Attribution != Kind::AmbiguousDeath ||
		ambiguousNonPlayer.Attribution != Kind::AmbiguousDeath)
		return Fail("null or nonplayer death without environmental evidence was over-attributed");

	Decision directEnemy = ClassifyDeath({},
		{ 10.0, DeathKiller::EnemyPlayer, true }, windowSeconds);
	if (directEnemy.Attribution != Kind::DirectEnemyKill)
		return Fail("direct enemy killer did not take precedence over environmental evidence");

	State ignored = ObserveDamage({}, { 9.0, DamageInstigator::SelfPlayer, true });
	ignored = ObserveDamage(ignored, { 9.5, DamageInstigator::NoneOrNonPlayer, true });
	if (!IsCleared(ignored))
		return Fail("self or nonplayer damage polluted enemy contribution state");

	return 0;
}
