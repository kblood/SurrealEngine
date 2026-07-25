#pragma once

#include <cstdint>
#include <optional>

namespace BotBenchmarkAvoidableSuicide
{
	// This classifier is deliberately independent of game scripts and live
	// probes. Runtime code may label a death avoidable only after it has captured
	// every input below at the pre-commit boundary and reconciled it at death.
	enum class DeathAttribution
	{
		UnassistedEnvironmentalDeath,
		DirectSelfKill,
		DirectEnemyKill,
		RecentEnemyContributedEnvironmentalDeath,
		AmbiguousDeath,
	};

	enum class Evidence
	{
		Unknown,
		No,
		Yes,
	};

	enum class Outcome
	{
		Avoidable,
		Unavoidable,
		Excluded,
		Unknown,
	};

	struct Input
	{
		DeathAttribution Attribution = DeathAttribution::AmbiguousDeath;
		bool HadRecentEnemyContribution = false;
		bool HadRecentEnemyMomentumContribution = false;
		bool RecordOverflowed = false;
		Evidence SameLifeActorAndCommand = Evidence::Unknown;
		Evidence MovementIntent = Evidence::Unknown;
		Evidence HarmfulOutcomePredictedBeforeCommit = Evidence::Unknown;
		Evidence TerminalMatchesPredictedOutcome = Evidence::Unknown;
		Evidence SafeAlternativeAtCommit = Evidence::Unknown;
		Evidence NoExternalIntervention = Evidence::Unknown;
	};

	Outcome Classify(const Input& input);
	const char* OutcomeName(Outcome outcome);

	struct Counters
	{
		uint64_t Avoidable = 0;
		uint64_t Unavoidable = 0;
		uint64_t Excluded = 0;
		uint64_t Unknown = 0;

		void Observe(Outcome outcome);
		uint64_t Total() const;
		std::optional<double> AvoidableRate() const;
		std::optional<double> UnknownFraction() const;
	};
}
