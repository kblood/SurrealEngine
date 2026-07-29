#pragma once

#include <cstdint>
#include <string>

namespace PawnMovement
{
	// Captured only when the benchmark Pawn.CanSee observer is selected. These
	// values describe the completed native query and never affect its result.
	struct PawnCanSeeObservation
	{
		uint64_t Sequence = 0;
		uint64_t ObserverTick = 0;
		uint64_t CallerInvocationToken = 0;
		uint64_t SourceLifeId = 0;
		uint64_t TargetLifeId = 0;
		int32_t SourceActorIndex = -1;
		int32_t TargetActorIndex = -1;
		float PeripheralVision = 0.0f;
		bool SightRadiusAccepted = false;
		bool LegacyConeAccepted = false;
		bool CorrectedConeAccepted = false;
		bool CorrectedConeSelected = false;
		bool ReturnedVisible = false;
		bool IntegrityValid = true;
		std::string CallerClass;
		std::string CallerFunction;
		std::string TargetActor;
		std::string TargetClass;
	};
}
