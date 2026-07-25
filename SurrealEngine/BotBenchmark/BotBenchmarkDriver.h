#pragma once

#include "BotBenchmarkDeathAttribution.h"
#include "UObject/PawnWalkingStepPreflight.h"

#include <array>
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
		uint64_t HarmfulZoneEscapeEpisodes = 0;
		uint64_t HarmfulZoneEscapeCenterEntries = 0;
		uint64_t HarmfulZoneEscapeFootEntries = 0;
		uint64_t HarmfulZoneEscapeRecoveryAttempts = 0;
		uint64_t HarmfulZoneEscapeSuccessfulEscapes = 0;
		uint64_t HarmfulZoneEscapeForcedReplans = 0;
		uint64_t HarmfulZoneEscapeNoSafeCandidates = 0;
		uint64_t HazardSwimEgressEpisodes = 0;
		uint64_t HazardSwimEgressEligible = 0;
		uint64_t HazardSwimEgressAuthorized = 0;
		uint64_t HazardSwimEgressDebounced = 0;
		uint64_t HazardSwimEgressNoAnchorRejected = 0;
		uint64_t HazardSwimEgressExited = 0;
		uint64_t HazardSwimEgressDeathsBeforeExit = 0;
		uint64_t HazardSwimEgressForcedReplans = 0;
		uint64_t HazardSwimEgressFallingPreMoveAnchorCaptures = 0;
		uint64_t HazardSwimEgressFallingPreMoveAnchorUses = 0;
		uint64_t HazardSwimEgressLiveApplies = 0;
		uint64_t HazardSwimEgressLiveActiveTicks = 0;
		uint64_t HazardSwimEgressLiveProbeRejected = 0;
		uint64_t HazardSwimEgressLiveSuccessfulExits = 0;
		uint64_t HazardSwimEgressDirectNavProbes = 0;
		uint64_t HazardSwimEgressDirectNavSafeCandidates = 0;
		std::string HazardSwimEgressDirectNavBestCandidateName;
		bool HazardSwimEgressAnchorKnown = false;
		std::string HazardSwimEgressAnchorSource;
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
		uint64_t WalkingStepPreflightObservations = 0;
		uint64_t WalkingStepPreflightUnsupportedEndpoints = 0;
		uint64_t WalkingStepPreflightNoDecisions = 0;
		uint64_t WalkingStepPreflightProvisionalAuthorizations = 0;
		uint64_t WalkingStepPreflightAuthorizations = 0;
		uint64_t WalkingStepPreflightAuthorizableEpisodes = 0;
		uint64_t WalkingStepPreflightPositiveDpsVetoEligible = 0;
		uint64_t WalkingStepPreflightPositiveDpsVetoApplied = 0;
		uint64_t WalkingStepPreflightPositiveDpsVetoDebounced = 0;
		uint64_t WalkingStepPreflightPositiveDpsVetoForcedReplans = 0;
		uint64_t WalkingStepPreflightPositiveDpsVetoRollbackRejected = 0;
		uint64_t WalkingStepPreflightPositiveDpsVetoActionOverflows = 0;
		uint64_t WalkingStepPreflightDiagnosticOverflows = 0;
		uint64_t FallingParityRealizedEpisodes = 0;
		uint64_t FallingParityRealizedSteps = 0;
		uint64_t FallingParityRealizedMatchedSteps = 0;
		uint64_t FallingParityRealizedMatchedLandingSteps = 0;
		uint64_t FallingParityRealizedMismatches = 0;
		uint64_t FallingParityRealizedUnknowns = 0;
		uint64_t FallingParityRealizedCallbackBarriers = 0;
		uint64_t FallingParityRealizedPainEntries = 0;
		uint64_t FallingParityRealizedDeaths = 0;
		uint64_t FallingParityRealizedLandings = 0;
		uint64_t FallingParityRealizedContinuityLosses = 0;
		uint64_t FallingParityRealizedRecordOverflows = 0;
		uint64_t VerticalPainColumnEpisodesStarted = 0;
		uint64_t VerticalPainColumnEpisodesCompleted = 0;
		uint64_t VerticalPainColumnTruePositiveOutcomes = 0;
		uint64_t VerticalPainColumnFalsePositiveOutcomes = 0;
		uint64_t VerticalPainColumnFalseNegativeOutcomes = 0;
		uint64_t VerticalPainColumnTrueNegativeOutcomes = 0;
		uint64_t VerticalPainColumnAmbiguousOutcomes = 0;
		uint64_t VerticalPainColumnUnknownOutcomes = 0;
		uint64_t VerticalPainColumnDiagnosticOverflows = 0;
		uint64_t VerticalPainColumnGenerationCapacityExhaustions = 0;
		uint64_t PersistentHarmfulFallCandidatesStarted = 0;
		uint64_t PersistentHarmfulFallPromotions = 0;
		uint64_t PersistentHarmfulFallResets = 0;
		uint64_t PersistentHarmfulFallConfirmedHarmfulEntries = 0;
		uint64_t PersistentHarmfulFallObservedLeadSamples = 0;
		uint64_t PersistentHarmfulFallObservedLeadMilliseconds = 0;
		std::array<uint64_t, PawnMovement::WalkingStepPreflightReasonCount>
			WalkingStepPreflightReasons = {};
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
			AccumulateCounter(current.HarmfulZoneEscapeEpisodes,
				Previous.HarmfulZoneEscapeEpisodes, totals.HarmfulZoneEscapeEpisodes);
			AccumulateCounter(current.HarmfulZoneEscapeCenterEntries,
				Previous.HarmfulZoneEscapeCenterEntries, totals.HarmfulZoneEscapeCenterEntries);
			AccumulateCounter(current.HarmfulZoneEscapeFootEntries,
				Previous.HarmfulZoneEscapeFootEntries, totals.HarmfulZoneEscapeFootEntries);
			AccumulateCounter(current.HarmfulZoneEscapeRecoveryAttempts,
				Previous.HarmfulZoneEscapeRecoveryAttempts,
				totals.HarmfulZoneEscapeRecoveryAttempts);
			AccumulateCounter(current.HarmfulZoneEscapeSuccessfulEscapes,
				Previous.HarmfulZoneEscapeSuccessfulEscapes,
				totals.HarmfulZoneEscapeSuccessfulEscapes);
			AccumulateCounter(current.HarmfulZoneEscapeForcedReplans,
				Previous.HarmfulZoneEscapeForcedReplans,
				totals.HarmfulZoneEscapeForcedReplans);
			AccumulateCounter(current.HarmfulZoneEscapeNoSafeCandidates,
				Previous.HarmfulZoneEscapeNoSafeCandidates,
				totals.HarmfulZoneEscapeNoSafeCandidates);
			AccumulateCounter(current.HazardSwimEgressEpisodes, Previous.HazardSwimEgressEpisodes,
				totals.HazardSwimEgressEpisodes);
			AccumulateCounter(current.HazardSwimEgressEligible, Previous.HazardSwimEgressEligible,
				totals.HazardSwimEgressEligible);
			AccumulateCounter(current.HazardSwimEgressAuthorized, Previous.HazardSwimEgressAuthorized,
				totals.HazardSwimEgressAuthorized);
			AccumulateCounter(current.HazardSwimEgressDebounced, Previous.HazardSwimEgressDebounced,
				totals.HazardSwimEgressDebounced);
			AccumulateCounter(current.HazardSwimEgressNoAnchorRejected,
				Previous.HazardSwimEgressNoAnchorRejected,
				totals.HazardSwimEgressNoAnchorRejected);
			AccumulateCounter(current.HazardSwimEgressExited, Previous.HazardSwimEgressExited,
				totals.HazardSwimEgressExited);
			AccumulateCounter(current.HazardSwimEgressDeathsBeforeExit,
				Previous.HazardSwimEgressDeathsBeforeExit,
				totals.HazardSwimEgressDeathsBeforeExit);
			AccumulateCounter(current.HazardSwimEgressForcedReplans,
				Previous.HazardSwimEgressForcedReplans,
				totals.HazardSwimEgressForcedReplans);
			AccumulateCounter(current.HazardSwimEgressFallingPreMoveAnchorCaptures,
				Previous.HazardSwimEgressFallingPreMoveAnchorCaptures,
				totals.HazardSwimEgressFallingPreMoveAnchorCaptures);
			AccumulateCounter(current.HazardSwimEgressFallingPreMoveAnchorUses,
				Previous.HazardSwimEgressFallingPreMoveAnchorUses,
				totals.HazardSwimEgressFallingPreMoveAnchorUses);
			AccumulateCounter(current.HazardSwimEgressLiveApplies,
				Previous.HazardSwimEgressLiveApplies, totals.HazardSwimEgressLiveApplies);
			AccumulateCounter(current.HazardSwimEgressLiveActiveTicks,
				Previous.HazardSwimEgressLiveActiveTicks, totals.HazardSwimEgressLiveActiveTicks);
			AccumulateCounter(current.HazardSwimEgressLiveProbeRejected,
				Previous.HazardSwimEgressLiveProbeRejected, totals.HazardSwimEgressLiveProbeRejected);
			AccumulateCounter(current.HazardSwimEgressLiveSuccessfulExits,
				Previous.HazardSwimEgressLiveSuccessfulExits,
				totals.HazardSwimEgressLiveSuccessfulExits);
			AccumulateCounter(current.HazardSwimEgressDirectNavProbes,
				Previous.HazardSwimEgressDirectNavProbes, totals.HazardSwimEgressDirectNavProbes);
			AccumulateCounter(current.HazardSwimEgressDirectNavSafeCandidates,
				Previous.HazardSwimEgressDirectNavSafeCandidates,
				totals.HazardSwimEgressDirectNavSafeCandidates);
			if (!current.HazardSwimEgressDirectNavBestCandidateName.empty())
			{
				totals.HazardSwimEgressDirectNavBestCandidateName =
					current.HazardSwimEgressDirectNavBestCandidateName;
			}
			totals.HazardSwimEgressAnchorKnown = current.HazardSwimEgressAnchorKnown;
			totals.HazardSwimEgressAnchorSource = current.HazardSwimEgressAnchorSource;
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
			AccumulateCounter(current.WalkingStepPreflightObservations,
				Previous.WalkingStepPreflightObservations,
				totals.WalkingStepPreflightObservations);
			AccumulateCounter(current.WalkingStepPreflightUnsupportedEndpoints,
				Previous.WalkingStepPreflightUnsupportedEndpoints,
				totals.WalkingStepPreflightUnsupportedEndpoints);
			AccumulateCounter(current.WalkingStepPreflightNoDecisions,
				Previous.WalkingStepPreflightNoDecisions,
				totals.WalkingStepPreflightNoDecisions);
			AccumulateCounter(current.WalkingStepPreflightProvisionalAuthorizations,
				Previous.WalkingStepPreflightProvisionalAuthorizations,
				totals.WalkingStepPreflightProvisionalAuthorizations);
			AccumulateCounter(current.WalkingStepPreflightAuthorizations,
				Previous.WalkingStepPreflightAuthorizations,
				totals.WalkingStepPreflightAuthorizations);
			AccumulateCounter(current.WalkingStepPreflightAuthorizableEpisodes,
				Previous.WalkingStepPreflightAuthorizableEpisodes,
				totals.WalkingStepPreflightAuthorizableEpisodes);
			AccumulateCounter(current.WalkingStepPreflightPositiveDpsVetoEligible,
				Previous.WalkingStepPreflightPositiveDpsVetoEligible,
				totals.WalkingStepPreflightPositiveDpsVetoEligible);
			AccumulateCounter(current.WalkingStepPreflightPositiveDpsVetoApplied,
				Previous.WalkingStepPreflightPositiveDpsVetoApplied,
				totals.WalkingStepPreflightPositiveDpsVetoApplied);
			AccumulateCounter(current.WalkingStepPreflightPositiveDpsVetoDebounced,
				Previous.WalkingStepPreflightPositiveDpsVetoDebounced,
				totals.WalkingStepPreflightPositiveDpsVetoDebounced);
			AccumulateCounter(current.WalkingStepPreflightPositiveDpsVetoForcedReplans,
				Previous.WalkingStepPreflightPositiveDpsVetoForcedReplans,
				totals.WalkingStepPreflightPositiveDpsVetoForcedReplans);
			AccumulateCounter(current.WalkingStepPreflightPositiveDpsVetoRollbackRejected,
				Previous.WalkingStepPreflightPositiveDpsVetoRollbackRejected,
				totals.WalkingStepPreflightPositiveDpsVetoRollbackRejected);
			AccumulateCounter(current.WalkingStepPreflightPositiveDpsVetoActionOverflows,
				Previous.WalkingStepPreflightPositiveDpsVetoActionOverflows,
				totals.WalkingStepPreflightPositiveDpsVetoActionOverflows);
			AccumulateCounter(current.WalkingStepPreflightDiagnosticOverflows,
				Previous.WalkingStepPreflightDiagnosticOverflows,
				totals.WalkingStepPreflightDiagnosticOverflows);
			AccumulateCounter(current.FallingParityRealizedEpisodes,
				Previous.FallingParityRealizedEpisodes,
				totals.FallingParityRealizedEpisodes);
			AccumulateCounter(current.FallingParityRealizedSteps,
				Previous.FallingParityRealizedSteps,
				totals.FallingParityRealizedSteps);
			AccumulateCounter(current.FallingParityRealizedMatchedSteps,
				Previous.FallingParityRealizedMatchedSteps,
				totals.FallingParityRealizedMatchedSteps);
			AccumulateCounter(current.FallingParityRealizedMatchedLandingSteps,
				Previous.FallingParityRealizedMatchedLandingSteps,
				totals.FallingParityRealizedMatchedLandingSteps);
			AccumulateCounter(current.FallingParityRealizedMismatches,
				Previous.FallingParityRealizedMismatches,
				totals.FallingParityRealizedMismatches);
			AccumulateCounter(current.FallingParityRealizedUnknowns,
				Previous.FallingParityRealizedUnknowns,
				totals.FallingParityRealizedUnknowns);
			AccumulateCounter(current.FallingParityRealizedCallbackBarriers,
				Previous.FallingParityRealizedCallbackBarriers,
				totals.FallingParityRealizedCallbackBarriers);
			AccumulateCounter(current.FallingParityRealizedPainEntries,
				Previous.FallingParityRealizedPainEntries,
				totals.FallingParityRealizedPainEntries);
			AccumulateCounter(current.FallingParityRealizedDeaths,
				Previous.FallingParityRealizedDeaths,
				totals.FallingParityRealizedDeaths);
			AccumulateCounter(current.FallingParityRealizedLandings,
				Previous.FallingParityRealizedLandings,
				totals.FallingParityRealizedLandings);
			AccumulateCounter(current.FallingParityRealizedContinuityLosses,
				Previous.FallingParityRealizedContinuityLosses,
				totals.FallingParityRealizedContinuityLosses);
			AccumulateCounter(current.FallingParityRealizedRecordOverflows,
				Previous.FallingParityRealizedRecordOverflows,
				totals.FallingParityRealizedRecordOverflows);
			AccumulateCounter(current.VerticalPainColumnEpisodesStarted,
				Previous.VerticalPainColumnEpisodesStarted,
				totals.VerticalPainColumnEpisodesStarted);
			AccumulateCounter(current.VerticalPainColumnEpisodesCompleted,
				Previous.VerticalPainColumnEpisodesCompleted,
				totals.VerticalPainColumnEpisodesCompleted);
			AccumulateCounter(current.VerticalPainColumnTruePositiveOutcomes,
				Previous.VerticalPainColumnTruePositiveOutcomes,
				totals.VerticalPainColumnTruePositiveOutcomes);
			AccumulateCounter(current.VerticalPainColumnFalsePositiveOutcomes,
				Previous.VerticalPainColumnFalsePositiveOutcomes,
				totals.VerticalPainColumnFalsePositiveOutcomes);
			AccumulateCounter(current.VerticalPainColumnFalseNegativeOutcomes,
				Previous.VerticalPainColumnFalseNegativeOutcomes,
				totals.VerticalPainColumnFalseNegativeOutcomes);
			AccumulateCounter(current.VerticalPainColumnTrueNegativeOutcomes,
				Previous.VerticalPainColumnTrueNegativeOutcomes,
				totals.VerticalPainColumnTrueNegativeOutcomes);
			AccumulateCounter(current.VerticalPainColumnAmbiguousOutcomes,
				Previous.VerticalPainColumnAmbiguousOutcomes,
				totals.VerticalPainColumnAmbiguousOutcomes);
			AccumulateCounter(current.VerticalPainColumnUnknownOutcomes,
				Previous.VerticalPainColumnUnknownOutcomes,
				totals.VerticalPainColumnUnknownOutcomes);
			AccumulateCounter(current.VerticalPainColumnDiagnosticOverflows,
				Previous.VerticalPainColumnDiagnosticOverflows,
				totals.VerticalPainColumnDiagnosticOverflows);
			AccumulateCounter(
				current.VerticalPainColumnGenerationCapacityExhaustions,
				Previous.VerticalPainColumnGenerationCapacityExhaustions,
				totals.VerticalPainColumnGenerationCapacityExhaustions);
			AccumulateCounter(current.PersistentHarmfulFallCandidatesStarted,
				Previous.PersistentHarmfulFallCandidatesStarted,
				totals.PersistentHarmfulFallCandidatesStarted);
			AccumulateCounter(current.PersistentHarmfulFallPromotions,
				Previous.PersistentHarmfulFallPromotions,
				totals.PersistentHarmfulFallPromotions);
			AccumulateCounter(current.PersistentHarmfulFallResets,
				Previous.PersistentHarmfulFallResets,
				totals.PersistentHarmfulFallResets);
			AccumulateCounter(current.PersistentHarmfulFallConfirmedHarmfulEntries,
				Previous.PersistentHarmfulFallConfirmedHarmfulEntries,
				totals.PersistentHarmfulFallConfirmedHarmfulEntries);
			AccumulateCounter(current.PersistentHarmfulFallObservedLeadSamples,
				Previous.PersistentHarmfulFallObservedLeadSamples,
				totals.PersistentHarmfulFallObservedLeadSamples);
			AccumulateCounter(current.PersistentHarmfulFallObservedLeadMilliseconds,
				Previous.PersistentHarmfulFallObservedLeadMilliseconds,
				totals.PersistentHarmfulFallObservedLeadMilliseconds);
			for (size_t index = 0; index < current.WalkingStepPreflightReasons.size(); index++)
			{
				AccumulateCounter(current.WalkingStepPreflightReasons[index],
					Previous.WalkingStepPreflightReasons[index],
					totals.WalkingStepPreflightReasons[index]);
			}
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
