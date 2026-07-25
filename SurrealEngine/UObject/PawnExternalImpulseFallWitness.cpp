#include "PawnExternalImpulseFallWitness.h"

namespace PawnMovement
{
	ExternalImpulseFallWitnessResult EvaluateExternalImpulseFallWitness(
		const ExternalImpulseFallWitnessInput& input)
	{
		ExternalImpulseFallWitnessResult result;
		if (!input.ExternalImpulseCommit)
			return result;
		if (!input.BaselineComplete || !input.BaselineHarmful
			|| !input.BaselineAvoidanceRelevant)
		{
			result.Decision = ExternalImpulseFallWitnessDecision::BaselineNotCertifiable;
			return result;
		}
		result.CountsHarmfulWitness = true;
		if (!input.AirControlAvailable)
		{
			result.Decision = ExternalImpulseFallWitnessDecision::NoAirControl;
			return result;
		}
		if (input.AlternativesTested == 0 || input.StaticDryAlternatives == 0)
		{
			result.Decision = ExternalImpulseFallWitnessDecision::Uncertified;
			return result;
		}
		result.Decision = ExternalImpulseFallWitnessDecision::Certified;
		result.Certified = true;
		return result;
	}
}
