#include "BotBenchmark/BotBenchmarkProtocol.h"
#include "BotBenchmark/BotBenchmarkTelemetry.h"

#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	const BotBenchmarkRunConfig config = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("7,4"), std::string("Loque,Tamerlane"));
	const std::string configId = BotBenchmarkTelemetryProtocol::ConfigIdentity(config);
	if (configId != "fnv1a64:b998eb71db26e88f")
		return Fail("bot benchmark v2 roster configuration identity changed");
	if (BotBenchmarkTelemetryProtocol::EventCap(config.GetMaxTicks()) != 74)
		return Fail("bot benchmark telemetry cap was not tied to max ticks");

	const std::string expectedManifest =
		"{\n"
		"  \"schema\": \"surreal-bot-benchmark-manifest-v2\",\n"
		"  \"driver\": \"bot-benchmark\",\n"
		"  \"config_id\": \"fnv1a64:b998eb71db26e88f\",\n"
		"  \"url\": \"DM-Test?Game=Botpack.DeathMatchPlus\",\n"
		"  \"output_directory\": \"evidence\",\n"
		"  \"seed\": \"18446744073709551615\",\n"
		"  \"max_ticks\": \"72\",\n"
		"  \"fixed_delta\": 0.020000000,\n"
		"  \"difficulty\": 7,\n"
		"  \"bot_count\": 2,\n"
		"  \"requested_roster\": [\n"
		"    {\"roster_index\": 0, \"requested_name\": \"Loque\", \"external_skill\": 7, \"identity_fragment\": \"participant-v1:index=0;external_skill=7;requested_name_hex=4c6f717565\"},\n"
		"    {\"roster_index\": 1, \"requested_name\": \"Tamerlane\", \"external_skill\": 4, \"identity_fragment\": \"participant-v1:index=1;external_skill=4;requested_name_hex=54616d65726c616e65\"}\n"
		"  ],\n"
		"  \"telemetry_event_cap\": \"74\",\n"
		"  \"death_attribution_recent_window_seconds\": 2.000000000,\n"
		"  \"suicides_exact_semantics\": \"legacy_scoreboard_self_or_nonplayer_killer\"\n"
		"}\n";
	if (BotBenchmarkTelemetryProtocol::ManifestJson(config) != expectedManifest)
		return Fail("bot benchmark v2 manifest serialization was not exact");

	const BotBenchmarkRunConfig changedRoster = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "other-output", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("4,7"), std::string("Loque,Tamerlane"));
	if (BotBenchmarkTelemetryProtocol::ConfigIdentity(changedRoster) == configId)
		return Fail("config identity did not bind ordered per-bot skills");
	const BotBenchmarkRunConfig sameRosterOtherOutput = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "other-output", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("7,4"), std::string("Loque,Tamerlane"));
	if (BotBenchmarkTelemetryProtocol::ConfigIdentity(sameRosterOtherOutput) != configId)
		return Fail("config identity incorrectly included output directory");
	const BotBenchmarkRunConfig changedName = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("7,4"), std::string("Xan,Tamerlane"));
	if (BotBenchmarkTelemetryProtocol::ConfigIdentity(changedName) == configId)
		return Fail("config identity did not bind ordered requested names");

	BotBenchmarkBotState second;
	second.Identity = "pri:2";
	second.Actor = "Bot2";
	second.PlayerName = "B\"ot";
	second.ClassName = "Botpack.Bot";
	second.State = "Roaming";
	second.PositionX = 1.25;
	second.PositionY = -2.5;
	second.PositionZ = -0.0;
	second.VelocityZ = 3.0;
	second.PhysicsMode = "Walking";
	second.LatentAction = "MoveToward";
	second.AccelerationX = 100.0;
	second.AccelerationY = -50.0;
	second.DestinationX = 512.0;
	second.DestinationY = 256.0;
	second.DestinationZ = -32.0;
	second.MoveTimer = 0.75;
	second.MoveTargetIdentity = "actor:PathNode3";
	second.MoveTargetName = "Path\"Node";
	second.Health = 87;
	second.Score = 2.5;
	second.PriDeaths = 3.0;
	second.MovementIntent = true;
	second.InHazardZone = true;
	second.KillsExact = 4;
	second.DeathsExact = 3;
	second.SuicidesExact = 2;
	second.EnvironmentalDeathsExact = 1;
	second.HazardExposedDeathsProxy = 1;
	second.DirectEnemyKills = 1;
	second.UnassistedEnvironmentalDeaths = 1;
	second.RecentEnemyContributedEnvironmentalDeathsProxy = 1;
	second.RecentEnemyMomentumContributedEnvironmentalDeathsProxy = 1;
	second.HitWallEventsExact = 9;
	second.PainLedgeVetoesExact = 8;
	second.PainLedgeRepeatVetoesExact = 6;
	second.PainLedgeRecoveryAttemptsExact = 7;
	second.PainLedgeRecoveryEscapesExact = 5;
	second.WallAdjustCallsExact = 12;
	second.WallAdjustRepeatsExact = 8;
	second.WallAdjustRecoveryAttemptsExact = 7;
	second.WallAdjustRecoverySuccessesExact = 6;
	second.WallAdjustForcedReplansExact = 2;
	second.MoveStallDetectionsExact = 3;
	second.MoveStallEpisodeResetsExact = 1;
	second.MoveStallForcedReplansExact = 2;
	second.MoveStallNavigationForcedReplansExact = 1;
	second.MoveStallTargetlessMoveToTimeoutsExact = 1;
	second.MoveStallEligibleSeconds = 4.25;
	second.FailedNavigationAvoidanceActivationsExact = 2;
	second.FailedNavigationSafeguardSuppressionsExact = 3;
	second.FailedNavigationRoutePenaltyApplicationsExact = 4;
	second.FallingSeamDetectionsExact = 11;
	second.HorizontalCornerCandidateProbesExact = 10;
	second.HorizontalCornerAuthorizedEscapesExact = 3;
	second.HorizontalCornerTargetProgressRejectsExact = 4;
	second.HorizontalCornerUnknownOrUnsafeSupportExact = 3;
	second.FallingSeamEpisodesExact = 4;
	second.FallingSeamInvalidGeometryRejectsExact = 7;
	second.FallingSeamAuthorizableEpisodesExact = 2;
	second.HorizontalCornerAuthorizedCandidatesExact = 3;
	second.HorizontalCornerBlockedSweepCandidatesExact = 2;
	second.HorizontalCornerNoStaticWalkableSupportCandidatesExact = 1;
	second.HorizontalCornerPainSupportCandidatesExact = 1;
	second.HorizontalCornerNoActiveMovementIntentOrTargetCandidatesExact = 1;
	second.HorizontalCornerTrueTargetRegressionCandidatesExact = 1;
	second.HorizontalCornerUnknownEvidenceCandidatesExact = 1;

	BotBenchmarkBotState first;
	first.Identity = "pri:1";
	first.Actor = "Bot1";
	first.PlayerName = "Line\nBreak";
	first.ClassName = "Botpack.Bot";
	first.State = "Attacking";
	first.Health = 100;

	BotBenchmarkTelemetryEvent event;
	event.Sequence = 5;
	event.Tick = 4;
	event.SimulatedSeconds = 0.08;
	event.Type = "tick";
	event.Map = "DM-\"Test";
	event.Status = "running";
	event.Bots = { second, first };
	std::ostringstream walkingPreflightSuffix;
	walkingPreflightSuffix
		<< ",\"walking_step_preflight_observations_exact\":\"0\""
		<< ",\"walking_step_preflight_unsupported_endpoints_exact\":\"0\""
		<< ",\"walking_step_preflight_no_decisions_exact\":\"0\""
		<< ",\"walking_step_preflight_provisional_authorizations_exact\":\"0\""
		<< ",\"walking_step_preflight_post_mayfall_confirmed_authorizations_exact\":\"0\""
		<< ",\"walking_step_preflight_authorizable_episodes_exact\":\"0\"";
	for (size_t index = 0; index < PawnMovement::WalkingStepPreflightReasonCount; index++)
	{
		walkingPreflightSuffix << ",\""
			<< PawnMovement::WalkingStepPreflightReasonMetricName(
				static_cast<PawnMovement::WalkingStepPreflightReason>(index))
			<< "\":\"0\"";
	}
	walkingPreflightSuffix
		<< ",\"walking_step_preflight_diagnostic_overflows_exact\":\"0\""
		<< ",\"walking_step_preflight_diagnostics\":[]"
		<< ",\"falling_parity_realized_episodes_exact\":\"0\""
		<< ",\"falling_parity_realized_steps_exact\":\"0\""
		<< ",\"falling_parity_realized_matched_steps_exact\":\"0\""
		<< ",\"falling_parity_realized_mismatches_exact\":\"0\""
		<< ",\"falling_parity_realized_unknowns_exact\":\"0\""
		<< ",\"falling_parity_realized_callback_barriers_exact\":\"0\""
		<< ",\"falling_parity_realized_pain_entries_exact\":\"0\""
		<< ",\"falling_parity_realized_deaths_exact\":\"0\""
		<< ",\"falling_parity_realized_landings_exact\":\"0\""
		<< ",\"falling_parity_realized_continuity_losses_exact\":\"0\""
		<< ",\"falling_parity_realized_record_overflows_exact\":\"0\""
		<< ",\"falling_parity_realized_records\":[]";
	const std::string expectedWalkingPreflightSuffix = walkingPreflightSuffix.str();
	const std::string expectedEvent =
		"{\"schema\":\"surreal-bot-benchmark-telemetry-v2\",\"seq\":\"5\",\"config_id\":\"fnv1a64:b998eb71db26e88f\",\"tick\":\"4\",\"simulated_seconds\":0.080000000,\"type\":\"tick\",\"map\":\"DM-\\\"Test\",\"status\":\"running\",\"failure_reason\":\"\",\"bots\":["
		"{\"identity\":\"pri:1\",\"actor\":\"Bot1\",\"player_name\":\"Line\\nBreak\",\"class\":\"Botpack.Bot\",\"position\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"velocity\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"physics_mode\":\"\",\"latent_action\":\"\",\"acceleration\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"destination\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"move_timer\":0.000000,\"move_target_identity\":\"\",\"move_target_name\":\"\",\"health\":100,\"score\":0.000000,\"pri_deaths\":0.000000,\"movement_intent\":false,\"in_hazard_zone\":false,\"kills_exact\":\"0\",\"deaths_exact\":\"0\",\"suicides_exact\":\"0\",\"environmental_deaths_exact\":\"0\",\"hazard_exposed_deaths_proxy\":\"0\",\"direct_self_kills\":\"0\",\"direct_enemy_kills\":\"0\",\"unassisted_environmental_deaths\":\"0\",\"recent_enemy_contributed_environmental_deaths_proxy\":\"0\",\"ambiguous_deaths\":\"0\",\"recent_enemy_momentum_contributed_environmental_deaths_proxy\":\"0\",\"hit_wall_events_exact\":\"0\",\"pain_ledge_vetoes_exact\":\"0\",\"pain_ledge_repeat_vetoes_exact\":\"0\",\"pain_ledge_recovery_attempts_exact\":\"0\",\"pain_ledge_recovery_escapes_exact\":\"0\",\"wall_adjust_calls_exact\":\"0\",\"wall_adjust_repeats_exact\":\"0\",\"wall_adjust_recovery_attempts_exact\":\"0\",\"wall_adjust_recovery_successes_exact\":\"0\",\"wall_adjust_forced_replans_exact\":\"0\",\"move_stall_detections_exact\":\"0\",\"move_stall_episode_resets_exact\":\"0\",\"move_stall_forced_replans_exact\":\"0\",\"move_stall_navigation_forced_replans_exact\":\"0\",\"move_stall_targetless_move_to_timeouts_exact\":\"0\",\"move_stall_eligible_seconds\":0.000000000,\"failed_navigation_avoidance_activations_exact\":\"0\",\"failed_navigation_safeguard_suppressions_exact\":\"0\",\"failed_navigation_route_penalty_applications_exact\":\"0\",\"falling_seam_detections_exact\":\"0\",\"horizontal_corner_candidate_probes_exact\":\"0\",\"horizontal_corner_authorized_escapes_exact\":\"0\",\"horizontal_corner_target_progress_rejects_exact\":\"0\",\"horizontal_corner_unknown_or_unsafe_support_exact\":\"0\",\"falling_seam_episodes_exact\":\"0\",\"falling_seam_invalid_geometry_rejects_exact\":\"0\",\"falling_seam_authorizable_episodes_exact\":\"0\",\"horizontal_corner_authorized_candidates_exact\":\"0\",\"horizontal_corner_blocked_sweep_candidates_exact\":\"0\",\"horizontal_corner_no_static_walkable_support_candidates_exact\":\"0\",\"horizontal_corner_pain_support_candidates_exact\":\"0\",\"horizontal_corner_no_active_movement_intent_or_target_candidates_exact\":\"0\",\"horizontal_corner_true_target_regression_candidates_exact\":\"0\",\"horizontal_corner_unknown_evidence_candidates_exact\":\"0\""
		+ expectedWalkingPreflightSuffix + ",\"state\":\"Attacking\"},"
		"{\"identity\":\"pri:2\",\"actor\":\"Bot2\",\"player_name\":\"B\\\"ot\",\"class\":\"Botpack.Bot\",\"position\":{\"x\":1.250000,\"y\":-2.500000,\"z\":0.000000},\"velocity\":{\"x\":0.000000,\"y\":0.000000,\"z\":3.000000},\"physics_mode\":\"Walking\",\"latent_action\":\"MoveToward\",\"acceleration\":{\"x\":100.000000,\"y\":-50.000000,\"z\":0.000000},\"destination\":{\"x\":512.000000,\"y\":256.000000,\"z\":-32.000000},\"move_timer\":0.750000,\"move_target_identity\":\"actor:PathNode3\",\"move_target_name\":\"Path\\\"Node\",\"health\":87,\"score\":2.500000,\"pri_deaths\":3.000000,\"movement_intent\":true,\"in_hazard_zone\":true,\"kills_exact\":\"4\",\"deaths_exact\":\"3\",\"suicides_exact\":\"2\",\"environmental_deaths_exact\":\"1\",\"hazard_exposed_deaths_proxy\":\"1\",\"direct_self_kills\":\"0\",\"direct_enemy_kills\":\"1\",\"unassisted_environmental_deaths\":\"1\",\"recent_enemy_contributed_environmental_deaths_proxy\":\"1\",\"ambiguous_deaths\":\"0\",\"recent_enemy_momentum_contributed_environmental_deaths_proxy\":\"1\",\"hit_wall_events_exact\":\"9\",\"pain_ledge_vetoes_exact\":\"8\",\"pain_ledge_repeat_vetoes_exact\":\"6\",\"pain_ledge_recovery_attempts_exact\":\"7\",\"pain_ledge_recovery_escapes_exact\":\"5\",\"wall_adjust_calls_exact\":\"12\",\"wall_adjust_repeats_exact\":\"8\",\"wall_adjust_recovery_attempts_exact\":\"7\",\"wall_adjust_recovery_successes_exact\":\"6\",\"wall_adjust_forced_replans_exact\":\"2\",\"move_stall_detections_exact\":\"3\",\"move_stall_episode_resets_exact\":\"1\",\"move_stall_forced_replans_exact\":\"2\",\"move_stall_navigation_forced_replans_exact\":\"1\",\"move_stall_targetless_move_to_timeouts_exact\":\"1\",\"move_stall_eligible_seconds\":4.250000000,\"failed_navigation_avoidance_activations_exact\":\"2\",\"failed_navigation_safeguard_suppressions_exact\":\"3\",\"failed_navigation_route_penalty_applications_exact\":\"4\",\"falling_seam_detections_exact\":\"11\",\"horizontal_corner_candidate_probes_exact\":\"10\",\"horizontal_corner_authorized_escapes_exact\":\"3\",\"horizontal_corner_target_progress_rejects_exact\":\"4\",\"horizontal_corner_unknown_or_unsafe_support_exact\":\"3\",\"falling_seam_episodes_exact\":\"4\",\"falling_seam_invalid_geometry_rejects_exact\":\"7\",\"falling_seam_authorizable_episodes_exact\":\"2\",\"horizontal_corner_authorized_candidates_exact\":\"3\",\"horizontal_corner_blocked_sweep_candidates_exact\":\"2\",\"horizontal_corner_no_static_walkable_support_candidates_exact\":\"1\",\"horizontal_corner_pain_support_candidates_exact\":\"1\",\"horizontal_corner_no_active_movement_intent_or_target_candidates_exact\":\"1\",\"horizontal_corner_true_target_regression_candidates_exact\":\"1\",\"horizontal_corner_unknown_evidence_candidates_exact\":\"1\""
		+ expectedWalkingPreflightSuffix + ",\"state\":\"Roaming\"}]}\n";
	if (BotBenchmarkTelemetryProtocol::EventJson(configId, event) != expectedEvent)
		return Fail("v2 telemetry event ordering, formatting, or escaping changed");

	PawnMovement::WalkingStepPreflightDiagnosticRecord diagnostic;
	diagnostic.SourcePawnActor = "Bot\"17";
	diagnostic.Sequence = 7;
	diagnostic.LifeGeneration = 2;
	diagnostic.InvocationToken = 9;
	diagnostic.WalkingIteration = 1;
	diagnostic.Phase = "post_mayfall_confirmation";
	diagnostic.TransitionOutcome = "begin_falling";
	diagnostic.Reason = PawnMovement::WalkingStepPreflightReason::HarmfulPainFall;
	diagnostic.PrecommitOrigin = vec3(1.0f, 2.0f, 3.0f);
	diagnostic.PredictedUnsupportedEndpoint = vec3(4.0f, 5.0f, 6.0f);
	diagnostic.ActualUnsupportedEndpoint = vec3(4.0f, 5.0f, 6.0f);
	diagnostic.SemanticTarget = "Bullet\"Box4";
	diagnostic.FallForecastAttempted = true;
	diagnostic.FallForecastOrigin = vec3(7.0f, 8.0f, 9.0f);
	diagnostic.FallForecastVelocity = vec3(10.0f, 11.0f, 12.0f);
	diagnostic.FallForecastAcceleration = vec3(13.0f, 14.0f, 15.0f);
	diagnostic.FallForecastGravityKnown = true;
	diagnostic.FallForecastGravity = vec3(0.0f, 0.0f, -950.0f);
	diagnostic.FallForecast.Complete = true;
	diagnostic.FallForecast.TotalDrop = 128.0f;
	diagnostic.FallForecast.Landing.Collision =
		PawnMovement::WalkingStepCollisionKind::StaticBsp;
	diagnostic.FallForecast.Landing.Zone = PawnMovement::WalkingStepZoneKind::Pain;
	diagnostic.FallHitFractions[0] = 0.5f;
	diagnostic.FallHitCount = 1;
	PawnMovement::WalkingStepPreflightDiagnosticRecord provisionalDiagnostic = diagnostic;
	provisionalDiagnostic.Sequence = 6;
	provisionalDiagnostic.Phase = "precommit_provisional";
	event.Bots.front().WalkingStepPreflightDiagnostics.push_back(provisionalDiagnostic);
	event.Bots.front().WalkingStepPreflightDiagnostics.push_back(diagnostic);
	const std::string diagnosticEvent =
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	const std::string correlationKey =
		"\"source_pawn_actor\":\"Bot\\\"17\",\"sequence\":";
	const std::string transactionKey =
		"\"life_generation\":\"2\",\"invocation_token\":\"9\",\"walking_iteration\":1";
	const size_t firstCorrelation = diagnosticEvent.find(correlationKey);
	const size_t secondCorrelation = firstCorrelation == std::string::npos
		? std::string::npos : diagnosticEvent.find(correlationKey,
			firstCorrelation + correlationKey.size());
	const size_t firstTransaction = diagnosticEvent.find(transactionKey);
	const size_t secondTransaction = firstTransaction == std::string::npos
		? std::string::npos : diagnosticEvent.find(transactionKey,
			firstTransaction + transactionKey.size());
	if (firstCorrelation == std::string::npos || secondCorrelation == std::string::npos
		|| firstTransaction == std::string::npos || secondTransaction == std::string::npos
		|| diagnosticEvent.find("\"phase\":\"post_mayfall_confirmation\"") == std::string::npos
		|| diagnosticEvent.find("\"semantic_target\":\"Bullet\\\"Box4\"") == std::string::npos
		|| diagnosticEvent.find("\"fall_forecast\":{\"attempted\":true,\"origin\":{\"x\":7.000000,\"y\":8.000000,\"z\":9.000000},\"velocity\":{\"x\":10.000000,\"y\":11.000000,\"z\":12.000000},\"acceleration\":{\"x\":13.000000,\"y\":14.000000,\"z\":15.000000},\"gravity_known\":true,\"gravity\":{\"x\":0.000000,\"y\":0.000000,\"z\":-950.000000}") == std::string::npos
		|| diagnosticEvent.find("\"hit_fractions\":[0.500000000]") == std::string::npos)
		return Fail("walking-step preflight diagnostic serialization was incomplete or unstable");
	event.Bots.front().WalkingStepPreflightDiagnostics.clear();

	PawnMovement::FallingParityRealizedRecord parityRecord;
	parityRecord.Correlation.SourcePawnActor = "Necroth";
	parityRecord.Correlation.LifeGeneration = 2;
	parityRecord.Correlation.InvocationToken = 9;
	parityRecord.Correlation.WalkingIteration = 1;
	parityRecord.StepOrdinal = 10;
	parityRecord.Outcome = PawnMovement::FallingParityRealizedOutcome::CallbackBarrier;
	parityRecord.Elapsed = 1.0f / 60.0f;
	parityRecord.Collision = PawnMovement::FallingParityCollisionKind::StaticWorld;
	parityRecord.HitFraction = 0.25f;
	parityRecord.HitNormal = vec3(0.0f, 1.0f, 0.0f);
	parityRecord.CallbackBarrierMask = 1u << 8;
	event.Bots.front().FallingParityRealizedRecords.push_back(parityRecord);
	const std::string parityEvent =
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	if (parityEvent.find("\"source_pawn_actor\":\"Necroth\",\"life_generation\":\"2\",\"invocation_token\":\"9\",\"walking_iteration\":1") == std::string::npos
		|| parityEvent.find("\"step_ordinal\":\"10\",\"outcome\":\"callback_barrier\"") == std::string::npos
		|| parityEvent.find("\"collision\":\"static_world\",\"hit_fraction\":0.250000000") == std::string::npos
		|| parityEvent.find("\"callback_barrier_mask\":\"256\"") == std::string::npos)
		return Fail("falling parity realized record serialization was incomplete or unstable");
	event.Bots.front().FallingParityRealizedRecords.clear();

	bool rejectedNonFinite = false;
	try
	{
		event.Bots.front().VelocityX = std::numeric_limits<double>::infinity();
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	}
	catch (const std::invalid_argument&)
	{
		rejectedNonFinite = true;
	}
	if (!rejectedNonFinite)
		return Fail("bot benchmark telemetry accepted a non-finite number");

	rejectedNonFinite = false;
	try
	{
		event.Bots.front().VelocityX = 0.0;
		event.Bots.front().MoveTimer = std::numeric_limits<double>::quiet_NaN();
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	}
	catch (const std::invalid_argument&)
	{
		rejectedNonFinite = true;
	}
	if (!rejectedNonFinite)
		return Fail("bot benchmark telemetry accepted a non-finite diagnostic number");

	rejectedNonFinite = false;
	try
	{
		event.Bots.front().MoveTimer = 0.0;
		event.Bots.front().MoveStallEligibleSeconds = std::numeric_limits<double>::infinity();
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	}
	catch (const std::invalid_argument&)
	{
		rejectedNonFinite = true;
	}
	if (!rejectedNonFinite)
		return Fail("bot benchmark telemetry accepted non-finite move-stall eligible seconds");

	return 0;
}
