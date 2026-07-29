#pragma once

#include <cstdint>
#include <string>

namespace PawnMovement
{
	// Captured only when the benchmark PickTarget observer is selected. The
	// values describe the query as it executed; they never participate in target
	// selection.
	struct PickTargetObservation
	{
	uint64_t Sequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t SourceLifeId = 0;
	uint64_t SelectedLifeId = 0;
	int32_t SourceActorIndex = -1;
	int32_t SelectedActorIndex = -1;
		uint32_t CandidatePawns = 0;
		uint32_t SelfRejects = 0;
		uint32_t DeadRejects = 0;
		uint32_t LivingCandidates = 0;
		uint32_t LivingSkippedByCurrentPredicate = 0;
		uint32_t TeamRejects = 0;
		uint32_t LivingGeometryEligible = 0;
		uint32_t LivingLineOfSightEligible = 0;
		bool ReturnedTarget = false;
		bool ReturnedLivingTarget = false;
		bool NoResultWithLivingLineOfSightCandidate = false;
		bool IntegrityValid = true;
		std::string CallerClass;
		std::string CallerFunction;
		std::string SelectedActor;
		std::string SelectedClass;
	};
}
