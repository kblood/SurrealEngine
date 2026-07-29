#pragma once

#include "PawnFallingHazardForecast.h"

#include <array>
#include <cstddef>

namespace PawnMovement
{
	constexpr size_t FallingAirRecoveryMaximumCandidates = 8;

	// This is deliberately a selector, not a simulator. Callers obtain each
	// candidate's collision/zone-complete forecast from the existing falling
	// parity forecast, then this type admits only a finite, dry, static landing.
	struct FallingAirRecoveryCandidate
	{
		vec2 Direction = vec2(0.0f);
		FallingHazardForecastUpdate Forecast;
	};

	enum class FallingAirRecoverySelectionDecision
	{
		NoCandidates,
		NoCertifiedLanding,
		Selected
	};

	struct FallingAirRecoverySelection
	{
		FallingAirRecoverySelectionDecision Decision =
			FallingAirRecoverySelectionDecision::NoCandidates;
		size_t CandidatesTested = 0;
		size_t CertifiedLandingCount = 0;
		size_t SelectedIndex = FallingAirRecoveryMaximumCandidates;
		vec2 Direction = vec2(0.0f);
	};

	bool IsCertifiedFallingAirRecoveryCandidate(
		const FallingAirRecoveryCandidate& candidate);

	FallingAirRecoverySelection SelectFallingAirRecoveryTrajectory(
		const std::array<FallingAirRecoveryCandidate,
			FallingAirRecoveryMaximumCandidates>& candidates,
		size_t candidateCount);
}
