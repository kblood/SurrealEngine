#include "BotBenchmark/BotBenchmarkAvoidableSuicide.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	using namespace BotBenchmarkAvoidableSuicide;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}

	Input AvoidableInput()
	{
		Input input;
		input.Attribution = DeathAttribution::UnassistedEnvironmentalDeath;
		input.SameLifeActorAndCommand = Evidence::Yes;
		input.MovementIntent = Evidence::Yes;
		input.HarmfulOutcomePredictedBeforeCommit = Evidence::Yes;
		input.TerminalMatchesPredictedOutcome = Evidence::Yes;
		input.SafeAlternativeAtCommit = Evidence::Yes;
		input.NoExternalIntervention = Evidence::Yes;
		return input;
	}
}

int main()
{
	Input input = AvoidableInput();
	Check(Classify(input) == Outcome::Avoidable,
		"complete pre-commit causal evidence must classify an unassisted death as avoidable");

	input.SafeAlternativeAtCommit = Evidence::No;
	Check(Classify(input) == Outcome::Unavoidable,
		"a proven absent safe alternative must be unavoidable, not avoidable");
	input = AvoidableInput();
	input.HarmfulOutcomePredictedBeforeCommit = Evidence::No;
	Check(Classify(input) == Outcome::Unavoidable,
		"a proven non-predictable terminal outcome must be unavoidable");

	input = AvoidableInput();
	input.NoExternalIntervention = Evidence::No;
	Check(Classify(input) == Outcome::Excluded,
		"external impulse deaths must stay out of the avoidability denominator");
	input = AvoidableInput();
	input.HadRecentEnemyMomentumContribution = true;
	Check(Classify(input) == Outcome::Excluded,
		"recent enemy momentum must exclude an otherwise complete record");
	input = AvoidableInput();
	input.Attribution = DeathAttribution::DirectSelfKill;
	Check(Classify(input) == Outcome::Excluded,
		"direct self kills require separate causal shot evidence and must be excluded");

	input = AvoidableInput();
	input.SameLifeActorAndCommand = Evidence::No;
	Check(Classify(input) == Outcome::Unknown,
		"a mismatched command witness must fail closed to unknown");
	input = AvoidableInput();
	input.RecordOverflowed = true;
	Check(Classify(input) == Outcome::Unknown,
		"overflowed witness streams must fail closed to unknown");
	input = AvoidableInput();
	input.MovementIntent = Evidence::No;
	Check(Classify(input) == Outcome::Unknown,
		"deaths without a movement decision must not be inferred avoidable");

	Counters counters;
	for (Outcome outcome : { Outcome::Avoidable, Outcome::Unavoidable,
		Outcome::Excluded, Outcome::Unknown })
	{
		counters.Observe(outcome);
	}
	Check(counters.Total() == 4 && counters.AvoidableRate() &&
		std::abs(*counters.AvoidableRate() - 0.5) < 0.000001 &&
		counters.UnknownFraction() && std::abs(*counters.UnknownFraction() - 0.25) < 0.000001,
		"partition counters must retain exact totals and derived rate denominators");
	Check(OutcomeName(Outcome::Avoidable) == std::string("avoidable") &&
		OutcomeName(Outcome::Unknown) == std::string("unknown"),
		"outcome names must remain stable telemetry tokens");
	return 0;
}
