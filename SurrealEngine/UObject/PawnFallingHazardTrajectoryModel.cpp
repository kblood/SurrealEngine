#include "PawnFallingHazardTrajectoryModel.h"

#include <cmath>

namespace PawnMovement
{
	namespace
	{
		bool SameLife(FallingHazardLifeId left, FallingHazardLifeId right)
		{
			return left.Value == right.Value;
		}

		bool SameFall(FallingHazardFallEpisodeId left,
			FallingHazardFallEpisodeId right)
		{
			return left.Value == right.Value;
		}

		bool SameGeneration(FallingHazardGenerationId left,
			FallingHazardGenerationId right)
		{
			return left.Value == right.Value;
		}

		bool SameZone(FallingHazardZoneId left, FallingHazardZoneId right)
		{
			return left.Known && right.Known
				&& left.ZoneActorId == right.ZoneActorId
				&& left.ZoneNumber == right.ZoneNumber;
		}

		void Complete(FallingHazardTrajectoryUpdate& update,
			FallingHazardTerminal terminal, bool unknown)
		{
			auto& generation = update.Model.ActiveGeneration;
			generation.Terminal = terminal;
			generation.ActualTrajectoryUnknown =
				generation.ActualTrajectoryUnknown || unknown;
			update.CompletedGeneration = generation;
			update.HasCompletedGeneration = true;
			update.Model.HasActiveGeneration = false;
		}

		FallingHazardTerminal SupersededTerminal(
			FallingHazardForecastSource source)
		{
			switch (source)
			{
			case FallingHazardForecastSource::CallbackReturnCommit:
			case FallingHazardForecastSource::AlignedContinuationCommit:
			case FallingHazardForecastSource::ThirdMoveContinuationCommit:
				return FallingHazardTerminal::CallbackBoundary;
			case FallingHazardForecastSource::ExternalImpulseCommit:
				return FallingHazardTerminal::ExternalImpulseBoundary;
			case FallingHazardForecastSource::HorizonContinuationCommit:
				return FallingHazardTerminal::ObservationHorizonExhausted;
			default:
				return FallingHazardTerminal::SupersededByCommittedSource;
			}
		}

		bool ValidArm(const FallingHazardForecastArm& arm)
		{
			const bool prechargedContinuation =
				arm.Source == FallingHazardForecastSource::AlignedContinuationCommit
				|| arm.Source
					== FallingHazardForecastSource::ThirdMoveContinuationCommit;
			if (arm.Life.Value == 0 || arm.FallEpisode.Value == 0
				|| arm.Source == FallingHazardForecastSource::Unknown
				|| !arm.StartingPhysicsZone.Known
				|| arm.SweptSegmentBudget == 0
				|| arm.SweptSegmentBudget > FallingHazardMaximumSweptSegments
				|| !std::isfinite(arm.ElapsedHorizon)
				|| arm.ElapsedHorizon <= 0.0f
				|| !std::isfinite(arm.PrechargedElapsed)
				|| arm.PrechargedElapsed < 0.0f
				|| arm.PrechargedElapsed > arm.ElapsedHorizon
				|| (prechargedContinuation && arm.PrechargedElapsed == 0.0f)
				|| (!prechargedContinuation && arm.PrechargedElapsed > 0.0f))
				return false;
			if (arm.Forecast == FallingHazardForecast::HarmfulPainObserved)
				return arm.ExpectedHarmfulFootZone.Known
					&& arm.ExpectedHarmfulPhysicsZone.Known;
			return !arm.ExpectedHarmfulFootZone.Known
				&& !arm.ExpectedHarmfulPhysicsZone.Known
				&& !arm.ExpectedHarmfulWaterEntry;
		}

		bool MatchesActive(const FallingHazardGenerationState& generation,
			FallingHazardLifeId life, FallingHazardFallEpisodeId fall,
			FallingHazardGenerationId id)
		{
			return SameLife(generation.Life, life)
				&& SameFall(generation.FallEpisode, fall)
				&& SameGeneration(generation.Generation, id);
		}

		bool ExternallyObservableTerminal(FallingHazardTerminal terminal)
		{
			return terminal == FallingHazardTerminal::HarmfulPainEntered
				|| terminal == FallingHazardTerminal::Landed
				|| terminal == FallingHazardTerminal::Died
				|| terminal == FallingHazardTerminal::CallbackBoundary
				|| terminal == FallingHazardTerminal::ExternalImpulseBoundary
				|| terminal == FallingHazardTerminal::ContinuityLost
				|| terminal == FallingHazardTerminal::WaterPhysicsBoundary
				|| terminal == FallingHazardTerminal::ObservationHorizonExhausted;
		}
	}

	FallingHazardArmUpdate ArmFallingHazardForecast(
		const FallingHazardTrajectoryModel& model,
		const FallingHazardForecastArm& arm)
	{
		FallingHazardArmUpdate update;
		update.Model = model;
		if (!ValidArm(arm))
			return update;

		if (update.Model.HasActiveGeneration)
		{
			const bool sameLife = SameLife(
				update.Model.ActiveGeneration.Life, arm.Life);
			const bool sameFall = SameFall(
				update.Model.ActiveGeneration.FallEpisode, arm.FallEpisode);
			const auto terminal = sameLife && sameFall
				? SupersededTerminal(arm.Source)
				: FallingHazardTerminal::ContinuityLost;
			FallingHazardTrajectoryUpdate completion;
			completion.Model = update.Model;
			Complete(completion, terminal, true);
			update.Model = completion.Model;
			update.CompletedGeneration = completion.CompletedGeneration;
			update.HasCompletedGeneration = true;
		}

		if (!SameLife(update.Model.CountedLife, arm.Life))
		{
			update.Model.CountedLife = arm.Life;
			update.Model.GenerationCountForLife = 0;
			update.Model.GenerationCapacityExceeded = false;
		}
		if (update.Model.GenerationCountForLife
			>= FallingHazardMaximumGenerationsPerLife
			|| update.Model.NextGenerationValue == 0)
		{
			update.Model.GenerationCapacityExceeded = true;
			return update;
		}

		auto& generation = update.Model.ActiveGeneration;
		generation = {};
		generation.Generation.Value = update.Model.NextGenerationValue++;
		generation.Life = arm.Life;
		generation.FallEpisode = arm.FallEpisode;
		generation.Source = arm.Source;
		generation.Forecast = arm.Forecast;
		generation.StartingPhysicsZone = arm.StartingPhysicsZone;
		generation.ExpectedHarmfulFootZone = arm.ExpectedHarmfulFootZone;
		generation.ExpectedHarmfulPhysicsZone = arm.ExpectedHarmfulPhysicsZone;
		generation.ExpectedHarmfulWaterEntry = arm.ExpectedHarmfulWaterEntry;
		generation.SweptSegmentBudget = arm.SweptSegmentBudget;
		generation.ElapsedHorizon = arm.ElapsedHorizon;
		generation.ObservedElapsed = arm.PrechargedElapsed;
		generation.HasPositiveElapsed = arm.PrechargedElapsed > 0.0f;
		update.Model.GenerationCountForLife++;
		update.Model.HasActiveGeneration = true;
		update.ArmedGeneration = generation.Generation;
		update.Armed = true;
		return update;
	}

	FallingHazardTrajectoryUpdate ObserveFallingHazardSweptSegment(
		const FallingHazardTrajectoryModel& model,
		const FallingHazardSweptSegment& observation)
	{
		FallingHazardTrajectoryUpdate update;
		update.Model = model;
		if (!update.Model.HasActiveGeneration)
			return update;
		auto& generation = update.Model.ActiveGeneration;
		if (!MatchesActive(generation, observation.Life,
			observation.FallEpisode, observation.Generation)
			|| observation.Ordinal != generation.SweptSegmentCount)
		{
			Complete(update, FallingHazardTerminal::ContinuityLost, true);
			return update;
		}
		if (generation.SweptSegmentCount >= generation.SweptSegmentBudget)
		{
			Complete(update,
				FallingHazardTerminal::SweptSegmentBudgetExceeded, true);
			return update;
		}
		if (!std::isfinite(observation.Elapsed) || observation.Elapsed < 0.0f
			|| (observation.Elapsed == 0.0f
				&& (!generation.HasPositiveElapsed
					|| observation.Leg == FallingHazardSweepLeg::Direct)))
		{
			Complete(update, FallingHazardTerminal::InvalidObservation, true);
			return update;
		}
		if (generation.ObservedElapsed + observation.Elapsed
			> generation.ElapsedHorizon + 0.000001f)
		{
			Complete(update,
				FallingHazardTerminal::ObservationHorizonExhausted, true);
			return update;
		}

		generation.SweptSegmentCount++;
		generation.ObservedElapsed += observation.Elapsed;
		generation.HasPositiveElapsed = generation.HasPositiveElapsed
			|| observation.Elapsed > 0.0f;
		const bool callbacksZoneOnly = observation.CallbackMaskKnown
			&& (observation.CallbackMask & ~FallingHazardZoneCallbackMask) == 0;
		const bool endpointMatched = observation.ForecastEndpointKnown
			&& observation.ForecastEndpointMatched;
		const bool stableCollision =
			observation.Collision == FallingHazardCollisionKind::Clear
			|| observation.Collision == FallingHazardCollisionKind::StaticWorld;
		const bool physicsZoneKnown = observation.PhysicsZone.Known;
		const bool physicsZoneChanged = physicsZoneKnown
			&& !SameZone(observation.PhysicsZone,
				generation.StartingPhysicsZone);
		const bool waterKnown = observation.RegionWaterKnown
			&& observation.FootWaterKnown && observation.HeadWaterKnown;
		const bool anyWater = observation.InRegionWater
			|| observation.InFootWater || observation.InHeadWater;
		generation.WaterEvidenceKnown = generation.WaterEvidenceKnown && waterKnown;
		generation.PhysicsZoneEvidenceKnown =
			generation.PhysicsZoneEvidenceKnown && physicsZoneKnown;
		if (physicsZoneKnown)
			generation.LastObservedPhysicsZone = observation.PhysicsZone;
		if (!stableCollision)
			generation.ActualTrajectoryUnknown = true;
		if (!observation.CallbackMaskKnown || !observation.ForecastEndpointKnown
			|| !waterKnown)
			generation.ActualTrajectoryUnknown = true;

		if (!observation.HarmfulFootZoneKnown)
			generation.HarmfulFootEvidenceKnown = false;
		if (observation.HarmfulFootZoneKnown && observation.InHarmfulFootZone)
		{
			generation.EnteredHarmfulFootZone = true;
			if (!observation.FootZone.Known || !physicsZoneKnown)
				generation.ActualTrajectoryUnknown = true;
			else
				generation.ObservedHarmfulFootZone = observation.FootZone;
			const bool expectedZone = SameZone(observation.FootZone,
				generation.ExpectedHarmfulFootZone);
			const bool expectedPhysicsZone = SameZone(observation.PhysicsZone,
				generation.ExpectedHarmfulPhysicsZone);
			const bool expectedWater = !generation.ExpectedHarmfulWaterEntry
				|| (observation.FootWaterKnown && observation.InFootWater);
			generation.ExpectedHarmfulPathMatched = expectedZone
				&& expectedPhysicsZone
				&& expectedWater && endpointMatched && callbacksZoneOnly;
			generation.CausalAmbiguity = !endpointMatched || !callbacksZoneOnly
				|| (generation.Forecast
					== FallingHazardForecast::HarmfulPainObserved
					&& (!expectedZone || !expectedPhysicsZone))
				|| (generation.Forecast
					== FallingHazardForecast::HarmfulPainObserved
					&& !generation.ExpectedHarmfulWaterEntry && anyWater);
			Complete(update, FallingHazardTerminal::HarmfulPainEntered, false);
			return update;
		}

		if (!stableCollision)
		{
			Complete(update, FallingHazardTerminal::ContinuityLost, true);
			return update;
		}
		if (!observation.HarmfulFootZoneKnown)
		{
			Complete(update, FallingHazardTerminal::ContinuityLost, true);
			return update;
		}
		if (!observation.CallbackMaskKnown || observation.CallbackMask != 0)
		{
			Complete(update, FallingHazardTerminal::CallbackBoundary, true);
			return update;
		}
		if (!endpointMatched)
		{
			Complete(update, FallingHazardTerminal::ContinuityLost, true);
			return update;
		}
		if (!physicsZoneKnown || physicsZoneChanged)
		{
			Complete(update, FallingHazardTerminal::ContinuityLost, true);
			return update;
		}
		if (!waterKnown || anyWater)
		{
			Complete(update, FallingHazardTerminal::WaterPhysicsBoundary, true);
			return update;
		}
		return update;
	}

	FallingHazardTrajectoryUpdate FinishFallingHazardGeneration(
		const FallingHazardTrajectoryModel& model,
		const FallingHazardTerminalObservation& observation)
	{
		FallingHazardTrajectoryUpdate update;
		update.Model = model;
		if (!update.Model.HasActiveGeneration)
			return update;
		auto& generation = update.Model.ActiveGeneration;
		if (!MatchesActive(generation, observation.Life,
			observation.FallEpisode, observation.Generation))
		{
			Complete(update, FallingHazardTerminal::ContinuityLost, true);
			return update;
		}
		if (!ExternallyObservableTerminal(observation.Terminal)
			|| (observation.Terminal == FallingHazardTerminal::HarmfulPainEntered
				&& !generation.EnteredHarmfulFootZone))
		{
			Complete(update, FallingHazardTerminal::InvalidObservation, true);
			return update;
		}
		generation.LandingCollision = observation.LandingCollision;
		const bool unknown = observation.Terminal != FallingHazardTerminal::Landed
			&& observation.Terminal != FallingHazardTerminal::Died
			&& observation.Terminal != FallingHazardTerminal::HarmfulPainEntered;
		if (observation.Terminal == FallingHazardTerminal::Landed
			&& observation.LandingCollision
				!= FallingHazardCollisionKind::StaticWorld)
			generation.ActualTrajectoryUnknown = true;
		Complete(update, observation.Terminal, unknown);
		return update;
	}

	FallingHazardCorrelation CorrelateFallingHazardGeneration(
		const FallingHazardGenerationState& generation)
	{
		if (generation.Terminal == FallingHazardTerminal::Active)
			return FallingHazardCorrelation::Pending;
		if (generation.Terminal == FallingHazardTerminal::HarmfulPainEntered)
		{
			if (generation.ActualTrajectoryUnknown
				|| !generation.HarmfulFootEvidenceKnown
				|| !generation.WaterEvidenceKnown
				|| !generation.PhysicsZoneEvidenceKnown
				|| !generation.HasPositiveElapsed)
				return FallingHazardCorrelation::Unknown;
			if (generation.CausalAmbiguity)
				return FallingHazardCorrelation::Ambiguous;
			if (generation.Forecast == FallingHazardForecast::Unknown)
				return FallingHazardCorrelation::Unknown;
			if (generation.Forecast == FallingHazardForecast::NoHarmfulPainObserved)
				return FallingHazardCorrelation::ActualOnly;
			return generation.ExpectedHarmfulPathMatched
				? FallingHazardCorrelation::ConfirmedHarmfulForecast
				: FallingHazardCorrelation::Ambiguous;
		}
		if (generation.Terminal == FallingHazardTerminal::Died)
			return FallingHazardCorrelation::Unknown;
		if (generation.Terminal != FallingHazardTerminal::Landed
			|| generation.LandingCollision != FallingHazardCollisionKind::StaticWorld
			|| generation.ActualTrajectoryUnknown || generation.CausalAmbiguity
			|| !generation.HarmfulFootEvidenceKnown
			|| !generation.WaterEvidenceKnown
			|| generation.SweptSegmentCount == 0
			|| !generation.HasPositiveElapsed)
			return FallingHazardCorrelation::Unknown;
		if (generation.Forecast == FallingHazardForecast::HarmfulPainObserved)
			return FallingHazardCorrelation::ForecastOnly;
		if (generation.Forecast == FallingHazardForecast::NoHarmfulPainObserved)
			return FallingHazardCorrelation::ConfirmedNoHarmfulObservation;
		return FallingHazardCorrelation::Unknown;
	}
}
