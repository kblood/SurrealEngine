#include "PawnHazardResidenceObserver.h"

#include <cmath>

namespace PawnMovement
{
	HazardResidenceUpdate AdvanceHazardResidence(const HazardResidenceState& state,
		bool positiveDpsHazard, bool alive, float elapsed, bool lifeBoundary,
		bool runEnd, float clearanceGraceSeconds)
	{
		HazardResidenceUpdate update;
		if (!std::isfinite(elapsed) || elapsed < 0.0f
			|| !std::isfinite(clearanceGraceSeconds) || clearanceGraceSeconds < 0.0f)
		{
			if (state.Active)
				update.Terminal = HazardResidenceTerminal::Unknown;
			return update;
		}
		if (!state.Active)
		{
			if (!positiveDpsHazard || !alive || lifeBoundary || runEnd)
				return update;
			update.State.Active = true;
			update.State.HarmfulSeconds = elapsed;
			update.Started = true;
			return update;
		}

		if (!alive)
		{
			update.Terminal = HazardResidenceTerminal::Death;
			return update;
		}
		if (lifeBoundary)
		{
			update.Terminal = HazardResidenceTerminal::LifeBoundary;
			return update;
		}
		if (runEnd)
		{
			update.Terminal = HazardResidenceTerminal::RunEnd;
			return update;
		}

		update.State = state;
		if (positiveDpsHazard)
		{
			update.State.HarmfulSeconds += elapsed;
			if (state.ClearancePending)
			{
				update.State.ClearancePending = false;
				update.State.ClearanceSeconds = 0.0f;
				update.State.Reentries++;
			}
			return update;
		}

		update.State.ClearancePending = true;
		update.State.ClearanceSeconds += elapsed;
		if (update.State.ClearanceSeconds >= clearanceGraceSeconds)
		{
			update.State = {};
			update.Terminal = HazardResidenceTerminal::Cleared;
		}
		return update;
	}

	HazardResidenceState ObserveHazardResidenceCandidate(
		const HazardResidenceState& state)
	{
		HazardResidenceState result = state;
		if (result.Active)
			result.CandidateObserved = true;
		return result;
	}

	HazardResidenceUpdate ObserveHazardResidenceCommand(
		const HazardResidenceState& state, bool targetsObservedCandidate)
	{
		HazardResidenceUpdate update;
		update.State = state;
		if (!state.Active)
			return update;
		update.State.CommandChanges++;
		if (state.CandidateObserved && !state.CandidateSuperseded
			&& !targetsObservedCandidate)
		{
			update.State.CandidateSuperseded = true;
			update.CandidateSupersededNow = true;
		}
		return update;
	}
}
