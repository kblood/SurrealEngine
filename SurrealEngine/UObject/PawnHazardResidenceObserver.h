#pragma once

#include <cstdint>
#include <string>

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
		int EntryHealth = 0;
		float HarmfulSeconds = 0.0f;
		float ClearanceSeconds = 0.0f;
		uint64_t CommandChanges = 0;
		uint64_t Reentries = 0;
	};

	// Captured immediately before an active residence is resolved as a death.
	// The benchmark binds it to the VM death-source scope before reporting it;
	// this value alone never attributes a death to PainTimer.
	struct HazardResidenceDeathWitness
	{
		int EntryHealth = 0;
		float HarmfulSeconds = 0.0f;
		uint64_t CommandChanges = 0;
		bool DirectSafeCandidateObserved = false;
		bool DirectSafeCandidateSuperseded = false;
		std::string DirectSafeCandidateName;
		uint64_t CommandOwnershipLifeId = 0;
	};

	struct HazardResidenceUpdate
	{
		HazardResidenceState State;
		bool Started = false;
		bool CandidateSupersededNow = false;
		HazardResidenceTerminal Terminal = HazardResidenceTerminal::None;
	};

	HazardResidenceUpdate AdvanceHazardResidence(const HazardResidenceState& state,
		bool positiveDpsHazard, bool alive, int health, float elapsed, bool lifeBoundary,
		bool runEnd, float clearanceGraceSeconds);
	HazardResidenceState ObserveHazardResidenceCandidate(
		const HazardResidenceState& state);
	HazardResidenceUpdate ObserveHazardResidenceCommand(
		const HazardResidenceState& state, bool targetsObservedCandidate);
}
