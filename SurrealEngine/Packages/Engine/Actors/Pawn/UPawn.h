#pragma once

#include "Packages/Engine/Actors/UActor.h"
#include "UObject/PawnFailedNavigationMemory.h"
#include "UObject/PawnFallingTwoPlaneSafety.h"
#include "UObject/PawnMoveStallWatchdog.h"
#include "UObject/PawnPainLedgeRecovery.h"
#include "UObject/PawnLedgeTransition.h"
#include "UObject/PawnFallingParityRealizedTrace.h"
#include "UObject/PawnFallingHitWallCallbackWitness.h"
#include "UObject/PawnFallingHazardRuntimeObserver.h"
#include "UObject/PawnHazardWaterEgressObserver.h"
#include "UObject/PawnWalkingStepPreflight.h"
#include "UObject/PawnInventoryReachability.h"
#include "UObject/PawnDirectReachCommandProvenance.h"
#include "UObject/PawnMovementCommandProvenance.h"
#include "UObject/PawnHazardResidenceCommandTransitionLedger.h"
#include "UObject/PawnHazardResidencePreentryCausalSlice.h"
#include "UObject/PawnRoutePathCommitProvenance.h"
#include "UObject/PawnPickTargetObserver.h"
#include "UObject/PawnCanSeeObserver.h"
#include "UObject/PawnFiniteMoveCommandGuard.h"
#include "UObject/PawnPickRegDestinationZeroDivideGuard.h"
#include "UObject/PawnVectorNonFiniteObserver.h"
#include "UObject/PawnWalkingHitWallDispatch.h"
#include "UObject/PawnWallAdjustRecovery.h"
#include "UObject/PawnHazardResidenceObserver.h"
#include "UObject/PawnExternalImpulseFallWitness.h"
#include "BotAI/HarmfulZoneEscapeGate.h"
#include "BotAI/FallingHazardRecoveryGate.h"
#include "BotAI/HazardSwimEgressGate.h"
#include "BotAI/HazardSwimEgressLiveSteer.h"

class USound;
class LevelReachSpec;
class UNavigationPoint;
class UWeapon;
class UInventory;
class UDecal;
class UDecoration;
class UPlayerReplicationInfo;
struct DXAISightLineOfSightResult;

struct PawnPathEndPointResult
{
	Array<class UNavigationPoint*> Points;
	Array<int32_t> EdgeReachSpecIndexes;
	int32_t RawEndpointCost = 0;
	int32_t AdjustedEndpointCost = 0;
	uint32_t FailedNavigationPenaltyApplications = 0;
};

class UPawn : public UActor
{
public:
	using UActor::UActor;

	void Tick(float elapsed) override;
	void TickRotating(float elapsed) override;

	void InitActorZone() override;
	void UpdateActorZone() override;
	void ObserveHarmfulZoneEscapeBoundary(UZoneInfo* oldZone, UZoneInfo* newZone,
		bool footBoundary);
	void BeginHazardSwimEgressFallingTick();
	void CaptureHazardSwimEgressFallingAnchorBeforePhysicsMove(bool directMove = true);
	void CaptureHazardSwimEgressAnchorBeforePhysicsMove();
	void ObserveHazardSwimEgressAfterPhysicsMove();
	void AdvanceHazardSwimEgressLiveSteer();
	void ObserveHazardSwimEgressLiveSteerShadowDecision(
		const BotAI::HazardSwimEgressLiveSteerDecision& decision);
	void EndHazardSwimEgressSwimSession();
	void EndHazardSwimEgressRun();
	void RecordHazardSwimEgressDeath();
	void EndHazardResidenceRun();
	bool RecordHazardResidenceDeath();
	std::optional<PawnMovement::HazardResidenceDeathWitness>
		DrainHazardResidenceDeathWitness();
	std::vector<std::pair<uint64_t, PawnMovement::HazardResidenceTerminal>>
		DrainDirectReachHazardResidenceTerminals();
	std::vector<PawnMovement::MovementCommandProvenanceObservation>
		DrainMovementCommandProvenanceObservations();
	std::vector<PawnMovement::HazardResidenceCommandTransitionLedgerRecord>
		DrainHazardResidenceCommandTransitionLedgerRecords();
	uint64_t HazardResidenceCommandTransitionLedgerOverflowCount() const
	{
		return HazardResidenceCommandTransitionLedgerOverflowCountValue;
	}
	std::vector<PawnMovement::HazardResidencePreentryCausalSliceRecord>
		DrainHazardResidencePreentryCausalSliceRecords();
	uint64_t HazardResidencePreentryCausalSliceOverflowCount() const
	{
		return HazardResidencePreentryCausalSliceOverflowCountValue;
	}
	void ObserveHazardResidenceMayFallBoundary();
	void ObserveHazardResidenceHitWallBoundary();
	uint64_t MovementCommandProvenanceOverflowCount() const
	{
		return MovementCommandProvenanceOverflowCountValue;
	}

	void MoveTo(const vec3& newDestination, float speed);
	void MoveToward(UActor* newTarget, float speed);
	void StrafeFacing(const vec3& newDestination, UActor* newTarget);
	void StrafeTo(const vec3& newDestination, const vec3& newFocus);
	void TurnTo(const vec3& newFocus);
	void TurnToward(UActor* newTarget);
	void WaitForLanding();
	void SetMoveDuration(const vec3& deltaMove);
	float GetSpeed();

	bool TickRotateTo(const vec3& target);
	bool TickMoveTo(const vec3& target, float elapsed, UActor* targetActor = nullptr);
	void RecordPainLedgeVeto(const vec3& origin, const vec2& unsafeDirection);
	void ObserveFallingSeamEscapeShadow(const vec3& requestedRemainingDelta,
		const vec3& actualDisplacement, const vec3& firstHitNormal,
		const vec3& secondHitNormal, bool normalDownwardGravity);
	void ObserveWalkingStepPreflightShadow(const vec3& stepUpDelta,
		const vec3& forwardDelta, const vec3& stepDownDelta, int walkingIteration,
		uint64_t invocationToken);
	bool ConfirmWalkingStepPreflightShadow(int walkingIteration, uint64_t invocationToken,
		PawnMovement::LedgeTransition transition);
	void RecordWalkingStepPreflightPositiveDpsVetoOutcome(bool applied);
	bool RecordWalkingHitWallDispatch(const CollisionHit& hit,
		const vec3& velocityBeforeCollision, float minHitWallBeforeCallback,
		int physicsBeforeCallback, PawnMovement::WalkingHitWallContactPhase contactPhase,
		PawnMovement::WalkingHitWallBlockerKind blockerBeforeCallback,
		bool callbackDispatched, bool callbackSelectedByMinHitWallCandidate);
	void SetWalkingHitWallFixtureContactLimit(uint32_t limit)
	{
		WalkingHitWallFixtureContactLimit = limit;
		WalkingHitWallFixtureContactCount = 0;
	}
	void ClearWalkingHitWallFixtureContactLimit()
	{
		WalkingHitWallFixtureContactLimit = 0;
		WalkingHitWallFixtureContactCount = 0;
	}
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord>
		DrainWalkingHitWallDispatchDiagnostics();
	void QueueWalkingStepPreflightPositiveDpsVetoAction(
		PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord record);
	void ArmFallingParityRealizedTrace(int walkingIteration, uint64_t invocationToken);
	bool HasActiveFallingParityRealizedModel() const
	{
		return FallingParityRealizedTrace.ModelActive;
	}
	bool HasActiveFallingParityRealizedLifecycle() const
	{
		return FallingParityRealizedTrace.LifecycleActive;
	}
	bool HasFallingParityRealizedContinuity()
	{
		return FallingParityRealizedTrace.LifecycleActive
			&& !bDeleteMe() && Physics() == PHYS_Falling && !bJustTeleported()
			&& FallingParityRealizedTrace.Correlation.LifeGeneration
				== WalkingStepPreflightLifeGeneration;
	}
	void RecordFallingParityRealizedStep(
		PawnMovement::FallingParityRealizedOutcome outcome,
		const PawnMovement::FallingParityRealizedRecord& evidence);
	void ObserveFallingParityRealizedPain();
	void FinishFallingParityRealizedTrace(
		PawnMovement::FallingParityRealizedOutcome outcome);
	std::vector<PawnMovement::FallingParityRealizedRecord>
		DrainFallingParityRealizedRecords();
	void QueueFallingHazardForecastSource(
		PawnMovement::FallingHazardForecastSource source);
	PawnMovement::FallingHazardForecastUpdate PredictFallingHazardTrajectory(
		const vec3& acceleration);
	void EnsureFallingHazardGeneration(float physicsSliceElapsed,
		const vec3& acceleration);
	bool PrepareFallingHazardSweep(PawnMovement::FallingHazardSweepLeg leg,
		const vec3& origin, const vec3& delta, float elapsedContribution);
	bool BeginFallingHazardTryMove();
	void EndFallingHazardTryMove();
	void LatchPendingFallingHazardSweepGeometry(
		const CollisionHit& hit, const MoveCallbackEvidence& callbacks);
	void CommitPendingFallingHazardSweepAtCenterBoundary(
		const PointRegion& center, bool actorLeavingCallbackDispatched);
	void CommitPendingFallingHazardSweepAtFootBoundary();
	void RecoverFallingHazardCallbackReturn();
	void CancelPendingFallingHazardSweep(bool callbackBoundary);
	std::optional<PawnMovement::FallingHazardForecastContinuationSeed>
		FinishFallingHazardCallbackBoundary();
	void ArmFallingHazardContinuation(
		PawnMovement::FallingHazardForecastSource source,
		const PawnMovement::FallingHazardForecastContinuationSeed& continuation,
		float physicsSliceElapsed, const vec3& acceleration);
	void CaptureFallingHazardAlignedCommandWitness(bool staticWorldCollision);
	PawnMovement::FallingHazardAlignedCommandProvenance
		ConsumeFallingHazardAlignedCommandWitness();
	void FinishFallingHazardLanding(const CollisionHit& hit,
		bool ditchSupportUnknown = false);
	void FinishFallingHazardDeath();
	bool AdvanceFallingHazardRecovery(float elapsed);
	void RecordFallingHazardRecoveryHarmfulEntry();
	const PawnMovement::FallingHazardRuntimeCounters&
		FallingHazardRuntimeCounterValues() const;
	std::vector<PawnMovement::FallingHazardDiagnosticRecord>
		DrainFallingHazardDiagnostics();
	void RecordFallingHitWallCallbackWitness(
		PawnMovement::FallingHitWallCallbackWitness witness);
	std::vector<PawnMovement::FallingHitWallCallbackWitness>
		DrainFallingHitWallCallbackWitnesses();
	std::vector<PawnMovement::HazardWaterEgressDiagnosticRecord>
		DrainHazardWaterEgressDiagnostics();
	uint64_t HazardWaterEgressDiagnosticOverflowCount() const;
	uint64_t BeginWalkingStepPreflightInvocation()
	{
		return ++WalkingStepPreflightInvocationSequence;
	}
	bool HasWalkingStepPreflightConfirmation(int walkingIteration,
		uint64_t invocationToken) const
	{
		return WalkingStepPreflightPendingConfirmation
			&& WalkingStepPreflightPendingIteration == walkingIteration
			&& WalkingStepPreflightPendingInvocation == invocationToken;
	}
	void EndWalkingStepPreflightLife();
	std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
		DrainWalkingStepPreflightDiagnostics();
	std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
		DrainWalkingStepPreflightPositiveDpsVetoActions();
	void RecordInventoryDirectReachSupportObservation(
		PawnMovement::InventoryDirectReachSupportDiagnosticRecord record);
	std::vector<PawnMovement::InventoryDirectReachSupportDiagnosticRecord>
		DrainInventoryDirectReachSupportDiagnostics();
	void RecordDirectReachCommandObservation(
		PawnMovement::DirectReachCommandObservation record);
	std::vector<PawnMovement::DirectReachCommandObservation>
		DrainDirectReachCommandObservations();
	uint64_t DirectReachCommandObservationCount() const
	{
		return DirectReachCommandObservationCountValue;
	}
	uint64_t DirectReachCommandSuccessCount() const { return DirectReachCommandSuccessCountValue; }
	uint64_t DirectReachCommandFailureCount() const { return DirectReachCommandFailureCountValue; }
	uint64_t DirectReachCommandOverflowCount() const { return DirectReachCommandOverflowCountValue; }
	uint64_t DirectReachCommandLifeId() const { return MoveStallRecoveryLifeId; }
	uint64_t PainLedgeVetoCount() const { return PainLedgeVetoCountValue; }
	uint64_t PainLedgeRepeatVetoCount() const { return PainLedgeRepeatVetoCountValue; }
	uint64_t PainLedgeRecoveryAttemptCount() const { return PainLedgeRecoveryAttemptCountValue; }
	uint64_t PainLedgeRecoveryEscapeCount() const { return PainLedgeRecoveryEscapeCountValue; }
	uint64_t WallAdjustCallCount() const { return WallAdjustCallCountValue; }
	uint64_t WallAdjustRepeatCount() const { return WallAdjustRepeatCountValue; }
	uint64_t WallAdjustRecoveryAttemptCount() const { return WallAdjustRecoveryAttemptCountValue; }
	uint64_t WallAdjustRecoverySuccessCount() const { return WallAdjustRecoverySuccessCountValue; }
	uint64_t WallAdjustForcedReplanCount() const { return WallAdjustForcedReplanCountValue; }
	uint64_t MoveStallDetectionCount() const { return MoveStallDetectionCountValue; }
	uint64_t MoveStallEpisodeResetCount() const { return MoveStallEpisodeResetCountValue; }
	uint64_t MoveStallForcedReplanCount() const { return MoveStallForcedReplanCountValue; }
	uint64_t MoveStallNavigationForcedReplanCount() const { return MoveStallNavigationForcedReplanCountValue; }
	uint64_t MoveStallTargetlessMoveToTimeoutCount() const { return MoveStallTargetlessMoveToTimeoutCountValue; }
	uint64_t MoveStallDirectActorMoveTowardTimeoutCount() const
	{
		return MoveStallDirectActorMoveTowardTimeoutCountValue;
	}
	double MoveStallEligibleSeconds() const { return MoveStallEligibleSecondsValue; }
	double MoveStallNoProgressSeconds() const { return MoveStallWatchdog.Active ? MoveStallWatchdog.NoProgressSeconds : 0.0; }
	uint64_t MoveStallRecoveryEpisodeStartCount() const { return MoveStallRecoveryEpisodeStartCountValue; }
	uint64_t MoveStallRecoveryClearedWithin2SecondsCount() const
	{
		return MoveStallRecoveryClearedWithin2SecondsCountValue;
	}
	uint64_t MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCount() const
	{
		return MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCountValue;
	}
	uint64_t MoveStallRecoveryReplannedWithin5SecondsCount() const
	{
		return MoveStallRecoveryReplannedWithin5SecondsCountValue;
	}
	uint64_t MoveStallRecoveryMissed5SecondDeadlineCount() const
	{
		return MoveStallRecoveryMissed5SecondDeadlineCountValue;
	}
	uint64_t MoveStallRecoveryExcludedIntentionalStopCount() const
	{
		return MoveStallRecoveryExcludedIntentionalStopCountValue;
	}
	uint64_t MoveStallRecoveryCensoredLifeBoundaryCount() const
	{
		return MoveStallRecoveryCensoredLifeBoundaryCountValue;
	}
	uint64_t MoveStallRecoveryCensoredRunEndCount() const
	{
		return MoveStallRecoveryCensoredRunEndCountValue;
	}
	uint64_t MoveStallRecoveryUnknownCount() const { return MoveStallRecoveryUnknownCountValue; }
	uint64_t MoveStallRecoveryEpisodeRecordOverflowCount() const
	{
		return MoveStallRecoveryEpisodeRecordOverflowCountValue;
	}
	uint64_t MoveStallRecoveryDecisionRecordOverflowCount() const
	{
		return MoveStallRecoveryDecisionRecordOverflowCountValue;
	}
	std::vector<PawnMoveStallRecoveryEpisodeRecord>
		DrainMoveStallRecoveryEpisodeRecords();
	std::vector<PawnMoveStallRecoveryDecisionRecord>
		DrainMoveStallRecoveryDecisionRecords();
	void EndMoveStallRecoveryLife();
	void EndMoveStallRecoveryRun();
	uint64_t FailedNavigationAvoidanceActivationCount() const { return FailedNavigationAvoidanceActivationCountValue; }
	uint64_t FailedNavigationSafeguardSuppressionCount() const { return FailedNavigationSafeguardSuppressionCountValue; }
	uint64_t FailedNavigationRoutePenaltyApplicationCount() const { return FailedNavigationRoutePenaltyApplicationCountValue; }
	uint64_t HarmfulZoneEscapeEpisodeCount() const { return HarmfulZoneEscapeEpisodeCountValue; }
	uint64_t HarmfulZoneEscapeCenterEntryCount() const { return HarmfulZoneEscapeCenterEntryCountValue; }
	uint64_t HarmfulZoneEscapeFootEntryCount() const { return HarmfulZoneEscapeFootEntryCountValue; }
	uint64_t HarmfulZoneEscapeRecoveryAttemptCount() const { return HarmfulZoneEscapeRecoveryAttemptCountValue; }
	uint64_t HarmfulZoneEscapeSuccessfulEscapeCount() const { return HarmfulZoneEscapeSuccessfulEscapeCountValue; }
	uint64_t HarmfulZoneEscapeForcedReplanCount() const { return HarmfulZoneEscapeForcedReplanCountValue; }
	uint64_t HarmfulZoneEscapeNoSafeCandidateCount() const { return HarmfulZoneEscapeNoSafeCandidateCountValue; }
	bool IsPainLedgeRecoveryActive() const
	{
		return PainLedgeRecovery.Active && PainLedgeRecovery.RecoveryAttempted;
	}
	uint64_t HazardSwimEgressEpisodeCount() const { return HazardSwimEgressEpisodeCountValue; }
	uint64_t HazardSwimEgressEligibleCount() const { return HazardSwimEgressEligibleCountValue; }
	uint64_t HazardSwimEgressAuthorizedCount() const { return HazardSwimEgressAuthorizedCountValue; }
	uint64_t HazardSwimEgressDebouncedCount() const { return HazardSwimEgressDebouncedCountValue; }
	uint64_t HazardSwimEgressNoAnchorRejectedCount() const { return HazardSwimEgressNoAnchorRejectedCountValue; }
	uint64_t HazardSwimEgressExitCount() const { return HazardSwimEgressExitCountValue; }
	uint64_t HazardSwimEgressDeathsBeforeExitCount() const { return HazardSwimEgressDeathsBeforeExitCountValue; }
	uint64_t HazardSwimEgressForcedReplanCount() const { return HazardSwimEgressForcedReplanCountValue; }
	uint64_t HazardSwimEgressForcedReplanSameCommandReissuedCount() const
	{
		return HazardSwimEgressForcedReplanSameCommandReissuedCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanDifferentCommandIssuedCount() const
	{
		return HazardSwimEgressForcedReplanDifferentCommandIssuedCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanHazardClearedBeforeCommandCount() const
	{
		return HazardSwimEgressForcedReplanHazardClearedBeforeCommandCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanFellBeforeCommandCount() const
	{
		return HazardSwimEgressForcedReplanFellBeforeCommandCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanDiedBeforeCommandCount() const
	{
		return HazardSwimEgressForcedReplanDiedBeforeCommandCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanLifeBoundaryCensoredCount() const
	{
		return HazardSwimEgressForcedReplanLifeBoundaryCensoredCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanRunEndCensoredCount() const
	{
		return HazardSwimEgressForcedReplanRunEndCensoredCountValue;
	}
	uint64_t HazardSwimEgressForcedReplanEpisodeAbandonedCount() const
	{
		return HazardSwimEgressForcedReplanEpisodeAbandonedCountValue;
	}
	uint64_t HazardSwimEgressFallingPreMoveAnchorCaptureCount() const
	{
		return HazardSwimEgressFallingPreMoveAnchorCaptureCountValue;
	}
	uint64_t HazardSwimEgressFallingPreMoveAnchorUseCount() const
	{
		return HazardSwimEgressFallingPreMoveAnchorUseCountValue;
	}
	uint64_t HazardSwimEgressLiveApplyCount() const { return HazardSwimEgressLiveApplyCountValue; }
	uint64_t HazardSwimEgressLiveActiveTickCount() const { return HazardSwimEgressLiveActiveTickCountValue; }
	uint64_t HazardSwimEgressLiveProbeRejectedCount() const { return HazardSwimEgressLiveProbeRejectedCountValue; }
	uint64_t HazardSwimEgressLiveSuccessfulExitCount() const { return HazardSwimEgressLiveSuccessfulExitCountValue; }
	uint64_t HazardSwimEgressLiveShadowCandidateCount() const { return HazardSwimEgressLiveShadowCandidateCountValue; }
	uint64_t HazardSwimEgressLiveShadowFallingTerminalCount() const { return HazardSwimEgressLiveShadowFallingTerminalCountValue; }
	uint64_t HazardSwimEgressLiveShadowHazardClearedTerminalCount() const { return HazardSwimEgressLiveShadowHazardClearedTerminalCountValue; }
	uint64_t HazardSwimEgressLiveShadowProbeBlockedTerminalCount() const { return HazardSwimEgressLiveShadowProbeBlockedTerminalCountValue; }
	uint64_t HazardSwimEgressDirectNavProbeCount() const { return HazardSwimEgressDirectNavProbeCountValue; }
	uint64_t HazardSwimEgressDirectNavSafeCandidateCount() const { return HazardSwimEgressDirectNavSafeCandidateCountValue; }
	uint64_t HazardResidenceEpisodeCount() const { return HazardResidenceEpisodeCountValue; }
	uint64_t HazardResidenceClearedCount() const { return HazardResidenceClearedCountValue; }
	uint64_t HazardResidenceDeathCount() const { return HazardResidenceDeathCountValue; }
	uint64_t HazardResidenceLifeBoundaryCensoredCount() const { return HazardResidenceLifeBoundaryCensoredCountValue; }
	uint64_t HazardResidenceRunEndCensoredCount() const { return HazardResidenceRunEndCensoredCountValue; }
	uint64_t HazardResidenceUnknownCount() const { return HazardResidenceUnknownCountValue; }
	uint64_t HazardResidenceReentryCount() const { return HazardResidenceReentryCountValue; }
	uint64_t HazardResidenceCommandChangeCount() const { return HazardResidenceCommandChangeCountValue; }
	uint64_t HazardResidenceCandidateObservedCount() const { return HazardResidenceCandidateObservedCountValue; }
	uint64_t HazardResidenceCandidateOtherCommandCount() const { return HazardResidenceCandidateOtherCommandCountValue; }
	uint64_t FallingHazardRecoveryPromotionCount() const { return FallingHazardRecoveryPromotionCountValue; }
	uint64_t FallingHazardRecoveryAdvanceCount() const { return FallingHazardRecoveryAdvanceCountValue; }
	uint64_t FallingHazardRecoveryContextRejectedCount() const { return FallingHazardRecoveryContextRejectedCountValue; }
	uint64_t FallingHazardRecoveryNoActiveFallEpisodeCount() const { return FallingHazardRecoveryNoActiveFallEpisodeCountValue; }
	uint64_t FallingHazardRecoveryNoPrefixCount() const { return FallingHazardRecoveryNoPrefixCountValue; }
	uint64_t FallingHazardRecoveryEligibleCount() const { return FallingHazardRecoveryEligibleCountValue; }
	uint64_t FallingHazardRecoveryAnchorRejectedCount() const { return FallingHazardRecoveryAnchorRejectedCountValue; }
	uint64_t FallingHazardRecoveryProbeRejectedCount() const { return FallingHazardRecoveryProbeRejectedCountValue; }
	uint64_t FallingHazardRecoveryLiveApplyCount() const { return FallingHazardRecoveryLiveApplyCountValue; }
	uint64_t FallingHazardRecoveryLiveActiveTickCount() const { return FallingHazardRecoveryLiveActiveTickCountValue; }
	uint64_t FallingHazardRecoverySafeLandingCount() const { return FallingHazardRecoverySafeLandingCountValue; }
	uint64_t FallingHazardRecoveryHarmfulEntryCount() const { return FallingHazardRecoveryHarmfulEntryCountValue; }
	uint64_t FallingHazardRecoveryDeathCount() const { return FallingHazardRecoveryDeathCountValue; }
	uint64_t FallingHazardRecoveryTimeoutCount() const { return FallingHazardRecoveryTimeoutCountValue; }
	uint64_t ExternalImpulseFallHarmfulWitnessCount() const { return ExternalImpulseFallHarmfulWitnessCountValue; }
	uint64_t ExternalImpulseFallNoAirControlCount() const { return ExternalImpulseFallNoAirControlCountValue; }
	uint64_t ExternalImpulseFallAlternativesTestedCount() const { return ExternalImpulseFallAlternativesTestedCountValue; }
	uint64_t ExternalImpulseFallCertifiedCount() const { return ExternalImpulseFallCertifiedCountValue; }
	uint64_t ExternalImpulseFallUncertifiedCount() const { return ExternalImpulseFallUncertifiedCountValue; }
	const std::string& HazardSwimEgressDirectNavBestCandidateName() const
	{
		return HazardSwimEgress.DirectNavBestCandidateName;
	}
	bool HasHazardSwimEgressAnchor() const { return HazardSwimEgress.AnchorKnown; }
	const char* HazardSwimEgressAnchorSourceName() const;
	uint64_t FallingSeamDetectionCount() const { return FallingSeamDetectionCountValue; }
	uint64_t HorizontalCornerCandidateProbeCount() const { return HorizontalCornerCandidateProbeCountValue; }
	uint64_t HorizontalCornerAuthorizedEscapeCount() const { return HorizontalCornerAuthorizedEscapeCountValue; }
	uint64_t HorizontalCornerTargetProgressRejectCount() const { return HorizontalCornerTargetProgressRejectCountValue; }
	uint64_t HorizontalCornerUnknownOrUnsafeSupportCount() const { return HorizontalCornerUnknownOrUnsafeSupportCountValue; }
	uint64_t FallingSeamEpisodeCount() const { return FallingSeamEpisodeCountValue; }
	uint64_t FallingSeamInvalidGeometryRejectCount() const { return FallingSeamInvalidGeometryRejectCountValue; }
	uint64_t FallingSeamAuthorizableEpisodeCount() const { return FallingSeamAuthorizableEpisodeCountValue; }
	uint64_t HorizontalCornerAuthorizedCandidateCount() const { return HorizontalCornerAuthorizedCandidateCountValue; }
	uint64_t HorizontalCornerBlockedSweepCandidateCount() const { return HorizontalCornerBlockedSweepCandidateCountValue; }
	uint64_t HorizontalCornerNoStaticWalkableSupportCandidateCount() const { return HorizontalCornerNoStaticWalkableSupportCandidateCountValue; }
	uint64_t HorizontalCornerPainSupportCandidateCount() const { return HorizontalCornerPainSupportCandidateCountValue; }
	uint64_t HorizontalCornerNoActiveMovementIntentOrTargetCandidateCount() const { return HorizontalCornerNoActiveMovementIntentOrTargetCandidateCountValue; }
	uint64_t HorizontalCornerTrueTargetRegressionCandidateCount() const { return HorizontalCornerTrueTargetRegressionCandidateCountValue; }
	uint64_t HorizontalCornerUnknownEvidenceCandidateCount() const { return HorizontalCornerUnknownEvidenceCandidateCountValue; }
	uint64_t WalkingStepPreflightObservationCount() const { return WalkingStepPreflightObservationCountValue; }
	uint64_t WalkingStepPreflightUnsupportedEndpointCount() const { return WalkingStepPreflightUnsupportedEndpointCountValue; }
	uint64_t WalkingStepPreflightNoDecisionCount() const { return WalkingStepPreflightNoDecisionCountValue; }
	uint64_t WalkingStepPreflightProvisionalAuthorizationCount() const { return WalkingStepPreflightProvisionalAuthorizationCountValue; }
	uint64_t WalkingStepPreflightAuthorizationCount() const { return WalkingStepPreflightAuthorizationCountValue; }
	uint64_t WalkingStepPreflightAuthorizableEpisodeCount() const { return WalkingStepPreflightAuthorizableEpisodeCountValue; }
	uint64_t WalkingStepPreflightDiagnosticOverflowCount() const { return WalkingStepPreflightDiagnosticOverflowCountValue; }
	uint64_t WalkingHitWallDispatchObservationCount() const { return WalkingHitWallDispatchObservationCountValue; }
	uint64_t WalkingHitWallDispatchLegacyZBandCount() const { return WalkingHitWallDispatchLegacyZBandCountValue; }
	uint64_t WalkingHitWallDispatchMinHitWallCount() const { return WalkingHitWallDispatchMinHitWallCountValue; }
	uint64_t WalkingHitWallDispatchMinHitWallCandidateActivationCount() const { return WalkingHitWallDispatchMinHitWallCandidateActivationCountValue; }
	uint64_t WalkingHitWallDispatchDisagreementCount() const { return WalkingHitWallDispatchDisagreementCountValue; }
	uint64_t WalkingHitWallDispatchCallbackCount() const { return WalkingHitWallDispatchCallbackCountValue; }
	uint64_t WalkingHitWallDispatchDiagnosticOverflowCount() const { return WalkingHitWallDispatchDiagnosticOverflowCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoEligibleCount() const { return WalkingStepPreflightPositiveDpsVetoEligibleCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoAppliedCount() const { return WalkingStepPreflightPositiveDpsVetoAppliedCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoDebouncedCount() const { return WalkingStepPreflightPositiveDpsVetoDebouncedCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoForcedReplanCount() const { return WalkingStepPreflightPositiveDpsVetoForcedReplanCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoRollbackRejectedCount() const { return WalkingStepPreflightPositiveDpsVetoRollbackRejectedCountValue; }
	uint64_t WalkingStepPreflightPositiveDpsVetoActionOverflowCount() const { return WalkingStepPreflightPositiveDpsVetoActionOverflowCountValue; }
	uint64_t InventoryDirectReachSupportObservationCount() const { return InventoryDirectReachSupportObservationCountValue; }
	uint64_t InventoryDirectReachSupportSafeSupportedCount() const { return InventoryDirectReachSupportSafeSupportedCountValue; }
	uint64_t InventoryDirectReachSupportSafeUnsupportedNoObservedHazardCount() const { return InventoryDirectReachSupportSafeUnsupportedNoObservedHazardCountValue; }
	uint64_t InventoryDirectReachSupportUnsafeHarmfulFootZoneCount() const { return InventoryDirectReachSupportUnsafeHarmfulFootZoneCountValue; }
	uint64_t InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulCount() const { return InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulCountValue; }
	uint64_t InventoryDirectReachSupportUnavailableCount() const { return InventoryDirectReachSupportUnavailableCountValue; }
	uint64_t InventoryDirectReachSupportDiagnosticOverflowCount() const { return InventoryDirectReachSupportDiagnosticOverflowCountValue; }
	uint64_t InventoryMarkerDirectReachRejectCount() const { return InventoryMarkerDirectReachRejectCountValue; }
	uint64_t FallingParityRealizedEpisodeCount() const { return FallingParityRealizedEpisodeCountValue; }
	uint64_t FallingParityRealizedStepCount() const { return FallingParityRealizedStepCountValue; }
	uint64_t FallingParityRealizedMatchedStepCount() const { return FallingParityRealizedMatchedStepCountValue; }
	uint64_t FallingParityRealizedMatchedLandingStepCount() const { return FallingParityRealizedMatchedLandingStepCountValue; }
	uint64_t FallingParityRealizedMismatchCount() const { return FallingParityRealizedMismatchCountValue; }
	uint64_t FallingParityRealizedUnknownCount() const { return FallingParityRealizedUnknownCountValue; }
	uint64_t FallingParityRealizedCallbackBarrierCount() const { return FallingParityRealizedCallbackBarrierCountValue; }
	uint64_t FallingParityRealizedPainEntryCount() const { return FallingParityRealizedPainEntryCountValue; }
	uint64_t FallingParityRealizedDeathCount() const { return FallingParityRealizedDeathCountValue; }
	uint64_t FallingParityRealizedLandingCount() const { return FallingParityRealizedLandingCountValue; }
	uint64_t FallingParityRealizedContinuityLossCount() const { return FallingParityRealizedContinuityLossCountValue; }
	uint64_t FallingParityRealizedRecordOverflowCount() const { return FallingParityRealizedRecordOverflowCountValue; }
	const std::array<uint64_t, PawnMovement::WalkingStepPreflightReasonCount>&
		WalkingStepPreflightReasonCounts() const { return WalkingStepPreflightReasonCountValues; }

	// Returns true if any of the several points of other is visible (origin, top, bottom)
	// ignoreDistance is a Deus Ex only parameter, it is always false on Unreal.
	bool LineOfSightTo(UActor* other, bool ignoreDistance);
	// Similar to LineOfSightTo() but takes the Pawn's peripheral vision into account (SightRadius and PeripheralVision)
	bool CanSee(UActor* other);
	bool CanHearNoise(UActor* source, float loudness);
	bool ActorReachable(UActor* anActor, bool checkNavpoint = false,
		PawnMovement::DirectReachCommandCallerOrigin callerOrigin =
			PawnMovement::DirectReachCommandCallerOrigin::Unknown);
	bool PointReachable(vec3 aPoint);

	void ClientHearSound(UActor* actor, int id, USound* sound, const vec3& soundLocation, const vec3& parameters);

	// If the obstruction is jumpable, start jumping and keep the destination
	// Otherwise try rotating destination 90 degrees to left and right
	bool PickWallAdjust();

	vec3 EAdjustJump();

	UActor* PickAnyTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart);
	UActor* PickTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart);
	std::vector<PawnMovement::PickTargetObservation> DrainPickTargetObservations();
	uint64_t PickTargetObservationOverflowCount() const
	{
		return PickTargetObservationOverflowCountValue;
	}
	uint64_t PickTargetObservationCount() const { return PickTargetObservationSequence; }
	uint64_t PickTargetObservationIntegrityFailureCount() const
	{
		return PickTargetObservationIntegrityFailureCountValue;
	}
	std::vector<PawnMovement::PawnCanSeeObservation> DrainPawnCanSeeObservations();
	uint64_t PawnCanSeeObservationOverflowCount() const
	{
		return PawnCanSeeObservationOverflowCountValue;
	}
	uint64_t PawnCanSeeObservationCount() const { return PawnCanSeeObservationSequence; }
	uint64_t PawnCanSeeObservationIntegrityFailureCount() const
	{
		return PawnCanSeeObservationIntegrityFailureCountValue;
	}
	void RecordUnrealScriptVectorNonFiniteObservation(
		PawnMovement::UnrealScriptVectorNonFiniteObservation observation);
	std::vector<PawnMovement::UnrealScriptVectorNonFiniteObservation>
		DrainUnrealScriptVectorNonFiniteObservations();
	uint64_t UnrealScriptVectorNonFiniteObservationCount() const
	{
		return UnrealScriptVectorNonFiniteObservationSequence;
	}
	uint64_t UnrealScriptVectorNonFiniteObservationOverflowCount() const
	{
		return UnrealScriptVectorNonFiniteObservationOverflowCountValue;
	}
	uint64_t UnrealScriptVectorNonFiniteObservationIntegrityFailureCount() const
	{
		return UnrealScriptVectorNonFiniteObservationIntegrityFailureCountValue;
	}
	void RecordFiniteMoveCommandGuardRejection(const vec3& requestedDestination,
		PawnMovement::FiniteMoveCommandGuardSource source,
		PawnMovement::FiniteMoveCommandGuardTerminal terminal);
	std::vector<PawnMovement::FiniteMoveCommandGuardDiagnosticRecord>
		DrainFiniteMoveCommandGuardDiagnostics();
	uint64_t FiniteMoveCommandGuardRejectionCount() const
	{
		return FiniteMoveCommandGuardRejectionCountValue;
	}
	uint64_t FiniteMoveCommandGuardDiagnosticOverflowCount() const
	{
		return FiniteMoveCommandGuardDiagnosticOverflowCountValue;
	}
	void RecordPickRegDestinationZeroDivideGuardActivation(uint64_t observerTick,
		uint64_t callerInvocationToken);
	std::vector<PawnMovement::PickRegDestinationZeroDivideGuardRecord>
		DrainPickRegDestinationZeroDivideGuardActivations();
	uint64_t PickRegDestinationZeroDivideGuardActivationCount() const
	{
		return PickRegDestinationZeroDivideGuardActivationCountValue;
	}
	uint64_t PickRegDestinationZeroDivideGuardActivationOverflowCount() const
	{
		return PickRegDestinationZeroDivideGuardActivationOverflowCountValue;
	}
	bool CheckIfBestTarget(UActor* actor, float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart);

	UActor* PathSpecialHandling(const PawnPathEndPointResult& result,
		PawnMovement::RoutePathCommitOrigin origin);
	UNavigationPoint* SetRouteCache(const Array<UNavigationPoint*>& points);
	PawnPathEndPointResult FindPathToEndPoint(UNavigationPoint* start, int maxNodes);
	PawnMovement::ReachSpecCapabilityProfile ReachSpecCapabilities();
	bool ReachSpecTraversable(const LevelReachSpec& spec);
	UNavigationPoint* CommitRoutePathCache(const PawnPathEndPointResult& result,
		PawnMovement::RoutePathCommitOrigin origin);
	std::vector<PawnMovement::RoutePathCommitRecord> DrainRoutePathCommitRecords();
	uint64_t RoutePathCommitOverflowCount() const { return RoutePathCommitOverflowCountValue; }

	void ClearPaths();
	UObject* FindRandomDest();
	UObject* FindPathTo(const vec3& aPoint, bool bSinglePath);
	UObject* FindPathToward(UObject* anActor, bool singlePath);
	UObject* FindBestInventoryPath(bool predictRespawns, float& outBestWeight);
	UNavigationPoint* FindClosestNavPoint(vec3 location);
	bool MarkReachableNavEndPoints();

	// Deus Ex AI functions
	float AICanHear(UActor* other, std::optional<float> volume, std::optional<float> radius);
	float AICanSee(UActor* other, std::optional<float> visibility, std::optional<bool> bCheckVisibility, std::optional<bool> bCheckDir, std::optional<bool> bCheckCylinder, std::optional<bool> bCheckLOS, DXAISightLineOfSightResult* lineOfSightResult = nullptr);
	float AICanSmell(UActor* other, std::optional<float> smell);

	float& AccelRate() { return Value<float>(PropOffsets_Pawn.AccelRate); }
	float& AirControl() { return Value<float>(PropOffsets_Pawn.AirControl); }
	float& AirSpeed() { return Value<float>(PropOffsets_Pawn.AirSpeed); }
	NameString& AlarmTag() { return Value<NameString>(PropOffsets_Pawn.AlarmTag); }
	float& Alertness() { return Value<float>(PropOffsets_Pawn.Alertness); }
	uint8_t& AttitudeToPlayer() { return Value<uint8_t>(PropOffsets_Pawn.AttitudeToPlayer); }
	float& AvgPhysicsTime() { return Value<float>(PropOffsets_Pawn.AvgPhysicsTime); }
	float& BaseEyeHeight() { return Value<float>(PropOffsets_Pawn.BaseEyeHeight); }
	float& CombatStyle() { return Value<float>(PropOffsets_Pawn.CombatStyle); }
	float& DamageScaling() { return Value<float>(PropOffsets_Pawn.DamageScaling); }
	float& DesiredSpeed() { return Value<float>(PropOffsets_Pawn.DesiredSpeed); }
	vec3& Destination() { return Value<vec3>(PropOffsets_Pawn.Destination); }
	USound*& Die() { return Value<USound*>(PropOffsets_Pawn.Die); }
	int& DieCount() { return Value<int>(PropOffsets_Pawn.DieCount); }
	UClass*& DropWhenKilled() { return Value<UClass*>(PropOffsets_Pawn.DropWhenKilled); }
	UPawn*& Enemy() { return Value<UPawn*>(PropOffsets_Pawn.Enemy); }
	float& EyeHeight() { return Value<float>(PropOffsets_Pawn.EyeHeight); }
	UActor*& FaceTarget() { return Value<UActor*>(PropOffsets_Pawn.FaceTarget); }
	vec3& Floor() { return Value<vec3>(PropOffsets_Pawn.Floor); }
	vec3& Focus() { return Value<vec3>(PropOffsets_Pawn.Focus); }
	PointRegion& FootRegion() { return Value<PointRegion>(PropOffsets_Pawn.FootRegion); }
	float& FovAngle() { return Value<float>(PropOffsets_Pawn.FovAngle); }
	float& GroundSpeed() { return Value<float>(PropOffsets_Pawn.GroundSpeed); }
	PointRegion& HeadRegion() { return Value<PointRegion>(PropOffsets_Pawn.HeadRegion); }
	int& Health() { return Value<int>(PropOffsets_Pawn.Health); }
	float& HearingThreshold() { return Value<float>(PropOffsets_Pawn.HearingThreshold); }
	USound*& HitSound1() { return Value<USound*>(PropOffsets_Pawn.HitSound1); }
	USound*& HitSound2() { return Value<USound*>(PropOffsets_Pawn.HitSound2); }
	uint8_t& Intelligence() { return Value<uint8_t>(PropOffsets_Pawn.Intelligence); }
	int& ItemCount() { return Value<int>(PropOffsets_Pawn.ItemCount); }
	float& JumpZ() { return Value<float>(PropOffsets_Pawn.JumpZ); }
	int& KillCount() { return Value<int>(PropOffsets_Pawn.KillCount); }
	USound*& Land() { return Value<USound*>(PropOffsets_Pawn.Land); }
	float& LastPainSound() { return Value<float>(PropOffsets_Pawn.LastPainSound); }
	vec3& LastSeeingPos() { return Value<vec3>(PropOffsets_Pawn.LastSeeingPos); }
	vec3& LastSeenPos() { return Value<vec3>(PropOffsets_Pawn.LastSeenPos); }
	float& LastSeenTime() { return Value<float>(PropOffsets_Pawn.LastSeenTime); }
	float& MaxDesiredSpeed() { return Value<float>(PropOffsets_Pawn.MaxDesiredSpeed); }
	float& MaxStepHeight() { return Value<float>(PropOffsets_Pawn.MaxStepHeight); }
	float& MeleeRange() { return Value<float>(PropOffsets_Pawn.MeleeRange); }
	std::string& MenuName() { return Value<std::string>(PropOffsets_Pawn.MenuName); }
	float& MinHitWall() { return Value<float>(PropOffsets_Pawn.MinHitWall); }
	UActor*& MoveTarget() { return Value<UActor*>(PropOffsets_Pawn.MoveTarget); }
	float& MoveTimer() { return Value<float>(PropOffsets_Pawn.MoveTimer); }
	std::string& NameArticle() { return Value<std::string>(PropOffsets_Pawn.NameArticle); }
	NameString& NextLabel() { return Value<NameString>(PropOffsets_Pawn.NextLabel); }
	NameString& NextState() { return Value<NameString>(PropOffsets_Pawn.NextState); }
	float& OldMessageTime() { return Value<float>(PropOffsets_Pawn.OldMessageTime); }
	float& OrthoZoom() { return Value<float>(PropOffsets_Pawn.OrthoZoom); }
	float& PainTime() { return Value<float>(PropOffsets_Pawn.PainTime); }
	UWeapon*& PendingWeapon() { return Value<UWeapon*>(PropOffsets_Pawn.PendingWeapon); }
	float& PeripheralVision() { return Value<float>(PropOffsets_Pawn.PeripheralVision); }
	NameString& PlayerReStartState() { return Value<NameString>(PropOffsets_Pawn.PlayerReStartState); }
	UPlayerReplicationInfo*& PlayerReplicationInfo() { return Value<UPlayerReplicationInfo*>(PropOffsets_Pawn.PlayerReplicationInfo); }
	UClass*& PlayerReplicationInfoClass() { return Value<UClass*>(PropOffsets_Pawn.PlayerReplicationInfoClass); }
	float& ReducedDamagePct() { return Value<float>(PropOffsets_Pawn.ReducedDamagePct); }
	NameString& ReducedDamageType() { return Value<NameString>(PropOffsets_Pawn.ReducedDamageType); }
	FixedArrayView<UNavigationPoint*, 16> RouteCache() { return FixedArray<UNavigationPoint*, 16>(PropOffsets_Pawn.RouteCache); }
	int& SecretCount() { return Value<int>(PropOffsets_Pawn.SecretCount); }
	UInventory*& SelectedItem() { return Value<UInventory*>(PropOffsets_Pawn.SelectedItem); }
	std::string& SelectionMesh() { return Value<std::string>(PropOffsets_Pawn.SelectionMesh); }
	UDecal*& Shadow() { return Value<UDecal*>(PropOffsets_Pawn.Shadow); }
	NameString& SharedAlarmTag() { return Value<NameString>(PropOffsets_Pawn.SharedAlarmTag); }
	float& SightCounter() { return Value<float>(PropOffsets_Pawn.SightCounter); }
	float& SightRadius() { return Value<float>(PropOffsets_Pawn.SightRadius); }
	float& Skill() { return Value<float>(PropOffsets_Pawn.Skill); }
	float& SoundDampening() { return Value<float>(PropOffsets_Pawn.SoundDampening); }
	UActor*& SpecialGoal() { return Value<UActor*>(PropOffsets_Pawn.SpecialGoal); }
	std::string& SpecialMesh() { return Value<std::string>(PropOffsets_Pawn.SpecialMesh); }
	float& SpecialPause() { return Value<float>(PropOffsets_Pawn.SpecialPause); }
	float& SpeechTime() { return Value<float>(PropOffsets_Pawn.SpeechTime); }
	float& SplashTime() { return Value<float>(PropOffsets_Pawn.SplashTime); }
	int& Spree() { return Value<int>(PropOffsets_Pawn.Spree); }
	float& Stimulus() { return Value<float>(PropOffsets_Pawn.Stimulus); }
	float& UnderWaterTime() { return Value<float>(PropOffsets_Pawn.UnderWaterTime); }
	Rotator& ViewRotation() { return Value<Rotator>(PropOffsets_Pawn.ViewRotation); }
	uint8_t& Visibility() { return Value<uint8_t>(PropOffsets_Pawn.Visibility); }
	uint8_t& VoicePitch() { return Value<uint8_t>(PropOffsets_Pawn.VoicePitch); }
	std::string& VoiceType() { return Value<std::string>(PropOffsets_Pawn.VoiceType); }
	vec3& WalkBob() { return Value<vec3>(PropOffsets_Pawn.WalkBob); }
	float& WaterSpeed() { return Value<float>(PropOffsets_Pawn.WaterSpeed); }
	USound*& WaterStep() { return Value<USound*>(PropOffsets_Pawn.WaterStep); }
	UWeapon*& Weapon() { return Value<UWeapon*>(PropOffsets_Pawn.Weapon); }
	BitfieldBool bAdvancedTactics() { return BoolValue(PropOffsets_Pawn.bAdvancedTactics); }
	uint8_t& bAltFire() { return Value<uint8_t>(PropOffsets_Pawn.bAltFire); }
	BitfieldBool bAutoActivate() { return BoolValue(PropOffsets_Pawn.bAutoActivate); }
	BitfieldBool bAvoidLedges() { return BoolValue(PropOffsets_Pawn.bAvoidLedges); }
	BitfieldBool bBehindView() { return BoolValue(PropOffsets_Pawn.bBehindView); }
	BitfieldBool bCanDoSpecial() { return BoolValue(PropOffsets_Pawn.bCanDoSpecial); }
	BitfieldBool bCanFly() { return BoolValue(PropOffsets_Pawn.bCanFly); }
	BitfieldBool bCanJump() { return BoolValue(PropOffsets_Pawn.bCanJump); }
	BitfieldBool bCanOpenDoors() { return BoolValue(PropOffsets_Pawn.bCanOpenDoors); }
	BitfieldBool bCanStrafe() { return BoolValue(PropOffsets_Pawn.bCanStrafe); }
	BitfieldBool bCanSwim() { return BoolValue(PropOffsets_Pawn.bCanSwim); }
	BitfieldBool bCanWalk() { return BoolValue(PropOffsets_Pawn.bCanWalk); }
	BitfieldBool bCountJumps() { return BoolValue(PropOffsets_Pawn.bCountJumps); }
	BitfieldBool bDrowning() { return BoolValue(PropOffsets_Pawn.bDrowning); }
	uint8_t& bDuck() { return Value<uint8_t>(PropOffsets_Pawn.bDuck); }
	uint8_t& bExtra0() { return Value<uint8_t>(PropOffsets_Pawn.bExtra0); }
	uint8_t& bExtra1() { return Value<uint8_t>(PropOffsets_Pawn.bExtra1); }
	uint8_t& bExtra2() { return Value<uint8_t>(PropOffsets_Pawn.bExtra2); }
	uint8_t& bExtra3() { return Value<uint8_t>(PropOffsets_Pawn.bExtra3); }
	uint8_t& bFire() { return Value<uint8_t>(PropOffsets_Pawn.bFire); }
	BitfieldBool bFixedStart() { return BoolValue(PropOffsets_Pawn.bFixedStart); }
	uint8_t& bFreeLook() { return Value<uint8_t>(PropOffsets_Pawn.bFreeLook); }
	BitfieldBool bFromWall() { return BoolValue(PropOffsets_Pawn.bFromWall); }
	BitfieldBool bHitSlopedWall() { return BoolValue(PropOffsets_Pawn.bHitSlopedWall); }
	BitfieldBool bHunting() { return BoolValue(PropOffsets_Pawn.bHunting); }
	BitfieldBool bIsFemale() { return BoolValue(PropOffsets_Pawn.bIsFemale); }
	BitfieldBool bIsHuman() { return BoolValue(PropOffsets_Pawn.bIsHuman); }
	BitfieldBool bIsMultiSkinned() { return BoolValue(PropOffsets_Pawn.bIsMultiSkinned); }
	BitfieldBool bIsPlayer() { return BoolValue(PropOffsets_Pawn.bIsPlayer); }
	BitfieldBool bIsWalking() { return BoolValue(PropOffsets_Pawn.bIsWalking); }
	BitfieldBool bJumpOffPawn() { return BoolValue(PropOffsets_Pawn.bJumpOffPawn); }
	BitfieldBool bJustLanded() { return BoolValue(PropOffsets_Pawn.bJustLanded); }
	BitfieldBool bLOSflag() { return BoolValue(PropOffsets_Pawn.bLOSflag); }
	uint8_t& bLook() { return Value<uint8_t>(PropOffsets_Pawn.bLook); }
	BitfieldBool bNeverSwitchOnPickup() { return BoolValue(PropOffsets_Pawn.bNeverSwitchOnPickup); }
	BitfieldBool bReducedSpeed() { return BoolValue(PropOffsets_Pawn.bReducedSpeed); }
	uint8_t& bRun() { return Value<uint8_t>(PropOffsets_Pawn.bRun); }
	BitfieldBool bShootSpecial() { return BoolValue(PropOffsets_Pawn.bShootSpecial); }
	uint8_t& bSnapLevel() { return Value<uint8_t>(PropOffsets_Pawn.bSnapLevel); }
	BitfieldBool bStopAtLedges() { return BoolValue(PropOffsets_Pawn.bStopAtLedges); }
	uint8_t& bStrafe() { return Value<uint8_t>(PropOffsets_Pawn.bStrafe); }
	BitfieldBool bUpAndOut() { return BoolValue(PropOffsets_Pawn.bUpAndOut); }
	BitfieldBool bUpdatingDisplay() { return BoolValue(PropOffsets_Pawn.bUpdatingDisplay); }
	BitfieldBool bViewTarget() { return BoolValue(PropOffsets_Pawn.bViewTarget); }
	BitfieldBool bWarping() { return BoolValue(PropOffsets_Pawn.bWarping); }
	uint8_t& bZoom() { return Value<uint8_t>(PropOffsets_Pawn.bZoom); }
	UDecoration*& carriedDecoration() { return Value<UDecoration*>(PropOffsets_Pawn.carriedDecoration); }
	UNavigationPoint*& home() { return Value<UNavigationPoint*>(PropOffsets_Pawn.home); }
	UPawn*& nextPawn() { return Value<UPawn*>(PropOffsets_Pawn.nextPawn); }
	float& noise1loudness() { return Value<float>(PropOffsets_Pawn.noise1loudness); }
	UPawn*& noise1other() { return Value<UPawn*>(PropOffsets_Pawn.noise1other); }
	vec3& noise1spot() { return Value<vec3>(PropOffsets_Pawn.noise1spot); }
	float& noise1time() { return Value<float>(PropOffsets_Pawn.noise1time); }
	float& noise2loudness() { return Value<float>(PropOffsets_Pawn.noise2loudness); }
	UPawn*& noise2other() { return Value<UPawn*>(PropOffsets_Pawn.noise2other); }
	vec3& noise2spot() { return Value<vec3>(PropOffsets_Pawn.noise2spot); }
	float& noise2time() { return Value<float>(PropOffsets_Pawn.noise2time); }
	// Deus Ex exclusive properties
	BitfieldBool bCanGlide() { return BoolValue(PropOffsets_Pawn.bCanGlide); }
	int& HealthHead() { return Value<int>(PropOffsets_Pawn.HealthHead); }
	int& HealthTorso() { return Value<int>(PropOffsets_Pawn.HealthTorso); }
	int& HealthLegLeft() { return Value<int>(PropOffsets_Pawn.HealthLegLeft); }
	int& HealthLegRight() { return Value<int>(PropOffsets_Pawn.HealthLegRight); }
	int& HealthArmLeft() { return Value<int>(PropOffsets_Pawn.HealthArmLeft); }
	int& HealthArmRight() { return Value<int>(PropOffsets_Pawn.HealthArmRight); }
	BitfieldBool bIsSpeaking() { return BoolValue(PropOffsets_Pawn.bIsSpeaking); }
	BitfieldBool bWasSpeaking() { return BoolValue(PropOffsets_Pawn.bWasSpeaking); }
	std::string& lastPhoneme() { return Value<std::string>(PropOffsets_Pawn.lastPhoneme); }
	std::string& nextPhoneme() { return Value<std::string>(PropOffsets_Pawn.nextPhoneme); }
	FixedArrayView<float, 4> animTimer() { return FixedArray<float, 4>(PropOffsets_Pawn.animTimer); }
	BitfieldBool bOnFire() { return BoolValue(PropOffsets_Pawn.bOnFire); }
	float& burnTimer() { return Value<float>(PropOffsets_Pawn.burnTimer); }
	float& AIHorizontalFov() { return Value<float>(PropOffsets_Pawn.AIHorizontalFov); }
	float& AspectRatio() { return Value<float>(PropOffsets_Pawn.AspectRatio); }
	float& AngularResolution() { return Value<float>(PropOffsets_Pawn.AngularResolution); }
	float& MinAngularSize() { return Value<float>(PropOffsets_Pawn.MinAngularSize); }
	float& VisibilityThreshold() { return Value<float>(PropOffsets_Pawn.VisibilityThreshold); }
	float& SmellThreshold() { return Value<float>(PropOffsets_Pawn.SmellThreshold); }
	NameString Alliance() { return Value<NameString>(PropOffsets_Pawn.Alliance);}
	Rotator& AIAddViewRotation() { return Value<Rotator>(PropOffsets_Pawn.AIAddViewRotation); }

private:
	void ObserveMoveStallWatchdog(float elapsed);
	void RecordMoveStallCommand();
	void AdvanceMoveStallRecoveryEpisode(float elapsed,
		PawnMovement::MoveStallRecoveryEpisodeEvent event);
	void RecordMoveStallRecoveryEpisodeOutcome(float secondsSinceDetection,
		PawnMovement::MoveStallRecoveryEpisodeOutcome outcome);
	void RecordMoveStallRecoveryDecision(PawnMovement::MoveStallLatentMode latentMode,
		PawnMovement::MoveStallRecoveryDecision decision, UActor* moveTarget);
	void AdvancePainLedgeRecovery(float elapsed);
	bool ApplyPainLedgeRecovery(const vec2& requestedDirection);
	void AdvanceWallAdjustRecovery(float elapsed);
	bool ApplyWallAdjustRecovery(const vec2& requestedDirection);
	bool ApplyHarmfulZoneEscape();
	void EndHarmfulZoneEscapeLife();
	void ResetFallingHazardRecovery();
	void ResetHazardSwimEgressObservation(
		BotAI::HazardSwimEgressPlannerHandoffOutcome outcome =
			BotAI::HazardSwimEgressPlannerHandoffOutcome::EpisodeAbandoned);
	void ResolveHazardSwimEgressPlannerHandoff(
		BotAI::HazardSwimEgressPlannerHandoffOutcome outcome);
	void ObserveHazardSwimEgressPlannerHandoffMovementCommand();
	void ObserveHazardSwimEgressDirectNavigationCandidates();
	void ObserveHazardSwimEgressStaticWalkCertificate();
	void AdvanceHazardResidence(float elapsed);
	void AdvanceHazardResidenceSample(bool positiveDpsHazard, float elapsed);
	void ObserveHazardResidenceCandidate(const std::string& candidateName);
	void ObserveHazardResidenceMovementCommand();
	void RecordMovementCommandProvenance(const char* kind);
	void BeginHazardResidenceCommandTransitionLedger();
	void ObserveHazardResidenceCommandTransition(
		const PawnMovement::MovementCommandProvenanceObservation& observation);
	void FinishHazardResidenceCommandTransitionLedger(
		PawnMovement::HazardResidenceTerminal terminal);
	void CaptureHazardResidencePreentryCausalSlice();
	void FinishHazardResidencePreentryCausalSlice(PawnMovement::HazardResidenceTerminal terminal);
	void ResolveHazardResidence(PawnMovement::HazardResidenceTerminal terminal);
	void ObserveExternalImpulseFallWitness(
		PawnMovement::FallingHazardForecastSource source,
		const PawnMovement::FallingHazardForecastInput& input,
		const PawnMovement::FallingHazardForecastUpdate& forecast);
	void CaptureExternalImpulseNavigationCommit();

	bool IsInPathSpecialHandling = false;
	PawnMovement::FailedNavigationMemoryState FailedNavigationMemory;
	PawnMovement::MoveStallWatchdogState MoveStallWatchdog;
	PawnMovement::MoveStallRecoveryEpisodeState MoveStallRecoveryEpisode;
	PawnMovement::MoveStallCommandKey MoveStallRecoveryEpisodeCommand;
	uint64_t MoveStallRecoveryLifeId = 1;
	PawnMovement::PainLedgeRecoveryState PainLedgeRecovery;
	PawnMovement::WallAdjustRecoveryState WallAdjustRecovery;
	struct HarmfulZoneEscapeState
	{
		bool CenterHarmful = false;
		bool FootHarmful = false;
		bool Active = false;
		bool RecoveryAttempted = false;
		vec2 IncomingDirection = vec2(0.0f);
	};
	HarmfulZoneEscapeState HarmfulZoneEscape;
	BotAI::HarmfulZoneEscapeGate HarmfulZoneEscapeGate;
	uint64_t HarmfulZoneEscapeLifeId = 1;
	uint64_t HarmfulZoneEscapeEpisodeId = 0;
	struct FallingHazardRecoveryState
	{
		bool AnchorKnown = false;
		vec3 Anchor = vec3(0.0f);
		uint64_t LifeId = 0;
		uint64_t FallEpisodeId = 0;
		bool PromotionObserved = false;
		bool AnchorRejectedObserved = false;
		bool ActionActive = false;
		float ActiveSeconds = 0.0f;
	};
	FallingHazardRecoveryState FallingHazardRecovery;
	BotAI::FallingHazardRecoveryGate FallingHazardRecoveryGate;
	struct HazardSwimEgressState
	{
		enum class AnchorSource : uint8_t
		{
			None,
			SafeSwimming,
			FallingPreMove
		};

		bool AnchorKnown = false;
		vec3 Anchor = vec3(0.0f);
		AnchorSource Source = AnchorSource::None;
		bool SwimmingSessionObserved = false;
		bool HarmfulWaterEpisodeActive = false;
		bool LiveActionAuthorized = false;
		bool LiveProbeRejected = false;
		bool LiveReplanIssued = false;
		bool ActionActive = false;
		bool PlannerHandoffWitnessPending = false;
		uint64_t PlannerHandoffMovementCommandToken = 0;
		UActor* PlannerHandoffMoveTarget = nullptr;
		vec3 PlannerHandoffDestination = vec3(0.0f);
		PawnMovement::HazardWaterEgressTransitionSource TransitionSource =
			PawnMovement::HazardWaterEgressTransitionSource::Unknown;
		std::string DirectNavBestCandidateName;
		bool DirectNavBestCandidateLocationKnown = false;
		vec3 DirectNavBestCandidateLocation = vec3(0.0f);
		float DirectNavBestCandidateDistance = std::numeric_limits<float>::infinity();
	};
	HazardSwimEgressState HazardSwimEgress;
	PawnMovement::HazardResidenceState HazardResidence;
	PawnMovement::MovementCommandProvenanceObservation HazardResidenceMovementCommand;
	struct HazardResidenceCommandTransitionLedgerState
	{
		bool Active = false;
		bool Overflowed = false;
		uint64_t EpisodeId = 0;
		uint64_t LifeId = 0;
		std::vector<PawnMovement::HazardResidenceCommandTransitionLedgerEntry> Entries;
	};
	HazardResidenceCommandTransitionLedgerState HazardResidenceCommandTransitionLedger;
	PawnMovement::HazardResidencePreentryCausalSliceRecord HazardResidencePreentryCausalSlice;
	bool HazardResidencePreentryCausalSliceActive = false;
	int32_t HazardResidencePreentryPriorPhysics = -1;
	PawnMovement::HazardResidencePreentryCausalSliceBoundary
		HazardResidencePreentryPendingMayFallBoundary,
		HazardResidencePreentryPendingHitWallBoundary;
	std::vector<PawnMovement::HazardResidencePreentryCausalSliceRecord> HazardResidencePreentryCausalSliceRecords;
	uint64_t HazardResidencePreentryCausalSliceSequence = 0, HazardResidencePreentryCausalSliceOverflowCountValue = 0;
	std::string HazardResidenceCandidateName;
	std::optional<PawnMovement::HazardResidenceDeathWitness>
		HazardResidenceDeathWitness;
	std::vector<std::pair<uint64_t, PawnMovement::HazardResidenceTerminal>>
		DirectReachHazardResidenceTerminals;
	struct ExternalImpulseNavigationCommitState
	{
		bool Active = false;
		bool FallingPhaseActive = false;
		uint64_t LifeId = 0;
		bool MovementCommandActive = false;
		uint64_t MovementCommandToken = 0;
		std::string MovementCommandKind;
		std::string MovementCommandTargetName;
		vec3 MovementCommandDestination = vec3(0.0f);
		std::string MoveTargetName;
		bool MoveTargetNavigation = false;
		bool RouteHeadKnown = false;
		std::string RouteHeadName;
		vec3 Location = vec3(0.0f);
		vec3 Velocity = vec3(0.0f);
		bool LaunchForecastKnown = false;
		bool LaunchForecastHarmful = false;
	};
	ExternalImpulseNavigationCommitState ExternalImpulseNavigationCommit;
	BotAI::HazardSwimEgressGate HazardSwimEgressGate;
	std::unique_ptr<PawnMovement::HazardWaterEgressObserver>
		HazardWaterEgressObserver;
	uint64_t HazardSwimEgressLifeId = 1;
	uint64_t HazardSwimEgressEpisodeId = 0;
	PawnMovement::FallingSeamEpisodeState FallingSeamEpisode;
	PawnMovement::WalkingStepPreflightEpisodeState WalkingStepPreflightEpisode;
	bool WalkingStepExplicitJumpRequested = false;
	uint64_t PainLedgeVetoCountValue = 0;
	uint64_t PainLedgeRepeatVetoCountValue = 0;
	uint64_t PainLedgeRecoveryAttemptCountValue = 0;
	uint64_t PainLedgeRecoveryEscapeCountValue = 0;
	uint64_t WallAdjustCallCountValue = 0;
	uint64_t WallAdjustRepeatCountValue = 0;
	uint64_t WallAdjustRecoveryAttemptCountValue = 0;
	uint64_t WallAdjustRecoverySuccessCountValue = 0;
	uint64_t WallAdjustForcedReplanCountValue = 0;
	uint64_t MoveStallDetectionCountValue = 0;
	uint64_t MoveStallEpisodeResetCountValue = 0;
	uint64_t MoveStallForcedReplanCountValue = 0;
	uint64_t MoveStallNavigationForcedReplanCountValue = 0;
	uint64_t MoveStallTargetlessMoveToTimeoutCountValue = 0;
	uint64_t MoveStallDirectActorMoveTowardTimeoutCountValue = 0;
	double MoveStallEligibleSecondsValue = 0.0;
	uint64_t MoveStallRecoveryEpisodeStartCountValue = 0;
	uint64_t MoveStallRecoveryClearedWithin2SecondsCountValue = 0;
	uint64_t MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCountValue = 0;
	uint64_t MoveStallRecoveryReplannedWithin5SecondsCountValue = 0;
	uint64_t MoveStallRecoveryMissed5SecondDeadlineCountValue = 0;
	uint64_t MoveStallRecoveryExcludedIntentionalStopCountValue = 0;
	uint64_t MoveStallRecoveryCensoredLifeBoundaryCountValue = 0;
	uint64_t MoveStallRecoveryCensoredRunEndCountValue = 0;
	uint64_t MoveStallRecoveryUnknownCountValue = 0;
	uint64_t MoveStallRecoveryEpisodeRecordOverflowCountValue = 0;
	uint64_t MoveStallRecoveryEpisodeRecordSequence = 0;
	uint64_t MoveStallRecoveryDecisionRecordOverflowCountValue = 0;
	uint64_t MoveStallRecoveryDecisionRecordSequence = 0;
	uint64_t MoveStallRecoveryEpisodeId = 0;
	std::vector<PawnMoveStallRecoveryEpisodeRecord> MoveStallRecoveryEpisodeRecords;
	std::vector<PawnMoveStallRecoveryDecisionRecord> MoveStallRecoveryDecisionRecords;
	uint64_t FailedNavigationAvoidanceActivationCountValue = 0;
	uint64_t FailedNavigationSafeguardSuppressionCountValue = 0;
	uint64_t FailedNavigationRoutePenaltyApplicationCountValue = 0;
	uint64_t HarmfulZoneEscapeEpisodeCountValue = 0;
	uint64_t HarmfulZoneEscapeCenterEntryCountValue = 0;
	uint64_t HarmfulZoneEscapeFootEntryCountValue = 0;
	uint64_t HarmfulZoneEscapeRecoveryAttemptCountValue = 0;
	uint64_t HarmfulZoneEscapeSuccessfulEscapeCountValue = 0;
	uint64_t HarmfulZoneEscapeForcedReplanCountValue = 0;
	uint64_t HarmfulZoneEscapeNoSafeCandidateCountValue = 0;
	uint64_t HazardSwimEgressEpisodeCountValue = 0;
	uint64_t HazardSwimEgressEligibleCountValue = 0;
	uint64_t HazardSwimEgressAuthorizedCountValue = 0;
	uint64_t HazardSwimEgressDebouncedCountValue = 0;
	uint64_t HazardSwimEgressNoAnchorRejectedCountValue = 0;
	uint64_t HazardSwimEgressExitCountValue = 0;
	uint64_t HazardSwimEgressDeathsBeforeExitCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanSameCommandReissuedCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanDifferentCommandIssuedCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanHazardClearedBeforeCommandCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanFellBeforeCommandCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanDiedBeforeCommandCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanLifeBoundaryCensoredCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanRunEndCensoredCountValue = 0;
	uint64_t HazardSwimEgressForcedReplanEpisodeAbandonedCountValue = 0;
	uint64_t HazardSwimEgressFallingPreMoveAnchorCaptureCountValue = 0;
	uint64_t HazardSwimEgressFallingPreMoveAnchorUseCountValue = 0;
	uint64_t HazardSwimEgressLiveApplyCountValue = 0;
	uint64_t HazardSwimEgressLiveActiveTickCountValue = 0;
	uint64_t HazardSwimEgressLiveProbeRejectedCountValue = 0;
	uint64_t HazardSwimEgressLiveSuccessfulExitCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowCandidateCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowFallingTerminalCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowHazardClearedTerminalCountValue = 0;
	uint64_t HazardSwimEgressLiveShadowProbeBlockedTerminalCountValue = 0;
	uint64_t HazardSwimEgressDirectNavProbeCountValue = 0;
	uint64_t HazardSwimEgressDirectNavSafeCandidateCountValue = 0;
	uint64_t HazardResidenceEpisodeCountValue = 0;
	uint64_t HazardResidenceClearedCountValue = 0;
	uint64_t HazardResidenceDeathCountValue = 0;
	uint64_t HazardResidenceLifeBoundaryCensoredCountValue = 0;
	uint64_t HazardResidenceRunEndCensoredCountValue = 0;
	uint64_t HazardResidenceUnknownCountValue = 0;
	uint64_t HazardResidenceReentryCountValue = 0;
	uint64_t HazardResidenceCommandChangeCountValue = 0;
	uint64_t HazardResidenceCandidateObservedCountValue = 0;
	uint64_t HazardResidenceCandidateOtherCommandCountValue = 0;
	uint64_t FallingHazardRecoveryPromotionCountValue = 0;
	uint64_t FallingHazardRecoveryAdvanceCountValue = 0;
	uint64_t FallingHazardRecoveryContextRejectedCountValue = 0;
	uint64_t FallingHazardRecoveryNoActiveFallEpisodeCountValue = 0;
	uint64_t FallingHazardRecoveryNoPrefixCountValue = 0;
	uint64_t FallingHazardRecoveryEligibleCountValue = 0;
	uint64_t FallingHazardRecoveryAnchorRejectedCountValue = 0;
	uint64_t FallingHazardRecoveryProbeRejectedCountValue = 0;
	uint64_t FallingHazardRecoveryLiveApplyCountValue = 0;
	uint64_t FallingHazardRecoveryLiveActiveTickCountValue = 0;
	uint64_t FallingHazardRecoverySafeLandingCountValue = 0;
	uint64_t FallingHazardRecoveryHarmfulEntryCountValue = 0;
	uint64_t FallingHazardRecoveryDeathCountValue = 0;
	uint64_t FallingHazardRecoveryTimeoutCountValue = 0;
	uint64_t ExternalImpulseFallHarmfulWitnessCountValue = 0;
	uint64_t ExternalImpulseFallNoAirControlCountValue = 0;
	uint64_t ExternalImpulseFallAlternativesTestedCountValue = 0;
	uint64_t ExternalImpulseFallCertifiedCountValue = 0;
	uint64_t ExternalImpulseFallUncertifiedCountValue = 0;
	uint64_t FallingSeamDetectionCountValue = 0;
	uint64_t HorizontalCornerCandidateProbeCountValue = 0;
	uint64_t HorizontalCornerAuthorizedEscapeCountValue = 0;
	uint64_t HorizontalCornerTargetProgressRejectCountValue = 0;
	uint64_t HorizontalCornerUnknownOrUnsafeSupportCountValue = 0;
	uint64_t FallingSeamEpisodeCountValue = 0;
	uint64_t FallingSeamInvalidGeometryRejectCountValue = 0;
	uint64_t FallingSeamAuthorizableEpisodeCountValue = 0;
	uint64_t HorizontalCornerAuthorizedCandidateCountValue = 0;
	uint64_t HorizontalCornerBlockedSweepCandidateCountValue = 0;
	uint64_t HorizontalCornerNoStaticWalkableSupportCandidateCountValue = 0;
	uint64_t HorizontalCornerPainSupportCandidateCountValue = 0;
	uint64_t HorizontalCornerNoActiveMovementIntentOrTargetCandidateCountValue = 0;
	uint64_t HorizontalCornerTrueTargetRegressionCandidateCountValue = 0;
	uint64_t HorizontalCornerUnknownEvidenceCandidateCountValue = 0;
	uint64_t WalkingStepPreflightObservationCountValue = 0;
	uint64_t WalkingStepPreflightUnsupportedEndpointCountValue = 0;
	uint64_t WalkingStepPreflightNoDecisionCountValue = 0;
	uint64_t WalkingStepPreflightProvisionalAuthorizationCountValue = 0;
	uint64_t WalkingStepPreflightAuthorizationCountValue = 0;
	uint64_t WalkingStepPreflightAuthorizableEpisodeCountValue = 0;
	uint64_t WalkingStepPreflightDiagnosticOverflowCountValue = 0;
	uint64_t WalkingHitWallDispatchObservationCountValue = 0;
	uint64_t WalkingHitWallDispatchLegacyZBandCountValue = 0;
	uint64_t WalkingHitWallDispatchMinHitWallCountValue = 0;
	uint64_t WalkingHitWallDispatchMinHitWallCandidateActivationCountValue = 0;
	uint64_t WalkingHitWallDispatchDisagreementCountValue = 0;
	uint64_t WalkingHitWallDispatchCallbackCountValue = 0;
	uint64_t WalkingHitWallDispatchDiagnosticOverflowCountValue = 0;
	uint64_t WalkingHitWallDispatchDiagnosticSequence = 0;
	uint32_t WalkingHitWallFixtureContactLimit = 0;
	uint32_t WalkingHitWallFixtureContactCount = 0;
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord>
		WalkingHitWallDispatchDiagnostics;
	uint64_t WalkingStepPreflightPositiveDpsVetoEligibleCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoAppliedCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoDebouncedCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoForcedReplanCountValue = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoRollbackRejectedCountValue = 0;
	uint64_t WalkingStepPreflightDiagnosticSequence = 0;
	std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
		WalkingStepPreflightDiagnostics;
	uint64_t WalkingStepPreflightPositiveDpsVetoActionOverflowCountValue = 0;
	uint64_t InventoryDirectReachSupportObservationCountValue = 0;
	uint64_t InventoryDirectReachSupportSafeSupportedCountValue = 0;
	uint64_t InventoryDirectReachSupportSafeUnsupportedNoObservedHazardCountValue = 0;
	uint64_t InventoryDirectReachSupportUnsafeHarmfulFootZoneCountValue = 0;
	uint64_t InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulCountValue = 0;
	uint64_t InventoryDirectReachSupportUnavailableCountValue = 0;
	uint64_t InventoryDirectReachSupportDiagnosticOverflowCountValue = 0;
	uint64_t InventoryMarkerDirectReachRejectCountValue = 0;
	uint64_t InventoryDirectReachSupportDiagnosticSequence = 0;
	std::vector<PawnMovement::InventoryDirectReachSupportDiagnosticRecord>
		InventoryDirectReachSupportDiagnostics;
	uint64_t DirectReachCommandObservationCountValue = 0;
	uint64_t DirectReachCommandSuccessCountValue = 0;
	uint64_t DirectReachCommandFailureCountValue = 0;
	uint64_t DirectReachCommandOverflowCountValue = 0;
	uint64_t DirectReachCommandSequence = 0;
	std::vector<PawnMovement::DirectReachCommandObservation>
		DirectReachCommandObservations;
	uint64_t MovementCommandProvenanceSequence = 0;
	uint64_t MovementCommandProvenanceToken = 0;
	uint64_t MovementCommandProvenanceOverflowCountValue = 0;
	std::vector<PawnMovement::MovementCommandProvenanceObservation>
		MovementCommandProvenanceObservations;
	PawnMovement::MovementCommandProvenanceObservation ActiveMovementCommandProvenance;
	uint64_t HazardResidenceCommandTransitionLedgerSequence = 0;
	uint64_t HazardResidenceCommandTransitionLedgerOverflowCountValue = 0;
	std::vector<PawnMovement::HazardResidenceCommandTransitionLedgerRecord>
		HazardResidenceCommandTransitionLedgerRecords;
	struct LastNativePathCommitProvenance
	{
		bool Known = false;
		uint64_t LifeId = 0;
		uint64_t Sequence = 0;
		int32_t FirstReachSpecIndex = -1;
		bool CacheClear = false;
		bool FirstRouteHeadKnown = false;
		int32_t FirstRouteHeadActorIndex = -1;
		std::string FirstRouteHeadName, FirstRouteHeadClass;
	};
	LastNativePathCommitProvenance LastMovementCommandPathCommit;
	uint64_t RoutePathCommitSequence = 0;
	uint64_t RoutePathCommitOverflowCountValue = 0;
	std::vector<PawnMovement::RoutePathCommitRecord> RoutePathCommitRecords;
	uint64_t PickTargetObservationSequence = 0;
	uint64_t PickTargetObservationOverflowCountValue = 0;
	uint64_t PickTargetObservationIntegrityFailureCountValue = 0;
	std::vector<PawnMovement::PickTargetObservation> PickTargetObservations;
	uint64_t PawnCanSeeObservationSequence = 0;
	uint64_t PawnCanSeeObservationOverflowCountValue = 0;
	uint64_t PawnCanSeeObservationIntegrityFailureCountValue = 0;
	std::vector<PawnMovement::PawnCanSeeObservation> PawnCanSeeObservations;
	uint64_t UnrealScriptVectorNonFiniteObservationSequence = 0;
	uint64_t UnrealScriptVectorNonFiniteObservationOverflowCountValue = 0;
	uint64_t UnrealScriptVectorNonFiniteObservationIntegrityFailureCountValue = 0;
	std::vector<PawnMovement::UnrealScriptVectorNonFiniteObservation>
		UnrealScriptVectorNonFiniteObservations;
	uint64_t FiniteMoveCommandGuardRejectionCountValue = 0;
	uint64_t FiniteMoveCommandGuardDiagnosticOverflowCountValue = 0;
	uint64_t FiniteMoveCommandGuardDiagnosticSequence = 0;
	std::vector<PawnMovement::FiniteMoveCommandGuardDiagnosticRecord>
		FiniteMoveCommandGuardDiagnostics;
	uint64_t PickRegDestinationZeroDivideGuardActivationCountValue = 0;
	uint64_t PickRegDestinationZeroDivideGuardActivationOverflowCountValue = 0;
	uint64_t PickRegDestinationZeroDivideGuardActivationSequence = 0;
	std::vector<PawnMovement::PickRegDestinationZeroDivideGuardRecord>
		PickRegDestinationZeroDivideGuardActivations;
	uint64_t WalkingStepPreflightPositiveDpsVetoActionSequence = 0;
	std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
		WalkingStepPreflightPositiveDpsVetoActions;
	bool WalkingStepPreflightPendingConfirmation = false;
	int WalkingStepPreflightPendingIteration = 0;
	uint64_t WalkingStepPreflightPendingInvocation = 0;
	uint64_t WalkingStepPreflightPendingLifeGeneration = 0;
	PawnMovement::WalkingStepPreflightDiagnosticRecord
		WalkingStepPreflightPendingDiagnostic;
	const void* WalkingStepPreflightPendingSemanticTarget = nullptr;
	PawnMovement::WalkingStepPreflightInput WalkingStepPreflightPendingInput;
	uint64_t WalkingStepPreflightInvocationSequence = 0;
	uint64_t WalkingStepPreflightLifeGeneration = 1;
	std::array<uint64_t, PawnMovement::WalkingStepPreflightReasonCount>
		WalkingStepPreflightReasonCountValues = {};
	bool WalkingStepPreflightObservedTransactionValid = false;
	int WalkingStepPreflightObservedIteration = 0;
	uint64_t WalkingStepPreflightObservedInvocation = 0;
	PawnMovement::FallingParityRealizedCorrelation
		WalkingStepPreflightObservedCorrelation;
	PawnMovement::FallingParityRealizedTraceState FallingParityRealizedTrace;
	std::vector<PawnMovement::FallingParityRealizedRecord>
		FallingParityRealizedRecords;
	uint64_t FallingParityRealizedEpisodeCountValue = 0;
	uint64_t FallingParityRealizedStepCountValue = 0;
	uint64_t FallingParityRealizedMatchedStepCountValue = 0;
	uint64_t FallingParityRealizedMatchedLandingStepCountValue = 0;
	uint64_t FallingParityRealizedMismatchCountValue = 0;
	uint64_t FallingParityRealizedUnknownCountValue = 0;
	uint64_t FallingParityRealizedCallbackBarrierCountValue = 0;
	uint64_t FallingParityRealizedPainEntryCountValue = 0;
	uint64_t FallingParityRealizedDeathCountValue = 0;
	uint64_t FallingParityRealizedLandingCountValue = 0;
	uint64_t FallingParityRealizedContinuityLossCountValue = 0;
	uint64_t FallingParityRealizedRecordOverflowCountValue = 0;
	struct FallingHazardPendingSweep
	{
		bool Active = false;
		bool GeometryLatched = false;
		bool ReentrantMoveObserved = false;
		bool HitWallCallbackExpected = false;
		uint32_t MoveDepth = 0;
		PawnMovement::FallingHazardSweepLeg Leg =
			PawnMovement::FallingHazardSweepLeg::Direct;
		vec3 Origin = vec3(0.0f);
		vec3 RequestedDelta = vec3(0.0f);
		float ElapsedContribution = 0.0f;
		PawnMovement::FallingHazardCollisionKind Collision =
			PawnMovement::FallingHazardCollisionKind::Unknown;
		float HitFraction = 1.0f;
		vec3 HitNormal = vec3(0.0f);
		uint32_t MoveCallbackMask = 0;
	};
	void CommitPendingFallingHazardSweep(const PointRegion& center,
		const PointRegion& foot, const PointRegion& head,
		uint32_t zoneCallbackMask, bool callbackAlreadyDispatched);
	void RecordFallingHazardMovementCommand();
	struct FallingHazardAlignedCommandWitness
	{
		bool Active = false;
		bool StaticWorldCollision = false;
		uint64_t CommandToken = 0;
		uint8_t LatentState = 0;
		UActor* MoveTarget = nullptr;
		vec3 Destination = vec3(0.0f);
		vec3 Acceleration = vec3(0.0f);
	};
	struct FallingHazardLastMovementCommand
	{
		bool Active = false;
		uint64_t Token = 0;
		uint8_t LatentState = 0;
		UActor* MoveTarget = nullptr;
		vec3 Destination = vec3(0.0f);
	};
	uint64_t FallingHazardMovementCommandToken = 0;
	FallingHazardLastMovementCommand LastFallingHazardMovementCommand;
	FallingHazardAlignedCommandWitness PendingFallingHazardAlignedCommandWitness;
	std::unique_ptr<PawnMovement::FallingHazardRuntimeObserver>
		FallingHazardObserver;
	FallingHazardPendingSweep FallingHazardPending;
	std::vector<PawnMovement::FallingHitWallCallbackWitness>
		FallingHitWallCallbackWitnesses;
	uint32_t FallingHazardTryMoveDepth = 0;
	PawnMovement::FallingHazardForecastSource FallingHazardQueuedSource =
		PawnMovement::FallingHazardForecastSource::Unknown;
	std::optional<PawnMovement::FallingHazardForecastContinuationSeed>
		FallingHazardCallbackContinuation;
};
