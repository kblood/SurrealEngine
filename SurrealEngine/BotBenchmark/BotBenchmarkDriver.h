#pragma once

#include "BotBenchmarkDeathAttribution.h"

#include <cmath>
#include <cstdint>

class HeadlessDriverRegistry;

namespace BotBenchmarkDriverDetail
{
	struct NativePawnCounters
	{
		uint64_t PainLedgeVetoes = 0;
		uint64_t PainLedgeRepeatVetoes = 0;
		uint64_t PainLedgeRecoveryAttempts = 0;
		uint64_t PainLedgeRecoveryEscapes = 0;
		uint64_t WallAdjustCalls = 0;
		uint64_t WallAdjustRepeats = 0;
		uint64_t WallAdjustRecoveryAttempts = 0;
		uint64_t WallAdjustRecoverySuccesses = 0;
		uint64_t WallAdjustForcedReplans = 0;
		uint64_t MoveStallDetections = 0;
		uint64_t MoveStallEpisodeResets = 0;
		uint64_t MoveStallForcedReplans = 0;
		uint64_t MoveStallNavigationForcedReplans = 0;
		uint64_t MoveStallTargetlessMoveToTimeouts = 0;
		double MoveStallEligibleSeconds = 0.0;
		uint64_t FailedNavigationAvoidanceActivations = 0;
		uint64_t FailedNavigationSafeguardSuppressions = 0;
		uint64_t FailedNavigationRoutePenaltyApplications = 0;
		uint64_t FallingSeamDetections = 0;
		uint64_t HorizontalCornerCandidateProbes = 0;
		uint64_t HorizontalCornerAuthorizedEscapes = 0;
		uint64_t HorizontalCornerTargetProgressRejects = 0;
		uint64_t HorizontalCornerUnknownOrUnsafeSupport = 0;
		uint64_t FallingSeamEpisodes = 0;
		uint64_t FallingSeamInvalidGeometryRejects = 0;
		uint64_t FallingSeamAuthorizableEpisodes = 0;
		uint64_t HorizontalCornerAuthorizedCandidates = 0;
		uint64_t HorizontalCornerBlockedSweepCandidates = 0;
		uint64_t HorizontalCornerNoStaticWalkableSupportCandidates = 0;
		uint64_t HorizontalCornerPainSupportCandidates = 0;
		uint64_t HorizontalCornerNoActiveMovementIntentOrTargetCandidates = 0;
		uint64_t HorizontalCornerTrueTargetRegressionCandidates = 0;
		uint64_t HorizontalCornerUnknownEvidenceCandidates = 0;
	};

	enum class NativePawnCounterSample
	{
		LivePawn,
		DeathFlush,
	};

	struct NativePawnCounterBegin
	{
		bool BeganPawn = false;
		bool ResetLifeAttribution = false;
	};

	class NativePawnCounterEpoch
	{
	public:
		NativePawnCounterBegin BeginPawn(const void* pawn, NativePawnCounterSample sample)
		{
			if (Pawn == pawn)
				return {};
			Pawn = pawn;
			Previous = {};
			return { true, sample == NativePawnCounterSample::LivePawn };
		}

		void Accumulate(const NativePawnCounters& current, NativePawnCounters& totals)
		{
			AccumulateCounter(current.PainLedgeVetoes, Previous.PainLedgeVetoes,
				totals.PainLedgeVetoes);
			AccumulateCounter(current.PainLedgeRepeatVetoes, Previous.PainLedgeRepeatVetoes,
				totals.PainLedgeRepeatVetoes);
			AccumulateCounter(current.PainLedgeRecoveryAttempts, Previous.PainLedgeRecoveryAttempts,
				totals.PainLedgeRecoveryAttempts);
			AccumulateCounter(current.PainLedgeRecoveryEscapes, Previous.PainLedgeRecoveryEscapes,
				totals.PainLedgeRecoveryEscapes);
			AccumulateCounter(current.WallAdjustCalls, Previous.WallAdjustCalls,
				totals.WallAdjustCalls);
			AccumulateCounter(current.WallAdjustRepeats, Previous.WallAdjustRepeats,
				totals.WallAdjustRepeats);
			AccumulateCounter(current.WallAdjustRecoveryAttempts,
				Previous.WallAdjustRecoveryAttempts, totals.WallAdjustRecoveryAttempts);
			AccumulateCounter(current.WallAdjustRecoverySuccesses,
				Previous.WallAdjustRecoverySuccesses, totals.WallAdjustRecoverySuccesses);
			AccumulateCounter(current.WallAdjustForcedReplans, Previous.WallAdjustForcedReplans,
				totals.WallAdjustForcedReplans);
			AccumulateCounter(current.MoveStallDetections, Previous.MoveStallDetections,
				totals.MoveStallDetections);
			AccumulateCounter(current.MoveStallEpisodeResets, Previous.MoveStallEpisodeResets,
				totals.MoveStallEpisodeResets);
			AccumulateCounter(current.MoveStallForcedReplans, Previous.MoveStallForcedReplans,
				totals.MoveStallForcedReplans);
			AccumulateCounter(current.MoveStallNavigationForcedReplans,
				Previous.MoveStallNavigationForcedReplans,
				totals.MoveStallNavigationForcedReplans);
			AccumulateCounter(current.MoveStallTargetlessMoveToTimeouts,
				Previous.MoveStallTargetlessMoveToTimeouts,
				totals.MoveStallTargetlessMoveToTimeouts);
			AccumulateDuration(current.MoveStallEligibleSeconds,
				Previous.MoveStallEligibleSeconds, totals.MoveStallEligibleSeconds);
			AccumulateCounter(current.FailedNavigationAvoidanceActivations,
				Previous.FailedNavigationAvoidanceActivations,
				totals.FailedNavigationAvoidanceActivations);
			AccumulateCounter(current.FailedNavigationSafeguardSuppressions,
				Previous.FailedNavigationSafeguardSuppressions,
				totals.FailedNavigationSafeguardSuppressions);
			AccumulateCounter(current.FailedNavigationRoutePenaltyApplications,
				Previous.FailedNavigationRoutePenaltyApplications,
				totals.FailedNavigationRoutePenaltyApplications);
			AccumulateCounter(current.FallingSeamDetections, Previous.FallingSeamDetections,
				totals.FallingSeamDetections);
			AccumulateCounter(current.HorizontalCornerCandidateProbes,
				Previous.HorizontalCornerCandidateProbes,
				totals.HorizontalCornerCandidateProbes);
			AccumulateCounter(current.HorizontalCornerAuthorizedEscapes,
				Previous.HorizontalCornerAuthorizedEscapes,
				totals.HorizontalCornerAuthorizedEscapes);
			AccumulateCounter(current.HorizontalCornerTargetProgressRejects,
				Previous.HorizontalCornerTargetProgressRejects,
				totals.HorizontalCornerTargetProgressRejects);
			AccumulateCounter(current.HorizontalCornerUnknownOrUnsafeSupport,
				Previous.HorizontalCornerUnknownOrUnsafeSupport,
				totals.HorizontalCornerUnknownOrUnsafeSupport);
			AccumulateCounter(current.FallingSeamEpisodes, Previous.FallingSeamEpisodes,
				totals.FallingSeamEpisodes);
			AccumulateCounter(current.FallingSeamInvalidGeometryRejects,
				Previous.FallingSeamInvalidGeometryRejects,
				totals.FallingSeamInvalidGeometryRejects);
			AccumulateCounter(current.FallingSeamAuthorizableEpisodes,
				Previous.FallingSeamAuthorizableEpisodes,
				totals.FallingSeamAuthorizableEpisodes);
			AccumulateCounter(current.HorizontalCornerAuthorizedCandidates,
				Previous.HorizontalCornerAuthorizedCandidates,
				totals.HorizontalCornerAuthorizedCandidates);
			AccumulateCounter(current.HorizontalCornerBlockedSweepCandidates,
				Previous.HorizontalCornerBlockedSweepCandidates,
				totals.HorizontalCornerBlockedSweepCandidates);
			AccumulateCounter(current.HorizontalCornerNoStaticWalkableSupportCandidates,
				Previous.HorizontalCornerNoStaticWalkableSupportCandidates,
				totals.HorizontalCornerNoStaticWalkableSupportCandidates);
			AccumulateCounter(current.HorizontalCornerPainSupportCandidates,
				Previous.HorizontalCornerPainSupportCandidates,
				totals.HorizontalCornerPainSupportCandidates);
			AccumulateCounter(current.HorizontalCornerNoActiveMovementIntentOrTargetCandidates,
				Previous.HorizontalCornerNoActiveMovementIntentOrTargetCandidates,
				totals.HorizontalCornerNoActiveMovementIntentOrTargetCandidates);
			AccumulateCounter(current.HorizontalCornerTrueTargetRegressionCandidates,
				Previous.HorizontalCornerTrueTargetRegressionCandidates,
				totals.HorizontalCornerTrueTargetRegressionCandidates);
			AccumulateCounter(current.HorizontalCornerUnknownEvidenceCandidates,
				Previous.HorizontalCornerUnknownEvidenceCandidates,
				totals.HorizontalCornerUnknownEvidenceCandidates);
		}

	private:
		static void AccumulateCounter(uint64_t current, uint64_t& previous, uint64_t& total)
		{
			total += current >= previous ? current - previous : current;
			previous = current;
		}

		static void AccumulateDuration(double current, double& previous, double& total)
		{
			if (!std::isfinite(current) || current < 0.0)
				return;
			const double delta = current >= previous ? current - previous : current;
			if (std::isfinite(total + delta))
				total += delta;
			previous = current;
		}

		const void* Pawn = nullptr;
		NativePawnCounters Previous;
	};

	struct DeathAttributionCounters
	{
		uint64_t DirectSelfKills = 0;
		uint64_t DirectEnemyKills = 0;
		uint64_t UnassistedEnvironmentalDeaths = 0;
		uint64_t RecentEnemyContributedEnvironmentalDeathsProxy = 0;
		uint64_t AmbiguousDeaths = 0;
		uint64_t RecentEnemyMomentumContributedEnvironmentalDeathsProxy = 0;

		void Apply(const BotBenchmarkDeathAttribution::Decision& decision)
		{
			using BotBenchmarkDeathAttribution::Kind;
			switch (decision.Attribution)
			{
			case Kind::DirectSelfKill: DirectSelfKills++; break;
			case Kind::DirectEnemyKill: DirectEnemyKills++; break;
			case Kind::UnassistedEnvironmentalDeath: UnassistedEnvironmentalDeaths++; break;
			case Kind::RecentEnemyContributedEnvironmentalDeathProxy:
				RecentEnemyContributedEnvironmentalDeathsProxy++;
				if (decision.HadRecentEnemyMomentumContribution)
					RecentEnemyMomentumContributedEnvironmentalDeathsProxy++;
				break;
			case Kind::AmbiguousDeath: AmbiguousDeaths++; break;
			}
		}

		uint64_t ClassifiedDeaths() const
		{
			return DirectSelfKills + DirectEnemyKills + UnassistedEnvironmentalDeaths +
				RecentEnemyContributedEnvironmentalDeathsProxy + AmbiguousDeaths;
		}
	};
}

void RegisterBotBenchmarkDriver(HeadlessDriverRegistry& registry);
