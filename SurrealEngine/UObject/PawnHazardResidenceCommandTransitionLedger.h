#pragma once

#include "PawnHazardResidenceObserver.h"
#include "PawnMovementCommandProvenance.h"

#include <cstdint>
#include <vector>

namespace PawnMovement
{
	// A terminal record is emitted only after a positive-DPS residence ends. It
	// is observer-only and keeps the entry command plus every later native
	// command replacement in that same life.
	struct HazardResidenceCommandTransitionLedgerEntry
	{
		uint64_t Sequence = 0;
		bool IsEntry = false;
		uint64_t PriorCommandToken = 0;
		MovementCommandProvenanceObservation Command;
	};

	struct HazardResidenceCommandTransitionLedgerRecord
	{
		uint64_t Sequence = 0;
		uint64_t EpisodeId = 0;
		uint64_t LifeId = 0;
		HazardResidenceTerminal Terminal = HazardResidenceTerminal::None;
		uint64_t EntryCommandToken = 0;
		uint64_t TerminalCommandToken = 0;
		bool EntryIntegrityValid = false;
		bool TerminalIntegrityValid = false;
		std::vector<HazardResidenceCommandTransitionLedgerEntry> Entries;
	};

	inline const char* HazardResidenceTerminalName(HazardResidenceTerminal terminal)
	{
		switch (terminal)
		{
		case HazardResidenceTerminal::Cleared: return "cleared";
		case HazardResidenceTerminal::Death: return "death";
		case HazardResidenceTerminal::LifeBoundary: return "life_boundary_censored";
		case HazardResidenceTerminal::RunEnd: return "run_end_censored";
		case HazardResidenceTerminal::Unknown: return "unknown";
		case HazardResidenceTerminal::None: return "none";
		}
		return "unknown";
	}
}
