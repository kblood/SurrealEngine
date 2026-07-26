#include "UObject/PawnFallingAirRecoverySelector.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	PawnMovement::FallingAirRecoveryCandidate Certified(vec2 direction)
	{
		using namespace PawnMovement;
		FallingAirRecoveryCandidate candidate;
		candidate.Direction = direction;
		candidate.Forecast.Complete = true;
		candidate.Forecast.Result.Classification =
			FallingHazardForecast::NoHarmfulPainObserved;
		candidate.Forecast.Result.Reason =
			FallingHazardForecastReason::NoHarmfulPainAtStaticLanding;
		candidate.Forecast.Result.Elapsed = 0.25f;
		candidate.Forecast.Result.SegmentCount = 1;
		return candidate;
	}
}

int main()
{
	using namespace PawnMovement;
	std::array<FallingAirRecoveryCandidate,
		FallingAirRecoveryMaximumCandidates> candidates = {};
	Expect(SelectFallingAirRecoveryTrajectory(candidates, 0).Decision
		== FallingAirRecoverySelectionDecision::NoCandidates,
		"an empty candidate set cannot authorize steering");
	Expect(SelectFallingAirRecoveryTrajectory(candidates, candidates.size() + 1).Decision
		== FallingAirRecoverySelectionDecision::NoCandidates,
		"an over-cap candidate set fails closed");

	candidates[0] = Certified(vec2(1.0f, 0.0f));
	Expect(IsCertifiedFallingAirRecoveryCandidate(candidates[0]),
		"a finite dry static landing with a unit direction certifies");
	candidates[0].Forecast.Result.TransientHarmfulPainObserved = true;
	Expect(!IsCertifiedFallingAirRecoveryCandidate(candidates[0]),
		"transient harmful pain cannot certify a landing");
	candidates[0] = Certified(vec2(1.0f, 0.0f));
	candidates[0].Forecast.Result.Reason = FallingHazardForecastReason::WaterBeforeHarm;
	Expect(!IsCertifiedFallingAirRecoveryCandidate(candidates[0]),
		"a water path cannot certify a landing");
	candidates[0] = Certified(vec2(1.0f, 0.0f));
	candidates[0].Forecast.Complete = false;
	Expect(!IsCertifiedFallingAirRecoveryCandidate(candidates[0]),
		"a pending forecast cannot certify a landing");
	candidates[0] = Certified(vec2(
		std::numeric_limits<float>::quiet_NaN(), 0.0f));
	Expect(!IsCertifiedFallingAirRecoveryCandidate(candidates[0]),
		"a non-finite steering direction cannot certify a landing");

	candidates = {};
	candidates[0] = Certified(vec2(1.0f, 0.0f));
	candidates[0].Forecast.Result.Classification = FallingHazardForecast::Unknown;
	candidates[1] = Certified(normalize(vec2(1.0f, 1.0f)));
	candidates[2] = Certified(vec2(-1.0f, 0.0f));
	const FallingAirRecoverySelection selection =
		SelectFallingAirRecoveryTrajectory(candidates, 3);
	Expect(selection.Decision == FallingAirRecoverySelectionDecision::Selected
		&& selection.SelectedIndex == 1 && selection.CandidatesTested == 3
		&& selection.CertifiedLandingCount == 2,
		"the first collision-validated safe landing is selected deterministically");
	Expect(std::abs(selection.Direction.x - candidates[1].Direction.x) < 0.001f
		&& std::abs(selection.Direction.y - candidates[1].Direction.y) < 0.001f,
		"the selected direction is copied without an anchor heuristic");
}
