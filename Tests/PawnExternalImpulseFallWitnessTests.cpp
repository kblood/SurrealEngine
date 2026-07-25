#include "UObject/PawnExternalImpulseFallWitness.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	using namespace PawnMovement;
	const ExternalImpulseFallWitnessInput baseline = {
		.ExternalImpulseCommit = true,
		.BaselineComplete = true,
		.BaselineHarmful = true,
		.BaselineAvoidanceRelevant = true,
		.AirControlAvailable = true,
		.AlternativesTested = 8,
		.StaticDryAlternatives = 1,
	};
	const auto certified = EvaluateExternalImpulseFallWitness(baseline);
	Expect(certified.Decision == ExternalImpulseFallWitnessDecision::Certified
		&& certified.CountsHarmfulWitness && certified.Certified,
		"a complete harmful external-impulse fall with a dry static alternative certifies");

	for (const auto input : {
		ExternalImpulseFallWitnessInput{},
		ExternalImpulseFallWitnessInput { .ExternalImpulseCommit = true },
		ExternalImpulseFallWitnessInput { .ExternalImpulseCommit = true,
			.BaselineComplete = true, .BaselineHarmful = true,
			.BaselineAvoidanceRelevant = true },
		ExternalImpulseFallWitnessInput { .ExternalImpulseCommit = true,
			.BaselineComplete = true, .BaselineHarmful = true,
			.BaselineAvoidanceRelevant = true, .AirControlAvailable = true,
			.AlternativesTested = 8 },
	})
	{
		const auto result = EvaluateExternalImpulseFallWitness(input);
		Expect(!result.Certified, "incomplete or unsafe evidence must fail closed");
	}

	const auto noControl = EvaluateExternalImpulseFallWitness({
		.ExternalImpulseCommit = true, .BaselineComplete = true,
		.BaselineHarmful = true, .BaselineAvoidanceRelevant = true,
	});
	Expect(noControl.Decision == ExternalImpulseFallWitnessDecision::NoAirControl
		&& noControl.CountsHarmfulWitness,
		"a harmful external impulse without air control is observed but not certifiable");
	return 0;
}
