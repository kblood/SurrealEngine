#pragma once

#include "PawnHazardResidenceObserver.h"
#include "PawnMovementCommandProvenance.h"

#include <cstdint>
#include <string>
#include <vector>

namespace PawnMovement
{
	struct HazardResidencePreentryCausalSliceBoundary
	{
		bool Observed = false;
		uint64_t NativeTick = 0;
		std::string Physics;
	};

	struct HazardResidencePreentryCausalSliceRecord
	{
		uint64_t Sequence = 0, EpisodeId = 0, LifeId = 0;
		HazardResidenceTerminal Terminal = HazardResidenceTerminal::None;
		int32_t ZoneActorIndex = -1;
		std::string ZoneName, ZoneClass;
		float EntryX = 0.0f, EntryY = 0.0f, EntryZ = 0.0f;
		std::string PreEntryPhysics, EntryPhysics;
		bool SupportKnown = false;
		int32_t SupportActorIndex = -1;
		std::string SupportClass, Transition;
		HazardResidencePreentryCausalSliceBoundary MayFallBoundary, HitWallBoundary;
		bool IntegrityValid = false, EntryIntegrityValid = false, TerminalIntegrityValid = false;
		std::vector<MovementCommandProvenanceObservation> CommandLineage;
	};
}
