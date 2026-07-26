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
		bool PrecedingPathCommitKnown = false;
		bool PrecedingPathCommitCacheClear = false;
		uint64_t PrecedingPathCommitSequence = 0;
		int32_t PrecedingPathCommitFirstReachSpecIndex = -1;
		bool RouteHeadKnown = false;
		bool RouteHeadFromPrecedingPathCommit = false;
		int32_t RouteHeadActorIndex = -1;
		std::string RouteHeadName, RouteHeadClass;
		bool CommandTargetKnown = false;
		int32_t CommandTargetActorIndex = -1;
		std::string CommandTargetName, CommandTargetClass;
		float EntryVelocityX = 0.0f, EntryVelocityY = 0.0f, EntryVelocityZ = 0.0f;
		bool EntryDirectionKnown = false;
		float EntryDirectionX = 0.0f, EntryDirectionY = 0.0f, EntryDirectionZ = 0.0f;
		bool TrajectoryInputFinite = false, TrajectoryComplete = false,
			TrajectoryResultFinite = false;
		std::string TrajectoryClassification, TrajectoryReason;
		float TrajectoryElapsed = 0.0f, TrajectoryPathDistance = 0.0f;
		uint64_t TrajectorySegmentCount = 0, TrajectorySampleCount = 0;
		bool TrajectoryEntryZoneKnown = false;
		int32_t TrajectoryEntryZoneActorIndex = -1, TrajectoryEntryZoneNumber = -1;
		bool TrajectoryExpectedHarmfulFootZoneKnown = false;
		int32_t TrajectoryExpectedHarmfulFootZoneActorIndex = -1,
			TrajectoryExpectedHarmfulFootZoneNumber = -1;
		bool TrajectoryExpectedHarmfulPhysicsZoneKnown = false;
		int32_t TrajectoryExpectedHarmfulPhysicsZoneActorIndex = -1,
			TrajectoryExpectedHarmfulPhysicsZoneNumber = -1;
		HazardResidencePreentryCausalSliceBoundary MayFallBoundary, HitWallBoundary;
		bool IntegrityValid = false, EntryIntegrityValid = false, TerminalIntegrityValid = false;
		std::vector<MovementCommandProvenanceObservation> CommandLineage;
	};
}
