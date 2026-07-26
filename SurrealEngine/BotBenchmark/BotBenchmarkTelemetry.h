#pragma once

#include "BotBenchmark/BotBenchmarkBuildIdentity.h"
#include "UObject/PawnWalkingStepPreflight.h"
#include "UObject/PawnInventoryReachability.h"
#include "UObject/PawnWalkingHitWallDispatch.h"
#include "UObject/PawnFallingParityRealizedTrace.h"
#include "UObject/PawnFallingHazardDiagnostics.h"
#include "UObject/PawnDirectHarmfulWaterEntryCertificate.h"
#include "UObject/PawnHazardWaterEgressObserver.h"
#include "UObject/PawnMoveStallWatchdog.h"
#include "UObject/PawnFiniteMoveCommandGuard.h"
#include "UObject/PawnVectorNonFiniteObserver.h"
#include "UObject/PawnMovementCommandProvenance.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class BotBenchmarkRunConfig;

struct BotBenchmarkHazardDeathPartitionRecord
{
	std::string SourcePawnActor;
	uint64_t Sequence = 0;
	double DeathTimeSeconds = 0.0;
	std::string KillerRelation;
	std::string Attribution;
	std::string EnvironmentalSource;
	bool HadRecentEnemyContribution = false;
	bool HadRecentEnemyMomentumContribution = false;
	std::string HazardPrefix;
	bool HazardResidenceTerminalExact = false;
	std::string HazardResidenceTerminal;
	int HazardResidenceEntryHealth = 0;
	float HazardResidenceHarmfulSeconds = 0.0f;
	uint64_t HazardResidenceCommandChanges = 0;
	bool HazardResidenceDirectSafeCandidateObserved = false;
	bool HazardResidenceDirectSafeCandidateSuperseded = false;
	std::string HazardResidenceDirectSafeCandidateName;
	uint64_t HazardResidenceCommandOwnershipLifeId = 0;
	bool HazardResidenceCommandOwnershipExact = false;
	std::string HazardResidenceCommandOwnershipTargetName;
	uint64_t HazardResidenceMovementCommandToken = 0;
	bool HazardResidenceMovementCommandProvenanceExact = false;
	std::string HazardResidenceMovementCommandCallerClass;
	std::string HazardResidenceMovementCommandCallerFunction;
	bool MoveTargetKnown = false;
	std::string MoveTargetName;
	bool MovementIntent = false;
	std::string PhysicsMode;
	bool WaterEgressTerminalKnown = false;
	uint64_t WaterEgressSequence = 0;
	uint64_t WaterEgressLifeId = 0;
	uint64_t WaterEgressEpisodeId = 0;
	bool FallingHazardTerminalKnown = false;
	uint64_t FallingHazardSequence = 0;
	uint64_t FallingHazardLifeId = 0;
	uint64_t FallingHazardFallEpisodeId = 0;
	uint64_t FallingHazardGenerationId = 0;
	std::string FallingHazardCorrelation;
	bool FallingParityTerminalKnown = false;
	uint64_t FallingParityLifeGeneration = 0;
	uint64_t FallingParityInvocationToken = 0;
	int FallingParityWalkingIteration = 0;
};

struct BotBenchmarkTargetSelectionRecord
{
	uint64_t Sequence = 0;
	std::string ContractId;
	std::string BotId;
	std::string PreviousTargetId;
	std::string RequestedTargetId;
	std::string ObservedTargetId;
	std::string Outcome;
};

struct BotBenchmarkPickTargetRecord
{
	uint64_t Sequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t SourceLifeId = 0;
	uint64_t SelectedLifeId = 0;
	int32_t SourceActorIndex = -1;
	int32_t SelectedActorIndex = -1;
	uint32_t CandidatePawns = 0;
	uint32_t SelfRejects = 0;
	uint32_t DeadRejects = 0;
	uint32_t LivingCandidates = 0;
	uint32_t LivingSkippedByCurrentPredicate = 0;
	uint32_t TeamRejects = 0;
	uint32_t LivingGeometryEligible = 0;
	uint32_t LivingLineOfSightEligible = 0;
	bool ReturnedTarget = false;
	bool ReturnedLivingTarget = false;
	bool NoResultWithLivingLineOfSightCandidate = false;
	bool IntegrityValid = true;
	std::string CallerClass;
	std::string CallerFunction;
	std::string SelectedActor;
	std::string SelectedClass;
};

struct BotBenchmarkPawnCanSeeRecord
{
	uint64_t Sequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t SourceLifeId = 0;
	uint64_t TargetLifeId = 0;
	int32_t SourceActorIndex = -1;
	int32_t TargetActorIndex = -1;
	float PeripheralVision = 0.0f;
	bool SightRadiusAccepted = false;
	bool LegacyConeAccepted = false;
	bool CorrectedConeAccepted = false;
	bool CorrectedConeSelected = false;
	bool ReturnedVisible = false;
	bool IntegrityValid = true;
	std::string CallerClass;
	std::string CallerFunction;
	std::string TargetActor;
	std::string TargetClass;
};

struct BotBenchmarkWarnTargetRecord
{
	uint64_t Sequence = 0;
	uint64_t NestedWarnTargetSequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t ReceiverLifeId = 0;
	int32_t ReceiverActorIndex = -1;
	int32_t ShooterActorIndex = -1;
	std::string Event;
	std::string ContractId;
	std::string ReceiverId;
	std::string ReceiverState;
	std::string ShooterId;
	bool NestedWarnTargetExact = false;
	bool IntegrityValid = true;
};

// Read-only post-call evidence for the retail TryToDuck outcome path.
struct BotBenchmarkTryToDuckOutcomeRecord
{
	uint64_t Sequence = 0;
	uint64_t NestedWarnTargetSequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t ReceiverLifeId = 0;
	int32_t ReceiverActorIndex = -1;
	double RequestedDuckDirX = 0.0;
	double RequestedDuckDirY = 0.0;
	double RequestedDuckDirZ = 0.0;
	bool RequestedReversed = false;
	double PostVelocityX = 0.0;
	double PostVelocityY = 0.0;
	double PostVelocityZ = 0.0;
	std::string PostPhysicsMode;
	std::string PostState;
	std::string PostLatentAction;
	bool IntegrityValid = true;
};

// A qualified post-call launch is intentionally narrower than a later hazard
// or wall terminal. It proves only the exact nested WarnTarget -> TryToDuck
// handoff and the state that script left for native physics.
struct BotBenchmarkWarningDodgeLaunchRecord
{
	uint64_t LaunchToken = 0;
	uint64_t Sequence = 0;
	uint64_t NestedWarnTargetSequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t ReceiverLifeId = 0;
	int32_t ReceiverActorIndex = -1;
	double LocationX = 0.0;
	double LocationY = 0.0;
	double LocationZ = 0.0;
	double VelocityX = 0.0;
	double VelocityY = 0.0;
	double VelocityZ = 0.0;
	double AccelerationX = 0.0;
	double AccelerationY = 0.0;
	double AccelerationZ = 0.0;
	std::string MoveTargetName;
	std::string RouteHeadName;
	std::string PostState;
	std::string PostLatentAction;
	bool IntegrityValid = true;
};

struct BotBenchmarkWarningDodgeTerminalRecord
{
	uint64_t LaunchToken = 0;
	uint64_t LaunchSequence = 0;
	uint64_t ReceiverLifeId = 0;
	int32_t ReceiverActorIndex = -1;
	uint64_t TerminalTick = 0;
	uint64_t WaterEgressSequence = 0;
	std::string Outcome;
	std::string UnknownReason;
	bool IntegrityValid = true;
};

struct BotBenchmarkDirectReachCommandRecord
{
	uint64_t Sequence = 0;
	uint64_t LifeId = 0;
	int32_t TargetActorIndex = -1;
	const void* TargetAddress = nullptr;
	std::string TargetName;
	std::string TargetClass;
	bool Reached = false;
	bool CheckNavpoint = false;
	bool ResolvedWallSlide = false;
	int WalkingSimulationIterations = 0;
	std::string LatentAction;
	bool RouteHeadPresent = false;
	std::string LinkStatus;
	uint64_t ActivationTick = 0;
	uint64_t TerminalTick = 0;
	std::string Terminal;
	bool HazardTerminalExact = false;
	uint64_t ReachSequence = 0;
	uint64_t NativeTick = 0;
	std::string CallerOrigin;
	std::string RejectReason;
	bool TargetIsInventory = false;
	bool MarkerKnown = false;
	bool MarkerLive = false;
	int32_t MarkerActorIndex = -1;
	std::string MarkerName;
	std::string MarkerClass;
};

struct BotBenchmarkFiniteMoveCommandGuardRecord
{
	uint64_t Sequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t LifeId = 0;
	int32_t ActorIndex = -1;
	std::string RequestedXClass;
	std::string RequestedYClass;
	std::string RequestedZClass;
	std::string Source;
	std::string Terminal;
	bool PriorDestinationFinite = false;
	bool PriorFocusFinite = false;
};

struct BotBenchmarkVectorNonFiniteRecord
{
	uint64_t Sequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t SourceLifeId = 0;
	int32_t SourceActorIndex = -1;
	std::string Operation;
	std::string LeftVectorClass;
	std::string RightVectorClass;
	std::string ScalarClass;
	std::string ResultVectorClass;
	bool RightVectorPresent = false;
	bool ScalarPresent = false;
	bool IntegrityValid = true;
	std::string CallerClass;
	std::string CallerFunction;
};

struct BotBenchmarkPickRegDestinationZeroDivideGuardRecord
{
	uint64_t Sequence = 0;
	uint64_t ObserverTick = 0;
	uint64_t CallerInvocationToken = 0;
	uint64_t SourceLifeId = 0;
	int32_t SourceActorIndex = -1;
};

struct BotBenchmarkBotState
{
	std::string Identity;
	std::string Actor;
	std::string PlayerName;
	std::string ClassName;
	std::string State;
	double PositionX = 0.0;
	double PositionY = 0.0;
	double PositionZ = 0.0;
	double VelocityX = 0.0;
	double VelocityY = 0.0;
	double VelocityZ = 0.0;
	std::string PhysicsMode;
	std::string LatentAction;
	double AccelerationX = 0.0;
	double AccelerationY = 0.0;
	double AccelerationZ = 0.0;
	double DestinationX = 0.0;
	double DestinationY = 0.0;
	double DestinationZ = 0.0;
	double MoveTimer = 0.0;
	std::string MoveTargetIdentity;
	std::string MoveTargetName;
	int Health = 0;
	double Score = 0.0;
	double PriDeaths = 0.0;
	bool MovementIntent = false;
	bool InHazardZone = false;
	uint64_t KillsExact = 0;
	uint64_t DeathsExact = 0;
	uint64_t SuicidesExact = 0;
	uint64_t TargetSelectionOutermostCallsExact = 0;
	uint64_t TargetSelectionNestedCallsExact = 0;
	uint64_t TargetSelectionAcceptedTargetChangesExact = 0;
	uint64_t TargetSelectionAcceptedSameTargetExact = 0;
	uint64_t TargetSelectionRejectedOrUnchangedExact = 0;
	uint64_t TargetSelectionMissingResultsExact = 0;
	uint64_t TargetSelectionInvalidIdentifierExact = 0;
	uint64_t TargetSelectionTrackerCapacityExceededExact = 0;
	uint64_t TargetSelectionRecordOverflowsExact = 0;
	uint64_t TargetSelectionIntegrityFailuresExact = 0;
	std::vector<BotBenchmarkTargetSelectionRecord> TargetSelectionRecords;
	uint64_t PickTargetObservationsExact = 0;
	uint64_t PickTargetCandidatesExact = 0;
	uint64_t PickTargetSelfRejectsExact = 0;
	uint64_t PickTargetDeadRejectsExact = 0;
	uint64_t PickTargetLivingCandidatesExact = 0;
	uint64_t PickTargetLivingSkippedByCurrentPredicateExact = 0;
	uint64_t PickTargetTeamRejectsExact = 0;
	uint64_t PickTargetLivingGeometryEligibleExact = 0;
	uint64_t PickTargetLivingLineOfSightEligibleExact = 0;
	uint64_t PickTargetReturnedTargetsExact = 0;
	uint64_t PickTargetReturnedLivingTargetsExact = 0;
	uint64_t PickTargetNoResultWithLivingLineOfSightCandidateExact = 0;
	uint64_t PickTargetObservationOverflowsExact = 0;
	uint64_t PickTargetIntegrityFailuresExact = 0;
	std::vector<BotBenchmarkPickTargetRecord> PickTargetRecords;
	uint64_t PawnCanSeeObservationsExact = 0;
	uint64_t PawnCanSeeReturnedVisibleExact = 0;
	uint64_t PawnCanSeeLegacyCorrectedDivergencesExact = 0;
	uint64_t PawnCanSeeObservationOverflowsExact = 0;
	uint64_t PawnCanSeeIntegrityFailuresExact = 0;
	std::vector<BotBenchmarkPawnCanSeeRecord> PawnCanSeeRecords;
	uint64_t VectorNonFiniteObservationsExact = 0;
	uint64_t VectorNonFiniteObservationOverflowsExact = 0;
	uint64_t VectorNonFiniteIntegrityFailuresExact = 0;
	std::vector<BotBenchmarkVectorNonFiniteRecord> VectorNonFiniteRecords;
	uint64_t WarnTargetObservationsExact = 0;
	uint64_t TryToDuckObservationsExact = 0;
	uint64_t WarnTargetExactNestedTryToDuckLinksExact = 0;
	uint64_t WarnTargetObservationOverflowsExact = 0;
	uint64_t WarnTargetIntegrityFailuresExact = 0;
	std::vector<BotBenchmarkWarnTargetRecord> WarnTargetRecords;
	uint64_t TryToDuckOutcomeOverflowsExact = 0;
	std::vector<BotBenchmarkTryToDuckOutcomeRecord> TryToDuckOutcomeRecords;
	uint64_t WarningDodgeLaunchesExact = 0;
	uint64_t WarningDodgeLaunchOverflowsExact = 0;
	std::vector<BotBenchmarkWarningDodgeLaunchRecord> WarningDodgeLaunchRecords;
	uint64_t InventoryDirectReachSupportObservationsExact = 0;
	uint64_t InventoryDirectReachSupportSafeSupportedExact = 0;
	uint64_t InventoryDirectReachSupportSafeUnsupportedNoObservedHazardExact = 0;
	uint64_t InventoryDirectReachSupportUnsafeHarmfulFootZoneExact = 0;
	uint64_t InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulExact = 0;
	uint64_t InventoryDirectReachSupportUnavailableExact = 0;
	uint64_t InventoryDirectReachSupportDiagnosticOverflowsExact = 0;
	uint64_t InventoryMarkerDirectReachRejectsExact = 0;
	std::vector<PawnMovement::InventoryDirectReachSupportDiagnosticRecord>
		InventoryDirectReachSupportDiagnostics;
	uint64_t DirectReachCommandObservationsExact = 0;
	uint64_t DirectReachCommandSuccessesExact = 0;
	uint64_t DirectReachCommandFailuresExact = 0;
	uint64_t DirectReachCommandSameLifeExactExact = 0;
	uint64_t DirectReachCommandUnlinkedExact = 0;
	uint64_t DirectReachCommandOverflowsExact = 0;
	uint64_t DirectReachCommandHazardousDeathsExact = 0;
	uint64_t DirectReachCommandNonhazardDeathsExact = 0;
	uint64_t DirectReachCommandClearedExact = 0;
	uint64_t DirectReachCommandLifeBoundaryCensoredExact = 0;
	uint64_t DirectReachCommandRunEndCensoredExact = 0;
	uint64_t DirectReachCommandCommandReplacedExact = 0;
	std::vector<BotBenchmarkDirectReachCommandRecord> DirectReachCommandRecords;
	uint64_t MovementCommandProvenanceObservationsExact = 0;
	uint64_t MovementCommandProvenanceOverflowsExact = 0;
	std::vector<PawnMovement::MovementCommandProvenanceObservation>
		MovementCommandProvenanceRecords;
	uint64_t FiniteMoveCommandGuardRejectionsExact = 0;
	uint64_t FiniteMoveCommandGuardDiagnosticOverflowsExact = 0;
	std::vector<BotBenchmarkFiniteMoveCommandGuardRecord> FiniteMoveCommandGuardDiagnostics;
	uint64_t PickRegDestinationZeroDivideGuardActivationsExact = 0;
	uint64_t PickRegDestinationZeroDivideGuardActivationOverflowsExact = 0;
	std::vector<BotBenchmarkPickRegDestinationZeroDivideGuardRecord>
		PickRegDestinationZeroDivideGuardActivations;
	uint64_t EnvironmentalDeathsExact = 0;
	uint64_t HazardExposedDeathsProxy = 0;
	uint64_t DamageTakenExact = 0;
	uint64_t DamageTakenFromOtherParticipantsExact = 0;
	uint64_t DamageTakenFromSelfExact = 0;
	uint64_t DamageTakenFromNonParticipantsExact = 0;
	uint64_t DamageDealtToOtherParticipantsExact = 0;
	uint64_t ConfirmedPickupsExact = 0;
	uint64_t ConfirmedWeaponPickupsExact = 0;
	uint64_t ConfirmedAmmoPickupsExact = 0;
	uint64_t ConfirmedHealthPickupsExact = 0;
	uint64_t ConfirmedArmorPickupsExact = 0;
	uint64_t ConfirmedOtherPickupsExact = 0;
	uint64_t PickupSourceConsumedUnconfirmedExact = 0;
	uint64_t NavigationCoverageVisitedNodesExact = 0;
	uint64_t NavigationCoverageCatalogNodesExact = 0;
	uint64_t NavigationCoverageUnionVisitedNodesExact = 0;
	uint64_t DirectSelfKills = 0;
	uint64_t DirectEnemyKills = 0;
	uint64_t UnassistedEnvironmentalDeaths = 0;
	uint64_t RecentEnemyContributedEnvironmentalDeathsProxy = 0;
	uint64_t AmbiguousDeaths = 0;
	uint64_t RecentEnemyMomentumContributedEnvironmentalDeathsProxy = 0;
	uint64_t HitWallEventsExact = 0;
	uint64_t WalkingHitWallDispatchObservationsExact = 0;
	uint64_t WalkingHitWallDispatchLegacyZBandExact = 0;
	uint64_t WalkingHitWallDispatchMinHitWallExact = 0;
	uint64_t WalkingHitWallDispatchMinHitWallCandidateActivationsExact = 0;
	uint64_t WalkingHitWallDispatchDisagreementsExact = 0;
	uint64_t WalkingHitWallDispatchCallbacksExact = 0;
	uint64_t WalkingHitWallDispatchDiagnosticOverflowsExact = 0;
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord>
		WalkingHitWallDispatchDiagnostics;
	uint64_t PainLedgeVetoesExact = 0;
	uint64_t PainLedgeRepeatVetoesExact = 0;
	uint64_t PainLedgeRecoveryAttemptsExact = 0;
	uint64_t PainLedgeRecoveryEscapesExact = 0;
	uint64_t WallAdjustCallsExact = 0;
	uint64_t WallAdjustRepeatsExact = 0;
	uint64_t WallAdjustRecoveryAttemptsExact = 0;
	uint64_t WallAdjustRecoverySuccessesExact = 0;
	uint64_t WallAdjustForcedReplansExact = 0;
	uint64_t MoveStallDetectionsExact = 0;
	uint64_t MoveStallEpisodeResetsExact = 0;
	uint64_t MoveStallForcedReplansExact = 0;
	uint64_t MoveStallNavigationForcedReplansExact = 0;
	uint64_t MoveStallTargetlessMoveToTimeoutsExact = 0;
	uint64_t MoveStallDirectActorMoveTowardTimeoutsExact = 0;
	double MoveStallEligibleSeconds = 0.0;
	uint64_t MoveStallRecoveryEpisodesExact = 0;
	uint64_t MoveStallRecoveryClearedWithin2SecondsExact = 0;
	uint64_t MoveStallRecoveryClearedAfter2SecondsWithin5SecondsExact = 0;
	uint64_t MoveStallRecoveryReplannedWithin5SecondsExact = 0;
	uint64_t MoveStallRecoveryMissed5SecondDeadlineExact = 0;
	uint64_t MoveStallRecoveryExcludedIntentionalStopsExact = 0;
	uint64_t MoveStallRecoveryCensoredLifeBoundariesExact = 0;
	uint64_t MoveStallRecoveryCensoredRunEndExact = 0;
	uint64_t MoveStallRecoveryUnknownExact = 0;
	uint64_t MoveStallRecoveryEpisodeRecordOverflowsExact = 0;
	std::vector<PawnMoveStallRecoveryEpisodeRecord> MoveStallRecoveryEpisodes;
	uint64_t MoveStallRecoveryDecisionRecordOverflowsExact = 0;
	std::vector<PawnMoveStallRecoveryDecisionRecord> MoveStallRecoveryDecisions;
	uint64_t FailedNavigationAvoidanceActivationsExact = 0;
	uint64_t FailedNavigationSafeguardSuppressionsExact = 0;
	uint64_t FailedNavigationRoutePenaltyApplicationsExact = 0;
	uint64_t HarmfulZoneEscapeEpisodesExact = 0;
	uint64_t HarmfulZoneEscapeCenterEntriesExact = 0;
	uint64_t HarmfulZoneEscapeFootEntriesExact = 0;
	uint64_t HarmfulZoneEscapeRecoveryAttemptsExact = 0;
	uint64_t PainLedgeRecoveryActiveHitWallEventsExact = 0;
	uint64_t HarmfulZoneEscapeSuccessfulEscapesExact = 0;
	uint64_t HarmfulZoneEscapeForcedReplansExact = 0;
	uint64_t HarmfulZoneEscapeNoSafeCandidatesExact = 0;
	uint64_t HazardSwimEgressEpisodesExact = 0;
	uint64_t HazardSwimEgressEligibleExact = 0;
	uint64_t HazardSwimEgressAuthorizedExact = 0;
	uint64_t HazardSwimEgressDebouncedExact = 0;
	uint64_t HazardSwimEgressNoAnchorRejectedExact = 0;
	uint64_t HazardSwimEgressExitedExact = 0;
	uint64_t HazardSwimEgressDeathsBeforeExitExact = 0;
	uint64_t HazardSwimEgressForcedReplansExact = 0;
	uint64_t HazardSwimEgressForcedReplanSameCommandReissuedExact = 0;
	uint64_t HazardSwimEgressForcedReplanDifferentCommandIssuedExact = 0;
	uint64_t HazardSwimEgressForcedReplanHazardClearedBeforeCommandExact = 0;
	uint64_t HazardSwimEgressForcedReplanFellBeforeCommandExact = 0;
	uint64_t HazardSwimEgressForcedReplanDiedBeforeCommandExact = 0;
	uint64_t HazardSwimEgressForcedReplanLifeBoundaryCensoredExact = 0;
	uint64_t HazardSwimEgressForcedReplanRunEndCensoredExact = 0;
	uint64_t HazardSwimEgressForcedReplanEpisodeAbandonedExact = 0;
	uint64_t HazardSwimEgressFallingPreMoveAnchorCapturesExact = 0;
	uint64_t HazardSwimEgressFallingPreMoveAnchorUsesExact = 0;
	uint64_t HazardSwimEgressLiveAppliesExact = 0;
	uint64_t HazardSwimEgressLiveActiveTicksExact = 0;
	uint64_t HazardSwimEgressLiveProbeRejectedExact = 0;
	uint64_t HazardSwimEgressLiveSuccessfulExitsExact = 0;
	uint64_t HazardSwimEgressLiveShadowCandidatesExact = 0;
	uint64_t HazardSwimEgressLiveShadowFallingTerminalsExact = 0;
	uint64_t HazardSwimEgressLiveShadowHazardClearedTerminalsExact = 0;
	uint64_t HazardSwimEgressLiveShadowProbeBlockedTerminalsExact = 0;
	uint64_t HazardSwimEgressDirectNavProbesExact = 0;
	uint64_t HazardSwimEgressDirectNavSafeCandidatesExact = 0;
	uint64_t HazardResidenceEpisodesExact = 0;
	uint64_t HazardResidenceClearedExact = 0;
	uint64_t HazardResidenceDeathsExact = 0;
	uint64_t HazardResidenceLifeBoundaryCensoredExact = 0;
	uint64_t HazardResidenceRunEndCensoredExact = 0;
	uint64_t HazardResidenceUnknownExact = 0;
	uint64_t HazardResidenceReentriesExact = 0;
	uint64_t HazardResidenceCommandChangesExact = 0;
	uint64_t HazardResidenceCandidatesObservedExact = 0;
	uint64_t HazardResidenceCandidateOtherCommandsExact = 0;
	std::string HazardSwimEgressDirectNavBestCandidateName;
	bool HazardSwimEgressAnchorKnown = false;
	std::string HazardSwimEgressAnchorSource;
	uint64_t HazardWaterEgressDiagnosticOverflowsExact = 0;
	std::vector<PawnMovement::HazardWaterEgressDiagnosticRecord>
		HazardWaterEgressDiagnostics;
	std::vector<BotBenchmarkHazardDeathPartitionRecord>
		HazardDeathPartitionRecords;
	uint64_t WarningDodgeTerminalOutcomesExact = 0;
	uint64_t WarningDodgeTerminalUnknownExact = 0;
	uint64_t WarningDodgeTerminalOverflowsExact = 0;
	std::vector<BotBenchmarkWarningDodgeTerminalRecord>
		WarningDodgeTerminalRecords;
	uint64_t FallingHazardRecoveryPromotionsExact = 0;
	uint64_t FallingHazardRecoveryAdvanceCallsExact = 0;
	uint64_t FallingHazardRecoveryContextRejectedExact = 0;
	uint64_t FallingHazardRecoveryNoActiveFallEpisodeExact = 0;
	uint64_t FallingHazardRecoveryNoPrefixExact = 0;
	uint64_t FallingHazardRecoveryEligibleExact = 0;
	uint64_t FallingHazardRecoveryAnchorRejectedExact = 0;
	uint64_t FallingHazardRecoveryProbeRejectedExact = 0;
	uint64_t FallingHazardRecoveryLiveAppliesExact = 0;
	uint64_t FallingHazardRecoveryLiveActiveTicksExact = 0;
	uint64_t FallingHazardRecoverySafeLandingsExact = 0;
	uint64_t FallingHazardRecoveryHarmfulEntriesExact = 0;
	uint64_t FallingHazardRecoveryDeathsExact = 0;
	uint64_t FallingHazardRecoveryTimeoutsExact = 0;
	uint64_t ExternalImpulseFallHarmfulWitnessesExact = 0;
	uint64_t ExternalImpulseFallNoAirControlExact = 0;
	uint64_t ExternalImpulseFallAlternativesTestedExact = 0;
	uint64_t ExternalImpulseFallCertifiedExact = 0;
	uint64_t ExternalImpulseFallUncertifiedExact = 0;
	uint64_t FallingSeamDetectionsExact = 0;
	uint64_t HorizontalCornerCandidateProbesExact = 0;
	uint64_t HorizontalCornerAuthorizedEscapesExact = 0;
	uint64_t HorizontalCornerTargetProgressRejectsExact = 0;
	uint64_t HorizontalCornerUnknownOrUnsafeSupportExact = 0;
	uint64_t FallingSeamEpisodesExact = 0;
	uint64_t FallingSeamInvalidGeometryRejectsExact = 0;
	uint64_t FallingSeamAuthorizableEpisodesExact = 0;
	uint64_t HorizontalCornerAuthorizedCandidatesExact = 0;
	uint64_t HorizontalCornerBlockedSweepCandidatesExact = 0;
	uint64_t HorizontalCornerNoStaticWalkableSupportCandidatesExact = 0;
	uint64_t HorizontalCornerPainSupportCandidatesExact = 0;
	uint64_t HorizontalCornerNoActiveMovementIntentOrTargetCandidatesExact = 0;
	uint64_t HorizontalCornerTrueTargetRegressionCandidatesExact = 0;
	uint64_t HorizontalCornerUnknownEvidenceCandidatesExact = 0;
	uint64_t WalkingStepPreflightObservationsExact = 0;
	uint64_t WalkingStepPreflightUnsupportedEndpointsExact = 0;
	uint64_t WalkingStepPreflightNoDecisionsExact = 0;
	uint64_t WalkingStepPreflightProvisionalAuthorizationsExact = 0;
	uint64_t WalkingStepPreflightAuthorizationsExact = 0;
	uint64_t WalkingStepPreflightAuthorizableEpisodesExact = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoEligibleExact = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoAppliedExact = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoDebouncedExact = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoForcedReplansExact = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoRollbackRejectedExact = 0;
	uint64_t WalkingStepPreflightPositiveDpsVetoActionOverflowsExact = 0;
	std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
		WalkingStepPreflightPositiveDpsVetoActions;
	uint64_t WalkingStepPreflightDiagnosticOverflowsExact = 0;
	std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
		WalkingStepPreflightDiagnostics;
	std::array<uint64_t, PawnMovement::WalkingStepPreflightReasonCount>
		WalkingStepPreflightReasonsExact = {};
	uint64_t FallingParityRealizedEpisodesExact = 0;
	uint64_t FallingParityRealizedStepsExact = 0;
	uint64_t FallingParityRealizedMatchedStepsExact = 0;
	uint64_t FallingParityRealizedMatchedLandingStepsExact = 0;
	uint64_t FallingParityRealizedMismatchesExact = 0;
	uint64_t FallingParityRealizedUnknownsExact = 0;
	uint64_t FallingParityRealizedCallbackBarriersExact = 0;
	uint64_t FallingParityRealizedPainEntriesExact = 0;
	uint64_t FallingParityRealizedDeathsExact = 0;
	uint64_t FallingParityRealizedLandingsExact = 0;
	uint64_t FallingParityRealizedContinuityLossesExact = 0;
	uint64_t FallingParityRealizedRecordOverflowsExact = 0;
	std::vector<PawnMovement::FallingParityRealizedRecord>
		FallingParityRealizedRecords;
	uint64_t VerticalPainColumnEpisodesStartedExact = 0;
	uint64_t VerticalPainColumnEpisodesCompletedExact = 0;
	uint64_t VerticalPainColumnTruePositiveOutcomesExact = 0;
	uint64_t VerticalPainColumnFalsePositiveOutcomesExact = 0;
	uint64_t VerticalPainColumnFalseNegativeOutcomesExact = 0;
	uint64_t VerticalPainColumnTrueNegativeOutcomesExact = 0;
	uint64_t VerticalPainColumnAmbiguousOutcomesExact = 0;
	uint64_t VerticalPainColumnUnknownOutcomesExact = 0;
	uint64_t VerticalPainColumnDiagnosticOverflowsExact = 0;
	uint64_t VerticalPainColumnGenerationCapacityExhaustionsExact = 0;
	uint64_t PersistentHarmfulFallCandidatesStartedExact = 0;
	uint64_t PersistentHarmfulFallPromotionsExact = 0;
	uint64_t PersistentHarmfulFallResetsExact = 0;
	uint64_t PersistentHarmfulFallConfirmedHarmfulEntriesExact = 0;
	uint64_t PersistentHarmfulFallObservedLeadSamplesExact = 0;
	uint64_t PersistentHarmfulFallObservedLeadMillisecondsExact = 0;
	uint64_t SingleHarmfulFallPrefixCandidatesStartedExact = 0;
	uint64_t SingleHarmfulFallPrefixPromotionsExact = 0;
	uint64_t SingleHarmfulFallPrefixResetsExact = 0;
	uint64_t SingleHarmfulFallPrefixConfirmedHarmfulEntriesExact = 0;
	uint64_t SingleHarmfulFallPrefixObservedLeadSamplesExact = 0;
	uint64_t SingleHarmfulFallPrefixObservedLeadMillisecondsExact = 0;
	uint64_t DirectHarmfulWaterEntryCandidatesExact = 0;
	uint64_t DirectHarmfulWaterEntryConfirmedExact = 0;
	uint64_t DirectHarmfulWaterEntryConfirmedNoHarmExact = 0;
	uint64_t DirectHarmfulWaterEntryUnresolvedExact = 0;
	uint64_t DirectHarmfulWaterEntryLeadSamplesExact = 0;
	uint64_t DirectHarmfulWaterEntryLeadMillisecondsExact = 0;
	std::array<uint64_t, PawnMovement::DirectHarmfulWaterEntryCertificateResultCount>
		DirectHarmfulWaterEntryCertificateResultsExact = {};
	std::vector<PawnMovement::FallingHazardDiagnosticRecord>
		VerticalPainColumnDiagnostics;
};

struct BotBenchmarkTelemetryEvent
{
	uint64_t Sequence = 0;
	uint64_t Tick = 0;
	double SimulatedSeconds = 0.0;
	std::string Type;
	std::string Map;
	std::string Status;
	std::string FailureReason;
	bool TargetSelectionObserverRequested = false;
	std::string TargetSelectionObserverStatus;
	std::string TargetSelectionObserverReason;
	bool PickTargetObserverRequested = false;
	bool PawnVisionObserverRequested = false;
	bool VectorNonFiniteObserverRequested = false;
	bool WarnTargetObserverRequested = false;
	std::string WarnTargetObserverStatus;
	std::string WarnTargetObserverReason;
	bool InventoryDirectReachSupportObserverRequested = false;
	bool NativePathCommitObserverRequested = false;
	bool DirectReachCommandObserverRequested = false;
	bool MovementCommandProvenanceObserverRequested = false;
	bool FiniteMoveCommandGuardRequested = false;
	bool PickRegDestinationZeroDivideGuardRequested = false;
	std::vector<BotBenchmarkBotState> Bots;
};

class BotBenchmarkTelemetryProtocol
{
public:
	static uint64_t EventCap(uint64_t maxTicks);
	static std::string ConfigIdentity(const BotBenchmarkRunConfig& config);
	static std::string ManifestJson(const BotBenchmarkRunConfig& config,
		const BotBenchmarkBuildIdentity& buildIdentity);
	static std::string EventJson(const std::string& configIdentity, BotBenchmarkTelemetryEvent event);
};
