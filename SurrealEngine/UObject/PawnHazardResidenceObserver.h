#pragma once

#include <cstdint>

namespace PawnMovement
{
	// Pure, observer-only state machine for a positive-DPS residence. It has no
	// dependency on UPawn so the lifecycle is fixture-testable before runtime
	// telemetry consumes it.
	enum class HazardResidenceTerminal : uint8_t
	{
		None,
		Cleared,
		Death,
		LifeBoundary,
		RunEnd,
		Unknown,
	};

	struct HazardResidenceState
	{
		bool Active = false;
		bool ClearancePending = false;
		bool CandidateObserved = false;
		bool CandidateSuperseded = false;
		float HarmfulSeconds = 0.0f;
		float ClearanceSeconds = 0.0f;
		uint64_t CommandChanges = 0;
		uint64_t Reentries = 0;
	};

	struct HazardResidenceUpdate
	{
		HazardResidenceState State;
		bool Started = false;
		bool CandidateSupersededNow = false;
		HazardResidenceTerminal Terminal = HazardResidenceTerminal::None;
	};

	HazardResidenceUpdate AdvanceHazardResidence(const HazardResidenceState& state,
		bool positiveDpsHazard, bool alive, float elapsed, bool lifeBoundary,
		bool runEnd, float clearanceGraceSeconds);
	HazardResidenceState ObserveHazardResidenceCandidate(
		const HazardResidenceState& state);
	HazardResidenceUpdate ObserveHazardResidenceCommand(
		const HazardResidenceState& state, bool targetsObservedCandidate);
}
