#include "BotBenchmarkAvoidableSuicide.h"

namespace BotBenchmarkAvoidableSuicide
{
	Outcome Classify(const Input& input)
	{
		// Enemy-caused, direct-self, and ambiguous deaths need their own causal
		// evidence. They are not environmental-navigation opportunities.
		if (input.Attribution != DeathAttribution::UnassistedEnvironmentalDeath ||
			input.HadRecentEnemyContribution || input.HadRecentEnemyMomentumContribution)
		{
			return Outcome::Excluded;
		}

		if (input.RecordOverflowed || input.SameLifeActorAndCommand != Evidence::Yes ||
			input.MovementIntent != Evidence::Yes ||
			input.TerminalMatchesPredictedOutcome != Evidence::Yes)
		{
			return Outcome::Unknown;
		}

		// An external impulse is outside the bot's decision. It must never improve
		// the measured avoidability rate by entering its denominator.
		if (input.NoExternalIntervention == Evidence::No)
			return Outcome::Excluded;
		if (input.NoExternalIntervention != Evidence::Yes)
			return Outcome::Unknown;

		if (input.HarmfulOutcomePredictedBeforeCommit == Evidence::No ||
			input.SafeAlternativeAtCommit == Evidence::No)
		{
			return Outcome::Unavoidable;
		}
		if (input.HarmfulOutcomePredictedBeforeCommit != Evidence::Yes ||
			input.SafeAlternativeAtCommit != Evidence::Yes)
		{
			return Outcome::Unknown;
		}
		return Outcome::Avoidable;
	}

	const char* OutcomeName(Outcome outcome)
	{
		switch (outcome)
		{
		case Outcome::Avoidable: return "avoidable";
		case Outcome::Unavoidable: return "unavoidable";
		case Outcome::Excluded: return "excluded";
		default: return "unknown";
		}
	}

	void Counters::Observe(Outcome outcome)
	{
		switch (outcome)
		{
		case Outcome::Avoidable: Avoidable++; break;
		case Outcome::Unavoidable: Unavoidable++; break;
		case Outcome::Excluded: Excluded++; break;
		case Outcome::Unknown: Unknown++; break;
		}
	}

	uint64_t Counters::Total() const
	{
		return Avoidable + Unavoidable + Excluded + Unknown;
	}

	std::optional<double> Counters::AvoidableRate() const
	{
		const uint64_t denominator = Avoidable + Unavoidable;
		if (denominator == 0)
			return {};
		return static_cast<double>(Avoidable) / static_cast<double>(denominator);
	}

	std::optional<double> Counters::UnknownFraction() const
	{
		const uint64_t total = Total();
		if (total == 0)
			return {};
		return static_cast<double>(Unknown) / static_cast<double>(total);
	}
}
