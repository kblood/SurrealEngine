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
	if (configId != "fnv1a64:dd3bba32ac6fbe9d")
		return Fail("bot benchmark v2 roster configuration identity changed");
	if (config.IsHarmfulZoneEscapeEnabled()
		|| config.IsWalkingPreflightPositiveDpsVetoEnabled()
		|| config.IsHazardSwimEgressEnabled()
		|| config.IsHazardSwimEgressLiveEnabled()
		|| config.IsFailedNavigationAvoidanceEnabled()
		|| config.IsFallingHazardRecoveryEnabled()
		|| config.IsFallingHazardRecoveryLiveEnabled())
		return Fail("bot benchmark experimental controls must default to disabled");
	const BotBenchmarkRunConfig controlEnabled = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("7,4"), std::string("Loque,Tamerlane"), std::string("1"));
	if (!controlEnabled.IsHarmfulZoneEscapeEnabled()
		|| BotBenchmarkTelemetryProtocol::ConfigIdentity(controlEnabled) == configId)
		return Fail("harmful-zone escape selection was not distinct and enabled");
	const BotBenchmarkRunConfig avoidanceEnabled = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("7,4"), std::string("Loque,Tamerlane"), {}, {}, {}, {},
		std::string("1"));
	if (!avoidanceEnabled.IsFailedNavigationAvoidanceEnabled()
		|| BotBenchmarkTelemetryProtocol::ConfigIdentity(avoidanceEnabled) == configId)
		return Fail("failed-navigation avoidance selection was not distinct and enabled");
	const BotBenchmarkRunConfig fallingRecoveryEnabled = BotBenchmarkRunConfig::Parse(
		"DM-Test?Game=Botpack.DeathMatchPlus", "evidence", "18446744073709551615", "72", "0.02", "7",
		std::string("2"), std::string("7,4"), std::string("Loque,Tamerlane"), {}, {}, {}, {}, {},
		std::string("1"), std::string("1"));
	if (!fallingRecoveryEnabled.IsFallingHazardRecoveryEnabled()
		|| !fallingRecoveryEnabled.IsFallingHazardRecoveryLiveEnabled()
		|| BotBenchmarkTelemetryProtocol::ConfigIdentity(fallingRecoveryEnabled) == configId)
		return Fail("falling-hazard recovery selection was not distinct and enabled");
	const BotBenchmarkRunSummary summary("complete", 0, 72, 1.44, "Unreal Tournament",
		"436", "DM-Test", "", {});
	if (summary.ToJson(controlEnabled).find("\"harmful_zone_escape_enabled\": true")
		== std::string::npos)
		return Fail("bot benchmark summary did not retain the enabled control mode");
	if (summary.ToJson(fallingRecoveryEnabled).find("\"falling_hazard_recovery_live_enabled\": true")
		== std::string::npos)
		return Fail("bot benchmark summary did not retain falling-hazard recovery mode");
	bool rejectedInvalidControl = false;
	try
	{
		BotBenchmarkRunConfig::Parse("", "", "", "", "", "", {}, {}, {}, std::string("true"));
	}
	catch (const std::invalid_argument&)
	{
		rejectedInvalidControl = true;
	}
	if (!rejectedInvalidControl)
		return Fail("harmful-zone escape accepted a non-exact flag value");
	if (BotBenchmarkTelemetryProtocol::EventCap(config.GetMaxTicks()) != 74)
		return Fail("bot benchmark telemetry cap was not tied to max ticks");

	const std::string expectedManifest =
		"{\n"
		"  \"schema\": \"surreal-bot-benchmark-manifest-v2\",\n"
		"  \"driver\": \"bot-benchmark\",\n"
		"  \"config_id\": \"fnv1a64:dd3bba32ac6fbe9d\",\n"
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
		"  \"harmful_zone_escape_enabled\": false,\n"
		"  \"walking_preflight_positive_dps_veto_enabled\": false,\n"
		"  \"hazard_swim_egress_enabled\": false,\n"
		"  \"hazard_swim_egress_live_enabled\": false,\n"
		"  \"failed_navigation_avoidance_enabled\": false,\n"
		"  \"falling_hazard_recovery_enabled\": false,\n"
		"  \"falling_hazard_recovery_live_enabled\": false,\n"
		"  \"death_attribution_recent_window_seconds\": 2.000000000,\n"
		"  \"suicides_exact_semantics\": \"legacy_scoreboard_self_or_nonplayer_killer\"\n"
		"}\n";
	if (BotBenchmarkTelemetryProtocol::ManifestJson(config) != expectedManifest)
		return Fail("bot benchmark v2 manifest serialization was not exact");
	if (summary.ToJson(config).find("\"harmful_zone_escape_enabled\": false")
		== std::string::npos)
		return Fail("bot benchmark summary did not retain the default-off control mode");

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
		<< ",\"walking_step_preflight_authorizable_episodes_exact\":\"0\""
		<< ",\"walking_step_preflight_positive_dps_veto_eligible_exact\":\"0\""
		<< ",\"walking_step_preflight_positive_dps_veto_applied_exact\":\"0\""
		<< ",\"walking_step_preflight_positive_dps_veto_debounced_exact\":\"0\""
		<< ",\"walking_step_preflight_positive_dps_veto_forced_replans_exact\":\"0\""
		<< ",\"walking_step_preflight_positive_dps_veto_rollback_rejected_exact\":\"0\"";
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
		<< ",\"walking_step_preflight_positive_dps_veto_action_overflows_exact\":\"0\""
		<< ",\"walking_step_preflight_positive_dps_veto_actions\":[]"
		<< ",\"falling_parity_realized_episodes_exact\":\"0\""
		<< ",\"falling_parity_realized_steps_exact\":\"0\""
		<< ",\"falling_parity_realized_matched_steps_exact\":\"0\""
		<< ",\"falling_parity_realized_matched_landing_steps_exact\":\"0\""
		<< ",\"falling_parity_realized_mismatches_exact\":\"0\""
		<< ",\"falling_parity_realized_unknowns_exact\":\"0\""
		<< ",\"falling_parity_realized_callback_barriers_exact\":\"0\""
		<< ",\"falling_parity_realized_pain_entries_exact\":\"0\""
		<< ",\"falling_parity_realized_deaths_exact\":\"0\""
		<< ",\"falling_parity_realized_landings_exact\":\"0\""
		<< ",\"falling_parity_realized_continuity_losses_exact\":\"0\""
		<< ",\"falling_parity_realized_record_overflows_exact\":\"0\""
		<< ",\"falling_parity_realized_records\":[]"
		<< ",\"vertical_pain_column_episodes_started_exact\":\"0\""
		<< ",\"vertical_pain_column_episodes_completed_exact\":\"0\""
		<< ",\"vertical_pain_column_true_positive_outcomes_exact\":\"0\""
		<< ",\"vertical_pain_column_false_positive_outcomes_exact\":\"0\""
		<< ",\"vertical_pain_column_false_negative_outcomes_exact\":\"0\""
		<< ",\"vertical_pain_column_true_negative_outcomes_exact\":\"0\""
		<< ",\"vertical_pain_column_ambiguous_outcomes_exact\":\"0\""
		<< ",\"vertical_pain_column_unknown_outcomes_exact\":\"0\""
		<< ",\"vertical_pain_column_diagnostic_overflows_exact\":\"0\""
		<< ",\"vertical_pain_column_generation_capacity_exhaustions_exact\":\"0\""
		<< ",\"persistent_harmful_fall_candidates_started_exact\":\"0\""
		<< ",\"persistent_harmful_fall_promotions_exact\":\"0\""
		<< ",\"persistent_harmful_fall_resets_exact\":\"0\""
		<< ",\"persistent_harmful_fall_confirmed_harmful_entries_exact\":\"0\""
		<< ",\"persistent_harmful_fall_observed_lead_samples_exact\":\"0\""
		<< ",\"persistent_harmful_fall_observed_lead_milliseconds_exact\":\"0\""
		<< ",\"single_harmful_fall_prefix_candidates_started_exact\":\"0\""
		<< ",\"single_harmful_fall_prefix_promotions_exact\":\"0\""
		<< ",\"single_harmful_fall_prefix_resets_exact\":\"0\""
		<< ",\"single_harmful_fall_prefix_confirmed_harmful_entries_exact\":\"0\""
		<< ",\"single_harmful_fall_prefix_observed_lead_samples_exact\":\"0\""
		<< ",\"single_harmful_fall_prefix_observed_lead_milliseconds_exact\":\"0\""
		<< ",\"damage_taken_exact\":\"0\""
		<< ",\"damage_taken_from_other_participants_exact\":\"0\""
		<< ",\"damage_taken_from_self_exact\":\"0\""
		<< ",\"damage_taken_from_nonparticipants_exact\":\"0\""
		<< ",\"damage_dealt_to_other_participants_exact\":\"0\""
		<< ",\"confirmed_pickups_exact\":\"0\""
		<< ",\"confirmed_weapon_pickups_exact\":\"0\""
		<< ",\"confirmed_ammo_pickups_exact\":\"0\""
		<< ",\"confirmed_health_pickups_exact\":\"0\""
		<< ",\"confirmed_armor_pickups_exact\":\"0\""
		<< ",\"confirmed_other_pickups_exact\":\"0\""
		<< ",\"pickup_source_consumed_unconfirmed_exact\":\"0\""
		<< ",\"navigation_coverage_visited_nodes_exact\":\"0\""
		<< ",\"navigation_coverage_catalog_nodes_exact\":\"0\""
		<< ",\"navigation_coverage_union_visited_nodes_exact\":\"0\""
		<< ",\"vertical_pain_column_diagnostics\":[]";
	const std::string expectedWalkingPreflightSuffix = walkingPreflightSuffix.str();
	std::string expectedEvent =
		"{\"schema\":\"surreal-bot-benchmark-telemetry-v2\",\"seq\":\"5\",\"config_id\":\"fnv1a64:dd3bba32ac6fbe9d\",\"tick\":\"4\",\"simulated_seconds\":0.080000000,\"type\":\"tick\",\"map\":\"DM-\\\"Test\",\"status\":\"running\",\"failure_reason\":\"\",\"bots\":["
		"{\"identity\":\"pri:1\",\"actor\":\"Bot1\",\"player_name\":\"Line\\nBreak\",\"class\":\"Botpack.Bot\",\"position\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"velocity\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"physics_mode\":\"\",\"latent_action\":\"\",\"acceleration\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"destination\":{\"x\":0.000000,\"y\":0.000000,\"z\":0.000000},\"move_timer\":0.000000,\"move_target_identity\":\"\",\"move_target_name\":\"\",\"health\":100,\"score\":0.000000,\"pri_deaths\":0.000000,\"movement_intent\":false,\"in_hazard_zone\":false,\"kills_exact\":\"0\",\"deaths_exact\":\"0\",\"suicides_exact\":\"0\",\"environmental_deaths_exact\":\"0\",\"hazard_exposed_deaths_proxy\":\"0\",\"direct_self_kills\":\"0\",\"direct_enemy_kills\":\"0\",\"unassisted_environmental_deaths\":\"0\",\"recent_enemy_contributed_environmental_deaths_proxy\":\"0\",\"ambiguous_deaths\":\"0\",\"recent_enemy_momentum_contributed_environmental_deaths_proxy\":\"0\",\"hit_wall_events_exact\":\"0\",\"pain_ledge_vetoes_exact\":\"0\",\"pain_ledge_repeat_vetoes_exact\":\"0\",\"pain_ledge_recovery_attempts_exact\":\"0\",\"pain_ledge_recovery_escapes_exact\":\"0\",\"wall_adjust_calls_exact\":\"0\",\"wall_adjust_repeats_exact\":\"0\",\"wall_adjust_recovery_attempts_exact\":\"0\",\"wall_adjust_recovery_successes_exact\":\"0\",\"wall_adjust_forced_replans_exact\":\"0\",\"move_stall_detections_exact\":\"0\",\"move_stall_episode_resets_exact\":\"0\",\"move_stall_forced_replans_exact\":\"0\",\"move_stall_navigation_forced_replans_exact\":\"0\",\"move_stall_targetless_move_to_timeouts_exact\":\"0\",\"move_stall_eligible_seconds\":0.000000000,\"failed_navigation_avoidance_activations_exact\":\"0\",\"failed_navigation_safeguard_suppressions_exact\":\"0\",\"failed_navigation_route_penalty_applications_exact\":\"0\",\"falling_seam_detections_exact\":\"0\",\"horizontal_corner_candidate_probes_exact\":\"0\",\"horizontal_corner_authorized_escapes_exact\":\"0\",\"horizontal_corner_target_progress_rejects_exact\":\"0\",\"horizontal_corner_unknown_or_unsafe_support_exact\":\"0\",\"falling_seam_episodes_exact\":\"0\",\"falling_seam_invalid_geometry_rejects_exact\":\"0\",\"falling_seam_authorizable_episodes_exact\":\"0\",\"horizontal_corner_authorized_candidates_exact\":\"0\",\"horizontal_corner_blocked_sweep_candidates_exact\":\"0\",\"horizontal_corner_no_static_walkable_support_candidates_exact\":\"0\",\"horizontal_corner_pain_support_candidates_exact\":\"0\",\"horizontal_corner_no_active_movement_intent_or_target_candidates_exact\":\"0\",\"horizontal_corner_true_target_regression_candidates_exact\":\"0\",\"horizontal_corner_unknown_evidence_candidates_exact\":\"0\""
		+ expectedWalkingPreflightSuffix + ",\"state\":\"Attacking\"},"
		"{\"identity\":\"pri:2\",\"actor\":\"Bot2\",\"player_name\":\"B\\\"ot\",\"class\":\"Botpack.Bot\",\"position\":{\"x\":1.250000,\"y\":-2.500000,\"z\":0.000000},\"velocity\":{\"x\":0.000000,\"y\":0.000000,\"z\":3.000000},\"physics_mode\":\"Walking\",\"latent_action\":\"MoveToward\",\"acceleration\":{\"x\":100.000000,\"y\":-50.000000,\"z\":0.000000},\"destination\":{\"x\":512.000000,\"y\":256.000000,\"z\":-32.000000},\"move_timer\":0.750000,\"move_target_identity\":\"actor:PathNode3\",\"move_target_name\":\"Path\\\"Node\",\"health\":87,\"score\":2.500000,\"pri_deaths\":3.000000,\"movement_intent\":true,\"in_hazard_zone\":true,\"kills_exact\":\"4\",\"deaths_exact\":\"3\",\"suicides_exact\":\"2\",\"environmental_deaths_exact\":\"1\",\"hazard_exposed_deaths_proxy\":\"1\",\"direct_self_kills\":\"0\",\"direct_enemy_kills\":\"1\",\"unassisted_environmental_deaths\":\"1\",\"recent_enemy_contributed_environmental_deaths_proxy\":\"1\",\"ambiguous_deaths\":\"0\",\"recent_enemy_momentum_contributed_environmental_deaths_proxy\":\"1\",\"hit_wall_events_exact\":\"9\",\"pain_ledge_vetoes_exact\":\"8\",\"pain_ledge_repeat_vetoes_exact\":\"6\",\"pain_ledge_recovery_attempts_exact\":\"7\",\"pain_ledge_recovery_escapes_exact\":\"5\",\"wall_adjust_calls_exact\":\"12\",\"wall_adjust_repeats_exact\":\"8\",\"wall_adjust_recovery_attempts_exact\":\"7\",\"wall_adjust_recovery_successes_exact\":\"6\",\"wall_adjust_forced_replans_exact\":\"2\",\"move_stall_detections_exact\":\"3\",\"move_stall_episode_resets_exact\":\"1\",\"move_stall_forced_replans_exact\":\"2\",\"move_stall_navigation_forced_replans_exact\":\"1\",\"move_stall_targetless_move_to_timeouts_exact\":\"1\",\"move_stall_eligible_seconds\":4.250000000,\"failed_navigation_avoidance_activations_exact\":\"2\",\"failed_navigation_safeguard_suppressions_exact\":\"3\",\"failed_navigation_route_penalty_applications_exact\":\"4\",\"falling_seam_detections_exact\":\"11\",\"horizontal_corner_candidate_probes_exact\":\"10\",\"horizontal_corner_authorized_escapes_exact\":\"3\",\"horizontal_corner_target_progress_rejects_exact\":\"4\",\"horizontal_corner_unknown_or_unsafe_support_exact\":\"3\",\"falling_seam_episodes_exact\":\"4\",\"falling_seam_invalid_geometry_rejects_exact\":\"7\",\"falling_seam_authorizable_episodes_exact\":\"2\",\"horizontal_corner_authorized_candidates_exact\":\"3\",\"horizontal_corner_blocked_sweep_candidates_exact\":\"2\",\"horizontal_corner_no_static_walkable_support_candidates_exact\":\"1\",\"horizontal_corner_pain_support_candidates_exact\":\"1\",\"horizontal_corner_no_active_movement_intent_or_target_candidates_exact\":\"1\",\"horizontal_corner_true_target_regression_candidates_exact\":\"1\",\"horizontal_corner_unknown_evidence_candidates_exact\":\"1\""
		+ expectedWalkingPreflightSuffix + ",\"state\":\"Roaming\"}]}\n";
	const std::string controlCounterSuffix =
		",\"harmful_zone_escape_episodes_exact\":\"0\""
		",\"harmful_zone_escape_center_entries_exact\":\"0\""
		",\"harmful_zone_escape_foot_entries_exact\":\"0\""
		",\"harmful_zone_escape_recovery_attempts_exact\":\"0\""
		",\"harmful_zone_escape_successful_escapes_exact\":\"0\""
		",\"harmful_zone_escape_forced_replans_exact\":\"0\""
		",\"harmful_zone_escape_no_safe_candidates_exact\":\"0\""
		",\"hazard_swim_egress_episodes_exact\":\"0\""
		",\"hazard_swim_egress_eligible_exact\":\"0\""
		",\"hazard_swim_egress_authorized_exact\":\"0\""
		",\"hazard_swim_egress_debounced_exact\":\"0\""
		",\"hazard_swim_egress_no_anchor_rejected_exact\":\"0\""
		",\"hazard_swim_egress_exited_exact\":\"0\""
		",\"hazard_swim_egress_died_before_exit_exact\":\"0\""
		",\"hazard_swim_egress_forced_replans_exact\":\"0\""
		",\"hazard_swim_egress_falling_pre_move_anchor_captures_exact\":\"0\""
		",\"hazard_swim_egress_falling_pre_move_anchor_uses_exact\":\"0\""
		",\"hazard_swim_egress_live_applies_exact\":\"0\""
		",\"hazard_swim_egress_live_active_ticks_exact\":\"0\""
		",\"hazard_swim_egress_live_probe_rejected_exact\":\"0\""
		",\"hazard_swim_egress_live_successful_exits_exact\":\"0\""
		",\"hazard_swim_egress_direct_nav_probes_exact\":\"0\""
		",\"hazard_swim_egress_direct_nav_safe_candidates_exact\":\"0\""
		",\"hazard_swim_egress_direct_nav_best_candidate_name\":\"\""
		",\"hazard_swim_egress_anchor_known\":false"
		",\"hazard_swim_egress_anchor_source\":\"\""
		",\"hazard_water_egress_diagnostic_overflows_exact\":\"0\""
		",\"hazard_water_egress_diagnostics\":[]"
		",\"falling_hazard_recovery_promotions_exact\":\"0\""
		",\"falling_hazard_recovery_advance_calls_exact\":\"0\""
		",\"falling_hazard_recovery_context_rejected_exact\":\"0\""
		",\"falling_hazard_recovery_no_active_fall_episode_exact\":\"0\""
		",\"falling_hazard_recovery_no_prefix_exact\":\"0\""
		",\"falling_hazard_recovery_eligible_exact\":\"0\""
		",\"falling_hazard_recovery_anchor_rejected_exact\":\"0\""
		",\"falling_hazard_recovery_probe_rejected_exact\":\"0\""
		",\"falling_hazard_recovery_live_applies_exact\":\"0\""
		",\"falling_hazard_recovery_live_active_ticks_exact\":\"0\""
		",\"falling_hazard_recovery_safe_landings_exact\":\"0\""
		",\"falling_hazard_recovery_harmful_entries_exact\":\"0\""
		",\"falling_hazard_recovery_deaths_exact\":\"0\""
		",\"falling_hazard_recovery_timeouts_exact\":\"0\"";
	for (const std::string controlCounterMarker : {
		std::string("\"failed_navigation_route_penalty_applications_exact\":\"0\""),
		std::string("\"failed_navigation_route_penalty_applications_exact\":\"4\"") })
	{
		const size_t controlCounterIndex = expectedEvent.find(controlCounterMarker);
		if (controlCounterIndex == std::string::npos)
			return Fail("telemetry control counter fixture marker was missing");
		expectedEvent.insert(controlCounterIndex + controlCounterMarker.size(), controlCounterSuffix);
	}
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
	diagnostic.FallForecast.PainDamagePerSecKnown = true;
	diagnostic.FallForecast.PainDamagePerSec = 20.0f;
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
		|| diagnosticEvent.find("\"landing_zone\":\"pain\",\"pain_damage_per_sec_known\":true,\"pain_damage_per_sec\":20.000000") == std::string::npos
		|| diagnosticEvent.find("\"hit_fractions\":[0.500000000]") == std::string::npos)
		return Fail("walking-step preflight diagnostic serialization was incomplete or unstable");
	event.Bots.front().WalkingStepPreflightDiagnostics.clear();
	diagnostic.Phase = "precommit_provisional";
	diagnostic.TransitionOutcome.clear();
	diagnostic.Reason = PawnMovement::WalkingStepPreflightReason::NonFinitePainDamagePerSec;
	diagnostic.FallForecast.PainDamagePerSec = std::numeric_limits<float>::quiet_NaN();
	event.Bots.front().WalkingStepPreflightDiagnostics.push_back(diagnostic);
	const std::string nonFiniteDpsEvent =
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	if (nonFiniteDpsEvent.find(
		"\"pain_damage_per_sec_known\":true,\"pain_damage_per_sec\":null")
		== std::string::npos)
	{
		return Fail("non-finite preflight landing DPS was not serialized as JSON-safe null");
	}
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
	parityRecord.Outcome = PawnMovement::FallingParityRealizedOutcome::MatchedLanding;
	parityRecord.HitNormal = vec3(0.0f, 0.0f, 1.0f);
	event.Bots.front().FallingParityRealizedRecords.front() = parityRecord;
	const std::string matchedLandingEvent =
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	if (matchedLandingEvent.find("\"outcome\":\"matched_landing\"")
		== std::string::npos)
		return Fail("matched landing step vocabulary was not serialized distinctly");
	event.Bots.front().FallingParityRealizedRecords.clear();

	PawnMovement::HazardWaterEgressDiagnosticRecord waterEgress;
	waterEgress.SourcePawnActor = "Alys";
	waterEgress.Sequence = 5;
	waterEgress.Entry.LifeId = 3;
	waterEgress.Entry.EpisodeId = 2;
	waterEgress.Entry.TransitionSource =
		PawnMovement::HazardWaterEgressTransitionSource::FallingDirectSweep;
	waterEgress.Entry.AnchorKnown = true;
	waterEgress.Entry.Anchor = vec3(1.0f, 2.0f, 3.0f);
	waterEgress.Entry.EntryLocation = vec3(4.0f, 5.0f, 6.0f);
	waterEgress.Entry.DamagePerSecond = 20.0f;
	waterEgress.Entry.MoveTargetName = "PathNode144";
	waterEgress.Entry.MoveTargetLocationKnown = true;
	waterEgress.Entry.MoveTargetLocation = vec3(7.0f, 8.0f, 9.0f);
	waterEgress.Entry.Destination = vec3(10.0f, 11.0f, 12.0f);
	waterEgress.CandidateKnown = true;
	waterEgress.Candidate = { "PathNode12", vec3(13.0f, 14.0f, 15.0f), 42.0f };
	waterEgress.CandidateDistanceKnown = true;
	waterEgress.MinimumCandidateDistance = 12.0f;
	waterEgress.TerminalCandidateDistance = 13.0f;
	waterEgress.CandidateProgressSamples = 4;
	waterEgress.CandidateRegressionSamples = 3;
	waterEgress.TargetDistanceKnown = true;
	waterEgress.EntryTargetDistance = 10.0f;
	waterEgress.MinimumTargetDistance = 4.0f;
	waterEgress.TerminalTargetDistance = 5.0f;
	waterEgress.TargetProgressSamples = 6;
	waterEgress.TargetRegressionSamples = 2;
	waterEgress.Terminal = PawnMovement::HazardWaterEgressTerminal::DeathBeforeExit;
	waterEgress.TerminalLocation = vec3(16.0f, 17.0f, 18.0f);
	waterEgress.TerminalMoveTargetName = "LiftExit6";
	waterEgress.TerminalDestination = vec3(19.0f, 20.0f, 21.0f);
	event.Bots.front().HazardWaterEgressDiagnosticOverflowsExact = 1;
	event.Bots.front().HazardWaterEgressDiagnostics.push_back(waterEgress);
	const std::string waterEgressEvent = BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	if (waterEgressEvent.find("\"source_pawn_actor\":\"Alys\",\"sequence\":\"5\",\"life_id\":\"3\",\"episode_id\":\"2\",\"transition_source\":\"falling_direct_sweep\"") == std::string::npos)
		return Fail("hazard-water egress identity serialization was incomplete");
	if (waterEgressEvent.find("\"candidate_known\":true,\"candidate_name\":\"PathNode12\"") == std::string::npos)
		return Fail("hazard-water egress candidate serialization was incomplete");
	if (waterEgressEvent.find("\"candidate_distance_known\":true,\"minimum_candidate_distance\":12.000000,\"terminal_candidate_distance\":13.000000,\"candidate_progress_samples\":\"4\",\"candidate_regression_samples\":\"3\"") == std::string::npos)
		return Fail("hazard-water egress candidate-progress serialization was incomplete");
	if (waterEgressEvent.find("\"target_progress_samples\":\"6\",\"target_regression_samples\":\"2\",\"terminal\":\"death_before_exit\"") == std::string::npos)
	{
		return Fail("hazard-water egress diagnostic serialization was incomplete or unstable");
	}
	event.Bots.front().HazardWaterEgressDiagnostics.clear();
	event.Bots.front().HazardWaterEgressDiagnosticOverflowsExact = 0;

	using namespace PawnMovement;
	if (std::string(FallingHazardForecastSourceName(
		FallingHazardForecastSource::AlignedContinuationCommit))
			!= "aligned_continuation_commit"
		|| std::string(FallingHazardForecastSourceName(
			FallingHazardForecastSource::ThirdMoveContinuationCommit))
			!= "third_move_continuation_commit"
		|| std::string(FallingHazardForecastSourceName(
			FallingHazardForecastSource::HorizonContinuationCommit))
			!= "horizon_continuation_commit"
		|| std::string(FallingHazardTerminalName(
			FallingHazardTerminal::HarmfulPainEntered)) != "harmful_pain_entered"
		|| std::string(FallingHazardCorrelationName(
			FallingHazardCorrelation::Ambiguous)) != "ambiguous")
		return Fail("falling hazard diagnostic enum vocabulary was incomplete");

	auto knownZone = [](uint32_t actorId, uint32_t zoneNumber)
	{
		return FallingHazardZoneId {
			.Known = true,
			.ZoneActorId = actorId,
			.ZoneNumber = zoneNumber
		};
	};
	FallingHazardDiagnosticRecord start;
	start.Kind = FallingHazardDiagnosticKind::Start;
	start.SourcePawnActor = "Bot\"Hazard";
	start.Sequence = std::numeric_limits<uint64_t>::max();
	start.PrechargedElapsed = 1.0f / 60.0f;
	start.Generation.Life.Value = std::numeric_limits<uint64_t>::max() - 1;
	start.Generation.FallEpisode.Value = std::numeric_limits<uint64_t>::max() - 2;
	start.Generation.Generation.Value = std::numeric_limits<uint32_t>::max();
	start.Generation.Source = FallingHazardForecastSource::AlignedContinuationCommit;
	start.Generation.Forecast = FallingHazardForecast::HarmfulPainObserved;
	start.Generation.StartingPhysicsZone = knownZone(17, 0);
	start.Generation.ExpectedHarmfulFootZone = knownZone(23, 0);
	start.Generation.ExpectedHarmfulPhysicsZone = knownZone(29, 3);
	start.Generation.ExpectedHarmfulWaterEntry = true;
	start.Generation.SweptSegmentBudget = 256;
	start.Generation.ElapsedHorizon = 4.0f;

	FallingHazardDiagnosticRecord terminal = start;
	terminal.Kind = FallingHazardDiagnosticKind::Terminal;
	terminal.Sequence--;
	terminal.Generation.Source =
		FallingHazardForecastSource::ThirdMoveContinuationCommit;
	terminal.Generation.Terminal = FallingHazardTerminal::HarmfulPainEntered;
	terminal.Generation.LastObservedPhysicsZone = knownZone(31, 0);
	terminal.Generation.ObservedHarmfulFootZone = knownZone(37, 5);
	terminal.Generation.ObservedHarmfulCenterZone = knownZone(41, 7);
	terminal.Generation.SweptSegmentCount = 3;
	terminal.Generation.ObservedElapsed = 0.02f;
	terminal.Generation.HasPositiveElapsed = true;
	terminal.Generation.PhysicsZoneEvidenceKnown = true;
	terminal.Generation.HarmfulFootEvidenceKnown = true;
	terminal.Generation.HarmfulCenterEvidenceKnown = true;
	terminal.Generation.WaterEvidenceKnown = true;
	terminal.Generation.EnteredHarmfulFootZone = true;
	terminal.Generation.EnteredHarmfulCenterZone = true;
	terminal.Generation.CausalAmbiguity = true;
	terminal.Generation.LandingCollision = FallingHazardCollisionKind::StaticWorld;
	terminal.Correlation = FallingHazardCorrelation::Ambiguous;

	FallingHazardDiagnosticRecord capacity;
	capacity.Kind = FallingHazardDiagnosticKind::GenerationCapacityExceeded;
	capacity.SourcePawnActor = "BotCapacity";
	capacity.Sequence = 9;
	capacity.Generation.Life.Value = 7;
	capacity.Generation.FallEpisode.Value = 8;
	capacity.Generation.Source =
		FallingHazardForecastSource::HorizonContinuationCommit;

	event.Bots.front().VerticalPainColumnEpisodesStartedExact = 10;
	event.Bots.front().VerticalPainColumnEpisodesCompletedExact = 6;
	event.Bots.front().VerticalPainColumnTruePositiveOutcomesExact = 1;
	event.Bots.front().VerticalPainColumnFalsePositiveOutcomesExact = 1;
	event.Bots.front().VerticalPainColumnFalseNegativeOutcomesExact = 1;
	event.Bots.front().VerticalPainColumnTrueNegativeOutcomesExact = 1;
	event.Bots.front().VerticalPainColumnAmbiguousOutcomesExact = 1;
	event.Bots.front().VerticalPainColumnUnknownOutcomesExact = 1;
	event.Bots.front().VerticalPainColumnDiagnosticOverflowsExact = 2;
	event.Bots.front().VerticalPainColumnGenerationCapacityExhaustionsExact = 3;
	event.Bots.front().VerticalPainColumnDiagnostics = { start, terminal, capacity };
	const std::string hazardEvent =
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	if (hazardEvent.find("\"source_pawn_actor\":\"Bot\\\"Hazard\"")
			== std::string::npos
		|| hazardEvent.find(
		"\"sequence\":\"18446744073709551615\",\"life_id\":\"18446744073709551614\",\"fall_episode_id\":\"18446744073709551613\",\"generation_id\":\"4294967295\",\"kind\":\"start\"")
			== std::string::npos
		|| hazardEvent.find(
			"\"starting_physics_zone\":{\"known\":true,\"zone_actor_id\":17,\"zone_number\":0}")
			== std::string::npos
		|| hazardEvent.find(
			"\"source\":\"aligned_continuation_commit\",\"forecast\":\"harmful_pain_observed\"")
			== std::string::npos
		|| hazardEvent.find("\"precharged_elapsed\":0.016666668")
			== std::string::npos)
		return Fail("falling hazard start diagnostic serialization was incomplete");
	if (hazardEvent.find(
		"\"kind\":\"terminal\",\"source\":\"third_move_continuation_commit\"")
			== std::string::npos
		|| hazardEvent.find(
			"\"terminal\":\"harmful_pain_entered\",\"correlation\":\"ambiguous\"")
			== std::string::npos
		|| hazardEvent.find(
			"\"last_observed_physics_zone\":{\"known\":true,\"zone_actor_id\":31,\"zone_number\":0}")
			== std::string::npos
		|| hazardEvent.find(
			"\"observed_harmful_foot_zone\":{\"known\":true,\"zone_actor_id\":37,\"zone_number\":5},\"observed_harmful_center_zone\":{\"known\":true,\"zone_actor_id\":41,\"zone_number\":7}")
			== std::string::npos
		|| hazardEvent.find(
			"\"swept_segment_count\":3,\"observed_elapsed\":0.020000000")
			== std::string::npos
		|| hazardEvent.find(
			"\"harmful_foot_evidence_known\":true,\"harmful_center_evidence_known\":true,\"water_evidence_known\":true")
			== std::string::npos
		|| hazardEvent.find(
			"\"entered_harmful_foot_zone\":true,\"entered_harmful_center_zone\":true,\"expected_harmful_path_matched\":false")
			== std::string::npos
		|| hazardEvent.find("\"causal_ambiguity\":true,\"actual_trajectory_unknown\":false")
			== std::string::npos)
		return Fail("falling hazard terminal diagnostic serialization was incomplete");
	if (hazardEvent.find(
		"\"source_pawn_actor\":\"BotCapacity\",\"sequence\":\"9\",\"life_id\":\"7\",\"fall_episode_id\":\"8\",\"generation_id\":\"0\",\"kind\":\"generation_capacity_exceeded\",\"attempted_source\":\"horizon_continuation_commit\"")
			== std::string::npos
		|| hazardEvent.find(
			"\"vertical_pain_column_true_positive_outcomes_exact\":\"1\"")
			== std::string::npos
		|| hazardEvent.find(
			"\"vertical_pain_column_generation_capacity_exhaustions_exact\":\"3\"")
			== std::string::npos)
		return Fail("falling hazard capacity/counter serialization was incomplete");
	bool rejectedHazardNonFinite = false;
	try
	{
		start.PrechargedElapsed = std::numeric_limits<float>::infinity();
		event.Bots.front().VerticalPainColumnDiagnostics = { start };
		BotBenchmarkTelemetryProtocol::EventJson(configId, event);
	}
	catch (const std::invalid_argument&)
	{
		rejectedHazardNonFinite = true;
	}
	if (!rejectedHazardNonFinite)
		return Fail("falling hazard diagnostics accepted non-finite elapsed evidence");
	event.Bots.front().VerticalPainColumnDiagnostics.clear();

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
