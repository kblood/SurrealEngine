#pragma once

#include "PawnFallingHazardForecast.h"

namespace PawnMovement
{
	// This certificate is prospective movement evidence only. It does not establish
	// that a later water entry, death, or route choice was caused by this forecast.
	enum class DirectHarmfulWaterEntryCertificateResult
	{
		Certified,
		ForecastIncompleteOrInconsistent,
		NotFullStep,
		NotHarmfulWaterEndpoint,
		DamageNotAvoidanceRelevant,
		InvalidExpectedHarmfulZones,
		UnsafeOrUnknownStart,
		IntermediateHazardObserved,
		NoDirectClearPath,
		InvalidPredictionAccounting,
	};

	struct DirectHarmfulWaterEntryCertificate
	{
		DirectHarmfulWaterEntryCertificateResult Result =
			DirectHarmfulWaterEntryCertificateResult::ForecastIncompleteOrInconsistent;
		size_t PredictedDirectSegmentCount = 0;
		float PredictedElapsed = 0.0f;
		float PredictedPathDistance = 0.0f;
		FallingHazardZoneId ExpectedHarmfulFootZone;
		FallingHazardZoneId ExpectedHarmfulPhysicsZone;
		float FirstHarmfulPainDepth = 0.0f;

		bool IsCertified() const
		{
			return Result == DirectHarmfulWaterEntryCertificateResult::Certified;
		}
	};

	DirectHarmfulWaterEntryCertificate
		CertifyDirectHarmfulWaterEntry(const FallingHazardForecastUpdate& forecast);
	const char* DirectHarmfulWaterEntryCertificateResultName(
		DirectHarmfulWaterEntryCertificateResult result);
}
