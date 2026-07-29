#pragma once

#include <cstddef>
#include <cstdint>

namespace PawnMovement
{
	// A read-only certificate for a harmful fall that began outside the native
	// bot-command domain. It deliberately knows nothing about UPawn or movement
	// writes: callers provide outcomes from the existing parity forecast.
	enum class ExternalImpulseFallWitnessDecision : uint8_t
	{
		NotExternalImpulse,
		BaselineNotCertifiable,
		NoAirControl,
		Uncertified,
		Certified,
	};

	struct ExternalImpulseFallWitnessInput
	{
		bool ExternalImpulseCommit = false;
		bool BaselineComplete = false;
		bool BaselineHarmful = false;
		bool BaselineAvoidanceRelevant = false;
		bool AirControlAvailable = false;
		size_t AlternativesTested = 0;
		size_t StaticDryAlternatives = 0;
	};

	struct ExternalImpulseFallWitnessResult
	{
		ExternalImpulseFallWitnessDecision Decision =
			ExternalImpulseFallWitnessDecision::NotExternalImpulse;
		bool CountsHarmfulWitness = false;
		bool Certified = false;
	};

	ExternalImpulseFallWitnessResult EvaluateExternalImpulseFallWitness(
		const ExternalImpulseFallWitnessInput& input);
}
