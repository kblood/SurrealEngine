#include "UObject/PawnHazardResidenceObserver.h"

#include <cmath>
#include <iostream>

namespace
{
	int Failures = 0;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			Failures++;
		}
	}
}

int main()
{
	using namespace PawnMovement;
	HazardResidenceUpdate update = AdvanceHazardResidence({}, true, true,
		0.1f, false, false, 0.25f);
	Check(update.Started && update.State.Active && update.State.HarmfulSeconds == 0.1f,
		"a positive-DPS residence starts independently of water-entry provenance");

	update.State = ObserveHazardResidenceCandidate(update.State);
	update = ObserveHazardResidenceCommand(update.State, false);
	Check(update.State.CommandChanges == 1 && update.State.CandidateSuperseded
		&& update.CandidateSupersededNow,
		"a changed stock command after a candidate is observed is attributable supersession");
	update = ObserveHazardResidenceCommand(update.State, false);
	Check(update.State.CommandChanges == 2 && !update.CandidateSupersededNow,
		"candidate supersession is reported only once while command churn remains exact");

	update = AdvanceHazardResidence(update.State, false, true, 0.1f, false, false, 0.25f);
	Check(update.State.Active && update.State.ClearancePending
		&& update.Terminal == HazardResidenceTerminal::None,
		"brief zone-boundary clearance is held through a grace interval");
	update = AdvanceHazardResidence(update.State, true, true, 0.1f, false, false, 0.25f);
	Check(update.State.Active && update.State.Reentries == 1 && !update.State.ClearancePending,
		"returning to positive DPS before the grace deadline is one re-entry");
	update = AdvanceHazardResidence(update.State, false, true, 0.25f, false, false, 0.25f);
	Check(update.Terminal == HazardResidenceTerminal::Cleared && !update.State.Active,
		"sustained clearance ends the residence exactly once");

	update = AdvanceHazardResidence({}, true, true, 0.1f, false, false, 0.25f);
	update = AdvanceHazardResidence(update.State, true, false, 0.0f, false, false, 0.25f);
	Check(update.Terminal == HazardResidenceTerminal::Death,
		"death terminates an active residence rather than counting clearance");
	update = AdvanceHazardResidence({}, true, true, 0.1f, false, false, 0.25f);
	update = AdvanceHazardResidence(update.State, true, true, 0.0f, true, false, 0.25f);
	Check(update.Terminal == HazardResidenceTerminal::LifeBoundary,
		"life boundary is a distinct censor");
	update = AdvanceHazardResidence({}, true, true, 0.1f, false, false, 0.25f);
	update = AdvanceHazardResidence(update.State, true, true, 0.0f, false, true, 0.25f);
	Check(update.Terminal == HazardResidenceTerminal::RunEnd,
		"run end is a distinct censor");
	update = AdvanceHazardResidence({}, true, true, std::nanf(""), false, false, 0.25f);
	Check(update.Terminal == HazardResidenceTerminal::None && !update.Started,
		"invalid time cannot start a residence");

	return Failures == 0 ? 0 : 1;
}
