#include "BotBenchmark/BotBenchmarkDeathAttributionCoordinator.h"
#include "BotBenchmark/BotBenchmarkDriver.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	using namespace BotBenchmarkDeathAttribution;

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool EnterEnemyDamage(Coordinator& coordinator, const char* victim, double time,
		int preHealth, int postHealth, bool impulse)
	{
		auto entered = coordinator.EnterTakeDamage({ victim, DamageInstigator::EnemyPlayer,
			time, preHealth, DamageFrameKind::Canonical, true });
		if (entered.Status != CoordinatorStatus::Accepted)
			return false;
		if (coordinator.ObserveAddVelocity(victim, impulse) != CoordinatorStatus::Accepted)
			return false;
		if (coordinator.ObserveTakeDamageResult(entered.Token, postHealth) != CoordinatorStatus::Accepted)
			return false;
		return coordinator.ExitTakeDamage(entered.Token) == CoordinatorStatus::Accepted;
	}

	KilledResult CompleteKilled(Coordinator& coordinator, const char* victim,
		DeathKiller killer, double time)
	{
		auto entered = coordinator.EnterKilled({ victim, killer, time, true });
		if (entered.Status != CoordinatorStatus::Accepted)
			return { entered.Status };
		KilledResult result = coordinator.ObserveKilledResult(entered.Token);
		if (coordinator.ExitKilled(entered.Token) != CoordinatorStatus::Accepted)
			return { CoordinatorStatus::OutOfOrder };
		return result;
	}

	BotBenchmarkDriverDetail::NativePawnCounters UniformNativeCounters(
		uint64_t value, double seconds)
	{
		using BotBenchmarkDriverDetail::NativePawnCounters;
		NativePawnCounters counters;
		counters.PainLedgeVetoes = value;
		counters.PainLedgeRepeatVetoes = value;
		counters.PainLedgeRecoveryAttempts = value;
		counters.PainLedgeRecoveryEscapes = value;
		counters.WallAdjustCalls = value;
		counters.WallAdjustRepeats = value;
		counters.WallAdjustRecoveryAttempts = value;
		counters.WallAdjustRecoverySuccesses = value;
		counters.WallAdjustForcedReplans = value;
		counters.WalkingHitWallDispatchMinHitWallCandidateActivations = value;
		counters.MoveStallDetections = value;
		counters.MoveStallEpisodeResets = value;
		counters.MoveStallForcedReplans = value;
		counters.MoveStallNavigationForcedReplans = value;
		counters.MoveStallTargetlessMoveToTimeouts = value;
		counters.MoveStallEligibleSeconds = seconds;
		counters.FailedNavigationAvoidanceActivations = value;
		counters.FailedNavigationSafeguardSuppressions = value;
		counters.FailedNavigationRoutePenaltyApplications = value;
		counters.FallingSeamDetections = value;
		counters.HorizontalCornerCandidateProbes = value * 3;
		counters.HorizontalCornerAuthorizedEscapes = value;
		counters.HorizontalCornerTargetProgressRejects = value;
		counters.HorizontalCornerUnknownOrUnsafeSupport = value;
		counters.FallingSeamEpisodes = value;
		counters.FallingSeamInvalidGeometryRejects = 0;
		counters.FallingSeamAuthorizableEpisodes = value;
		counters.HorizontalCornerAuthorizedCandidates = value;
		counters.HorizontalCornerBlockedSweepCandidates = 0;
		counters.HorizontalCornerNoStaticWalkableSupportCandidates = 0;
		counters.HorizontalCornerPainSupportCandidates = 0;
		counters.HorizontalCornerNoActiveMovementIntentOrTargetCandidates = 0;
		counters.HorizontalCornerTrueTargetRegressionCandidates = value;
		counters.HorizontalCornerUnknownEvidenceCandidates = value;
		counters.WalkingStepPreflightObservations = value * 2;
		counters.WalkingStepPreflightUnsupportedEndpoints = value;
		counters.WalkingStepPreflightNoDecisions = value;
		counters.WalkingStepPreflightProvisionalAuthorizations = value;
		counters.WalkingStepPreflightAuthorizations = value;
		counters.WalkingStepPreflightAuthorizableEpisodes = value;
		counters.WalkingStepPreflightDiagnosticOverflows = 0;
		counters.WalkingStepPreflightReasons.fill(value);
		counters.FallingParityRealizedEpisodes = value;
		counters.FallingParityRealizedSteps = value * 4;
		counters.FallingParityRealizedMatchedSteps = value;
		counters.FallingParityRealizedMatchedLandingSteps = value;
		counters.FallingParityRealizedMismatches = value;
		counters.FallingParityRealizedUnknowns = 0;
		counters.FallingParityRealizedCallbackBarriers = value;
		counters.FallingParityRealizedPainEntries = value;
		counters.FallingParityRealizedDeaths = value;
		counters.FallingParityRealizedLandings = value;
		counters.FallingParityRealizedContinuityLosses = value;
		counters.FallingParityRealizedRecordOverflows = 0;
		counters.VerticalPainColumnEpisodesStarted = value;
		counters.VerticalPainColumnEpisodesCompleted = value;
		counters.VerticalPainColumnTruePositiveOutcomes = value;
		counters.VerticalPainColumnFalsePositiveOutcomes = value;
		counters.VerticalPainColumnFalseNegativeOutcomes = value;
		counters.VerticalPainColumnTrueNegativeOutcomes = value;
		counters.VerticalPainColumnAmbiguousOutcomes = value;
		counters.VerticalPainColumnUnknownOutcomes = value;
		counters.VerticalPainColumnDiagnosticOverflows = value;
		counters.VerticalPainColumnGenerationCapacityExhaustions = value;
		return counters;
	}

	bool HasUniformNativeCounters(const BotBenchmarkDriverDetail::NativePawnCounters& counters,
		uint64_t value, double seconds)
	{
		return counters.PainLedgeVetoes == value &&
			counters.PainLedgeRepeatVetoes == value &&
			counters.PainLedgeRecoveryAttempts == value &&
			counters.PainLedgeRecoveryEscapes == value &&
			counters.WallAdjustCalls == value && counters.WallAdjustRepeats == value &&
			counters.WallAdjustRecoveryAttempts == value &&
			counters.WallAdjustRecoverySuccesses == value &&
			counters.WallAdjustForcedReplans == value &&
			counters.WalkingHitWallDispatchMinHitWallCandidateActivations == value &&
			counters.MoveStallDetections == value &&
			counters.MoveStallEpisodeResets == value &&
			counters.MoveStallForcedReplans == value &&
			counters.MoveStallNavigationForcedReplans == value &&
			counters.MoveStallTargetlessMoveToTimeouts == value &&
			counters.MoveStallEligibleSeconds == seconds &&
			counters.FailedNavigationAvoidanceActivations == value &&
			counters.FailedNavigationSafeguardSuppressions == value &&
			counters.FailedNavigationRoutePenaltyApplications == value &&
			counters.FallingSeamDetections == value &&
			counters.HorizontalCornerCandidateProbes == value * 3 &&
			counters.HorizontalCornerAuthorizedEscapes == value &&
			counters.HorizontalCornerTargetProgressRejects == value &&
			counters.HorizontalCornerUnknownOrUnsafeSupport == value &&
			counters.FallingSeamEpisodes == value &&
			counters.FallingSeamInvalidGeometryRejects == 0 &&
			counters.FallingSeamAuthorizableEpisodes == value &&
			counters.HorizontalCornerAuthorizedCandidates == value &&
			counters.HorizontalCornerBlockedSweepCandidates == 0 &&
			counters.HorizontalCornerNoStaticWalkableSupportCandidates == 0 &&
			counters.HorizontalCornerPainSupportCandidates == 0 &&
			counters.HorizontalCornerNoActiveMovementIntentOrTargetCandidates == 0 &&
			counters.HorizontalCornerTrueTargetRegressionCandidates == value &&
			counters.HorizontalCornerUnknownEvidenceCandidates == value &&
			counters.WalkingStepPreflightObservations == value * 2 &&
			counters.WalkingStepPreflightUnsupportedEndpoints == value &&
			counters.WalkingStepPreflightNoDecisions == value &&
			counters.WalkingStepPreflightProvisionalAuthorizations == value &&
			counters.WalkingStepPreflightAuthorizations == value &&
			counters.WalkingStepPreflightAuthorizableEpisodes == value &&
			counters.WalkingStepPreflightDiagnosticOverflows == 0 &&
			counters.FallingParityRealizedEpisodes == value &&
			counters.FallingParityRealizedSteps == value * 4 &&
			counters.FallingParityRealizedMatchedSteps == value &&
			counters.FallingParityRealizedMatchedLandingSteps == value &&
			counters.FallingParityRealizedMismatches == value &&
			counters.FallingParityRealizedUnknowns == 0 &&
			counters.FallingParityRealizedCallbackBarriers == value &&
			counters.FallingParityRealizedPainEntries == value &&
			counters.FallingParityRealizedDeaths == value &&
			counters.FallingParityRealizedLandings == value &&
			counters.FallingParityRealizedContinuityLosses == value &&
			counters.FallingParityRealizedRecordOverflows == 0 &&
			counters.VerticalPainColumnEpisodesStarted == value &&
			counters.VerticalPainColumnEpisodesCompleted == value &&
			counters.VerticalPainColumnTruePositiveOutcomes == value &&
			counters.VerticalPainColumnFalsePositiveOutcomes == value &&
			counters.VerticalPainColumnFalseNegativeOutcomes == value &&
			counters.VerticalPainColumnTrueNegativeOutcomes == value &&
			counters.VerticalPainColumnAmbiguousOutcomes == value &&
			counters.VerticalPainColumnUnknownOutcomes == value &&
			counters.VerticalPainColumnDiagnosticOverflows == value &&
			counters.VerticalPainColumnGenerationCapacityExhaustions == value &&
			std::all_of(counters.WalkingStepPreflightReasons.begin(),
				counters.WalkingStepPreflightReasons.end(),
				[value](uint64_t reason) { return reason == value; });
	}
}

int main()
{
	constexpr double windowSeconds = 2.0;
	using namespace BotBenchmarkDriverDetail;

	int firstPawn = 0;
	int replacementPawn = 0;
	NativePawnCounterEpoch sampledEpoch;
	NativePawnCounters sampledTotals;
	auto firstLive = sampledEpoch.BeginPawn(&firstPawn, NativePawnCounterSample::LivePawn);
	if (!firstLive.BeganPawn || !firstLive.ResetLifeAttribution)
		return Fail("first live pawn did not begin a resettable native counter epoch");
	sampledEpoch.Accumulate(UniformNativeCounters(2, 0.5), sampledTotals);
	if (sampledEpoch.BeginPawn(&firstPawn, NativePawnCounterSample::DeathFlush).BeganPawn)
		return Fail("death flush replaced the already sampled pawn epoch");
	sampledEpoch.Accumulate(UniformNativeCounters(5, 1.25), sampledTotals);
	if (!HasUniformNativeCounters(sampledTotals, 5, 1.25))
		return Fail("death flush lost native counter increments after the last tick sample");
	sampledEpoch.Accumulate(UniformNativeCounters(5, 1.25), sampledTotals);
	if (sampledEpoch.BeginPawn(&firstPawn, NativePawnCounterSample::LivePawn).BeganPawn)
		return Fail("subsequent capture replaced the death-flushed pawn epoch");
	sampledEpoch.Accumulate(UniformNativeCounters(5, 1.25), sampledTotals);
	if (!HasUniformNativeCounters(sampledTotals, 5, 1.25))
		return Fail("repeated death flush or capture double-counted native counters");

	NativePawnCounterEpoch recoveryEpoch;
	NativePawnCounters recoveryTotals;
	recoveryEpoch.BeginPawn(&firstPawn, NativePawnCounterSample::LivePawn);
	NativePawnCounters recoveryFirst;
	recoveryFirst.FallingHazardRecoveryAdvanceCalls = 4;
	recoveryFirst.FallingHazardRecoveryNoPrefix = 3;
	recoveryEpoch.Accumulate(recoveryFirst, recoveryTotals);
	NativePawnCounters recoverySecond = recoveryFirst;
	recoverySecond.FallingHazardRecoveryAdvanceCalls = 7;
	recoverySecond.FallingHazardRecoveryPromotions = 1;
	recoverySecond.FallingHazardRecoveryEligible = 1;
	recoverySecond.FallingHazardRecoveryLiveApplies = 1;
	recoveryEpoch.Accumulate(recoverySecond, recoveryTotals);
	if (recoveryTotals.FallingHazardRecoveryAdvanceCalls != 7
		|| recoveryTotals.FallingHazardRecoveryNoPrefix != 3
		|| recoveryTotals.FallingHazardRecoveryPromotions != 1
		|| recoveryTotals.FallingHazardRecoveryEligible != 1
		|| recoveryTotals.FallingHazardRecoveryLiveApplies != 1)
		return Fail("falling-hazard recovery native counters did not accumulate across samples");

	NativePawnCounterEpoch unsampledEpoch;
	NativePawnCounters unsampledTotals;
	auto firstDeath = unsampledEpoch.BeginPawn(&firstPawn, NativePawnCounterSample::DeathFlush);
	if (!firstDeath.BeganPawn || firstDeath.ResetLifeAttribution)
		return Fail("death before the first sample requested a life-attribution reset");
	unsampledEpoch.Accumulate(UniformNativeCounters(7, 1.75), unsampledTotals);
	if (!HasUniformNativeCounters(unsampledTotals, 7, 1.75))
		return Fail("death before the first sample did not capture native counters");

	auto respawn = sampledEpoch.BeginPawn(&replacementPawn, NativePawnCounterSample::LivePawn);
	if (!respawn.BeganPawn || !respawn.ResetLifeAttribution)
		return Fail("replacement pawn did not begin a new live counter epoch");
	sampledEpoch.Accumulate(UniformNativeCounters(2, 0.5), sampledTotals);
	if (!HasUniformNativeCounters(sampledTotals, 7, 1.75))
		return Fail("replacement pawn with the same identity did not preserve prior totals");

	DeathAttributionCounters partition;
	for (Kind kind : { Kind::DirectSelfKill, Kind::DirectEnemyKill,
		Kind::UnassistedEnvironmentalDeath,
		Kind::RecentEnemyContributedEnvironmentalDeathProxy, Kind::AmbiguousDeath })
	{
		Decision decision;
		decision.Attribution = kind;
		decision.HadRecentEnemyMomentumContribution =
			kind == Kind::RecentEnemyContributedEnvironmentalDeathProxy;
		partition.Apply(decision);
	}
	if (partition.ClassifiedDeaths() != 5 || partition.DirectSelfKills != 1 ||
		partition.DirectEnemyKills != 1 || partition.UnassistedEnvironmentalDeaths != 1 ||
		partition.RecentEnemyContributedEnvironmentalDeathsProxy != 1 ||
		partition.AmbiguousDeaths != 1 ||
		partition.RecentEnemyMomentumContributedEnvironmentalDeathsProxy != 1)
		return Fail("five-way death attribution partition or momentum subset was not preserved");

	Coordinator nested(windowSeconds);
	auto wrapper = nested.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		8.0, 100, DamageFrameKind::Override, true });
	auto canonical = nested.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		8.25, 100, DamageFrameKind::Canonical, true });
	if (!wrapper.Token || !canonical.Token || canonical.IsOutermost)
		return Fail("nested override/canonical scopes were not represented");
	if (nested.ObserveAddVelocity("BotA", false) != CoordinatorStatus::Accepted ||
		nested.ObserveTakeDamageResult(canonical.Token, 90) != CoordinatorStatus::Accepted ||
		nested.ExitTakeDamage(canonical.Token) != CoordinatorStatus::Accepted ||
		nested.ObserveTakeDamageResult(wrapper.Token, 90) != CoordinatorStatus::Accepted ||
		nested.ExitTakeDamage(wrapper.Token) != CoordinatorStatus::Accepted)
		return Fail("nested damage scopes did not unwind normally");
	const State* nestedState = nested.FindLifeState("BotA");
	if (!nestedState || nestedState->LastEnemyContributionSeconds != 8.25 ||
		nestedState->HasEnemyMomentumContribution)
		return Fail("override frame was counted instead of the canonical health loss");

	Coordinator impulseOnly(windowSeconds);
	if (!EnterEnemyDamage(impulseOnly, "BotA", 9.0, 100, 100, true))
		return Fail("zero-damage impulse fixture failed");
	const State* impulseState = impulseOnly.FindLifeState("BotA");
	if (!impulseState || !impulseState->HasEnemyContribution ||
		!impulseState->HasEnemyMomentumContribution)
		return Fail("zero-damage AddVelocity impulse was not retained");

	Coordinator deathFlushContribution(windowSeconds);
	if (!EnterEnemyDamage(deathFlushContribution, "BotA", 9.0, 100, 90, true))
		return Fail("death-flush contribution fixture failed");
	NativePawnCounterEpoch deathFlushEpoch;
	NativePawnCounters deathFlushTotals;
	auto deathFlushBegin = deathFlushEpoch.BeginPawn(
		&firstPawn, NativePawnCounterSample::DeathFlush);
	if (deathFlushBegin.ResetLifeAttribution)
		deathFlushContribution.ResetLife("BotA");
	deathFlushEpoch.Accumulate(UniformNativeCounters(3, 0.75), deathFlushTotals);
	const State* contributionAfterFlush = deathFlushContribution.FindLifeState("BotA");
	if (!contributionAfterFlush || !contributionAfterFlush->HasEnemyContribution ||
		!contributionAfterFlush->HasEnemyMomentumContribution)
		return Fail("death-time native counter flush cleared recent enemy contribution");
	auto flushSource = deathFlushContribution.EnterEnvironmentalSource({ "BotA",
		EnvironmentalSource::PainTimer, 10.0, true });
	KilledResult contributedAfterFlush = CompleteKilled(deathFlushContribution,
		"BotA", DeathKiller::None, 10.0);
	if (!contributedAfterFlush.Attribution ||
		contributedAfterFlush.Attribution->Attribution !=
			Kind::RecentEnemyContributedEnvironmentalDeathProxy ||
		!contributedAfterFlush.Attribution->HadRecentEnemyMomentumContribution)
		return Fail("enemy contribution did not survive through death classification after flush");
	deathFlushContribution.ExitEnvironmentalSource(flushSource.Token);

	Coordinator zeroEffect(windowSeconds);
	if (!EnterEnemyDamage(zeroEffect, "BotA", 9.0, 100, 100, false) ||
		zeroEffect.FindLifeState("BotA"))
		return Fail("zero-damage zero-impulse call created a contribution");

	Coordinator nonEnemy(windowSeconds);
	for (DamageInstigator instigator : {
		DamageInstigator::SelfPlayer, DamageInstigator::NoneOrNonPlayer })
	{
		auto entered = nonEnemy.EnterTakeDamage({ "BotA", instigator,
			9.0, 100, DamageFrameKind::Canonical, true });
		nonEnemy.ObserveAddVelocity("BotA", true);
		nonEnemy.ObserveTakeDamageResult(entered.Token, 75);
		nonEnemy.ExitTakeDamage(entered.Token);
	}
	if (nonEnemy.FindLifeState("BotA"))
		return Fail("self or nonplayer damage polluted enemy contribution state");

	Coordinator abnormal(windowSeconds);
	auto abandoned = abnormal.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		9.0, 100, DamageFrameKind::Canonical, true });
	abnormal.ObserveAddVelocity("BotA", true);
	if (abnormal.ExitTakeDamage(abandoned.Token) != CoordinatorStatus::Accepted ||
		abnormal.FindLifeState("BotA") || abnormal.HasActiveScopes())
		return Fail("abnormal cleanup committed an unreturned damage call");

	Coordinator order(windowSeconds);
	auto outer = order.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		1.0, 100, DamageFrameKind::Override, true });
	auto inner = order.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		1.0, 100, DamageFrameKind::Canonical, true });
	if (order.ExitTakeDamage(outer.Token) != CoordinatorStatus::OutOfOrder ||
		order.ExitTakeDamage(inner.Token) != CoordinatorStatus::Accepted ||
		order.ExitTakeDamage(outer.Token) != CoordinatorStatus::Accepted)
		return Fail("out-of-order cleanup did not fail closed and remain recoverable");

	Coordinator landed(windowSeconds);
	if (!EnterEnemyDamage(landed, "BotA", 9.25, 100, 100, true))
		return Fail("Unreal landed contribution setup failed");
	auto landedSource = landed.EnterEnvironmentalSource({ "BotA",
		EnvironmentalSource::Landed, 10.0, true });
	KilledResult landedDeath = CompleteKilled(landed, "BotA", DeathKiller::None, 10.0);
	if (!landedDeath.Attribution ||
		landedDeath.Attribution->Attribution != Kind::RecentEnemyContributedEnvironmentalDeathProxy ||
		!landedDeath.Attribution->HadRecentEnemyMomentumContribution ||
		landedDeath.Source != EnvironmentalSource::Landed ||
		landed.FindLifeState("BotA"))
		return Fail("direct Unreal Landed death did not consume recent knockback attribution");
	if (landed.ExitEnvironmentalSource(landedSource.Token) != CoordinatorStatus::Accepted)
		return Fail("Unreal Landed source did not unwind");

	Coordinator unassistedLanded(windowSeconds);
	auto unassistedSource = unassistedLanded.EnterEnvironmentalSource({ "BotA",
		EnvironmentalSource::Landed, 10.0, true });
	KilledResult unassistedDeath = CompleteKilled(unassistedLanded, "BotA",
		DeathKiller::None, 10.0);
	if (!unassistedDeath.Attribution ||
		unassistedDeath.Attribution->Attribution != Kind::UnassistedEnvironmentalDeath)
		return Fail("direct Unreal Landed death without contribution was not unassisted");
	unassistedLanded.ExitEnvironmentalSource(unassistedSource.Token);

	Coordinator nestedKilled(windowSeconds);
	auto painOuter = nestedKilled.EnterEnvironmentalSource({ "BotA",
		EnvironmentalSource::PainTimer, 10.0, true });
	auto painInner = nestedKilled.EnterEnvironmentalSource({ "BotA",
		EnvironmentalSource::PainTimer, 10.0, true });
	auto killedOuter = nestedKilled.EnterKilled({ "BotA", DeathKiller::None, 10.0, true });
	auto killedInner = nestedKilled.EnterKilled({ "BotA", DeathKiller::None, 10.0, true });
	if (!killedOuter.IsOutermost || killedInner.IsOutermost)
		return Fail("nested Killed scopes did not identify the outer call");
	KilledResult ignoredInner = nestedKilled.ObserveKilledResult(killedInner.Token);
	if (ignoredInner.Attribution || nestedKilled.ExitKilled(killedInner.Token) != CoordinatorStatus::Accepted)
		return Fail("nested Killed call produced a duplicate classification");
	KilledResult outerResult = nestedKilled.ObserveKilledResult(killedOuter.Token);
	if (!outerResult.Attribution ||
		outerResult.Attribution->Attribution != Kind::UnassistedEnvironmentalDeath ||
		outerResult.Source != EnvironmentalSource::PainTimer ||
		nestedKilled.ExitKilled(killedOuter.Token) != CoordinatorStatus::Accepted)
		return Fail("outer Killed call did not classify its captured source");
	if (nestedKilled.ExitEnvironmentalSource(painInner.Token) != CoordinatorStatus::Accepted ||
		nestedKilled.ExitEnvironmentalSource(painOuter.Token) != CoordinatorStatus::Accepted)
		return Fail("nested environmental source depths did not unwind");

	Coordinator differentVictims(windowSeconds);
	auto killedA = differentVictims.EnterKilled({ "BotA", DeathKiller::SelfPlayer,
		10.0, true });
	auto killedB = differentVictims.EnterKilled({ "BotB", DeathKiller::EnemyPlayer,
		10.0, true });
	if (!killedA.IsOutermost || !killedB.IsOutermost)
		return Fail("synchronously nested Killed calls for different victims were conflated");
	KilledResult resultB = differentVictims.ObserveKilledResult(killedB.Token);
	if (!resultB.Attribution || resultB.Attribution->Attribution != Kind::DirectEnemyKill ||
		differentVictims.ExitKilled(killedB.Token) != CoordinatorStatus::Accepted)
		return Fail("nested different-victim Killed call was not classified");
	KilledResult resultA = differentVictims.ObserveKilledResult(killedA.Token);
	if (!resultA.Attribution || resultA.Attribution->Attribution != Kind::DirectSelfKill ||
		differentVictims.ExitKilled(killedA.Token) != CoordinatorStatus::Accepted ||
		differentVictims.HasActiveScopes())
		return Fail("outer different-victim Killed call did not resume after nested classification");

	Coordinator directPrecedence(windowSeconds);
	auto fallSource = directPrecedence.EnterEnvironmentalSource({ "BotA",
		EnvironmentalSource::TakeFallingDamage, 10.0, true });
	KilledResult directEnemy = CompleteKilled(directPrecedence, "BotA",
		DeathKiller::EnemyPlayer, 10.0);
	if (!directEnemy.Attribution || directEnemy.Attribution->Attribution != Kind::DirectEnemyKill)
		return Fail("direct enemy killer did not take precedence over fall context");
	directPrecedence.ExitEnvironmentalSource(fallSource.Token);

	Coordinator invalid(windowSeconds);
	auto badContract = invalid.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		1.0, 100, DamageFrameKind::Canonical, false });
	auto badTime = invalid.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		std::numeric_limits<double>::quiet_NaN(), 100, DamageFrameKind::Canonical, true });
	auto badSource = invalid.EnterEnvironmentalSource({ "BotA",
		EnvironmentalSource::Landed, 1.0, false });
	auto badKilled = invalid.EnterKilled({ "BotA", DeathKiller::None,
		std::numeric_limits<double>::infinity(), true });
	auto badKilledContract = invalid.EnterKilled({ "BotA", DeathKiller::None,
		1.0, false });
	auto negativeTime = invalid.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		-0.01, 100, DamageFrameKind::Canonical, true });
	if (badContract.Status != CoordinatorStatus::InvalidContract || badContract.Token ||
		badTime.Status != CoordinatorStatus::InvalidTime || badTime.Token ||
		badSource.Status != CoordinatorStatus::InvalidContract || badSource.Token ||
		badKilled.Status != CoordinatorStatus::InvalidTime || badKilled.Token ||
		badKilledContract.Status != CoordinatorStatus::InvalidContract || badKilledContract.Token ||
		negativeTime.Status != CoordinatorStatus::InvalidTime || negativeTime.Token ||
		invalid.HasActiveScopes())
		return Fail("contract/time rejection did not fail closed");
	KilledResult ambiguous = CompleteKilled(invalid, "BotA", DeathKiller::None, 2.0);
	if (!ambiguous.Attribution || ambiguous.Attribution->Attribution != Kind::AmbiguousDeath)
		return Fail("rejected environmental contract leaked environmental evidence");

	Coordinator invalidWindow(-1.0);
	if (invalidWindow.IsReady() ||
		invalidWindow.EnterKilled({ "BotA", DeathKiller::None, 1.0, true }).Status !=
			CoordinatorStatus::InvalidConfiguration)
		return Fail("invalid attribution window did not disable the coordinator");

	Coordinator regressed(windowSeconds);
	if (!EnterEnemyDamage(regressed, "BotA", 5.0, 100, 90, false))
		return Fail("clock-regression fixture setup failed");
	auto olderDamage = regressed.EnterTakeDamage({ "BotA", DamageInstigator::EnemyPlayer,
		4.0, 90, DamageFrameKind::Canonical, true });
	regressed.ObserveTakeDamageResult(olderDamage.Token, 80);
	if (regressed.ExitTakeDamage(olderDamage.Token) != CoordinatorStatus::InvalidTime ||
		regressed.FindLifeState("BotA")->LastEnemyContributionSeconds != 5.0)
		return Fail("regressed damage time overwrote newer contribution state");
	auto olderDeath = regressed.EnterKilled({ "BotA", DeathKiller::None, 4.5, true });
	if (regressed.ObserveKilledResult(olderDeath.Token).Status != CoordinatorStatus::InvalidTime ||
		regressed.ExitKilled(olderDeath.Token) != CoordinatorStatus::Accepted ||
		!regressed.FindLifeState("BotA"))
		return Fail("regressed death time classified or cleared current life state");

	Coordinator postMortem(windowSeconds);
	if (!EnterEnemyDamage(postMortem, "BotA", 1.0, 0, -10, true) ||
		postMortem.FindLifeState("BotA"))
		return Fail("post-mortem damage polluted the next life");

	return 0;
}
