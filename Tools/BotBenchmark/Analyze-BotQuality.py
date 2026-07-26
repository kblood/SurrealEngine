#!/usr/bin/env python3
"""Validate unified bot telemetry and report the quality signals it supports."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import sys
import re
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


MANIFEST_SCHEMA = "surreal-bot-benchmark-manifest-v1"
MANIFEST_SCHEMA_V2 = "surreal-bot-benchmark-manifest-v2"
MANIFEST_SCHEMA_V3 = "surreal-bot-benchmark-manifest-v3"
TELEMETRY_SCHEMA = "surreal-bot-benchmark-telemetry-v1"
TELEMETRY_SCHEMA_V2 = "surreal-bot-benchmark-telemetry-v2"
SUMMARY_SCHEMA = "surreal-bot-benchmark-summary-v1"
SUMMARY_SCHEMA_V2 = "surreal-bot-benchmark-summary-v2"
SUMMARY_SCHEMA_V3 = "surreal-bot-benchmark-summary-v3"
SUMMARY_SCHEMA_V4 = "surreal-bot-benchmark-summary-v4"
METADATA_SCHEMA = "surreal-bot-quality-run-metadata-v1"
REPORT_SCHEMA = "surreal-bot-quality-analysis-v1"
TOOL_VERSION = 28

DISTANCE_EPSILON = 0.25
STUCK_WINDOW_SECONDS = 2.0
BUILD_ID_HEX = re.compile(r"[0-9a-f]{40}\Z")
BUILD_SHA256 = re.compile(r"[0-9A-F]{64}\Z")


def _validate_build_identity(raw: Any, context: str) -> dict[str, Any]:
    value = _object(raw, context)
    if set(value) != {"schema", "id", "source", "executable"}:
        raise QualityError(f"{context}: build identity fields are not exact")
    if value["schema"] != "surreal-engine-build-identity-v1":
        raise QualityError(f"{context}.schema: unsupported build identity")
    identity_id = _string(value, "id", context, nonempty=True)
    if not re.fullmatch(r"sha256:[0-9A-F]{64}", identity_id):
        raise QualityError(f"{context}.id: invalid build identity digest")
    source = _object(value["source"], f"{context}.source")
    if set(source) != {"commit", "tree", "dirty"}:
        raise QualityError(f"{context}.source: build source fields are not exact")
    for field in ("commit", "tree"):
        text = _string(source, field, f"{context}.source", nonempty=True)
        if not BUILD_ID_HEX.fullmatch(text):
            raise QualityError(f"{context}.source.{field}: expected lowercase 40-hex Git ID")
    _boolean(source["dirty"], f"{context}.source.dirty")
    executable = _object(value["executable"], f"{context}.executable")
    if set(executable) != {"name", "size_bytes", "sha256"}:
        raise QualityError(f"{context}.executable: executable fields are not exact")
    name = _string(executable, "name", f"{context}.executable", nonempty=True)
    if "/" in name or "\\\\" in name:
        raise QualityError(f"{context}.executable.name: must be a filename")
    _integer(executable["size_bytes"], f"{context}.executable.size_bytes", minimum=1)
    digest = _string(executable, "sha256", f"{context}.executable", nonempty=True)
    if not BUILD_SHA256.fullmatch(digest):
        raise QualityError(f"{context}.executable.sha256: expected uppercase SHA-256")
    return value

METRIC_DIRECTIONS: dict[str, str | None] = {
    "distance_traveled": None,
    "active_movement_seconds": None,
    "active_movement_fraction": None,
    "no_progress_seconds_proxy": "lower",
    "longest_no_progress_seconds_proxy": "lower",
    "stuck_events_proxy": "lower",
    "movement_intent_no_progress_seconds_proxy": "lower",
    "longest_movement_intent_no_progress_seconds_proxy": "lower",
    "movement_intent_stuck_events_proxy": "lower",
    "kills_exact": "higher",
    "deaths_exact": "lower",
    "suicides_exact": "lower",
    "environmental_deaths_exact": "lower",
    "direct_self_kills": "lower",
    "direct_enemy_kills": None,
    "unassisted_environmental_deaths": "lower",
    "recent_enemy_contributed_environmental_deaths_proxy": "lower",
    "ambiguous_deaths": "lower",
    "recent_enemy_momentum_contributed_environmental_deaths_proxy": "lower",
    "post_mayfall_harmful_begin_falling_command_witnesses_exact": None,
    "post_mayfall_harmful_begin_falling_command_witness_parity_deaths_exact": None,
    "post_mayfall_harmful_begin_falling_command_witness_unassisted_environmental_deaths_exact": None,
    "suicide_to_kill_ratio": "lower",
    "match_score_delta": "higher",
    "hit_wall_events_exact": "lower",
    "pain_ledge_vetoes_exact": None,
    "pain_ledge_repeat_vetoes_exact": None,
    "pain_ledge_recovery_attempts_exact": None,
    "pain_ledge_recovery_escapes_exact": None,
    "wall_adjust_calls_exact": None,
    "wall_adjust_repeats_exact": None,
    "wall_adjust_recovery_attempts_exact": None,
    "wall_adjust_recovery_successes_exact": None,
    "wall_adjust_forced_replans_exact": None,
    "move_stall_detections_exact": None,
    "move_stall_episode_resets_exact": None,
    "move_stall_forced_replans_exact": None,
    "move_stall_navigation_forced_replans_exact": None,
    "move_stall_targetless_move_to_timeouts_exact": None,
    "move_stall_direct_actor_move_toward_timeouts_exact": None,
    "move_stall_eligible_seconds": None,
    "move_stall_recovery_episodes_exact": None,
    "move_stall_recovery_cleared_within_2_seconds_exact": None,
    "move_stall_recovery_cleared_after_2_seconds_within_5_seconds_exact": None,
    "move_stall_recovery_replanned_within_5_seconds_exact": None,
    "move_stall_recovery_missed_5_second_deadline_exact": None,
    "move_stall_recovery_excluded_intentional_stops_exact": None,
    "move_stall_recovery_censored_life_boundaries_exact": None,
    "move_stall_recovery_censored_run_end_exact": None,
    "move_stall_recovery_unknown_exact": None,
    "move_stall_recovery_episode_record_overflows_exact": None,
    "move_stall_recovery_decision_record_overflows_exact": None,
    "recoverable_movement_episode_clear_within_2s_fraction": "higher",
    "recoverable_movement_episode_clear_or_replanned_within_5s_fraction": "higher",
    "failed_navigation_avoidance_activations_exact": None,
    "failed_navigation_safeguard_suppressions_exact": None,
    "failed_navigation_route_penalty_applications_exact": None,
    "falling_seam_detections_exact": None,
    "horizontal_corner_candidate_probes_exact": None,
    "horizontal_corner_authorized_escapes_exact": None,
    "horizontal_corner_target_progress_rejects_exact": None,
    "horizontal_corner_unknown_or_unsafe_support_exact": None,
    "falling_seam_episodes_exact": None,
    "falling_seam_invalid_geometry_rejects_exact": None,
    "falling_seam_authorizable_episodes_exact": None,
    "horizontal_corner_authorized_candidates_exact": None,
    "horizontal_corner_blocked_sweep_candidates_exact": None,
    "horizontal_corner_no_static_walkable_support_candidates_exact": None,
    "horizontal_corner_pain_support_candidates_exact": None,
    "horizontal_corner_no_active_movement_intent_or_target_candidates_exact": None,
    "horizontal_corner_true_target_regression_candidates_exact": None,
    "horizontal_corner_unknown_evidence_candidates_exact": None,
    "hazard_exposure_seconds": "lower",
    "hazard_entries": "lower",
    "hazard_exposed_deaths_proxy": "lower",
    "hazard_exposed_death_fraction_proxy": "lower",
    "damage_taken_exact": "lower",
    "damage_taken_from_other_participants_exact": "lower",
    "damage_taken_from_self_exact": "lower",
    "damage_taken_from_nonparticipants_exact": "lower",
    "damage_dealt_to_other_participants_exact": "higher",
    "damage_efficiency_to_other_participants": "higher",
    "health_loss_observed": "lower",
    "minimum_health_observed": "higher",
    "survived_to_final_sample": "higher",
    "completion": "higher",
    "falling_parity_realized_episode_completion_fraction": "higher",
    "falling_parity_realized_comparable_step_fraction": "higher",
    "falling_parity_realized_matched_step_fraction": "higher",
    "falling_parity_realized_mismatch_fraction": "lower",
    "falling_parity_realized_unknown_step_fraction": "lower",
    "vertical_pain_column_precision": "higher",
    "vertical_pain_column_recall": "higher",
    "vertical_pain_column_false_positive_rate": "lower",
    "vertical_pain_column_labeled_episode_fraction": "higher",
    "vertical_pain_column_episode_completion_fraction": "higher",
    "vertical_pain_column_unknown_outcome_fraction": "lower",
    "vertical_pain_column_ambiguous_outcome_fraction": "lower",
    "vertical_pain_column_diagnostic_coverage_fraction": "higher",
    "vertical_pain_column_generation_capacity_exhaustion_rate": "lower",
    "ai_frame_p95_ms": "lower",
}

FUTURE_METRICS = {
    "accuracy": "Attributed shot and finalized hit/miss events are required.",
    "objective_progress": "Mode-specific, attributed objective events are required.",
}

CORE_EXACT_COUNTERS = (
    "kills_exact", "deaths_exact", "suicides_exact", "environmental_deaths_exact",
    "hazard_exposed_deaths_proxy", "hit_wall_events_exact",
)
PAIN_LEDGE_EXACT_COUNTERS = (
    "pain_ledge_vetoes_exact", "pain_ledge_repeat_vetoes_exact",
    "pain_ledge_recovery_attempts_exact", "pain_ledge_recovery_escapes_exact",
    "pain_ledge_recovery_active_hitwall_events_exact",
)
WALL_ADJUST_EXACT_COUNTERS = (
    "wall_adjust_calls_exact", "wall_adjust_repeats_exact",
    "wall_adjust_recovery_attempts_exact", "wall_adjust_recovery_successes_exact",
    "wall_adjust_forced_replans_exact",
)
MOVE_STALL_EXACT_COUNTERS = (
    "move_stall_detections_exact", "move_stall_episode_resets_exact",
    "move_stall_forced_replans_exact", "move_stall_navigation_forced_replans_exact",
    "move_stall_targetless_move_to_timeouts_exact",
    "move_stall_direct_actor_move_toward_timeouts_exact",
)
FAILED_NAVIGATION_EXACT_COUNTERS = (
    "failed_navigation_avoidance_activations_exact",
    "failed_navigation_safeguard_suppressions_exact",
    "failed_navigation_route_penalty_applications_exact",
)
HARMFUL_ZONE_ESCAPE_EXACT_COUNTERS = (
    "harmful_zone_escape_episodes_exact",
    "harmful_zone_escape_center_entries_exact",
    "harmful_zone_escape_foot_entries_exact",
    "harmful_zone_escape_recovery_attempts_exact",
    "harmful_zone_escape_successful_escapes_exact",
    "harmful_zone_escape_forced_replans_exact",
    "harmful_zone_escape_no_safe_candidates_exact",
)
MOVE_STALL_RECOVERY_EXACT_COUNTERS = (
    "move_stall_recovery_episodes_exact",
    "move_stall_recovery_cleared_within_2_seconds_exact",
    "move_stall_recovery_cleared_after_2_seconds_within_5_seconds_exact",
    "move_stall_recovery_replanned_within_5_seconds_exact",
    "move_stall_recovery_missed_5_second_deadline_exact",
    "move_stall_recovery_excluded_intentional_stops_exact",
    "move_stall_recovery_censored_life_boundaries_exact",
    "move_stall_recovery_censored_run_end_exact",
    "move_stall_recovery_unknown_exact",
    "move_stall_recovery_episode_record_overflows_exact",
)
MOVE_STALL_RECOVERY_OUTCOME_COUNTERS = (
    "move_stall_recovery_cleared_within_2_seconds_exact",
    "move_stall_recovery_cleared_after_2_seconds_within_5_seconds_exact",
    "move_stall_recovery_replanned_within_5_seconds_exact",
    "move_stall_recovery_missed_5_second_deadline_exact",
    "move_stall_recovery_excluded_intentional_stops_exact",
    "move_stall_recovery_censored_life_boundaries_exact",
    "move_stall_recovery_censored_run_end_exact",
    "move_stall_recovery_unknown_exact",
)
HAZARD_SWIM_EGRESS_EXACT_COUNTERS = (
    "hazard_swim_egress_episodes_exact",
    "hazard_swim_egress_eligible_exact",
    "hazard_swim_egress_authorized_exact",
    "hazard_swim_egress_debounced_exact",
    "hazard_swim_egress_no_anchor_rejected_exact",
    "hazard_swim_egress_exited_exact",
    "hazard_swim_egress_died_before_exit_exact",
    "hazard_swim_egress_forced_replans_exact",
)
HAZARD_SWIM_EGRESS_PLANNER_HANDOFF_OUTCOME_COUNTERS = (
    "hazard_swim_egress_forced_replan_same_command_reissued_exact",
    "hazard_swim_egress_forced_replan_different_command_issued_exact",
    "hazard_swim_egress_forced_replan_hazard_cleared_before_command_exact",
    "hazard_swim_egress_forced_replan_fell_before_command_exact",
    "hazard_swim_egress_forced_replan_died_before_command_exact",
    "hazard_swim_egress_forced_replan_life_boundary_censored_exact",
    "hazard_swim_egress_forced_replan_run_end_censored_exact",
    "hazard_swim_egress_forced_replan_episode_abandoned_exact",
)
FALLING_PRE_MOVE_ANCHOR_COUNTERS = (
	"falling_pre_move_anchor_captures_exact",
	"falling_pre_move_anchor_uses_exact",
)
HAZARD_SWIM_EGRESS_LIVE_COUNTERS = (
	"hazard_swim_egress_live_applies_exact",
	"hazard_swim_egress_live_active_ticks_exact",
	"hazard_swim_egress_live_probe_rejected_exact",
	"hazard_swim_egress_live_successful_exits_exact",
)
HAZARD_SWIM_EGRESS_DIRECT_NAV_COUNTERS = (
    "hazard_swim_egress_direct_nav_probes_exact",
    "hazard_swim_egress_direct_nav_safe_candidates_exact",
)
HAZARD_RESIDENCE_COUNTERS = (
    "hazard_residence_episodes_exact",
    "hazard_residence_cleared_exact",
    "hazard_residence_deaths_exact",
    "hazard_residence_life_boundary_censored_exact",
    "hazard_residence_run_end_censored_exact",
    "hazard_residence_unknown_exact",
    "hazard_residence_reentries_exact",
    "hazard_residence_command_changes_exact",
    "hazard_residence_candidates_observed_exact",
    "hazard_residence_candidate_other_commands_exact",
)
HAZARD_WATER_EGRESS_DIAGNOSTIC_OVERFLOW_COUNTER = \
    "hazard_water_egress_diagnostic_overflows_exact"
FALLING_HAZARD_RECOVERY_COUNTERS = (
    "falling_hazard_recovery_promotions_exact",
    "falling_hazard_recovery_advance_calls_exact",
    "falling_hazard_recovery_context_rejected_exact",
    "falling_hazard_recovery_no_active_fall_episode_exact",
    "falling_hazard_recovery_no_prefix_exact",
    "falling_hazard_recovery_eligible_exact",
    "falling_hazard_recovery_anchor_rejected_exact",
    "falling_hazard_recovery_probe_rejected_exact",
    "falling_hazard_recovery_live_applies_exact",
    "falling_hazard_recovery_live_active_ticks_exact",
    "falling_hazard_recovery_safe_landings_exact",
    "falling_hazard_recovery_harmful_entries_exact",
    "falling_hazard_recovery_deaths_exact",
    "falling_hazard_recovery_timeouts_exact",
)
EXTERNAL_IMPULSE_FALL_WITNESS_COUNTERS = (
    "external_impulse_fall_harmful_witnesses_exact",
    "external_impulse_fall_no_air_control_exact",
    "external_impulse_fall_alternatives_tested_exact",
    "external_impulse_fall_certified_exact",
    "external_impulse_fall_uncertified_exact",
)
DEATH_ATTRIBUTION_COUNTERS = (
    "direct_self_kills", "direct_enemy_kills", "unassisted_environmental_deaths",
    "recent_enemy_contributed_environmental_deaths_proxy", "ambiguous_deaths",
    "recent_enemy_momentum_contributed_environmental_deaths_proxy",
)
FALLING_SEAM_SHADOW_COUNTERS = (
    "falling_seam_detections_exact",
    "horizontal_corner_candidate_probes_exact",
    "horizontal_corner_authorized_escapes_exact",
    "horizontal_corner_target_progress_rejects_exact",
    "horizontal_corner_unknown_or_unsafe_support_exact",
)
FALLING_SEAM_DETAILED_COUNTERS = (
    "falling_seam_episodes_exact",
    "falling_seam_invalid_geometry_rejects_exact",
    "falling_seam_authorizable_episodes_exact",
    "horizontal_corner_authorized_candidates_exact",
    "horizontal_corner_blocked_sweep_candidates_exact",
    "horizontal_corner_no_static_walkable_support_candidates_exact",
    "horizontal_corner_pain_support_candidates_exact",
    "horizontal_corner_no_active_movement_intent_or_target_candidates_exact",
    "horizontal_corner_true_target_regression_candidates_exact",
    "horizontal_corner_unknown_evidence_candidates_exact",
)
WALKING_STEP_PREFLIGHT_REASON_COUNTERS = (
    "walking_step_preflight_reason_ineligible_unknown_actor_exact",
    "walking_step_preflight_reason_ineligible_human_player_exact",
    "walking_step_preflight_reason_ineligible_scripted_pawn_exact",
    "walking_step_preflight_reason_not_walking_exact",
    "walking_step_preflight_reason_unknown_start_support_exact",
    "walking_step_preflight_reason_non_static_start_support_exact",
    "walking_step_preflight_reason_non_walkable_start_support_exact",
    "walking_step_preflight_reason_unknown_start_zone_exact",
    "walking_step_preflight_reason_unsafe_start_zone_exact",
    "walking_step_preflight_reason_unknown_gravity_exact",
    "walking_step_preflight_reason_non_axial_downward_gravity_exact",
    "walking_step_preflight_reason_upward_jump_requested_exact",
    "walking_step_preflight_reason_collision_callback_required_script_transition_unknown_exact",
    "walking_step_preflight_reason_invalid_bounds_exact",
    "walking_step_preflight_reason_invalid_step_delta_exact",
    "walking_step_preflight_reason_unknown_step_evidence_exact",
    "walking_step_preflight_reason_mover_step_evidence_exact",
    "walking_step_preflight_reason_dynamic_step_evidence_exact",
    "walking_step_preflight_reason_invalid_step_sequence_exact",
    "walking_step_preflight_reason_supported_step_endpoint_exact",
    "walking_step_preflight_reason_incomplete_fall_forecast_exact",
    "walking_step_preflight_reason_fall_continuation_limit_exceeded_exact",
    "walking_step_preflight_reason_invalid_fall_delta_exact",
    "walking_step_preflight_reason_unknown_fall_evidence_exact",
    "walking_step_preflight_reason_mover_fall_evidence_exact",
    "walking_step_preflight_reason_dynamic_fall_evidence_exact",
    "walking_step_preflight_reason_invalid_fall_continuation_exact",
    "walking_step_preflight_reason_invalid_fall_landing_exact",
    "walking_step_preflight_reason_unknown_landing_zone_exact",
    "walking_step_preflight_reason_safe_fall_landing_exact",
    "walking_step_preflight_reason_unknown_pain_damage_immunity_exact",
    "walking_step_preflight_reason_pain_damage_immune_exact",
    "walking_step_preflight_reason_unknown_pain_damage_per_sec_exact",
    "walking_step_preflight_reason_non_finite_pain_damage_per_sec_exact",
    "walking_step_preflight_reason_non_harmful_pain_damage_per_sec_exact",
    "walking_step_preflight_reason_harmful_pain_fall_exact",
)
WALKING_STEP_PREFLIGHT_COUNTERS = (
    "walking_step_preflight_observations_exact",
    "walking_step_preflight_unsupported_endpoints_exact",
    "walking_step_preflight_no_decisions_exact",
    "walking_step_preflight_provisional_authorizations_exact",
    "walking_step_preflight_post_mayfall_confirmed_authorizations_exact",
    "walking_step_preflight_authorizable_episodes_exact",
    "walking_step_preflight_diagnostic_overflows_exact",
) + WALKING_STEP_PREFLIGHT_REASON_COUNTERS
WALKING_STEP_PREFLIGHT_POSITIVE_DPS_VETO_COUNTERS = (
    "walking_step_preflight_positive_dps_veto_eligible_exact",
    "walking_step_preflight_positive_dps_veto_applied_exact",
    "walking_step_preflight_positive_dps_veto_debounced_exact",
    "walking_step_preflight_positive_dps_veto_forced_replans_exact",
    "walking_step_preflight_positive_dps_veto_rollback_rejected_exact",
    "walking_step_preflight_positive_dps_veto_action_overflows_exact",
)
WALKING_HITWALL_DISPATCH_COUNTERS = (
    "walking_hitwall_dispatch_observations_exact",
    "walking_hitwall_dispatch_legacy_z_band_exact",
    "walking_hitwall_dispatch_minhitwall_exact",
    "walking_hitwall_dispatch_disagreements_exact",
    "walking_hitwall_dispatch_callbacks_exact",
    "walking_hitwall_dispatch_diagnostic_overflows_exact",
)
DAMAGE_COUNTERS = (
    "damage_taken_exact",
    "damage_taken_from_other_participants_exact",
    "damage_taken_from_self_exact",
    "damage_taken_from_nonparticipants_exact",
    "damage_dealt_to_other_participants_exact",
)
CONFIRMED_PICKUP_COUNTERS = (
    "confirmed_pickups_exact",
    "confirmed_weapon_pickups_exact",
    "confirmed_ammo_pickups_exact",
    "confirmed_health_pickups_exact",
    "confirmed_armor_pickups_exact",
    "confirmed_other_pickups_exact",
)
PICKUP_SOURCE_CONSUMED_UNCONFIRMED_COUNTER = "pickup_source_consumed_unconfirmed_exact"
NAVIGATION_COVERAGE_COUNTERS = (
    "navigation_coverage_visited_nodes_exact",
    "navigation_coverage_catalog_nodes_exact",
    "navigation_coverage_union_visited_nodes_exact",
)
FALLING_PARITY_REALIZED_STEP_COUNTERS = (
    "falling_parity_realized_matched_steps_exact",
    "falling_parity_realized_matched_landing_steps_exact",
    "falling_parity_realized_mismatches_exact",
    "falling_parity_realized_unknowns_exact",
    "falling_parity_realized_callback_barriers_exact",
)
FALLING_PARITY_COUNTERS = (
    "falling_parity_realized_episodes_exact",
    "falling_parity_realized_steps_exact",
) + FALLING_PARITY_REALIZED_STEP_COUNTERS + (
    "falling_parity_realized_pain_entries_exact",
    "falling_parity_realized_deaths_exact",
    "falling_parity_realized_landings_exact",
    "falling_parity_realized_continuity_losses_exact",
    "falling_parity_realized_record_overflows_exact",
)
FALLING_PARITY_LEGACY_COUNTERS = tuple(
    name for name in FALLING_PARITY_COUNTERS
    if name != "falling_parity_realized_matched_landing_steps_exact"
)
VERTICAL_PAIN_COLUMN_OUTCOME_COUNTERS = (
    "vertical_pain_column_true_positive_outcomes_exact",
    "vertical_pain_column_false_positive_outcomes_exact",
    "vertical_pain_column_false_negative_outcomes_exact",
    "vertical_pain_column_true_negative_outcomes_exact",
    "vertical_pain_column_ambiguous_outcomes_exact",
    "vertical_pain_column_unknown_outcomes_exact",
)
VERTICAL_PAIN_COLUMN_LEGACY_COUNTERS = (
    "vertical_pain_column_episodes_started_exact",
    "vertical_pain_column_episodes_completed_exact",
) + VERTICAL_PAIN_COLUMN_OUTCOME_COUNTERS + (
    "vertical_pain_column_diagnostic_overflows_exact",
)
VERTICAL_PAIN_COLUMN_COUNTERS = VERTICAL_PAIN_COLUMN_LEGACY_COUNTERS + (
    "vertical_pain_column_generation_capacity_exhaustions_exact",
)
PERSISTENT_HARMFUL_FALL_COUNTERS = (
    "persistent_harmful_fall_candidates_started_exact",
    "persistent_harmful_fall_promotions_exact",
    "persistent_harmful_fall_resets_exact",
    "persistent_harmful_fall_confirmed_harmful_entries_exact",
    "persistent_harmful_fall_observed_lead_samples_exact",
    "persistent_harmful_fall_observed_lead_milliseconds_exact",
)
SINGLE_HARMFUL_FALL_PREFIX_COUNTERS = (
    "single_harmful_fall_prefix_candidates_started_exact",
    "single_harmful_fall_prefix_promotions_exact",
    "single_harmful_fall_prefix_resets_exact",
    "single_harmful_fall_prefix_confirmed_harmful_entries_exact",
    "single_harmful_fall_prefix_observed_lead_samples_exact",
    "single_harmful_fall_prefix_observed_lead_milliseconds_exact",
)
DIRECT_HARMFUL_WATER_ENTRY_COUNTERS = (
    "direct_harmful_water_entry_candidates_exact",
    "direct_harmful_water_entry_confirmed_exact",
    "direct_harmful_water_entry_confirmed_no_harm_exact",
    "direct_harmful_water_entry_unresolved_exact",
    "direct_harmful_water_entry_lead_samples_exact",
    "direct_harmful_water_entry_lead_milliseconds_exact",
)
DIRECT_HARMFUL_WATER_ENTRY_CERTIFICATE_RESULT_COUNTERS = (
    "direct_harmful_water_entry_certificate_source_not_eligible_exact",
    "direct_harmful_water_entry_certificate_certified_exact",
    "direct_harmful_water_entry_certificate_forecast_incomplete_or_inconsistent_exact",
    "direct_harmful_water_entry_certificate_not_full_step_exact",
    "direct_harmful_water_entry_certificate_not_harmful_water_endpoint_exact",
    "direct_harmful_water_entry_certificate_damage_not_avoidance_relevant_exact",
    "direct_harmful_water_entry_certificate_invalid_expected_harmful_zones_exact",
    "direct_harmful_water_entry_certificate_unsafe_or_unknown_start_exact",
    "direct_harmful_water_entry_certificate_intermediate_hazard_observed_exact",
    "direct_harmful_water_entry_certificate_no_direct_clear_path_exact",
    "direct_harmful_water_entry_certificate_invalid_prediction_accounting_exact",
)
METRIC_DIRECTIONS.update({
	name: None for name in (
		WALKING_STEP_PREFLIGHT_COUNTERS + FALLING_PARITY_COUNTERS
		+ VERTICAL_PAIN_COLUMN_COUNTERS + HAZARD_SWIM_EGRESS_EXACT_COUNTERS
		+ HAZARD_SWIM_EGRESS_PLANNER_HANDOFF_OUTCOME_COUNTERS
		+ FALLING_PRE_MOVE_ANCHOR_COUNTERS + HAZARD_SWIM_EGRESS_LIVE_COUNTERS
		+ HAZARD_SWIM_EGRESS_DIRECT_NAV_COUNTERS + PERSISTENT_HARMFUL_FALL_COUNTERS
		+ HAZARD_RESIDENCE_COUNTERS
		+ (HAZARD_WATER_EGRESS_DIAGNOSTIC_OVERFLOW_COUNTER,)
		+ SINGLE_HARMFUL_FALL_PREFIX_COUNTERS + FALLING_HAZARD_RECOVERY_COUNTERS
		+ EXTERNAL_IMPULSE_FALL_WITNESS_COUNTERS
		+ DIRECT_HARMFUL_WATER_ENTRY_COUNTERS
		+ DIRECT_HARMFUL_WATER_ENTRY_CERTIFICATE_RESULT_COUNTERS
		+ WALKING_HITWALL_DISPATCH_COUNTERS
        + CONFIRMED_PICKUP_COUNTERS + (PICKUP_SOURCE_CONSUMED_UNCONFIRMED_COUNTER,)
        + NAVIGATION_COVERAGE_COUNTERS + (
            "navigation_coverage_fraction", "navigation_coverage_union_fraction"))
})
OPTIONAL_EXACT_COUNTERS = (
    PAIN_LEDGE_EXACT_COUNTERS + WALL_ADJUST_EXACT_COUNTERS + MOVE_STALL_EXACT_COUNTERS
    + MOVE_STALL_RECOVERY_EXACT_COUNTERS
    + FAILED_NAVIGATION_EXACT_COUNTERS + HARMFUL_ZONE_ESCAPE_EXACT_COUNTERS
	+ HAZARD_SWIM_EGRESS_EXACT_COUNTERS
	+ HAZARD_SWIM_EGRESS_PLANNER_HANDOFF_OUTCOME_COUNTERS
	+ FALLING_PRE_MOVE_ANCHOR_COUNTERS
	+ HAZARD_SWIM_EGRESS_LIVE_COUNTERS
	+ HAZARD_SWIM_EGRESS_DIRECT_NAV_COUNTERS
	+ HAZARD_RESIDENCE_COUNTERS
	+ (HAZARD_WATER_EGRESS_DIAGNOSTIC_OVERFLOW_COUNTER,)
	+ FALLING_HAZARD_RECOVERY_COUNTERS
	+ EXTERNAL_IMPULSE_FALL_WITNESS_COUNTERS
    + DEATH_ATTRIBUTION_COUNTERS + DAMAGE_COUNTERS
    + FALLING_SEAM_SHADOW_COUNTERS + FALLING_SEAM_DETAILED_COUNTERS
    + WALKING_STEP_PREFLIGHT_COUNTERS + FALLING_PARITY_COUNTERS
    + VERTICAL_PAIN_COLUMN_COUNTERS + PERSISTENT_HARMFUL_FALL_COUNTERS
    + SINGLE_HARMFUL_FALL_PREFIX_COUNTERS
	+ DIRECT_HARMFUL_WATER_ENTRY_COUNTERS
	+ DIRECT_HARMFUL_WATER_ENTRY_CERTIFICATE_RESULT_COUNTERS
    + WALKING_HITWALL_DISPATCH_COUNTERS
    + WALKING_STEP_PREFLIGHT_POSITIVE_DPS_VETO_COUNTERS
    + CONFIRMED_PICKUP_COUNTERS + (PICKUP_SOURCE_CONSUMED_UNCONFIRMED_COUNTER,)
    + (
        "navigation_coverage_visited_nodes_exact",
        "navigation_coverage_union_visited_nodes_exact",
        "move_stall_recovery_decision_record_overflows_exact",
    )
)
TARGET_SELECTION_COUNTERS = (
    "target_selection_outermost_calls_exact",
    "target_selection_nested_calls_exact",
    "target_selection_accepted_target_changes_exact",
    "target_selection_accepted_same_target_exact",
    "target_selection_rejected_or_unchanged_exact",
    "target_selection_missing_results_exact",
    "target_selection_invalid_identifier_exact",
    "target_selection_tracker_capacity_exceeded_exact",
    "target_selection_record_overflows_exact",
    "target_selection_integrity_failures_exact",
)
OPTIONAL_EXACT_COUNTERS += TARGET_SELECTION_COUNTERS
PICK_TARGET_COUNTERS = (
    "pick_target_observations_exact",
    "pick_target_candidates_exact",
    "pick_target_self_rejects_exact",
    "pick_target_dead_rejects_exact",
    "pick_target_living_candidates_exact",
    "pick_target_living_skipped_by_current_predicate_exact",
    "pick_target_team_rejects_exact",
    "pick_target_living_geometry_eligible_exact",
    "pick_target_living_line_of_sight_eligible_exact",
    "pick_target_returned_targets_exact",
    "pick_target_returned_living_targets_exact",
    "pick_target_no_result_with_living_line_of_sight_candidate_exact",
    "pick_target_observation_overflows_exact",
    "pick_target_integrity_failures_exact",
)
OPTIONAL_EXACT_COUNTERS += PICK_TARGET_COUNTERS
PAWN_CAN_SEE_COUNTERS = (
    "pawn_can_see_observations_exact",
    "pawn_can_see_returned_visible_exact",
    "pawn_can_see_legacy_corrected_divergences_exact",
    "pawn_can_see_observation_overflows_exact",
    "pawn_can_see_integrity_failures_exact",
)
OPTIONAL_EXACT_COUNTERS += PAWN_CAN_SEE_COUNTERS
FINITE_MOVE_COMMAND_GUARD_COUNTERS = (
    "finite_move_command_guard_rejections_exact",
    "finite_move_command_guard_diagnostic_overflows_exact",
)
OPTIONAL_EXACT_COUNTERS += FINITE_MOVE_COMMAND_GUARD_COUNTERS
WARN_TARGET_COUNTERS = (
    "warn_target_observations_exact",
    "try_to_duck_observations_exact",
    "warn_target_exact_nested_try_to_duck_links_exact",
    "warn_target_observation_overflows_exact",
    "warn_target_integrity_failures_exact",
)
WARNING_DODGE_LAUNCH_COUNTERS = (
    "warning_dodge_launches_exact",
    "warning_dodge_launch_overflows_exact",
)
WARNING_DODGE_TERMINAL_COUNTERS = (
    "warning_dodge_terminal_outcomes_exact",
    "warning_dodge_terminal_unknown_exact",
    "warning_dodge_terminal_overflows_exact",
)
OPTIONAL_EXACT_COUNTERS += (WARN_TARGET_COUNTERS + WARNING_DODGE_LAUNCH_COUNTERS
                            + WARNING_DODGE_TERMINAL_COUNTERS)
INVENTORY_DIRECT_REACH_SUPPORT_COUNTERS = (
    "inventory_direct_reach_support_observations_exact",
    "inventory_direct_reach_support_safe_supported_exact",
    "inventory_direct_reach_support_safe_unsupported_no_observed_hazard_exact",
    "inventory_direct_reach_support_unsafe_harmful_foot_zone_exact",
    "inventory_direct_reach_support_unsafe_unsupported_over_harmful_exact",
    "inventory_direct_reach_support_unavailable_exact",
    "inventory_direct_reach_support_diagnostic_overflows_exact",
)
OPTIONAL_EXACT_COUNTERS += INVENTORY_DIRECT_REACH_SUPPORT_COUNTERS
MOVE_STALL_DECISION_RECORD_OVERFLOW_COUNTER = \
    "move_stall_recovery_decision_record_overflows_exact"
OPTIONAL_CUMULATIVE_NUMBERS = ("move_stall_eligible_seconds",)
OPTIONAL_CUMULATIVE_METRICS = OPTIONAL_EXACT_COUNTERS + OPTIONAL_CUMULATIVE_NUMBERS
OPTIONAL_STATIC_METRICS = ("navigation_coverage_catalog_nodes_exact",)
MOVE_STALL_LEGACY_TELEMETRY_GROUP = (
    "move_stall_detections_exact", "move_stall_episode_resets_exact",
    "move_stall_eligible_seconds",
)
MOVE_STALL_TELEMETRY_GROUP = (
    "move_stall_detections_exact", "move_stall_forced_replans_exact",
    "move_stall_eligible_seconds",
)
MOVE_STALL_ATTRIBUTED_TELEMETRY_GROUP = (
    "move_stall_detections_exact", "move_stall_episode_resets_exact",
    "move_stall_forced_replans_exact", "move_stall_navigation_forced_replans_exact",
    "move_stall_targetless_move_to_timeouts_exact", "move_stall_eligible_seconds",
)
MOVE_STALL_DIRECT_ACTOR_ATTRIBUTED_TELEMETRY_GROUP = (
    *MOVE_STALL_ATTRIBUTED_TELEMETRY_GROUP,
    "move_stall_direct_actor_move_toward_timeouts_exact",
)
OPTIONAL_DIAGNOSTIC_FIELDS = (
    "physics_mode", "latent_action", "acceleration", "destination", "move_timer",
    "move_target_identity", "move_target_name", "walking_step_preflight_diagnostics",
    "walking_step_preflight_positive_dps_veto_actions",
    "falling_parity_realized_records", "vertical_pain_column_diagnostics",
    "hazard_water_egress_diagnostics", "hazard_death_partition_records",
    "move_stall_recovery_episodes", "move_stall_recovery_decisions",
    "walking_hitwall_dispatch_diagnostics", "target_selection_records", "pick_target_records",
    "pawn_can_see_records",
    "finite_move_command_guard_diagnostics",
    "inventory_direct_reach_support_diagnostics",
)
HAZARD_DEATH_KILLER_RELATIONS = {"none", "self_player", "enemy_player", "non_player"}
HAZARD_DEATH_ATTRIBUTIONS = {
    "direct_self_kill", "direct_enemy_kill", "unassisted_environmental_death",
    "recent_enemy_contributed_environmental_death_proxy", "ambiguous_death",
}
HAZARD_DEATH_ENVIRONMENTAL_SOURCES = {
    "", "pain_timer", "fell_out_of_world", "take_falling_damage",
    "mover_encroaching_on", "landed",
}
HAZARD_DEATH_PREFIXES = {
    "none", "water_egress_death", "falling_death_without_water_egress",
}
FALLING_HAZARD_CORRELATIONS = {
    "pending", "confirmed_harmful_forecast", "forecast_only", "actual_only",
    "confirmed_no_harmful_observation", "ambiguous", "unknown",
}
PHYSICS_MODES = {
    "", "None", "Walking", "Falling", "Swimming", "Flying", "Rotating", "Projectile",
    "Rolling", "Interpolating", "MovingBrush", "Spider", "Trailer", "Unknown",
}
LATENT_ACTIONS = {
    "", "Continue", "Stop", "Sleep", "FinishAnim", "FinishInterpolation", "MoveTo",
    "MoveToward", "StrafeTo", "StrafeFacing", "TurnTo", "TurnToward", "WaitForLanding",
    "Unknown",
}
WALKING_STEP_PREFLIGHT_COLLISIONS = {
    "unknown", "clear", "static_bsp", "mover", "dynamic_actor",
}
WALKING_STEP_PREFLIGHT_ZONES = {"unknown", "safe", "pain", "water"}
WALKING_HITWALL_DISPATCH_BLOCKERS = {
    "unknown", "static_world", "mover", "dynamic_actor",
}
WALKING_HITWALL_DISPATCH_CONTACT_PHASES = {
    "primary_forward", "aligned_slide", "forward_retry_result",
}
WALKING_STEP_PREFLIGHT_PHASES = {
    "precommit_provisional", "post_mayfall_confirmation",
}
WALKING_STEP_PREFLIGHT_TRANSITIONS = {
    "abort", "restore_grounded", "begin_falling", "post_callback_evidence_changed",
    "post_callback_forecast_rejected",
}
WALKING_STEP_PREFLIGHT_POSITIVE_DPS_VETO_OUTCOMES = {
    "legacy_pain_ledge_superseded", "rollback_test_rejected", "rollback_actual_rejected",
    "applied",
}
FALLING_PARITY_REALIZED_OUTCOMES = {
    "episode_started", "matched_clear", "matched_landing", "mismatch", "unknown",
    "callback_barrier",
    "pain_entered", "landed", "died", "continuity_lost",
}
FALLING_PARITY_REALIZED_STEP_OUTCOMES = {
    "matched_clear", "matched_landing", "mismatch", "unknown", "callback_barrier",
}
FALLING_PARITY_REALIZED_COLLISIONS = {
    "unknown", "clear", "static_world", "mover", "dynamic_actor",
}
FALLING_PARITY_REALIZED_MAX_STEPS = 96
FALLING_PARITY_REALIZED_MATCH_EPSILON = 0.001
VERTICAL_PAIN_COLUMN_SOURCES = {
    "unsupported_walk_commit", "existing_falling_commit",
    "post_wall_deflection_commit", "aligned_continuation_commit",
    "third_move_continuation_commit", "callback_return_commit",
    "script_tick_transition_commit",
    "external_impulse_commit", "horizon_continuation_commit",
}
VERTICAL_PAIN_COLUMN_FORECASTS = {
    "unknown", "no_harmful_pain_observed", "harmful_pain_observed",
}
VERTICAL_PAIN_COLUMN_TERMINALS = {
    "harmful_pain_entered", "landed", "died", "callback_boundary",
    "external_impulse_boundary", "continuity_lost", "water_physics_boundary",
    "observation_horizon_exhausted", "superseded_by_committed_source",
    "swept_segment_budget_exceeded", "invalid_observation",
}
VERTICAL_PAIN_COLUMN_CORRELATIONS = {
    "unknown", "ambiguous", "confirmed_harmful_forecast", "forecast_only",
    "actual_only", "confirmed_no_harmful_observation",
}
VERTICAL_PAIN_COLUMN_COLLISIONS = {
    "unknown", "clear", "static_world", "mover", "dynamic_actor",
}
VERTICAL_PAIN_COLUMN_MAX_SEGMENTS = 256
HAZARD_WATER_EGRESS_TRANSITION_SOURCES = {
    "unknown", "falling_direct_sweep", "falling_non_direct_sweep", "swimming_motion",
}
VERTICAL_PAIN_COLUMN_ALIGNED_COMMAND_PROVENANCE = {
    "not_aligned_continuation", "no_command_witness", "nonstatic_collision",
    "no_live_movement_command", "command_token_changed", "latent_state_changed",
    "move_target_changed", "destination_changed", "acceleration_changed",
    "intact_command_but_no_action_lead",
}
HAZARD_WATER_EGRESS_TERMINALS = {
    "primary_zone_cleared", "death_before_exit", "life_reset", "episode_abandoned",
}
HAZARD_WATER_EGRESS_STATIC_WALK_CERTIFICATES = {
    "not_attempted_missing_anchor", "certified_static_walk_continuation",
    "no_eligible_direct_first_hop", "no_static_walk_continuation",
    "search_budget_exhausted",
}


class QualityError(ValueError):
    pass


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise QualityError(f"{context} must be a JSON object")
    return value


def _string(fields: dict[str, Any], name: str, context: str, *, nonempty: bool = False) -> str:
    value = fields.get(name)
    if not isinstance(value, str) or (nonempty and not value):
        suffix = " non-empty" if nonempty else ""
        raise QualityError(f"{context}.{name} must be a{suffix} string")
    return value


def _integer(value: Any, context: str, *, minimum: int | None = None) -> int:
    if isinstance(value, bool):
        raise QualityError(f"{context} must be an integer")
    try:
        result = int(value)
    except (TypeError, ValueError, OverflowError) as exc:
        raise QualityError(f"{context} must be an integer") from exc
    if isinstance(value, float) and value != result:
        raise QualityError(f"{context} must be an integer")
    if isinstance(value, str) and (str(result) != value or not value):
        raise QualityError(f"{context} must be a canonical decimal integer string")
    if minimum is not None and result < minimum:
        raise QualityError(f"{context} must be at least {minimum}")
    return result


def _strict_integer(value: Any, context: str, *, minimum: int | None = None,
                    maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise QualityError(f"{context} must be an integer")
    if minimum is not None and value < minimum:
        raise QualityError(f"{context} must be at least {minimum}")
    if maximum is not None and value > maximum:
        raise QualityError(f"{context} must be at most {maximum}")
    return value


def _number(value: Any, context: str, *, minimum: float | None = None) -> float:
    if isinstance(value, bool):
        raise QualityError(f"{context} must be a finite number")
    try:
        result = float(value)
    except (TypeError, ValueError, OverflowError) as exc:
        raise QualityError(f"{context} must be a finite number") from exc
    if not math.isfinite(result):
        raise QualityError(f"{context} must be a finite number")
    if minimum is not None and result < minimum:
        raise QualityError(f"{context} must be at least {minimum}")
    return result


def _boolean(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise QualityError(f"{context} must be a boolean")
    return value


def _exact_object(value: Any, context: str, fields: set[str]) -> dict[str, Any]:
    result = _object(value, context)
    missing = fields - result.keys()
    unexpected = result.keys() - fields
    if missing:
        raise QualityError(f"{context} is missing fields: {', '.join(sorted(missing))}")
    if unexpected:
        raise QualityError(f"{context} has unexpected fields: {', '.join(sorted(unexpected))}")
    return result


def _diagnostic_vector(value: Any, context: str) -> dict[str, float]:
    vector = _exact_object(value, context, {"x", "y", "z"})
    return {axis: _number(vector.get(axis), f"{context}.{axis}") for axis in "xyz"}


def _nullable_diagnostic_vector(value: Any, context: str) -> dict[str, float] | None:
    if value is None:
        return None
    return _diagnostic_vector(value, context)


def _nullable_number(value: Any, context: str) -> float | None:
    if value is None:
        return None
    return _number(value, context)


def _walking_hitwall_dispatch_diagnostic(value: Any, context: str) -> dict[str, Any]:
    fields = _exact_object(value, context, {
        "source_pawn_actor", "sequence", "contact_phase", "hit_normal", "velocity", "min_hit_wall",
        "normal_velocity_dot", "valid", "legacy_vertical_wall_band",
        "min_hit_wall_dispatch", "blocker", "callback_dispatched",
        "physics_changed_by_callback", "pawn_deleted_by_callback",
    })
    valid = _boolean(fields.get("valid"), f"{context}.valid")
    hit_normal = _nullable_diagnostic_vector(
        fields.get("hit_normal"), f"{context}.hit_normal")
    velocity = _nullable_diagnostic_vector(fields.get("velocity"), f"{context}.velocity")
    min_hit_wall = _nullable_number(fields.get("min_hit_wall"), f"{context}.min_hit_wall")
    normal_velocity_dot = _nullable_number(
        fields.get("normal_velocity_dot"), f"{context}.normal_velocity_dot")
    if valid and any(item is None for item in (
            hit_normal, velocity, min_hit_wall, normal_velocity_dot)):
        raise QualityError(f"{context}: valid observation requires finite geometry")
    blocker = _string(fields, "blocker", context, nonempty=True)
    if blocker not in WALKING_HITWALL_DISPATCH_BLOCKERS:
        raise QualityError(f"{context}.blocker is not recognized")
    contact_phase = _string(fields, "contact_phase", context, nonempty=True)
    if contact_phase not in WALKING_HITWALL_DISPATCH_CONTACT_PHASES:
        raise QualityError(f"{context}.contact_phase is not recognized")
    return {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(
            fields.get("sequence"), f"{context}.sequence", minimum=0),
        "contact_phase": contact_phase,
        "hit_normal": hit_normal,
        "velocity": velocity,
        "min_hit_wall": min_hit_wall,
        "normal_velocity_dot": normal_velocity_dot,
        "valid": valid,
        "legacy_vertical_wall_band": _boolean(
            fields.get("legacy_vertical_wall_band"),
            f"{context}.legacy_vertical_wall_band"),
        "min_hit_wall_dispatch": _boolean(
            fields.get("min_hit_wall_dispatch"), f"{context}.min_hit_wall_dispatch"),
        "blocker": blocker,
        "callback_dispatched": _boolean(
            fields.get("callback_dispatched"), f"{context}.callback_dispatched"),
        "physics_changed_by_callback": _boolean(
            fields.get("physics_changed_by_callback"),
            f"{context}.physics_changed_by_callback"),
        "pawn_deleted_by_callback": _boolean(
            fields.get("pawn_deleted_by_callback"),
            f"{context}.pawn_deleted_by_callback"),
    }


def _walking_hitwall_dispatch_diagnostics(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _walking_hitwall_dispatch_diagnostic(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


def _walking_step_preflight_probe(value: Any, context: str) -> dict[str, Any]:
    probe = _exact_object(value, context, {"collision", "fraction", "delta", "normal"})
    collision = _string(probe, "collision", context, nonempty=True)
    if collision not in WALKING_STEP_PREFLIGHT_COLLISIONS:
        raise QualityError(f"{context}.collision is not recognized")
    fraction = _number(probe.get("fraction"), f"{context}.fraction", minimum=0.0)
    if fraction > 1.0:
        raise QualityError(f"{context}.fraction must be at most 1.0")
    return {
        "collision": collision,
        "fraction": fraction,
        "delta": _diagnostic_vector(probe.get("delta"), f"{context}.delta"),
        "normal": _diagnostic_vector(probe.get("normal"), f"{context}.normal"),
    }


def _walking_step_preflight_forecast(value: Any, context: str) -> dict[str, Any]:
    fields = _exact_object(value, context, {
        "attempted", "origin", "velocity", "acceleration", "gravity_known", "gravity",
        "complete", "total_drop", "continuation_count", "landing_collision",
        "landing_normal", "landing_zone", "pain_damage_per_sec_known",
        "pain_damage_per_sec", "hit_fractions",
    })
    attempted = _boolean(fields.get("attempted"), f"{context}.attempted")
    gravity_known = _boolean(fields.get("gravity_known"), f"{context}.gravity_known")
    complete = _boolean(fields.get("complete"), f"{context}.complete")
    continuation_count = _strict_integer(
        fields.get("continuation_count"), f"{context}.continuation_count", minimum=0, maximum=2)
    landing_collision = _string(fields, "landing_collision", context, nonempty=True)
    if landing_collision not in WALKING_STEP_PREFLIGHT_COLLISIONS:
        raise QualityError(f"{context}.landing_collision is not recognized")
    landing_zone = _string(fields, "landing_zone", context, nonempty=True)
    if landing_zone not in WALKING_STEP_PREFLIGHT_ZONES:
        raise QualityError(f"{context}.landing_zone is not recognized")
    pain_damage_per_sec_known = _boolean(
        fields.get("pain_damage_per_sec_known"), f"{context}.pain_damage_per_sec_known")
    raw_pain_damage_per_sec = fields.get("pain_damage_per_sec")
    if raw_pain_damage_per_sec is None:
        pain_damage_per_sec = None
    else:
        pain_damage_per_sec = _number(
            raw_pain_damage_per_sec, f"{context}.pain_damage_per_sec")
    if not pain_damage_per_sec_known and pain_damage_per_sec is not None:
        raise QualityError(
            f"{context}.pain_damage_per_sec must be null when its value is unknown")
    fractions_raw = fields.get("hit_fractions")
    if not isinstance(fractions_raw, list):
        raise QualityError(f"{context}.hit_fractions must be an array")
    if len(fractions_raw) > 3:
        raise QualityError(f"{context}.hit_fractions must contain at most 3 values")
    fractions = []
    for index, raw_fraction in enumerate(fractions_raw):
        fraction = _number(raw_fraction, f"{context}.hit_fractions[{index}]", minimum=0.0)
        if fraction > 1.0:
            raise QualityError(f"{context}.hit_fractions[{index}] must be at most 1.0")
        fractions.append(fraction)
    if len(fractions) < continuation_count or len(fractions) > continuation_count + 1:
        raise QualityError(
            f"{context}.hit_fractions do not match the continuation count")
    if complete and len(fractions) != continuation_count + 1:
        raise QualityError(
            f"{context}.complete forecast requires one landing fraction after continuations")
    if complete and not attempted:
        raise QualityError(f"{context}.complete forecast must have been attempted")
    if attempted and not gravity_known:
        raise QualityError(f"{context}.attempted forecast requires known gravity")
    if not attempted and (complete or continuation_count or fractions):
        raise QualityError(f"{context}.unattempted forecast must not contain results")
    return {
        "attempted": attempted,
        "origin": _diagnostic_vector(fields.get("origin"), f"{context}.origin"),
        "velocity": _diagnostic_vector(fields.get("velocity"), f"{context}.velocity"),
        "acceleration": _diagnostic_vector(
            fields.get("acceleration"), f"{context}.acceleration"),
        "gravity_known": gravity_known,
        "gravity": _diagnostic_vector(fields.get("gravity"), f"{context}.gravity"),
        "complete": complete,
        "total_drop": _number(fields.get("total_drop"), f"{context}.total_drop"),
        "continuation_count": continuation_count,
        "landing_collision": landing_collision,
        "landing_normal": _diagnostic_vector(
            fields.get("landing_normal"), f"{context}.landing_normal"),
        "landing_zone": landing_zone,
        "pain_damage_per_sec_known": pain_damage_per_sec_known,
        "pain_damage_per_sec": pain_damage_per_sec,
        "hit_fractions": fractions,
    }


def _walking_step_preflight_diagnostic(value: Any, context: str) -> dict[str, Any]:
    # The command token was added after the initial v2 diagnostics. Normalize
    # earlier artifacts before strict field validation; zero means unavailable.
    if isinstance(value, dict) and "movement_command_token" not in value:
        value = {**value, "movement_command_token": "0"}
    fields = _exact_object(value, context, {
        "source_pawn_actor", "sequence", "life_generation", "invocation_token",
        "movement_command_token", "walking_iteration", "phase",
        "transition_outcome", "reason", "origin", "predicted_unsupported_endpoint",
        "actual_unsupported_endpoint", "semantic_target", "semantic_destination",
        "start_support", "step_up", "forward", "actual_step_down", "support_probe",
        "fall_forecast",
    })
    phase = _string(fields, "phase", context, nonempty=True)
    if phase not in WALKING_STEP_PREFLIGHT_PHASES:
        raise QualityError(f"{context}.phase is not recognized")
    transition = _string(fields, "transition_outcome", context)
    if phase == "precommit_provisional" and transition:
        raise QualityError(f"{context}.transition_outcome must be empty for provisional records")
    if phase == "post_mayfall_confirmation" and transition not in \
            WALKING_STEP_PREFLIGHT_TRANSITIONS:
        raise QualityError(f"{context}.transition_outcome is not recognized")
    reason = _string(fields, "reason", context, nonempty=True)
    if reason not in WALKING_STEP_PREFLIGHT_REASON_COUNTERS:
        raise QualityError(f"{context}.reason is not recognized")
    if phase == "post_mayfall_confirmation":
        harmful_reason = "walking_step_preflight_reason_harmful_pain_fall_exact"
        if transition == "post_callback_forecast_rejected" and reason == harmful_reason:
            raise QualityError(
                f"{context}.reason must identify the rejected post-callback forecast")
        if transition != "post_callback_forecast_rejected" and reason != harmful_reason:
            raise QualityError(
                f"{context}.reason must preserve the provisional authorization reason")
    forecast = _walking_step_preflight_forecast(
        fields.get("fall_forecast"), f"{context}.fall_forecast")
    requires_exact_dps = phase == "precommit_provisional" \
        or transition == "begin_falling" \
        or transition == "post_callback_forecast_rejected"
    if requires_exact_dps and reason == "walking_step_preflight_reason_unknown_pain_damage_per_sec_exact" \
            and (forecast["pain_damage_per_sec_known"]
                 or forecast["pain_damage_per_sec"] is not None):
        raise QualityError(
            f"{context}.unknown pain damage per sec reason requires an unknown DPS value")
    if requires_exact_dps and reason == "walking_step_preflight_reason_non_finite_pain_damage_per_sec_exact" \
            and (not forecast["pain_damage_per_sec_known"]
                 or forecast["pain_damage_per_sec"] is not None):
        raise QualityError(
            f"{context}.non-finite pain damage per sec reason requires a known non-finite DPS value")
    if requires_exact_dps and reason == "walking_step_preflight_reason_non_harmful_pain_damage_per_sec_exact" \
            and (not forecast["pain_damage_per_sec_known"]
                 or forecast["pain_damage_per_sec"] is None
                 or forecast["pain_damage_per_sec"] > 0.0):
        raise QualityError(
            f"{context}.non-harmful pain damage per sec reason requires known non-positive DPS")
    if requires_exact_dps and reason == "walking_step_preflight_reason_harmful_pain_fall_exact" \
            and (not forecast["pain_damage_per_sec_known"]
                 or forecast["pain_damage_per_sec"] is None
                 or forecast["pain_damage_per_sec"] <= 0.0):
        raise QualityError(
            f"{context}.harmful pain fall reason requires known positive DPS")
    return {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(fields.get("sequence"), f"{context}.sequence", minimum=0),
        "life_generation": _integer(
            fields.get("life_generation"), f"{context}.life_generation", minimum=0),
        "invocation_token": _integer(
            fields.get("invocation_token"), f"{context}.invocation_token", minimum=0),
        # Pre-token v2 artifacts remain valid but cannot supply a causal witness.
        "movement_command_token": _integer(
            fields.get("movement_command_token"), f"{context}.movement_command_token",
            minimum=0) if "movement_command_token" in fields else 0,
        "walking_iteration": _strict_integer(
            fields.get("walking_iteration"), f"{context}.walking_iteration", minimum=0,
            maximum=4),
        "phase": phase,
        "transition_outcome": transition,
        "reason": reason,
        "origin": _diagnostic_vector(fields.get("origin"), f"{context}.origin"),
        "predicted_unsupported_endpoint": _diagnostic_vector(
            fields.get("predicted_unsupported_endpoint"),
            f"{context}.predicted_unsupported_endpoint"),
        "actual_unsupported_endpoint": _diagnostic_vector(
            fields.get("actual_unsupported_endpoint"), f"{context}.actual_unsupported_endpoint"),
        "semantic_target": _string(fields, "semantic_target", context),
        "semantic_destination": _diagnostic_vector(
            fields.get("semantic_destination"), f"{context}.semantic_destination"),
        "start_support": _walking_step_preflight_probe(
            fields.get("start_support"), f"{context}.start_support"),
        "step_up": _walking_step_preflight_probe(fields.get("step_up"), f"{context}.step_up"),
        "forward": _walking_step_preflight_probe(fields.get("forward"), f"{context}.forward"),
        "actual_step_down": _walking_step_preflight_probe(
            fields.get("actual_step_down"), f"{context}.actual_step_down"),
        "support_probe": _walking_step_preflight_probe(
            fields.get("support_probe"), f"{context}.support_probe"),
        "fall_forecast": forecast,
    }


def _walking_step_preflight_diagnostics(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _walking_step_preflight_diagnostic(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


def _walking_step_preflight_positive_dps_veto_action(value: Any, context: str) -> dict[str, Any]:
    fields = _exact_object(value, context, {
        "source_pawn_actor", "sequence", "life_generation", "invocation_token",
        "walking_iteration", "outcome", "legacy_pain_ledge_superseded", "rollback_delta",
        "rollback_test_attempted", "rollback_test_fraction", "rollback_actual_attempted",
        "rollback_actual_fraction", "forced_replan",
    })
    outcome = _string(fields, "outcome", context, nonempty=True)
    if outcome not in WALKING_STEP_PREFLIGHT_POSITIVE_DPS_VETO_OUTCOMES:
        raise QualityError(f"{context}.outcome is not recognized")
    result = {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(fields.get("sequence"), f"{context}.sequence", minimum=0),
        "life_generation": _integer(fields.get("life_generation"), f"{context}.life_generation", minimum=0),
        "invocation_token": _integer(fields.get("invocation_token"), f"{context}.invocation_token", minimum=0),
        "walking_iteration": _integer(fields.get("walking_iteration"), f"{context}.walking_iteration", minimum=0),
        "outcome": outcome,
        "legacy_pain_ledge_superseded": _boolean(
            fields.get("legacy_pain_ledge_superseded"), f"{context}.legacy_pain_ledge_superseded"),
        "rollback_delta": _diagnostic_vector(fields.get("rollback_delta"), f"{context}.rollback_delta"),
        "rollback_test_attempted": _boolean(
            fields.get("rollback_test_attempted"), f"{context}.rollback_test_attempted"),
        "rollback_test_fraction": _number(
            fields.get("rollback_test_fraction"), f"{context}.rollback_test_fraction", minimum=0.0),
        "rollback_actual_attempted": _boolean(
            fields.get("rollback_actual_attempted"), f"{context}.rollback_actual_attempted"),
        "rollback_actual_fraction": _number(
            fields.get("rollback_actual_fraction"), f"{context}.rollback_actual_fraction", minimum=0.0),
        "forced_replan": _boolean(fields.get("forced_replan"), f"{context}.forced_replan"),
    }
    if result["rollback_test_fraction"] > 1.0 or result["rollback_actual_fraction"] > 1.0:
        raise QualityError(f"{context}: rollback fractions must be at most 1")
    return result


def _walking_step_preflight_positive_dps_veto_actions(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _walking_step_preflight_positive_dps_veto_action(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


def _falling_parity_realized_record(value: Any, context: str) -> dict[str, Any]:
    fields = _exact_object(value, context, {
        "source_pawn_actor", "life_generation", "invocation_token", "walking_iteration",
        "step_ordinal", "outcome", "elapsed", "collision", "hit_fraction", "hit_normal",
        "velocity_error", "requested_delta_error", "endpoint_error",
        "callback_barrier_mask",
    })
    outcome = _string(fields, "outcome", context, nonempty=True)
    if outcome not in FALLING_PARITY_REALIZED_OUTCOMES:
        raise QualityError(f"{context}.outcome is not recognized")
    collision = _string(fields, "collision", context, nonempty=True)
    if collision not in FALLING_PARITY_REALIZED_COLLISIONS:
        raise QualityError(f"{context}.collision is not recognized")
    hit_fraction = _number(fields.get("hit_fraction"), f"{context}.hit_fraction", minimum=0.0)
    if hit_fraction > 1.0:
        raise QualityError(f"{context}.hit_fraction must be at most 1.0")
    elapsed = _number(fields.get("elapsed"), f"{context}.elapsed", minimum=0.0)
    velocity_error = _number(
        fields.get("velocity_error"), f"{context}.velocity_error", minimum=0.0)
    requested_delta_error = _number(
        fields.get("requested_delta_error"),
        f"{context}.requested_delta_error", minimum=0.0)
    endpoint_error = _number(
        fields.get("endpoint_error"), f"{context}.endpoint_error", minimum=0.0)
    hit_normal = _diagnostic_vector(fields.get("hit_normal"), f"{context}.hit_normal")
    callback_mask = _integer(
        fields.get("callback_barrier_mask"), f"{context}.callback_barrier_mask", minimum=0)
    if callback_mask > 0x1ff:
        raise QualityError(f"{context}.callback_barrier_mask must be at most 511")
    if outcome == "callback_barrier" and callback_mask == 0:
        raise QualityError(f"{context}.callback_barrier outcome requires a nonzero callback mask")
    if outcome != "callback_barrier" and callback_mask != 0:
        raise QualityError(f"{context}.callback_barrier_mask requires a callback_barrier outcome")
    if outcome in FALLING_PARITY_REALIZED_STEP_OUTCOMES and elapsed <= 0.0:
        raise QualityError(f"{context}.elapsed must be positive for a realized step")
    if outcome in {"matched_clear", "matched_landing", "mismatch"}:
        clear_evidence = collision == "clear" and hit_fraction == 1.0
        landing_evidence = (
            collision == "static_world" and hit_fraction < 1.0
            and hit_normal["z"] > 0.7)
        if outcome == "matched_clear" and not clear_evidence:
            raise QualityError(
                f"{context}: matched_clear requires clear collision evidence at full fraction")
        if outcome == "matched_landing" and not landing_evidence:
            raise QualityError(
                f"{context}: matched_landing requires walkable static-world landing evidence")
        if outcome == "mismatch" and not (clear_evidence or landing_evidence):
            raise QualityError(
                f"{context}: mismatch requires clear or walkable static-world landing evidence")
        errors = (velocity_error, requested_delta_error, endpoint_error)
        if outcome in {"matched_clear", "matched_landing"} and any(
                error > FALLING_PARITY_REALIZED_MATCH_EPSILON for error in errors):
            raise QualityError(
                f"{context}: {outcome} errors must be at most "
                f"{FALLING_PARITY_REALIZED_MATCH_EPSILON}")
        if outcome == "mismatch" and not any(
                error > FALLING_PARITY_REALIZED_MATCH_EPSILON for error in errors):
            raise QualityError(
                f"{context}: mismatch requires at least one error greater than "
                f"{FALLING_PARITY_REALIZED_MATCH_EPSILON}")

    record = {
        "source_pawn_actor": _string(
            fields, "source_pawn_actor", context, nonempty=True),
        "life_generation": _integer(
            fields.get("life_generation"), f"{context}.life_generation", minimum=0),
        "invocation_token": _integer(
            fields.get("invocation_token"), f"{context}.invocation_token", minimum=1),
        "walking_iteration": _strict_integer(
            fields.get("walking_iteration"), f"{context}.walking_iteration",
            minimum=0, maximum=4),
        "step_ordinal": _integer(
            fields.get("step_ordinal"), f"{context}.step_ordinal", minimum=0),
        "outcome": outcome,
        "elapsed": elapsed,
        "collision": collision,
        "hit_fraction": hit_fraction,
        "hit_normal": hit_normal,
        "velocity_error": velocity_error,
        "requested_delta_error": requested_delta_error,
        "endpoint_error": endpoint_error,
        "callback_barrier_mask": callback_mask,
    }
    if record["life_generation"] > 0xffffffffffffffff:
        raise QualityError(f"{context}.life_generation must be at most 18446744073709551615")
    if record["invocation_token"] > 0xffffffffffffffff:
        raise QualityError(f"{context}.invocation_token must be at most 18446744073709551615")
    if record["step_ordinal"] > 0xffffffff:
        raise QualityError(f"{context}.step_ordinal must be at most 4294967295")
    evidence_free = {
        "episode_started", "pain_entered", "landed", "died", "continuity_lost",
    }
    if outcome in evidence_free and (
            elapsed != 0.0 or collision != "unknown" or hit_fraction != 1.0
            or any(record["hit_normal"][axis] != 0.0 for axis in "xyz")
            or velocity_error != 0.0 or requested_delta_error != 0.0
            or endpoint_error != 0.0):
        raise QualityError(f"{context}: {outcome} must not contain realized-step evidence")
    if outcome == "episode_started" and record["step_ordinal"] != 0:
        raise QualityError(f"{context}.step_ordinal must be zero for episode_started")
    return record


def _falling_parity_realized_records(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _falling_parity_realized_record(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


def _vertical_pain_column_zone(value: Any, context: str) -> dict[str, Any]:
    fields = _exact_object(value, context, {"known", "zone_actor_id", "zone_number"})
    known = _boolean(fields.get("known"), f"{context}.known")
    actor = _strict_integer(
        fields.get("zone_actor_id"), f"{context}.zone_actor_id",
        minimum=0, maximum=0xffffffff)
    number = _strict_integer(
        fields.get("zone_number"), f"{context}.zone_number",
        minimum=0, maximum=0xffffffff)
    if known and actor == 0:
        raise QualityError(f"{context}.zone_actor_id must be positive when known")
    if not known and (actor != 0 or number != 0):
        raise QualityError(f"{context}: unknown zone identity must use zero identifiers")
    return {"known": known, "zone_actor_id": actor, "zone_number": number}


def _same_vertical_pain_column_zone(left: dict[str, Any], right: dict[str, Any]) -> bool:
    return left["known"] and right["known"] \
        and left["zone_actor_id"] == right["zone_actor_id"] \
        and left["zone_number"] == right["zone_number"]


def _vertical_pain_column_id(value: Any, context: str, maximum: int) -> int:
    result = _integer(value, context, minimum=0)
    if not isinstance(value, str):
        raise QualityError(f"{context} must be a canonical decimal integer string")
    if result > maximum:
        raise QualityError(f"{context} must be at most {maximum}")
    return result


def _vertical_pain_column_source(fields: dict[str, Any], name: str,
                                 context: str) -> str:
    source = _string(fields, name, context, nonempty=True)
    if source not in VERTICAL_PAIN_COLUMN_SOURCES:
        raise QualityError(f"{context}.{name} is not recognized")
    return source


def _validate_vertical_pain_column_forecast_zones(
        forecast: str, expected_foot: dict[str, Any],
        expected_physics: dict[str, Any], expected_water: bool, context: str) -> None:
    if forecast == "harmful_pain_observed":
        if not expected_foot["known"] or not expected_physics["known"]:
            raise QualityError(f"{context}: harmful forecast requires known expected zones")
    elif expected_foot["known"] or expected_physics["known"] or expected_water:
        raise QualityError(f"{context}: non-harmful/unknown forecast must not expect a hazard zone")


def _vertical_pain_column_diagnostic(value: Any, context: str) -> dict[str, Any]:
    raw = _object(value, context)
    kind = _string(raw, "kind", context, nonempty=True)
    common = {
        "source_pawn_actor", "sequence", "life_id", "fall_episode_id",
        "generation_id", "kind",
    }
    start_fields = common | {
        "source", "forecast", "starting_physics_zone",
        "expected_harmful_foot_zone", "expected_harmful_physics_zone",
        "expected_harmful_water_entry", "swept_segment_budget",
        "elapsed_horizon", "precharged_elapsed", "aligned_command_provenance",
    }
    terminal_fields = common | {
        "source", "forecast", "terminal", "correlation",
        "starting_physics_zone", "expected_harmful_foot_zone",
        "expected_harmful_physics_zone", "last_observed_physics_zone",
        "observed_harmful_foot_zone", "observed_harmful_center_zone",
        "expected_harmful_water_entry",
        "swept_segment_budget", "swept_segment_count", "elapsed_horizon",
        "observed_elapsed", "has_positive_elapsed",
        "physics_zone_evidence_known", "harmful_foot_evidence_known",
        "harmful_center_evidence_known", "water_evidence_known",
        "entered_harmful_foot_zone", "entered_harmful_center_zone",
        "expected_harmful_path_matched", "causal_ambiguity",
        "actual_trajectory_unknown", "landing_collision", "aligned_command_provenance",
    }
    capacity_fields = common | {"attempted_source"}
    expected_fields = {
        "start": start_fields,
        "terminal": terminal_fields,
        "generation_capacity_exceeded": capacity_fields,
    }.get(kind)
    if expected_fields is None:
        raise QualityError(f"{context}.kind is not recognized")
    provenance_field = "aligned_command_provenance"
    fields = _exact_object(
        raw, context,
        expected_fields if provenance_field in raw
        else expected_fields - {provenance_field})
    record = {
        "source_pawn_actor": _string(
            fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _vertical_pain_column_id(
            fields.get("sequence"), f"{context}.sequence", 0xffffffffffffffff),
        "life_id": _vertical_pain_column_id(
            fields.get("life_id"), f"{context}.life_id", 0xffffffffffffffff),
        "fall_episode_id": _vertical_pain_column_id(
            fields.get("fall_episode_id"), f"{context}.fall_episode_id",
            0xffffffffffffffff),
        "generation_id": _vertical_pain_column_id(
            fields.get("generation_id"), f"{context}.generation_id", 0xffffffff),
        "kind": kind,
    }
    if record["sequence"] == 0 or record["life_id"] == 0 \
            or record["fall_episode_id"] == 0:
        raise QualityError(f"{context}: sequence, life, and fall episode IDs must be positive")
    if kind == "generation_capacity_exceeded":
        if record["generation_id"] != 0:
            raise QualityError(f"{context}.generation_id must be zero for capacity exhaustion")
        record["attempted_source"] = _vertical_pain_column_source(
            fields, "attempted_source", context)
        return record
    if record["generation_id"] == 0:
        raise QualityError(f"{context}.generation_id must be positive")

    source = _vertical_pain_column_source(fields, "source", context)
    forecast = _string(fields, "forecast", context, nonempty=True)
    if forecast not in VERTICAL_PAIN_COLUMN_FORECASTS:
        raise QualityError(f"{context}.forecast is not recognized")
    starting_zone = _vertical_pain_column_zone(
        fields.get("starting_physics_zone"), f"{context}.starting_physics_zone")
    expected_foot = _vertical_pain_column_zone(
        fields.get("expected_harmful_foot_zone"),
        f"{context}.expected_harmful_foot_zone")
    expected_physics = _vertical_pain_column_zone(
        fields.get("expected_harmful_physics_zone"),
        f"{context}.expected_harmful_physics_zone")
    expected_water = _boolean(
        fields.get("expected_harmful_water_entry"),
        f"{context}.expected_harmful_water_entry")
    if not starting_zone["known"]:
        raise QualityError(f"{context}.starting_physics_zone must be known")
    _validate_vertical_pain_column_forecast_zones(
        forecast, expected_foot, expected_physics, expected_water, context)
    segment_budget = _strict_integer(
        fields.get("swept_segment_budget"), f"{context}.swept_segment_budget",
        minimum=1, maximum=VERTICAL_PAIN_COLUMN_MAX_SEGMENTS)
    elapsed_horizon = _number(
        fields.get("elapsed_horizon"), f"{context}.elapsed_horizon", minimum=0.0)
    if elapsed_horizon <= 0.0:
        raise QualityError(f"{context}.elapsed_horizon must be positive")
    record.update({
        "source": source,
        "forecast": forecast,
        "starting_physics_zone": starting_zone,
        "expected_harmful_foot_zone": expected_foot,
        "expected_harmful_physics_zone": expected_physics,
        "expected_harmful_water_entry": expected_water,
        "swept_segment_budget": segment_budget,
        "elapsed_horizon": elapsed_horizon,
    })
    aligned_command_provenance = fields.get("aligned_command_provenance")
    if aligned_command_provenance is None:
        # Pre-v25 telemetry had no command witness. Retain that fact rather
        # than inferring bot controllability while keeping those artifacts
        # analyzable.
        aligned_command_provenance = (
            "no_command_witness" if source == "aligned_continuation_commit"
            else "not_aligned_continuation")
    else:
        aligned_command_provenance = _string(
            fields, "aligned_command_provenance", context, nonempty=True)
    if aligned_command_provenance not in VERTICAL_PAIN_COLUMN_ALIGNED_COMMAND_PROVENANCE:
        raise QualityError(f"{context}.aligned_command_provenance is not recognized")
    if (source == "aligned_continuation_commit") != (
            aligned_command_provenance != "not_aligned_continuation"):
        raise QualityError(
            f"{context}.aligned_command_provenance does not match the forecast source")
    record["aligned_command_provenance"] = aligned_command_provenance
    if kind == "start":
        precharged = _number(
            fields.get("precharged_elapsed"), f"{context}.precharged_elapsed", minimum=0.0)
        if precharged > elapsed_horizon:
            raise QualityError(f"{context}.precharged_elapsed exceeds elapsed_horizon")
        continuation = source in {
            "aligned_continuation_commit", "third_move_continuation_commit",
        }
        if continuation != (precharged > 0.0):
            raise QualityError(
                f"{context}.precharged_elapsed does not match the forecast source")
        record["precharged_elapsed"] = precharged
        return record

    terminal = _string(fields, "terminal", context, nonempty=True)
    if terminal not in VERTICAL_PAIN_COLUMN_TERMINALS:
        raise QualityError(f"{context}.terminal is not recognized")
    correlation = _string(fields, "correlation", context, nonempty=True)
    if correlation not in VERTICAL_PAIN_COLUMN_CORRELATIONS:
        raise QualityError(f"{context}.correlation is not recognized")
    landing = _string(fields, "landing_collision", context, nonempty=True)
    if landing not in VERTICAL_PAIN_COLUMN_COLLISIONS:
        raise QualityError(f"{context}.landing_collision is not recognized")
    segment_count = _strict_integer(
        fields.get("swept_segment_count"), f"{context}.swept_segment_count",
        minimum=0, maximum=VERTICAL_PAIN_COLUMN_MAX_SEGMENTS)
    if segment_count > segment_budget:
        raise QualityError(f"{context}.swept_segment_count exceeds its budget")
    observed_elapsed = _number(
        fields.get("observed_elapsed"), f"{context}.observed_elapsed", minimum=0.0)
    if observed_elapsed > elapsed_horizon + 0.000001:
        raise QualityError(f"{context}.observed_elapsed exceeds elapsed_horizon")
    last_physics = _vertical_pain_column_zone(
        fields.get("last_observed_physics_zone"),
        f"{context}.last_observed_physics_zone")
    observed_foot = _vertical_pain_column_zone(
        fields.get("observed_harmful_foot_zone"),
        f"{context}.observed_harmful_foot_zone")
    observed_center = _vertical_pain_column_zone(
        fields.get("observed_harmful_center_zone"),
        f"{context}.observed_harmful_center_zone")
    booleans = {
        name: _boolean(fields.get(name), f"{context}.{name}") for name in (
            "has_positive_elapsed", "physics_zone_evidence_known",
            "harmful_foot_evidence_known", "harmful_center_evidence_known",
            "water_evidence_known", "entered_harmful_foot_zone",
            "entered_harmful_center_zone", "expected_harmful_path_matched",
            "causal_ambiguity", "actual_trajectory_unknown",
        )
    }
    if booleans["has_positive_elapsed"] != (observed_elapsed > 0.0):
        raise QualityError(f"{context}.has_positive_elapsed disagrees with observed_elapsed")
    if booleans["entered_harmful_foot_zone"] != observed_foot["known"]:
        raise QualityError(
            f"{context}: harmful foot entry requires an exact known zone identity")
    if booleans["entered_harmful_center_zone"] != observed_center["known"]:
        raise QualityError(
            f"{context}: harmful center entry requires an exact known zone identity")
    if booleans["entered_harmful_foot_zone"] \
            and not booleans["harmful_foot_evidence_known"]:
        raise QualityError(
            f"{context}: entered_harmful_foot_zone requires harmful_foot_evidence_known")
    if booleans["entered_harmful_center_zone"] \
            and not booleans["harmful_center_evidence_known"]:
        raise QualityError(
            f"{context}: entered_harmful_center_zone requires harmful_center_evidence_known")
    if booleans["expected_harmful_path_matched"]:
        entered_expected_zone = (
            booleans["entered_harmful_foot_zone"]
            and _same_vertical_pain_column_zone(observed_foot, expected_foot)
        ) or (
            booleans["entered_harmful_center_zone"]
            and _same_vertical_pain_column_zone(observed_center, expected_foot)
        )
        if not entered_expected_zone \
                or not _same_vertical_pain_column_zone(last_physics, expected_physics):
            raise QualityError(f"{context}: matched harmful path requires matching zone identities")
    if terminal == "harmful_pain_entered" \
            and not (booleans["entered_harmful_foot_zone"]
                     or booleans["entered_harmful_center_zone"]):
        raise QualityError(f"{context}: harmful_pain_entered requires harmful entry")
    record.update({
        "terminal": terminal,
        "correlation": correlation,
        "last_observed_physics_zone": last_physics,
        "observed_harmful_foot_zone": observed_foot,
        "observed_harmful_center_zone": observed_center,
        "swept_segment_count": segment_count,
        "observed_elapsed": observed_elapsed,
        "landing_collision": landing,
        **booleans,
    })
    expected_correlation = _vertical_pain_column_expected_correlation(record)
    if correlation != expected_correlation:
        raise QualityError(
            f"{context}.correlation is {correlation!r}, expected {expected_correlation!r}")
    return record


def _vertical_pain_column_expected_correlation(record: dict[str, Any]) -> str:
    terminal = record["terminal"]
    if terminal == "harmful_pain_entered":
        if record["actual_trajectory_unknown"] \
                or not record["harmful_foot_evidence_known"] \
                or not record["harmful_center_evidence_known"] \
                or not record["water_evidence_known"] \
                or not record["physics_zone_evidence_known"] \
                or not record["has_positive_elapsed"]:
            return "unknown"
        if record["causal_ambiguity"]:
            return "ambiguous"
        if record["forecast"] == "unknown":
            return "unknown"
        if record["forecast"] == "no_harmful_pain_observed":
            return "actual_only"
        return "confirmed_harmful_forecast" \
            if record["expected_harmful_path_matched"] else "ambiguous"
    if terminal == "died":
        return "unknown"
    if terminal != "landed" or record["landing_collision"] != "static_world" \
            or record["actual_trajectory_unknown"] or record["causal_ambiguity"] \
            or not record["harmful_foot_evidence_known"] \
            or not record["harmful_center_evidence_known"] \
            or not record["water_evidence_known"] \
            or record["swept_segment_count"] == 0 \
            or not record["has_positive_elapsed"]:
        return "unknown"
    if record["forecast"] == "harmful_pain_observed":
        return "forecast_only"
    if record["forecast"] == "no_harmful_pain_observed":
        return "confirmed_no_harmful_observation"
    return "unknown"


def _vertical_pain_column_diagnostics(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _vertical_pain_column_diagnostic(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


def _hazard_water_egress_diagnostic(value: Any, context: str) -> dict[str, Any]:
    required_fields = {
        "source_pawn_actor", "sequence", "life_id", "episode_id", "transition_source",
        "anchor_known", "anchor", "entry_location", "damage_per_second",
        "entry_move_target_name", "entry_move_target_location_known",
        "entry_move_target_location", "entry_destination",
        "static_walk_certificate_result", "static_walk_first_hop_known",
        "static_walk_first_hop_name", "static_walk_first_hop_location_known",
        "static_walk_first_hop_location", "static_walk_first_hop_distance_known",
        "static_walk_first_hop_entry_distance", "static_walk_minimum_first_hop_distance",
        "static_walk_terminal_first_hop_distance", "static_walk_first_hop_progress_samples",
        "static_walk_first_hop_regression_samples", "static_walk_continuation_known",
        "static_walk_continuation_name", "static_walk_cost", "static_walk_hops",
        "static_walk_visited_nodes", "candidate_known",
        "candidate_name", "candidate_location", "candidate_entry_distance",
        "candidate_distance_known", "minimum_candidate_distance",
        "terminal_candidate_distance", "candidate_progress_samples",
        "candidate_regression_samples", "target_distance_known", "entry_target_distance",
        "minimum_target_distance", "terminal_target_distance",
        "target_progress_samples", "target_regression_samples", "terminal",
        "terminal_location", "terminal_move_target_name", "terminal_destination",
    }
    external_impulse_provenance_fields = {
        "external_impulse_navigation_commit_known", "external_impulse_move_target_name",
        "external_impulse_move_target_navigation", "external_impulse_route_head_known",
        "external_impulse_route_head_name", "external_impulse_commit_location",
        "external_impulse_commit_velocity",
    }
    external_impulse_launch_forecast_fields = {
        "external_impulse_launch_forecast_known",
        "external_impulse_launch_forecast_harmful",
    }
    falling_launch_snapshot_fields = {
        "falling_launch_snapshot_known", "falling_launch_life_id",
        "falling_launch_movement_command_active", "falling_launch_movement_command_token",
        "falling_launch_movement_command_kind",
        "falling_launch_movement_command_target_name",
        "falling_launch_movement_command_destination",
        "falling_launch_move_target_name", "falling_launch_move_target_navigation",
        "falling_launch_route_head_known", "falling_launch_route_head_name",
        "falling_launch_location", "falling_launch_velocity",
        "falling_launch_forecast_known", "falling_launch_forecast_harmful",
    }
    static_walk_current_first_hop_probe_fields = {
        "static_walk_current_first_hop_probe_known",
        "static_walk_current_first_hop_probe_clear",
    }
    raw_fields = _object(value, context)
    present_external_impulse_fields = raw_fields.keys() & external_impulse_provenance_fields
    present_external_impulse_launch_forecast_fields = (
        raw_fields.keys() & external_impulse_launch_forecast_fields)
    present_falling_launch_snapshot_fields = (
        raw_fields.keys() & falling_launch_snapshot_fields)
    falling_launch_snapshot_is_current = bool(present_falling_launch_snapshot_fields)
    if present_falling_launch_snapshot_fields and present_external_impulse_fields:
        raise QualityError(f"{context}: falling-launch and legacy external-impulse provenance cannot mix")
    if present_falling_launch_snapshot_fields \
            and present_falling_launch_snapshot_fields != falling_launch_snapshot_fields:
        missing = sorted(falling_launch_snapshot_fields - present_falling_launch_snapshot_fields)
        raise QualityError(f"{context}: incomplete falling-launch snapshot fields: {', '.join(missing)}")
    present_static_walk_current_first_hop_probe_fields = (
        raw_fields.keys() & static_walk_current_first_hop_probe_fields)
    fields = _exact_object(
        raw_fields, context,
        required_fields | (external_impulse_provenance_fields
                           if present_external_impulse_fields else set())
        | (external_impulse_launch_forecast_fields
           if present_external_impulse_launch_forecast_fields else set())
        | (falling_launch_snapshot_fields
           if present_falling_launch_snapshot_fields else set())
        | (static_walk_current_first_hop_probe_fields
           if present_static_walk_current_first_hop_probe_fields else set()))
    transition_source = _string(fields, "transition_source", context, nonempty=True)
    if transition_source not in HAZARD_WATER_EGRESS_TRANSITION_SOURCES:
        raise QualityError(f"{context}.transition_source is not recognized")
    terminal = _string(fields, "terminal", context, nonempty=True)
    if terminal not in HAZARD_WATER_EGRESS_TERMINALS:
        raise QualityError(f"{context}.terminal is not recognized")
    static_walk_result = _string(fields, "static_walk_certificate_result", context,
                                 nonempty=True)
    if static_walk_result not in HAZARD_WATER_EGRESS_STATIC_WALK_CERTIFICATES:
        raise QualityError(f"{context}.static_walk_certificate_result is not recognized")
    static_walk_first_hop_known = _boolean(
        fields.get("static_walk_first_hop_known"),
        f"{context}.static_walk_first_hop_known")
    static_walk_first_hop_name = _string(fields, "static_walk_first_hop_name", context)
    static_walk_first_hop_location_known = _boolean(
        fields.get("static_walk_first_hop_location_known"),
        f"{context}.static_walk_first_hop_location_known")
    static_walk_first_hop_location = _diagnostic_vector(
        fields.get("static_walk_first_hop_location"),
        f"{context}.static_walk_first_hop_location")
    static_walk_first_hop_distance_known = _boolean(
        fields.get("static_walk_first_hop_distance_known"),
        f"{context}.static_walk_first_hop_distance_known")
    if present_static_walk_current_first_hop_probe_fields:
        static_walk_current_first_hop_probe_known = _boolean(
            fields.get("static_walk_current_first_hop_probe_known"),
            f"{context}.static_walk_current_first_hop_probe_known")
        static_walk_current_first_hop_probe_clear = _boolean(
            fields.get("static_walk_current_first_hop_probe_clear"),
            f"{context}.static_walk_current_first_hop_probe_clear")
    else:
        static_walk_current_first_hop_probe_known = False
        static_walk_current_first_hop_probe_clear = False
    static_walk_first_hop_entry_distance = _number(
        fields.get("static_walk_first_hop_entry_distance"),
        f"{context}.static_walk_first_hop_entry_distance", minimum=0.0)
    static_walk_minimum_first_hop_distance = _number(
        fields.get("static_walk_minimum_first_hop_distance"),
        f"{context}.static_walk_minimum_first_hop_distance", minimum=0.0)
    static_walk_terminal_first_hop_distance = _number(
        fields.get("static_walk_terminal_first_hop_distance"),
        f"{context}.static_walk_terminal_first_hop_distance", minimum=0.0)
    static_walk_first_hop_progress_samples = _integer(
        fields.get("static_walk_first_hop_progress_samples"),
        f"{context}.static_walk_first_hop_progress_samples", minimum=0)
    static_walk_first_hop_regression_samples = _integer(
        fields.get("static_walk_first_hop_regression_samples"),
        f"{context}.static_walk_first_hop_regression_samples", minimum=0)
    static_walk_continuation_known = _boolean(
        fields.get("static_walk_continuation_known"),
        f"{context}.static_walk_continuation_known")
    static_walk_continuation_name = _string(
        fields, "static_walk_continuation_name", context)
    static_walk_cost = _number(fields.get("static_walk_cost"),
                                f"{context}.static_walk_cost", minimum=0.0)
    static_walk_hops = _integer(fields.get("static_walk_hops"),
                                f"{context}.static_walk_hops", minimum=0)
    static_walk_visited_nodes = _integer(fields.get("static_walk_visited_nodes"),
                                         f"{context}.static_walk_visited_nodes", minimum=0)
    if static_walk_first_hop_known != bool(static_walk_first_hop_name):
        raise QualityError(f"{context}: static-walk first-hop availability must match its name")
    if static_walk_first_hop_location_known != static_walk_first_hop_known \
            or static_walk_first_hop_distance_known != static_walk_first_hop_known:
        raise QualityError(f"{context}: static-walk first-hop location and distance availability must match its name")
    if static_walk_current_first_hop_probe_known and not static_walk_first_hop_known:
        raise QualityError(f"{context}: current first-hop probe requires a static-walk first hop")
    if not static_walk_current_first_hop_probe_known \
            and static_walk_current_first_hop_probe_clear:
        raise QualityError(f"{context}: current first-hop probe cannot be clear when unknown")
    if static_walk_first_hop_distance_known and (
            static_walk_minimum_first_hop_distance > static_walk_first_hop_entry_distance
            or static_walk_minimum_first_hop_distance > static_walk_terminal_first_hop_distance):
        raise QualityError(f"{context}: static-walk first-hop minimum distance is inconsistent")
    if not static_walk_first_hop_distance_known and (
            static_walk_first_hop_entry_distance != 0.0
            or static_walk_minimum_first_hop_distance != 0.0
            or static_walk_terminal_first_hop_distance != 0.0
            or static_walk_first_hop_progress_samples != 0
            or static_walk_first_hop_regression_samples != 0):
        raise QualityError(f"{context}: unknown static-walk first hop must not claim distance evidence")
    if static_walk_continuation_known != bool(static_walk_continuation_name):
        raise QualityError(f"{context}: static-walk continuation availability must match its name")
    if static_walk_result == "certified_static_walk_continuation":
        if not static_walk_first_hop_known or not static_walk_continuation_known \
                or static_walk_hops < 1 or static_walk_visited_nodes < 2:
            raise QualityError(f"{context}: certified static walk requires first-hop and continuation evidence")
    elif static_walk_continuation_known or static_walk_cost != 0.0 or static_walk_hops != 0:
        raise QualityError(f"{context}: rejected static walk must not claim a continuation")
    if static_walk_result == "not_attempted_missing_anchor" and (
            fields.get("anchor_known") or static_walk_first_hop_known
            or static_walk_visited_nodes != 0):
        raise QualityError(f"{context}: missing-anchor static walk must not claim a graph probe")
    if static_walk_result == "no_eligible_direct_first_hop" and (
            static_walk_first_hop_known or static_walk_visited_nodes != 0):
        raise QualityError(f"{context}: no-first-hop static walk must not claim a graph probe")
    if present_falling_launch_snapshot_fields:
        falling_launch_snapshot_known = _boolean(
            fields.get("falling_launch_snapshot_known"),
            f"{context}.falling_launch_snapshot_known")
        falling_launch_life_id = _integer(
            fields.get("falling_launch_life_id"), f"{context}.falling_launch_life_id",
            minimum=0)
        falling_launch_movement_command_active = _boolean(
            fields.get("falling_launch_movement_command_active"),
            f"{context}.falling_launch_movement_command_active")
        falling_launch_movement_command_token = _integer(
            fields.get("falling_launch_movement_command_token"),
            f"{context}.falling_launch_movement_command_token", minimum=0)
        falling_launch_movement_command_kind = _string(
            fields, "falling_launch_movement_command_kind", context)
        falling_launch_movement_command_target_name = _string(
            fields, "falling_launch_movement_command_target_name", context)
        falling_launch_movement_command_destination = _diagnostic_vector(
            fields.get("falling_launch_movement_command_destination"),
            f"{context}.falling_launch_movement_command_destination")
        falling_launch_move_target_name = _string(
            fields, "falling_launch_move_target_name", context)
        falling_launch_move_target_navigation = _boolean(
            fields.get("falling_launch_move_target_navigation"),
            f"{context}.falling_launch_move_target_navigation")
        falling_launch_route_head_known = _boolean(
            fields.get("falling_launch_route_head_known"),
            f"{context}.falling_launch_route_head_known")
        falling_launch_route_head_name = _string(
            fields, "falling_launch_route_head_name", context)
        falling_launch_location = _diagnostic_vector(
            fields.get("falling_launch_location"), f"{context}.falling_launch_location")
        falling_launch_velocity = _diagnostic_vector(
            fields.get("falling_launch_velocity"), f"{context}.falling_launch_velocity")
        falling_launch_forecast_known = _boolean(
            fields.get("falling_launch_forecast_known"),
            f"{context}.falling_launch_forecast_known")
        falling_launch_forecast_harmful = _boolean(
            fields.get("falling_launch_forecast_harmful"),
            f"{context}.falling_launch_forecast_harmful")
        # The generic spelling supersedes the legacy external-impulse fields.
        # Keep their internal variables neutral so the legacy compatibility
        # checks below remain safe for either representation.
        external_impulse_navigation_commit_known = False
        external_impulse_move_target_name = ""
        external_impulse_move_target_navigation = False
        external_impulse_route_head_known = False
        external_impulse_route_head_name = ""
        external_impulse_commit_location = {"x": 0.0, "y": 0.0, "z": 0.0}
        external_impulse_commit_velocity = {"x": 0.0, "y": 0.0, "z": 0.0}
        external_impulse_launch_forecast_known = False
        external_impulse_launch_forecast_harmful = False
    elif present_external_impulse_fields:
        external_impulse_navigation_commit_known = _boolean(
            fields.get("external_impulse_navigation_commit_known"),
            f"{context}.external_impulse_navigation_commit_known")
        external_impulse_move_target_name = _string(
            fields, "external_impulse_move_target_name", context)
        external_impulse_move_target_navigation = _boolean(
            fields.get("external_impulse_move_target_navigation"),
            f"{context}.external_impulse_move_target_navigation")
        external_impulse_route_head_known = _boolean(
            fields.get("external_impulse_route_head_known"),
            f"{context}.external_impulse_route_head_known")
        external_impulse_route_head_name = _string(
            fields, "external_impulse_route_head_name", context)
        external_impulse_commit_location = _diagnostic_vector(
            fields.get("external_impulse_commit_location"),
            f"{context}.external_impulse_commit_location")
        external_impulse_commit_velocity = _diagnostic_vector(
            fields.get("external_impulse_commit_velocity"),
            f"{context}.external_impulse_commit_velocity")
        falling_launch_snapshot_known = external_impulse_navigation_commit_known
        falling_launch_life_id = 0
        falling_launch_movement_command_active = False
        falling_launch_movement_command_token = 0
        falling_launch_movement_command_kind = ""
        falling_launch_movement_command_target_name = ""
        falling_launch_movement_command_destination = {"x": 0.0, "y": 0.0, "z": 0.0}
        falling_launch_move_target_name = external_impulse_move_target_name
        falling_launch_move_target_navigation = external_impulse_move_target_navigation
        falling_launch_route_head_known = external_impulse_route_head_known
        falling_launch_route_head_name = external_impulse_route_head_name
        falling_launch_location = external_impulse_commit_location
        falling_launch_velocity = external_impulse_commit_velocity
        falling_launch_forecast_known = False
        falling_launch_forecast_harmful = False
    else:
        external_impulse_navigation_commit_known = False
        external_impulse_move_target_name = ""
        external_impulse_move_target_navigation = False
        external_impulse_route_head_known = False
        external_impulse_route_head_name = ""
        external_impulse_commit_location = {"x": 0.0, "y": 0.0, "z": 0.0}
        external_impulse_commit_velocity = {"x": 0.0, "y": 0.0, "z": 0.0}
        falling_launch_snapshot_known = False
        falling_launch_life_id = 0
        falling_launch_movement_command_active = False
        falling_launch_movement_command_token = 0
        falling_launch_movement_command_kind = ""
        falling_launch_movement_command_target_name = ""
        falling_launch_movement_command_destination = {"x": 0.0, "y": 0.0, "z": 0.0}
        falling_launch_move_target_name = ""
        falling_launch_move_target_navigation = False
        falling_launch_route_head_known = False
        falling_launch_route_head_name = ""
        falling_launch_location = {"x": 0.0, "y": 0.0, "z": 0.0}
        falling_launch_velocity = {"x": 0.0, "y": 0.0, "z": 0.0}
        falling_launch_forecast_known = False
        falling_launch_forecast_harmful = False
    if external_impulse_move_target_navigation and not external_impulse_move_target_name:
        raise QualityError(f"{context}: external-impulse navigation target requires a target name")
    if external_impulse_route_head_known != bool(external_impulse_route_head_name):
        raise QualityError(f"{context}: external-impulse route-head availability must match its name")
    if not external_impulse_navigation_commit_known and (
            external_impulse_move_target_name or external_impulse_move_target_navigation
            or external_impulse_route_head_known or external_impulse_route_head_name
            or external_impulse_commit_location != {"x": 0.0, "y": 0.0, "z": 0.0}
            or external_impulse_commit_velocity != {"x": 0.0, "y": 0.0, "z": 0.0}):
        raise QualityError(f"{context}: unknown external-impulse commit must not claim provenance")
    if present_falling_launch_snapshot_fields:
        pass
    elif present_external_impulse_launch_forecast_fields:
        external_impulse_launch_forecast_known = _boolean(
            fields.get("external_impulse_launch_forecast_known"),
            f"{context}.external_impulse_launch_forecast_known")
        external_impulse_launch_forecast_harmful = _boolean(
            fields.get("external_impulse_launch_forecast_harmful"),
            f"{context}.external_impulse_launch_forecast_harmful")
    else:
        external_impulse_launch_forecast_known = False
        external_impulse_launch_forecast_harmful = False
    if external_impulse_launch_forecast_harmful \
            and not external_impulse_launch_forecast_known:
        raise QualityError(f"{context}: harmful external-impulse launch forecast must be known")
    if not falling_launch_snapshot_is_current and present_external_impulse_fields:
        falling_launch_forecast_known = external_impulse_launch_forecast_known
        falling_launch_forecast_harmful = external_impulse_launch_forecast_harmful
    if falling_launch_snapshot_known:
        if falling_launch_snapshot_is_current and falling_launch_life_id != _integer(
                fields.get("life_id"), f"{context}.life_id", minimum=1):
            raise QualityError(f"{context}: falling-launch snapshot must belong to the water episode life")
    elif falling_launch_life_id != 0 or falling_launch_movement_command_active \
            or falling_launch_movement_command_token != 0 or falling_launch_move_target_name \
            or falling_launch_move_target_navigation or falling_launch_route_head_known \
            or falling_launch_route_head_name \
            or falling_launch_location != {"x": 0.0, "y": 0.0, "z": 0.0} \
            or falling_launch_velocity != {"x": 0.0, "y": 0.0, "z": 0.0} \
            or falling_launch_forecast_known or falling_launch_forecast_harmful:
        raise QualityError(f"{context}: unknown falling-launch snapshot must not claim provenance")
    if falling_launch_move_target_navigation and not falling_launch_move_target_name:
        raise QualityError(f"{context}: falling-launch navigation target requires a target name")
    if falling_launch_movement_command_active \
            != (falling_launch_movement_command_token != 0):
        raise QualityError(f"{context}: falling-launch command availability must match its token")
    if falling_launch_movement_command_active:
        if falling_launch_movement_command_kind not in {
                "move_to", "move_toward", "strafe_to", "strafe_facing"}:
            raise QualityError(f"{context}: active falling-launch command has an invalid kind")
    elif falling_launch_movement_command_kind \
            or falling_launch_movement_command_target_name \
            or falling_launch_movement_command_destination != {"x": 0.0, "y": 0.0, "z": 0.0}:
        raise QualityError(f"{context}: inactive falling-launch command must not claim issue-time context")
    if falling_launch_route_head_known != bool(falling_launch_route_head_name):
        raise QualityError(f"{context}: falling-launch route-head availability must match its name")
    if falling_launch_forecast_harmful and not falling_launch_forecast_known:
        raise QualityError(f"{context}: harmful falling-launch forecast must be known")
    candidate_known = _boolean(fields.get("candidate_known"), f"{context}.candidate_known")
    candidate_distance_known = _boolean(
        fields.get("candidate_distance_known"), f"{context}.candidate_distance_known")
    if candidate_known != candidate_distance_known:
        raise QualityError(f"{context}: candidate distance availability must match candidate availability")
    candidate_name = _string(fields, "candidate_name", context)
    if candidate_known != bool(candidate_name):
        raise QualityError(f"{context}: candidate name availability must match candidate availability")
    target_distance_known = _boolean(
        fields.get("target_distance_known"), f"{context}.target_distance_known")
    target_name = _string(fields, "entry_move_target_name", context)
    target_location_known = _boolean(fields.get("entry_move_target_location_known"),
                                     f"{context}.entry_move_target_location_known")
    if target_distance_known and (not target_name or not target_location_known):
        raise QualityError(f"{context}: target distance requires an entry target location")
    entry_candidate_distance = _number(fields.get("candidate_entry_distance"),
                                       f"{context}.candidate_entry_distance", minimum=0.0)
    minimum_candidate_distance = _number(fields.get("minimum_candidate_distance"),
                                         f"{context}.minimum_candidate_distance", minimum=0.0)
    terminal_candidate_distance = _number(fields.get("terminal_candidate_distance"),
                                          f"{context}.terminal_candidate_distance", minimum=0.0)
    if candidate_distance_known and (
            minimum_candidate_distance > entry_candidate_distance
            or minimum_candidate_distance > terminal_candidate_distance):
        raise QualityError(f"{context}: candidate minimum distance is inconsistent")
    entry_target_distance = _number(fields.get("entry_target_distance"),
                                    f"{context}.entry_target_distance", minimum=0.0)
    minimum_target_distance = _number(fields.get("minimum_target_distance"),
                                      f"{context}.minimum_target_distance", minimum=0.0)
    terminal_target_distance = _number(fields.get("terminal_target_distance"),
                                       f"{context}.terminal_target_distance", minimum=0.0)
    if target_distance_known and (
            minimum_target_distance > entry_target_distance
            or minimum_target_distance > terminal_target_distance):
        raise QualityError(f"{context}: target minimum distance is inconsistent")
    return {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(fields.get("sequence"), f"{context}.sequence", minimum=1),
        "life_id": _integer(fields.get("life_id"), f"{context}.life_id", minimum=1),
        "episode_id": _integer(fields.get("episode_id"), f"{context}.episode_id", minimum=1),
        "transition_source": transition_source,
        "anchor_known": _boolean(fields.get("anchor_known"), f"{context}.anchor_known"),
        "anchor": _diagnostic_vector(fields.get("anchor"), f"{context}.anchor"),
        "entry_location": _diagnostic_vector(fields.get("entry_location"), f"{context}.entry_location"),
        "damage_per_second": _number(fields.get("damage_per_second"),
                                      f"{context}.damage_per_second", minimum=0.0),
        "entry_move_target_name": target_name,
        "entry_move_target_location_known": target_location_known,
        "entry_move_target_location": _diagnostic_vector(
            fields.get("entry_move_target_location"), f"{context}.entry_move_target_location"),
        "entry_destination": _diagnostic_vector(fields.get("entry_destination"),
                                                  f"{context}.entry_destination"),
        "falling_launch_snapshot_known": falling_launch_snapshot_known,
        "falling_launch_life_id": falling_launch_life_id,
        "falling_launch_movement_command_active": falling_launch_movement_command_active,
        "falling_launch_movement_command_token": falling_launch_movement_command_token,
        "falling_launch_movement_command_kind": falling_launch_movement_command_kind,
        "falling_launch_movement_command_target_name": falling_launch_movement_command_target_name,
        "falling_launch_movement_command_destination": falling_launch_movement_command_destination,
        "falling_launch_move_target_name": falling_launch_move_target_name,
        "falling_launch_move_target_navigation": falling_launch_move_target_navigation,
        "falling_launch_route_head_known": falling_launch_route_head_known,
        "falling_launch_route_head_name": falling_launch_route_head_name,
        "falling_launch_location": falling_launch_location,
        "falling_launch_velocity": falling_launch_velocity,
        "falling_launch_forecast_known": falling_launch_forecast_known,
        "falling_launch_forecast_harmful": falling_launch_forecast_harmful,
        "static_walk_certificate_result": static_walk_result,
        "static_walk_first_hop_known": static_walk_first_hop_known,
        "static_walk_first_hop_name": static_walk_first_hop_name,
        "static_walk_first_hop_location_known": static_walk_first_hop_location_known,
        "static_walk_first_hop_location": static_walk_first_hop_location,
        "static_walk_first_hop_distance_known": static_walk_first_hop_distance_known,
        "static_walk_current_first_hop_probe_known": static_walk_current_first_hop_probe_known,
        "static_walk_current_first_hop_probe_clear": static_walk_current_first_hop_probe_clear,
        "static_walk_first_hop_entry_distance": static_walk_first_hop_entry_distance,
        "static_walk_minimum_first_hop_distance": static_walk_minimum_first_hop_distance,
        "static_walk_terminal_first_hop_distance": static_walk_terminal_first_hop_distance,
        "static_walk_first_hop_progress_samples": static_walk_first_hop_progress_samples,
        "static_walk_first_hop_regression_samples": static_walk_first_hop_regression_samples,
        "static_walk_continuation_known": static_walk_continuation_known,
        "static_walk_continuation_name": static_walk_continuation_name,
        "static_walk_cost": static_walk_cost,
        "static_walk_hops": static_walk_hops,
        "static_walk_visited_nodes": static_walk_visited_nodes,
        "candidate_known": candidate_known,
        "candidate_name": candidate_name,
        "candidate_location": _diagnostic_vector(fields.get("candidate_location"),
                                                   f"{context}.candidate_location"),
        "candidate_entry_distance": entry_candidate_distance,
        "candidate_distance_known": candidate_distance_known,
        "minimum_candidate_distance": minimum_candidate_distance,
        "terminal_candidate_distance": terminal_candidate_distance,
        "candidate_progress_samples": _integer(fields.get("candidate_progress_samples"),
                                                  f"{context}.candidate_progress_samples", minimum=0),
        "candidate_regression_samples": _integer(fields.get("candidate_regression_samples"),
                                                    f"{context}.candidate_regression_samples", minimum=0),
        "target_distance_known": target_distance_known,
        "entry_target_distance": entry_target_distance,
        "minimum_target_distance": minimum_target_distance,
        "terminal_target_distance": terminal_target_distance,
        "target_progress_samples": _integer(fields.get("target_progress_samples"),
                                               f"{context}.target_progress_samples", minimum=0),
        "target_regression_samples": _integer(fields.get("target_regression_samples"),
                                                 f"{context}.target_regression_samples", minimum=0),
        "terminal": terminal,
        "terminal_location": _diagnostic_vector(fields.get("terminal_location"),
                                                  f"{context}.terminal_location"),
        "terminal_move_target_name": _string(fields, "terminal_move_target_name", context),
        "terminal_destination": _diagnostic_vector(fields.get("terminal_destination"),
                                                     f"{context}.terminal_destination"),
    }


def _hazard_water_egress_diagnostics(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _hazard_water_egress_diagnostic(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


MOVE_STALL_RECOVERY_OUTCOMES = {
    "cleared_within_2_seconds":
        "move_stall_recovery_cleared_within_2_seconds_exact",
    "cleared_after_2_seconds_within_5_seconds":
        "move_stall_recovery_cleared_after_2_seconds_within_5_seconds_exact",
    "replanned_within_5_seconds":
        "move_stall_recovery_replanned_within_5_seconds_exact",
    "missed_5_second_deadline":
        "move_stall_recovery_missed_5_second_deadline_exact",
    "excluded_intentional_stop":
        "move_stall_recovery_excluded_intentional_stops_exact",
    "censored_life_boundary":
        "move_stall_recovery_censored_life_boundaries_exact",
    "censored_run_end":
        "move_stall_recovery_censored_run_end_exact",
    "unknown": "move_stall_recovery_unknown_exact",
}


def _move_stall_recovery_episode(value: Any, context: str) -> dict[str, Any]:
    fields = _object(value, context)
    outcome = _string(fields, "outcome", context)
    if outcome not in MOVE_STALL_RECOVERY_OUTCOMES:
        raise QualityError(f"{context}.outcome is not recognized")
    seconds = _number(fields.get("seconds_since_detection"),
                      f"{context}.seconds_since_detection", minimum=0.0)
    if outcome == "cleared_within_2_seconds" and seconds > 2.0:
        raise QualityError(f"{context}: cleared-within-2 outcome exceeds two seconds")
    if outcome == "cleared_after_2_seconds_within_5_seconds" and not (2.0 < seconds <= 5.0):
        raise QualityError(f"{context}: cleared-after-2 outcome is outside (2, 5] seconds")
    if outcome == "replanned_within_5_seconds" and seconds > 5.0:
        raise QualityError(f"{context}: replanned-within-5 outcome exceeds five seconds")
    if outcome == "missed_5_second_deadline" and seconds <= 5.0:
        raise QualityError(f"{context}: missed-deadline outcome must exceed five seconds")
    return {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(fields.get("sequence"), f"{context}.sequence", minimum=1),
        "life_id": _integer(fields.get("life_id"), f"{context}.life_id", minimum=1),
        "episode_id": _integer(fields.get("episode_id"), f"{context}.episode_id", minimum=1),
        "seconds_since_detection": seconds,
        "outcome": outcome,
    }


def _move_stall_recovery_episodes(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _move_stall_recovery_episode(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


MOVE_STALL_DECISIONS = {
    "none", "navigation_replan", "targetless_timeout", "direct_actor_move_toward_timeout"}
MOVE_STALL_LATENT_MODES = {"other", "move_to", "move_toward", "strafe_to", "strafe_facing"}


def _move_stall_recovery_decision(value: Any, context: str) -> dict[str, Any]:
    fields = _object(value, context)
    latent_mode = _string(fields, "latent_mode", context)
    decision = _string(fields, "decision", context)
    if latent_mode not in MOVE_STALL_LATENT_MODES:
        raise QualityError(f"{context}.latent_mode is not recognized")
    if decision not in MOVE_STALL_DECISIONS:
        raise QualityError(f"{context}.decision is not recognized")
    target_known = _boolean(fields.get("move_target_known"), f"{context}.move_target_known")
    target_live = _boolean(fields.get("move_target_live"), f"{context}.move_target_live")
    target_name = fields.get("move_target_name")
    target_class = fields.get("move_target_class")
    if not isinstance(target_name, str) or not isinstance(target_class, str):
        raise QualityError(f"{context}: move target name/class must be strings")
    if target_live and not target_known:
        raise QualityError(f"{context}: live move target requires known target")
    timer = _number(fields.get("move_timer"), f"{context}.move_timer")
    if decision == "navigation_replan":
        if latent_mode != "move_toward" or not target_live or not target_class:
            raise QualityError(f"{context}: navigation replan lacks a live MoveToward target")
    if decision == "targetless_timeout":
        if latent_mode != "move_to" or target_known or timer <= 0.0:
            raise QualityError(f"{context}: targetless timeout lacks an armed targetless MoveTo")
    if decision == "direct_actor_move_toward_timeout":
        if latent_mode != "move_toward" or not target_live or not target_class or timer <= 0.0:
            raise QualityError(f"{context}: direct-actor timeout lacks a live armed MoveToward target")
    return {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(fields.get("sequence"), f"{context}.sequence", minimum=1),
        "life_id": _integer(fields.get("life_id"), f"{context}.life_id", minimum=1),
        "episode_id": _integer(fields.get("episode_id"), f"{context}.episode_id", minimum=1),
        "latent_mode": latent_mode, "decision": decision,
        "move_target_known": target_known, "move_target_live": target_live,
        "move_target_name": target_name, "move_target_class": target_class,
        "move_timer": timer,
    }


def _move_stall_recovery_decisions(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [_move_stall_recovery_decision(item, f"{context}[{index}]")
            for index, item in enumerate(value)]


def _hazard_death_partition_record(value: Any, context: str) -> dict[str, Any]:
    fields = _object(value, context)
    killer_relation = _string(fields, "killer_relation", context)
    attribution = _string(fields, "attribution", context)
    environmental_source = _string(fields, "environmental_source", context)
    hazard_prefix = _string(fields, "hazard_prefix", context)
    physics_mode = _string(fields, "physics_mode", context)
    if killer_relation not in HAZARD_DEATH_KILLER_RELATIONS:
        raise QualityError(f"{context}.killer_relation is not recognized")
    if attribution not in HAZARD_DEATH_ATTRIBUTIONS:
        raise QualityError(f"{context}.attribution is not recognized")
    if environmental_source not in HAZARD_DEATH_ENVIRONMENTAL_SOURCES:
        raise QualityError(f"{context}.environmental_source is not recognized")
    if hazard_prefix not in HAZARD_DEATH_PREFIXES:
        raise QualityError(f"{context}.hazard_prefix is not recognized")
    if physics_mode not in PHYSICS_MODES:
        raise QualityError(f"{context}.physics_mode is not recognized")
    move_target_known = _boolean(fields.get("move_target_known"),
                                 f"{context}.move_target_known")
    move_target_name = _string(fields, "move_target_name", context)
    if move_target_known != bool(move_target_name):
        raise QualityError(f"{context}: move target availability must match its name")
    water_known = _boolean(fields.get("water_egress_terminal_known"),
                             f"{context}.water_egress_terminal_known")
    water_sequence = _integer(fields.get("water_egress_sequence"),
                              f"{context}.water_egress_sequence", minimum=0)
    water_life = _integer(fields.get("water_egress_life_id"),
                          f"{context}.water_egress_life_id", minimum=0)
    water_episode = _integer(fields.get("water_egress_episode_id"),
                             f"{context}.water_egress_episode_id", minimum=0)
    falling_known = _boolean(fields.get("falling_hazard_terminal_known"),
                               f"{context}.falling_hazard_terminal_known")
    falling_sequence = _integer(fields.get("falling_hazard_sequence"),
                                f"{context}.falling_hazard_sequence", minimum=0)
    falling_life = _integer(fields.get("falling_hazard_life_id"),
                            f"{context}.falling_hazard_life_id", minimum=0)
    falling_episode = _integer(fields.get("falling_hazard_fall_episode_id"),
                               f"{context}.falling_hazard_fall_episode_id", minimum=0)
    falling_generation = _integer(fields.get("falling_hazard_generation_id"),
                                  f"{context}.falling_hazard_generation_id", minimum=0)
    falling_correlation = _string(fields, "falling_hazard_correlation", context)
    parity_known = _boolean(fields.get("falling_parity_terminal_known"),
                              f"{context}.falling_parity_terminal_known")
    parity_life = _integer(fields.get("falling_parity_life_generation"),
                           f"{context}.falling_parity_life_generation", minimum=0)
    parity_invocation = _integer(fields.get("falling_parity_invocation_token"),
                                 f"{context}.falling_parity_invocation_token", minimum=0)
    parity_iteration = _integer(fields.get("falling_parity_walking_iteration"),
                                f"{context}.falling_parity_walking_iteration", minimum=0)
    if water_known and not all(value >= 1 for value in (
            water_sequence, water_life, water_episode)):
        raise QualityError(f"{context}: water terminal availability must match its identifiers")
    if not water_known and any(value != 0 for value in (
            water_sequence, water_life, water_episode)):
        raise QualityError(f"{context}: unknown water terminal must not claim identifiers")
    if falling_known and not all(value >= 1 for value in (
            falling_sequence, falling_life, falling_episode, falling_generation)):
        raise QualityError(f"{context}: falling terminal availability must match its identifiers")
    if not falling_known and any(value != 0 for value in (
            falling_sequence, falling_life, falling_episode, falling_generation)):
        raise QualityError(f"{context}: unknown falling terminal must not claim identifiers")
    if falling_known != bool(falling_correlation):
        raise QualityError(f"{context}: falling terminal availability must match its correlation")
    if falling_correlation and falling_correlation not in FALLING_HAZARD_CORRELATIONS:
        raise QualityError(f"{context}.falling_hazard_correlation is not recognized")
    if parity_known and not all(value >= 1 for value in (parity_life, parity_invocation)):
        raise QualityError(f"{context}: falling parity availability must match its identifiers")
    if not parity_known and any(value != 0 for value in (
            parity_life, parity_invocation, parity_iteration)):
        raise QualityError(f"{context}: unknown falling parity terminal must not claim identifiers")
    expected_prefix = "water_egress_death" if water_known else (
        "falling_death_without_water_egress" if falling_known else "none")
    if hazard_prefix != expected_prefix:
        raise QualityError(f"{context}: hazard prefix does not match same-death terminal evidence")
    return {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(fields.get("sequence"), f"{context}.sequence", minimum=1),
        "death_time_seconds": _number(fields.get("death_time_seconds"),
                                       f"{context}.death_time_seconds", minimum=0.0),
        "killer_relation": killer_relation,
        "attribution": attribution,
        "environmental_source": environmental_source,
        "had_recent_enemy_contribution": _boolean(
            fields.get("had_recent_enemy_contribution"),
            f"{context}.had_recent_enemy_contribution"),
        "had_recent_enemy_momentum_contribution": _boolean(
            fields.get("had_recent_enemy_momentum_contribution"),
            f"{context}.had_recent_enemy_momentum_contribution"),
        "hazard_prefix": hazard_prefix,
        "move_target_known": move_target_known,
        "move_target_name": move_target_name,
        "movement_intent": _boolean(fields.get("movement_intent"),
                                      f"{context}.movement_intent"),
        "physics_mode": physics_mode,
        "water_egress_terminal_known": water_known,
        "water_egress_sequence": water_sequence,
        "water_egress_life_id": water_life,
        "water_egress_episode_id": water_episode,
        "falling_hazard_terminal_known": falling_known,
        "falling_hazard_sequence": falling_sequence,
        "falling_hazard_life_id": falling_life,
        "falling_hazard_fall_episode_id": falling_episode,
        "falling_hazard_generation_id": falling_generation,
        "falling_hazard_correlation": falling_correlation,
        "falling_parity_terminal_known": parity_known,
        "falling_parity_life_generation": parity_life,
        "falling_parity_invocation_token": parity_invocation,
        "falling_parity_walking_iteration": parity_iteration,
    }


def _hazard_death_partition_records(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _hazard_death_partition_record(item, f"{context}[{index}]")
        for index, item in enumerate(value)
    ]


def _validate_falling_parity_realized_record_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    primary_counters = (
        "falling_parity_realized_episodes_exact",
        "falling_parity_realized_steps_exact",
        "falling_parity_realized_pain_entries_exact",
        "falling_parity_realized_deaths_exact",
        "falling_parity_realized_landings_exact",
        "falling_parity_realized_continuity_losses_exact",
    )
    reconciled_counters = primary_counters + FALLING_PARITY_REALIZED_STEP_COUNTERS
    outcome_counters = {
        "episode_started": ("falling_parity_realized_episodes_exact",),
        "matched_clear": (
            "falling_parity_realized_steps_exact",
            "falling_parity_realized_matched_steps_exact"),
        "matched_landing": (
            "falling_parity_realized_steps_exact",
            "falling_parity_realized_matched_landing_steps_exact"),
        "mismatch": (
            "falling_parity_realized_steps_exact",
            "falling_parity_realized_mismatches_exact"),
        "unknown": (
            "falling_parity_realized_steps_exact",
            "falling_parity_realized_unknowns_exact"),
        "callback_barrier": (
            "falling_parity_realized_steps_exact",
            "falling_parity_realized_callback_barriers_exact"),
        "pain_entered": ("falling_parity_realized_pain_entries_exact",),
        "died": ("falling_parity_realized_deaths_exact",),
        "landed": ("falling_parity_realized_landings_exact",),
        "continuity_lost": ("falling_parity_realized_continuity_losses_exact",),
    }
    previous: dict[str, dict[str, int]] = {}
    last_correlation: dict[tuple[str, str], tuple[int, int, int]] = {}
    states: dict[tuple[str, str, int, int, int], dict[str, Any]] = {}
    history_tainted: defaultdict[tuple[str, str], bool] = defaultdict(bool)

    for event in events:
        for bot in event["bots"]:
            records = bot.get("falling_parity_realized_records")
            if records is None:
                continue
            identity = bot["identity"]
            prior = previous.get(identity, {name: 0 for name in (
                reconciled_counters + ("falling_parity_realized_record_overflows_exact",))})
            deltas = {name: bot[name] - prior[name] for name in reconciled_counters}
            overflow_name = "falling_parity_realized_record_overflows_exact"
            overflow_delta = bot[overflow_name] - prior[overflow_name]
            observed: defaultdict[str, int] = defaultdict(int)

            for record in records:
                actor = record["source_pawn_actor"]
                if actor != bot["actor"]:
                    raise QualityError(
                        f"{path}: falling parity record actor does not match {identity} "
                        f"at telemetry sequence {event['seq']}")
                stream = (identity, actor)
                correlation = (
                    record["life_generation"], record["invocation_token"],
                    record["walking_iteration"])
                prior_correlation = last_correlation.get(stream)
                if prior_correlation is not None:
                    prior_life, prior_invocation, prior_iteration = prior_correlation
                    life, invocation, iteration = correlation
                    if life < prior_life or invocation < prior_invocation or (
                            life == prior_life and invocation == prior_invocation
                            and iteration < prior_iteration):
                        raise QualityError(
                            f"{path}: falling parity record correlation regressed for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                new_correlation = correlation != prior_correlation
                if new_correlation and prior_correlation is not None:
                    prior_state = states[(identity, actor, *prior_correlation)]
                    if not prior_state["terminal"] and not prior_state["tainted"]:
                        raise QualityError(
                            f"{path}: falling parity episode correlation changed before a "
                            f"terminal outcome for {identity}/{actor} at telemetry sequence "
                            f"{event['seq']}")
                key = (identity, actor, *correlation)
                state = states.setdefault(key, {
                    "started": False, "last_ordinal": None, "next_ordinal": 0,
                    "pain": False, "terminal": False, "model_stopped": False,
                    "tainted": history_tainted[stream],
                })
                outcome = record["outcome"]
                ordinal = record["step_ordinal"]
                if new_correlation and outcome != "episode_started" \
                        and not history_tainted[stream]:
                    raise QualityError(
                        f"{path}: falling parity correlation does not begin with episode_started "
                        f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                if state["last_ordinal"] is not None and ordinal < state["last_ordinal"]:
                    raise QualityError(
                        f"{path}: falling parity step ordinal regressed for {identity}/{actor} "
                        f"at telemetry sequence {event['seq']}")
                if state["terminal"] and not state["tainted"]:
                    raise QualityError(
                        f"{path}: falling parity record follows a terminal outcome for "
                        f"{identity}/{actor} at telemetry sequence {event['seq']}")
                if outcome == "episode_started":
                    if state["started"]:
                        raise QualityError(
                            f"{path}: duplicate falling parity episode_started record for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    state.update({
                        "started": True, "last_ordinal": 0, "next_ordinal": 0,
                        "pain": False, "terminal": False, "model_stopped": False,
                        "tainted": False,
                    })
                    history_tainted[stream] = False
                elif outcome in FALLING_PARITY_REALIZED_STEP_OUTCOMES:
                    if ordinal >= FALLING_PARITY_REALIZED_MAX_STEPS:
                        raise QualityError(
                            f"{path}: falling parity realized step ordinal must be below "
                            f"{FALLING_PARITY_REALIZED_MAX_STEPS} for {identity}/{actor} "
                            f"at telemetry sequence {event['seq']}")
                    if not state["started"] and not state["tainted"]:
                        raise QualityError(
                            f"{path}: falling parity step has no episode_started record for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    expected = state["next_ordinal"]
                    if ordinal < expected or (ordinal != expected and not state["tainted"]):
                        raise QualityError(
                            f"{path}: falling parity realized step ordinals are not consecutive for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    if state["model_stopped"] and not state["tainted"]:
                        raise QualityError(
                            f"{path}: falling parity realized step follows a model-stopping outcome "
                            f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                    state["next_ordinal"] = ordinal + 1
                    if outcome != "matched_clear":
                        state["model_stopped"] = True
                elif outcome == "pain_entered":
                    if ordinal > FALLING_PARITY_REALIZED_MAX_STEPS:
                        raise QualityError(
                            f"{path}: falling parity pain ordinal must be at most "
                            f"{FALLING_PARITY_REALIZED_MAX_STEPS} for {identity}/{actor} "
                            f"at telemetry sequence {event['seq']}")
                    if ordinal != state["next_ordinal"] and not state["tainted"]:
                        raise QualityError(
                            f"{path}: falling parity pain record does not match the current "
                            f"step ordinal for {identity}/{actor} at telemetry sequence "
                            f"{event['seq']}")
                    if state["pain"] and not state["tainted"]:
                        raise QualityError(
                            f"{path}: duplicate falling parity pain_entered record for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    state["pain"] = True
                else:
                    if ordinal > FALLING_PARITY_REALIZED_MAX_STEPS:
                        raise QualityError(
                            f"{path}: falling parity terminal ordinal must be at most "
                            f"{FALLING_PARITY_REALIZED_MAX_STEPS} for {identity}/{actor} "
                            f"at telemetry sequence {event['seq']}")
                    if ordinal != state["next_ordinal"] and not state["tainted"]:
                        raise QualityError(
                            f"{path}: falling parity terminal record does not match the current "
                            f"step ordinal for {identity}/{actor} at telemetry sequence "
                            f"{event['seq']}")
                    state["terminal"] = True
                state["last_ordinal"] = ordinal
                last_correlation[stream] = correlation
                for counter in outcome_counters[outcome]:
                    observed[counter] += 1

            for name in reconciled_counters:
                if observed[name] > deltas[name]:
                    raise QualityError(
                        f"{path}: falling parity records exceed {name} delta for {identity} "
                        f"at telemetry sequence {event['seq']}")
                if overflow_delta == 0 and observed[name] != deltas[name]:
                    raise QualityError(
                        f"{path}: falling parity records do not reconcile with {name} delta "
                        f"for {identity} at telemetry sequence {event['seq']}")
            primary_delta = sum(deltas[name] for name in primary_counters)
            if primary_delta != len(records) + overflow_delta:
                raise QualityError(
                    f"{path}: falling parity records and overflows do not reconcile with "
                    f"counter deltas for {identity} at telemetry sequence {event['seq']}")
            if overflow_delta:
                history_tainted[(identity, bot["actor"])] = True
                for key, state in states.items():
                    if key[0] == identity and key[1] == bot["actor"] and not state["terminal"]:
                        state["tainted"] = True
            previous[identity] = {
                name: bot[name] for name in (
                    reconciled_counters + ("falling_parity_realized_record_overflows_exact",))
            }


def _validate_vertical_pain_column_diagnostic_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    primary_counters = (
        "vertical_pain_column_episodes_started_exact",
        "vertical_pain_column_episodes_completed_exact",
        "vertical_pain_column_generation_capacity_exhaustions_exact",
    )
    overflow_name = "vertical_pain_column_diagnostic_overflows_exact"
    reconciled_counters = primary_counters + VERTICAL_PAIN_COLUMN_OUTCOME_COUNTERS
    correlation_counters = {
        "confirmed_harmful_forecast":
            "vertical_pain_column_true_positive_outcomes_exact",
        "forecast_only": "vertical_pain_column_false_positive_outcomes_exact",
        "actual_only": "vertical_pain_column_false_negative_outcomes_exact",
        "confirmed_no_harmful_observation":
            "vertical_pain_column_true_negative_outcomes_exact",
        "ambiguous": "vertical_pain_column_ambiguous_outcomes_exact",
        "unknown": "vertical_pain_column_unknown_outcomes_exact",
    }
    previous: dict[str, dict[str, int]] = {}
    streams: dict[tuple[str, str], dict[str, Any]] = {}

    for event in events:
        for bot in event["bots"]:
            records = bot.get("vertical_pain_column_diagnostics")
            if records is None:
                continue
            identity = bot["identity"]
            prior = previous.get(identity, {
                name: 0 for name in reconciled_counters + (overflow_name,)
            })
            deltas = {name: bot[name] - prior[name] for name in reconciled_counters}
            overflow_delta = bot[overflow_name] - prior[overflow_name]
            observed: defaultdict[str, int] = defaultdict(int)

            for record in records:
                actor = record["source_pawn_actor"]
                if actor != bot["actor"]:
                    raise QualityError(
                        f"{path}: vertical pain column diagnostic actor does not match "
                        f"{identity} at telemetry sequence {event['seq']}")
                stream = streams.setdefault((identity, actor), {
                    "last_sequence": None,
                    "last_life": None,
                    "last_fall": None,
                    "last_generation": None,
                    "active": None,
                    "active_start": None,
                    "tainted": False,
                })
                sequence = record["sequence"]
                if stream["last_sequence"] is not None:
                    if sequence <= stream["last_sequence"]:
                        raise QualityError(
                            f"{path}: vertical pain column diagnostic sequence did not increase "
                            f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                    if not stream["tainted"] and overflow_delta == 0 \
                            and sequence != stream["last_sequence"] + 1:
                        raise QualityError(
                            f"{path}: vertical pain column diagnostic sequence is not consecutive "
                            f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                stream["last_sequence"] = sequence
                key = (record["life_id"], record["fall_episode_id"],
                       record["generation_id"])
                kind = record["kind"]
                if kind == "start":
                    if not stream["tainted"] and stream["active"] is not None:
                        raise QualityError(
                            f"{path}: vertical pain column start precedes the prior terminal for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    last_life = stream["last_life"]
                    last_fall = stream["last_fall"]
                    last_generation = stream["last_generation"]
                    if not stream["tainted"] and last_life is not None:
                        if record["life_id"] < last_life \
                                or (record["life_id"] == last_life
                                    and record["fall_episode_id"] < last_fall) \
                                or (record["life_id"] == last_life
                                    and record["generation_id"] <= last_generation):
                            raise QualityError(
                                f"{path}: vertical pain column life/fall/generation regressed for "
                                f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    stream.update({
                        "last_life": record["life_id"],
                        "last_fall": record["fall_episode_id"],
                        "last_generation": record["generation_id"],
                        "active": key,
                        "active_start": record,
                    })
                    observed["vertical_pain_column_episodes_started_exact"] += 1
                    if overflow_delta == 0:
                        stream["tainted"] = False
                elif kind == "terminal":
                    if not stream["tainted"] and stream["active"] != key:
                        raise QualityError(
                            f"{path}: vertical pain column terminal has no matching start for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    if not stream["tainted"] and stream["active_start"] is not None:
                        stable_fields = (
                            "source", "forecast", "starting_physics_zone",
                            "expected_harmful_foot_zone",
                            "expected_harmful_physics_zone",
                            "expected_harmful_water_entry", "swept_segment_budget",
                            "elapsed_horizon", "aligned_command_provenance",
                        )
                        if any(record[name] != stream["active_start"][name]
                               for name in stable_fields):
                            raise QualityError(
                                f"{path}: vertical pain column terminal does not preserve its "
                                f"start fields for {identity}/{actor} at telemetry sequence "
                                f"{event['seq']}")
                    if stream["active"] == key:
                        stream["active"] = None
                        stream["active_start"] = None
                    observed["vertical_pain_column_episodes_completed_exact"] += 1
                    observed[correlation_counters[record["correlation"]]] += 1
                else:
                    if not stream["tainted"] and stream["active"] is not None:
                        raise QualityError(
                            f"{path}: vertical pain column capacity exhaustion precedes the "
                            f"active terminal for {identity}/{actor} at telemetry sequence "
                            f"{event['seq']}")
                    observed[
                        "vertical_pain_column_generation_capacity_exhaustions_exact"] += 1

            for name in reconciled_counters:
                if observed[name] > deltas[name]:
                    raise QualityError(
                        f"{path}: vertical pain column diagnostics exceed {name} delta for "
                        f"{identity} at telemetry sequence {event['seq']}")
                if overflow_delta == 0 and observed[name] != deltas[name]:
                    raise QualityError(
                        f"{path}: vertical pain column diagnostics do not reconcile with {name} "
                        f"delta for {identity} at telemetry sequence {event['seq']}")
            primary_delta = sum(deltas[name] for name in primary_counters)
            if primary_delta != len(records) + overflow_delta:
                raise QualityError(
                    f"{path}: vertical pain column diagnostics and overflows do not reconcile "
                    f"with counter deltas for {identity} at telemetry sequence {event['seq']}")
            if overflow_delta:
                for (stream_identity, _actor), stream in streams.items():
                    if stream_identity == identity:
                        stream["tainted"] = True
            previous[identity] = {
                name: bot[name] for name in reconciled_counters + (overflow_name,)
            }


def _validate_hazard_water_egress_diagnostic_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    streams: dict[tuple[str, str], dict[str, Any]] = {}
    final_counts: dict[str, tuple[int, int]] = {}
    overflow_name = HAZARD_WATER_EGRESS_DIAGNOSTIC_OVERFLOW_COUNTER
    for event in events:
        for bot in event["bots"]:
            records = bot.get("hazard_water_egress_diagnostics")
            if records is None:
                continue
            identity = bot["identity"]
            if overflow_name not in bot or "hazard_swim_egress_episodes_exact" not in bot:
                raise QualityError(
                    f"{path}: hazard-water egress diagnostics require episode and overflow counters")
            final_counts[identity] = (
                bot["hazard_swim_egress_episodes_exact"], bot[overflow_name])
            for record in records:
                actor = record["source_pawn_actor"]
                if actor != bot["actor"]:
                    raise QualityError(
                        f"{path}: hazard-water egress diagnostic actor does not match "
                        f"{identity} at telemetry sequence {event['seq']}")
                stream = streams.setdefault((identity, actor), {
                    "sequence": 0, "keys": set(), "count": 0,
                })
                if record["sequence"] <= stream["sequence"]:
                    raise QualityError(
                        f"{path}: hazard-water egress diagnostic sequence did not increase "
                        f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                key = (record["life_id"], record["episode_id"])
                if key in stream["keys"]:
                    raise QualityError(
                        f"{path}: duplicate hazard-water egress terminal record for "
                        f"{identity}/{actor} at telemetry sequence {event['seq']}")
                stream["sequence"] = record["sequence"]
                stream["keys"].add(key)
                stream["count"] += 1
    by_identity: defaultdict[str, int] = defaultdict(int)
    for (identity, _), stream in streams.items():
        by_identity[identity] += stream["count"]
    for identity, (episodes, overflows) in final_counts.items():
        if by_identity[identity] + overflows > episodes:
            raise QualityError(
                f"{path}: hazard-water egress diagnostics and overflows exceed "
                f"observed episodes for {identity}")


def _validate_move_stall_recovery_episode_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    overflow_name = "move_stall_recovery_episode_record_overflows_exact"
    previous: dict[str, dict[str, int]] = {}
    streams: dict[tuple[str, str], dict[str, Any]] = {}
    history_tainted: defaultdict[str, bool] = defaultdict(bool)
    final_counts: dict[str, dict[str, int]] = {}
    for event in events:
        for bot in event["bots"]:
            records = bot.get("move_stall_recovery_episodes")
            if records is None:
                continue
            identity = bot["identity"]
            if any(name not in bot for name in MOVE_STALL_RECOVERY_EXACT_COUNTERS):
                raise QualityError(
                    f"{path}: move-stall recovery records require the complete counter group")
            prior = previous.get(identity, {
                name: 0 for name in MOVE_STALL_RECOVERY_EXACT_COUNTERS
            })
            deltas = {
                name: bot[name] - prior[name] for name in MOVE_STALL_RECOVERY_EXACT_COUNTERS
            }
            overflow_delta = deltas[overflow_name]
            observed: defaultdict[str, int] = defaultdict(int)
            for record in records:
                actor = record["source_pawn_actor"]
                if actor != bot["actor"]:
                    raise QualityError(
                        f"{path}: move-stall recovery record actor does not match {identity} "
                        f"at telemetry sequence {event['seq']}")
                stream = streams.setdefault((identity, actor), {
                    "last_sequence": None,
                    "last_life": None,
                    "last_episode": None,
                    "keys": set(),
                    "tainted": history_tainted[identity],
                })
                sequence = record["sequence"]
                if stream["last_sequence"] is not None:
                    if sequence <= stream["last_sequence"]:
                        raise QualityError(
                            f"{path}: move-stall recovery record sequence did not increase for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                    if not stream["tainted"] and overflow_delta == 0 \
                            and sequence != stream["last_sequence"] + 1:
                        raise QualityError(
                            f"{path}: move-stall recovery record sequence is not consecutive for "
                            f"{identity}/{actor} at telemetry sequence {event['seq']}")
                elif sequence != 1 and not stream["tainted"] and overflow_delta == 0:
                    raise QualityError(
                        f"{path}: first move-stall recovery record sequence must be one for "
                        f"{identity}/{actor} at telemetry sequence {event['seq']}")
                key = (record["life_id"], record["episode_id"])
                if key in stream["keys"]:
                    raise QualityError(
                        f"{path}: duplicate move-stall recovery terminal record for "
                        f"{identity}/{actor} at telemetry sequence {event['seq']}")
                last_life = stream["last_life"]
                last_episode = stream["last_episode"]
                if last_life is not None and (
                        record["life_id"] < last_life or
                        record["episode_id"] <= last_episode):
                    raise QualityError(
                        f"{path}: move-stall recovery life or episode id regressed for "
                        f"{identity}/{actor} at telemetry sequence {event['seq']}")
                stream["last_sequence"] = sequence
                stream["last_life"] = record["life_id"]
                stream["last_episode"] = record["episode_id"]
                stream["keys"].add(key)
                observed[MOVE_STALL_RECOVERY_OUTCOMES[record["outcome"]]] += 1
            for name in MOVE_STALL_RECOVERY_OUTCOME_COUNTERS:
                if observed[name] > deltas[name]:
                    raise QualityError(
                        f"{path}: move-stall recovery records exceed {name} delta for "
                        f"{identity} at telemetry sequence {event['seq']}")
                if overflow_delta == 0 and observed[name] != deltas[name]:
                    raise QualityError(
                        f"{path}: move-stall recovery records do not reconcile with {name} "
                        f"delta for {identity} at telemetry sequence {event['seq']}")
            terminal_delta = sum(deltas[name] for name in MOVE_STALL_RECOVERY_OUTCOME_COUNTERS)
            if terminal_delta != len(records) + overflow_delta:
                raise QualityError(
                    f"{path}: move-stall recovery records and overflows do not reconcile "
                    f"with terminal outcome deltas for {identity} at telemetry sequence "
                    f"{event['seq']}")
            if overflow_delta:
                history_tainted[identity] = True
                for (stream_identity, _actor), stream in streams.items():
                    if stream_identity == identity:
                        stream["tainted"] = True
            previous[identity] = {
                name: bot[name] for name in MOVE_STALL_RECOVERY_EXACT_COUNTERS
            }
            final_counts[identity] = previous[identity]
    for identity, counters in final_counts.items():
        terminals = sum(counters[name] for name in MOVE_STALL_RECOVERY_OUTCOME_COUNTERS)
        if terminals != counters["move_stall_recovery_episodes_exact"]:
            raise QualityError(
                f"{path}: move-stall recovery terminal outcomes do not partition all "
                f"episodes for {identity}")


def _validate_move_stall_recovery_decision_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    last_sequence: dict[tuple[str, str], int] = {}
    observed: defaultdict[str, dict[str, int]] = defaultdict(
        lambda: {"all": 0, "navigation_replan": 0, "targetless_timeout": 0,
                 "direct_actor_move_toward_timeout": 0})
    finals: dict[str, dict[str, int]] = {}
    for event in events:
        for bot in event["bots"]:
            records = bot.get("move_stall_recovery_decisions")
            if records is None:
                continue
            identity = bot["identity"]
            required = (
                "move_stall_detections_exact", "move_stall_forced_replans_exact",
                "move_stall_navigation_forced_replans_exact",
                "move_stall_targetless_move_to_timeouts_exact",
                "move_stall_direct_actor_move_toward_timeouts_exact",
                MOVE_STALL_DECISION_RECORD_OVERFLOW_COUNTER,
            )
            if any(name not in bot for name in required):
                raise QualityError(
                    f"{path}: move-stall decision records require complete watchdog counters")
            for record in records:
                actor = record["source_pawn_actor"]
                if actor != bot["actor"]:
                    raise QualityError(
                        f"{path}: move-stall decision actor does not match {identity} "
                        f"at telemetry sequence {event['seq']}")
                key = (identity, actor)
                prior = last_sequence.get(key, 0)
                if record["sequence"] <= prior:
                    raise QualityError(
                        f"{path}: move-stall decision sequence did not increase for "
                        f"{identity}/{actor} at telemetry sequence {event['seq']}")
                last_sequence[key] = record["sequence"]
                observed[identity]["all"] += 1
                if record["decision"] != "none":
                    observed[identity][record["decision"]] += 1
            finals[identity] = {name: bot[name] for name in required}
    for identity, counters in finals.items():
        counts = observed[identity]
        overflow = counters[MOVE_STALL_DECISION_RECORD_OVERFLOW_COUNTER]
        if counts["all"] + overflow != counters["move_stall_detections_exact"]:
            raise QualityError(
                f"{path}: move-stall decision records and overflows do not partition "
                f"detections for {identity}")
        if counts["navigation_replan"] != counters["move_stall_navigation_forced_replans_exact"]:
            raise QualityError(
                f"{path}: navigation decision records do not reconcile for {identity}")
        if counts["targetless_timeout"] != counters["move_stall_targetless_move_to_timeouts_exact"]:
            raise QualityError(
                f"{path}: targetless decision records do not reconcile for {identity}")
        if counts["direct_actor_move_toward_timeout"] != \
                counters["move_stall_direct_actor_move_toward_timeouts_exact"]:
            raise QualityError(
                f"{path}: direct-actor decision records do not reconcile for {identity}")
        if counts["navigation_replan"] + counts["targetless_timeout"] \
                + counts["direct_actor_move_toward_timeout"] \
                != counters["move_stall_forced_replans_exact"]:
            raise QualityError(
                f"{path}: decision records do not partition forced replans for {identity}")


def _validate_hazard_death_partition_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    counter_for_attribution = {
        "direct_self_kill": "direct_self_kills",
        "direct_enemy_kill": "direct_enemy_kills",
        "unassisted_environmental_death": "unassisted_environmental_deaths",
        "recent_enemy_contributed_environmental_death_proxy":
            "recent_enemy_contributed_environmental_deaths_proxy",
        "ambiguous_death": "ambiguous_deaths",
    }
    prior: dict[str, dict[str, int]] = {}
    last_sequence: dict[tuple[str, str], int] = {}
    last_death_time: dict[tuple[str, str], float] = {}
    emitted: defaultdict[str, dict[str, int]] = defaultdict(
        lambda: {name: 0 for name in DEATH_ATTRIBUTION_COUNTERS[:-1]})

    for event in events:
        for bot in event["bots"]:
            records = bot.get("hazard_death_partition_records")
            if records is None:
                continue
            identity = bot["identity"]
            if any(name not in bot for name in DEATH_ATTRIBUTION_COUNTERS):
                raise QualityError(
                    f"{path}: hazard death partition records require the complete "
                    f"death-attribution counter group")
            current = {name: bot[name] for name in DEATH_ATTRIBUTION_COUNTERS[:-1]}
            previous = prior.get(identity, {name: 0 for name in current})
            deltas = {name: current[name] - previous[name] for name in current}
            observed = {name: 0 for name in current}
            water_terminals = {
                (diagnostic["sequence"], diagnostic["life_id"], diagnostic["episode_id"])
                for diagnostic in bot.get("hazard_water_egress_diagnostics", [])
                if diagnostic["terminal"] == "death_before_exit"
            }
            falling_terminals = {
                (diagnostic["sequence"], diagnostic["life_id"],
                 diagnostic["fall_episode_id"], diagnostic["generation_id"])
                for diagnostic in bot.get("vertical_pain_column_diagnostics", [])
                if diagnostic["kind"] == "terminal" and diagnostic["terminal"] == "died"
            }
            parity_terminals = {
                (diagnostic["life_generation"], diagnostic["invocation_token"],
                 diagnostic["walking_iteration"])
                for diagnostic in bot.get("falling_parity_realized_records", [])
                if diagnostic["outcome"] == "died"
            }
            for record in records:
                actor = record["source_pawn_actor"]
                if actor != bot["actor"]:
                    raise QualityError(
                        f"{path}: hazard death partition actor does not match {identity} "
                        f"at telemetry sequence {event['seq']}")
                stream = (identity, actor)
                sequence = record["sequence"]
                if sequence <= last_sequence.get(stream, 0):
                    raise QualityError(
                        f"{path}: hazard death partition sequence did not increase for "
                        f"{identity}/{actor} at telemetry sequence {event['seq']}")
                death_time = record["death_time_seconds"]
                if death_time < last_death_time.get(stream, 0.0):
                    raise QualityError(
                        f"{path}: hazard death partition time regressed for {identity}/{actor} "
                        f"at telemetry sequence {event['seq']}")
                last_sequence[stream] = sequence
                last_death_time[stream] = death_time
                if record["water_egress_terminal_known"] and (
                        record["water_egress_sequence"], record["water_egress_life_id"],
                        record["water_egress_episode_id"]) not in water_terminals:
                    raise QualityError(
                        f"{path}: hazard death partition water witness has no same-event "
                        f"death_before_exit diagnostic for {identity} at telemetry sequence "
                        f"{event['seq']}")
                if record["falling_hazard_terminal_known"] and (
                        record["falling_hazard_sequence"], record["falling_hazard_life_id"],
                        record["falling_hazard_fall_episode_id"],
                        record["falling_hazard_generation_id"]) not in falling_terminals:
                    raise QualityError(
                        f"{path}: hazard death partition falling witness has no same-event died "
                        f"diagnostic for {identity} at telemetry sequence {event['seq']}")
                if record["falling_parity_terminal_known"] and (
                        record["falling_parity_life_generation"],
                        record["falling_parity_invocation_token"],
                        record["falling_parity_walking_iteration"]) not in parity_terminals:
                    raise QualityError(
                        f"{path}: hazard death partition parity witness has no same-event died "
                        f"record for {identity} at telemetry sequence {event['seq']}")
                counter = counter_for_attribution[record["attribution"]]
                observed[counter] += 1
                emitted[identity][counter] += 1
            for name in current:
                if observed[name] != deltas[name]:
                    raise QualityError(
                        f"{path}: hazard death partition records do not reconcile with {name} "
                        f"delta for {identity} at telemetry sequence {event['seq']}")
            prior[identity] = current

    for identity, current in prior.items():
        for name, value in current.items():
            if emitted[identity][name] != value:
                raise QualityError(
                    f"{path}: hazard death partition records do not reconcile with final "
                    f"{name} for {identity}")


def _validate_walking_step_preflight_diagnostic_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    stream_order: dict[tuple[str, str], tuple[int, int, int, int]] = {}
    provisionals: dict[tuple[str, str, int, int, int], dict[str, Any]] = {}
    emitted: defaultdict[str, int] = defaultdict(int)
    provisional_records: defaultdict[str, int] = defaultdict(int)
    post_records: defaultdict[str, int] = defaultdict(int)
    missing_provisionals: defaultdict[str, int] = defaultdict(int)
    final_overflows: dict[str, int] = {}
    stable_pair_fields = (
        "movement_command_token", "origin", "predicted_unsupported_endpoint", "semantic_target",
        "semantic_destination", "start_support", "step_up", "forward", "actual_step_down",
        "support_probe",
    )

    for event in events:
        for bot in event["bots"]:
            diagnostics = bot.get("walking_step_preflight_diagnostics")
            if diagnostics is None:
                continue
            identity = bot["identity"]
            for diagnostic in diagnostics:
                actor = diagnostic["source_pawn_actor"]
                stream = (identity, actor)
                sequence = diagnostic["sequence"]
                life = diagnostic["life_generation"]
                invocation = diagnostic["invocation_token"]
                iteration = diagnostic["walking_iteration"]
                prior_order = stream_order.get(stream)
                if prior_order is not None:
                    prior_sequence, prior_life, prior_invocation, prior_iteration = prior_order
                    if sequence <= prior_sequence:
                        raise QualityError(
                            f"{path}: walking step diagnostic sequence is not strictly increasing "
                            f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                    if life < prior_life:
                        raise QualityError(
                            f"{path}: walking step diagnostic life generation regressed "
                            f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                    if invocation < prior_invocation:
                        raise QualityError(
                            f"{path}: walking step diagnostic invocation regressed "
                            f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                    if life == prior_life and invocation == prior_invocation \
                            and iteration < prior_iteration:
                        raise QualityError(
                            f"{path}: walking step diagnostic iteration regressed within an invocation "
                            f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                stream_order[stream] = (sequence, life, invocation, iteration)
                key = (identity, actor, life, invocation, iteration)
                phase = diagnostic["phase"]
                if phase == "precommit_provisional":
                    if key in provisionals:
                        raise QualityError(
                            f"{path}: duplicate walking step provisional diagnostic for "
                            f"{identity}/{actor} life {life} invocation {invocation} iteration {iteration}")
                    provisionals[key] = diagnostic
                    if diagnostic["reason"] == \
                            "walking_step_preflight_reason_harmful_pain_fall_exact":
                        provisional_records[identity] += 1
                else:
                    post_records[identity] += 1
                    provisional = provisionals.pop(key, None)
                    if provisional is None:
                        missing_provisionals[identity] += 1
                    else:
                        if provisional["reason"] != \
                                "walking_step_preflight_reason_harmful_pain_fall_exact":
                            raise QualityError(
                                f"{path}: post-MayFall diagnostic does not follow an authorization "
                                f"for {identity}/{actor}")
                        if any(provisional[name] != diagnostic[name]
                               for name in stable_pair_fields):
                            raise QualityError(
                                f"{path}: provisional and post-MayFall diagnostics do not correlate "
                                f"for {identity}/{actor} life {life} invocation {invocation} "
                                f"iteration {iteration}")
                emitted[identity] += 1

            observations = bot["walking_step_preflight_observations_exact"]
            provisional_count = bot["walking_step_preflight_provisional_authorizations_exact"]
            overflow_count = bot["walking_step_preflight_diagnostic_overflows_exact"]
            final_overflows[identity] = overflow_count
            if emitted[identity] + overflow_count > observations + provisional_count:
                raise QualityError(
                    f"{path}: walking step diagnostics and overflows exceed observation evidence "
                    f"for {identity} at telemetry sequence {event['seq']}")
            if provisional_records[identity] > provisional_count:
                raise QualityError(
                    f"{path}: provisional walking step diagnostics exceed authorization evidence "
                    f"for {identity} at telemetry sequence {event['seq']}")
            if post_records[identity] > provisional_count:
                raise QualityError(
                    f"{path}: post-MayFall diagnostics exceed provisional authorization evidence "
                    f"for {identity} at telemetry sequence {event['seq']}")
            if missing_provisionals[identity] > overflow_count:
                raise QualityError(
                    f"{path}: post-MayFall diagnostic has no correlatable provisional record "
                    f"or overflow evidence for {identity} at telemetry sequence {event['seq']}")

    unmatched_authorizations: defaultdict[str, int] = defaultdict(int)
    for (identity, _actor, _life, _invocation, _iteration), diagnostic in provisionals.items():
        if diagnostic["reason"] == \
                "walking_step_preflight_reason_harmful_pain_fall_exact":
            unmatched_authorizations[identity] += 1
    for identity in set(missing_provisionals) | set(unmatched_authorizations):
        uncorrelated = missing_provisionals[identity] + unmatched_authorizations[identity]
        if uncorrelated > final_overflows.get(identity, 0):
            raise QualityError(
                f"{path}: walking step authorization diagnostics are not fully correlated "
                f"for {identity} and overflow evidence is insufficient")


def _validate_walking_hitwall_dispatch_diagnostic_stream(
        events: list[dict[str, Any]], path: Path) -> None:
    previous_sequence: dict[tuple[str, str], int] = {}
    for event in events:
        for bot in event["bots"]:
            identity = bot["identity"]
            for diagnostic in bot.get("walking_hitwall_dispatch_diagnostics", []):
                actor = diagnostic["source_pawn_actor"]
                if actor != bot["actor"]:
                    raise QualityError(
                        f"{path}: walking HitWall diagnostic actor does not match "
                        f"{identity} at telemetry sequence {event['seq']}")
                stream = (identity, actor)
                previous = previous_sequence.get(stream)
                if previous is not None and diagnostic["sequence"] <= previous:
                    raise QualityError(
                        f"{path}: walking HitWall diagnostic sequence is not strictly increasing "
                        f"for {identity}/{actor} at telemetry sequence {event['seq']}")
                previous_sequence[stream] = diagnostic["sequence"]


def _load_json(path: Path, context: str) -> dict[str, Any]:
    try:
        return _object(json.loads(path.read_text(encoding="utf-8-sig")), context)
    except FileNotFoundError as exc:
        raise QualityError(f"missing {context}: {path}") from exc
    except json.JSONDecodeError as exc:
        raise QualityError(f"invalid {context} JSON at {path}: {exc}") from exc


def _validate_positive_dps_veto_action_stream(events: list[dict[str, Any]], path: Path) -> None:
    previous_sequence: dict[tuple[str, str], int] = {}
    seen_keys: set[tuple[str, str, int, int, int]] = set()
    counts: defaultdict[str, defaultdict[str, int]] = defaultdict(lambda: defaultdict(int))
    final: dict[str, dict[str, Any]] = {}
    for event in events:
        for bot in event["bots"]:
            identity = bot["identity"]
            final[identity] = bot
            for action in bot.get("walking_step_preflight_positive_dps_veto_actions", []):
                stream = (identity, action["source_pawn_actor"])
                previous = previous_sequence.get(stream)
                if previous is not None and action["sequence"] <= previous:
                    raise QualityError(f"{path}: positive-DPS veto action sequence regressed for {stream}")
                previous_sequence[stream] = action["sequence"]
                key = (*stream, action["life_generation"], action["invocation_token"],
                       action["walking_iteration"])
                if key in seen_keys:
                    raise QualityError(f"{path}: duplicate positive-DPS veto action join key for {stream}")
                seen_keys.add(key)
                outcome = action["outcome"]
                test = action["rollback_test_attempted"]
                actual = action["rollback_actual_attempted"]
                forced = action["forced_replan"]
                if outcome == "legacy_pain_ledge_superseded":
                    valid = action["legacy_pain_ledge_superseded"] and not test and not actual and not forced
                elif outcome == "rollback_test_rejected":
                    valid = not action["legacy_pain_ledge_superseded"] and test and not actual \
                        and action["rollback_test_fraction"] < 1.0 and not forced
                elif outcome == "rollback_actual_rejected":
                    valid = not action["legacy_pain_ledge_superseded"] and test and actual \
                        and action["rollback_test_fraction"] == 1.0 \
                        and action["rollback_actual_fraction"] < 1.0 and not forced
                else:
                    valid = not action["legacy_pain_ledge_superseded"] and test and actual \
                        and action["rollback_test_fraction"] == 1.0 \
                        and action["rollback_actual_fraction"] == 1.0 and forced
                if not valid:
                    raise QualityError(f"{path}: invalid positive-DPS veto action outcome evidence")
                counts[identity][outcome] += 1
    for identity, bot in final.items():
        observed = counts[identity]
        if observed["applied"] != bot["walking_step_preflight_positive_dps_veto_applied_exact"]:
            raise QualityError(f"{path}: applied positive-DPS action records do not reconcile")
        if observed["applied"] != bot["walking_step_preflight_positive_dps_veto_forced_replans_exact"]:
            raise QualityError(f"{path}: forced replan action records do not reconcile")
        rejected = observed["rollback_test_rejected"] + observed["rollback_actual_rejected"]
        if rejected != bot["walking_step_preflight_positive_dps_veto_rollback_rejected_exact"]:
            raise QualityError(f"{path}: rejected positive-DPS action records do not reconcile")
        emitted = sum(observed.values())
        overflow = bot["walking_step_preflight_positive_dps_veto_action_overflows_exact"]
        if emitted + overflow > bot["walking_step_preflight_positive_dps_veto_eligible_exact"]:
            raise QualityError(f"{path}: positive-DPS action records exceed eligibility")


def _config_id(url: str, seed: int, max_ticks: int, fixed_delta: float, difficulty: int,
               bot_count: int | None = None, requested_roster: list[dict[str, Any]] | None = None,
               harmful_zone_escape_enabled: bool | None = None,
               walking_preflight_positive_dps_veto_enabled: bool | None = None,
               hazard_swim_egress_enabled: bool | None = None,
               hazard_swim_egress_live_enabled: bool | None = None,
               failed_navigation_avoidance_enabled: bool | None = None,
               falling_hazard_recovery_enabled: bool | None = None,
               falling_hazard_recovery_live_enabled: bool | None = None,
               targetless_move_to_timeout_enabled: bool | None = None,
               direct_actor_move_toward_timeout_enabled: bool | None = None,
               target_selection_observer_enabled: bool | None = None,
               pick_target_observer_enabled: bool | None = None,
               pick_target_predicate_mode: str | None = None,
               warn_target_observer_enabled: bool | None = None,
               inventory_direct_reach_support_observer_enabled: bool | None = None,
               inventory_marker_direct_reach_safety_enabled: bool | None = None,
               native_path_commit_observer_enabled: bool | None = None,
               reachspec_capability_observer_enabled: bool | None = None,
               direct_reach_command_observer_enabled: bool | None = None,
               pawn_vision_cone_enabled: bool | None = None,
               pawn_vision_observer_enabled: bool | None = None,
               shadow_policy_set: list[str] | None = None,
               finite_move_command_guard_enabled: bool | None = None) -> str:
    canonical_text = (
        f"url={url}\nseed={seed}\nmax_ticks={max_ticks}\n"
        f"fixed_delta={fixed_delta:.9f}\ndifficulty={difficulty}\n"
    )
    if bot_count is not None:
        canonical_text += f"bot_count={bot_count}\n"
        if harmful_zone_escape_enabled is not None:
            canonical_text += "harmful_zone_escape_enabled=" + (
                "1\n" if harmful_zone_escape_enabled else "0\n")
        if walking_preflight_positive_dps_veto_enabled is not None:
            canonical_text += "walking_preflight_positive_dps_veto_enabled=" + (
                "1\n" if walking_preflight_positive_dps_veto_enabled else "0\n")
        if hazard_swim_egress_enabled is not None:
            canonical_text += "hazard_swim_egress_enabled=" + (
                "1\n" if hazard_swim_egress_enabled else "0\n")
        if hazard_swim_egress_live_enabled is not None:
            canonical_text += "hazard_swim_egress_live_enabled=" + (
                "1\n" if hazard_swim_egress_live_enabled else "0\n")
        if failed_navigation_avoidance_enabled is not None:
            canonical_text += "failed_navigation_avoidance_enabled=" + (
                "1\n" if failed_navigation_avoidance_enabled else "0\n")
        if falling_hazard_recovery_enabled is not None:
            canonical_text += "falling_hazard_recovery_enabled=" + (
                "1\n" if falling_hazard_recovery_enabled else "0\n")
        if falling_hazard_recovery_live_enabled is not None:
            canonical_text += "falling_hazard_recovery_live_enabled=" + (
                "1\n" if falling_hazard_recovery_live_enabled else "0\n")
        if targetless_move_to_timeout_enabled is not None:
            canonical_text += "targetless_move_to_timeout_enabled=" + (
                "1\n" if targetless_move_to_timeout_enabled else "0\n")
        if direct_actor_move_toward_timeout_enabled is not None:
            canonical_text += "direct_actor_move_toward_timeout_enabled=" + (
                "1\n" if direct_actor_move_toward_timeout_enabled else "0\n")
        if target_selection_observer_enabled is not None:
            canonical_text += "target_selection_observer_enabled=" + (
                "1\n" if target_selection_observer_enabled else "0\n")
        if pick_target_observer_enabled is not None:
            canonical_text += "pick_target_observer_enabled=" + (
                "1\n" if pick_target_observer_enabled else "0\n")
        if pick_target_predicate_mode is not None:
            canonical_text += f"pick_target_predicate_mode={pick_target_predicate_mode}\n"
        if warn_target_observer_enabled is not None:
            canonical_text += "warn_target_observer_enabled=" + (
                "1\n" if warn_target_observer_enabled else "0\n")
        if inventory_direct_reach_support_observer_enabled is not None:
            canonical_text += "inventory_direct_reach_support_observer_enabled=" + (
                "1\n" if inventory_direct_reach_support_observer_enabled else "0\n")
        if inventory_marker_direct_reach_safety_enabled is not None:
            canonical_text += "inventory_marker_direct_reach_safety_enabled=" + (
                "1\n" if inventory_marker_direct_reach_safety_enabled else "0\n")
        if native_path_commit_observer_enabled is not None:
            canonical_text += "native_path_commit_observer_enabled=" + (
                "1\n" if native_path_commit_observer_enabled else "0\n")
        if reachspec_capability_observer_enabled is not None:
            canonical_text += "reachspec_capability_observer_enabled=" + (
                "1\n" if reachspec_capability_observer_enabled else "0\n")
        if direct_reach_command_observer_enabled is not None:
            canonical_text += "direct_reach_command_observer_enabled=" + (
                "1\n" if direct_reach_command_observer_enabled else "0\n")
        if pawn_vision_cone_enabled is not None:
            canonical_text += "pawn_vision_cone_enabled=" + (
                "1\n" if pawn_vision_cone_enabled else "0\n")
        if pawn_vision_observer_enabled is not None:
            canonical_text += "pawn_vision_observer_enabled=" + (
                "1\n" if pawn_vision_observer_enabled else "0\n")
        if finite_move_command_guard_enabled is not None:
            canonical_text += "finite_move_command_guard_enabled=" + (
                "1\n" if finite_move_command_guard_enabled else "0\n")
        if shadow_policy_set is not None:
            canonical_text += "".join(f"shadow_policy={policy}\n" for policy in shadow_policy_set)
        assert requested_roster is not None
        canonical_text += "".join(f"roster={entry['identity_fragment']}\n" for entry in requested_roster)
    canonical = canonical_text.encode("utf-8")
    digest = 1469598103934665603
    for byte in canonical:
        digest ^= byte
        digest = (digest * 1099511628211) & ((1 << 64) - 1)
    return f"fnv1a64:{digest:016x}"


def _layout_fingerprint(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.startswith("sha256:"):
        raise QualityError(f"{context} must be a sha256: fingerprint")
    digest = value.removeprefix("sha256:")
    if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
        raise QualityError(f"{context} must be a lowercase sha256: fingerprint")
    return value


def _close(left: float, right: float) -> bool:
    return math.isclose(left, right, rel_tol=1e-7, abs_tol=1e-8)


def _ascii_lower(value: str) -> str:
    return "".join(chr(ord(character) + 32) if "A" <= character <= "Z" else character
                   for character in value)


def _validate_shadow_policy_set(value: Any, context: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise QualityError(f"{context} must be a non-empty array")
    policies: list[str] = []
    for index, policy in enumerate(value):
        if not isinstance(policy, str) or not policy:
            raise QualityError(f"{context}[{index}] must be a non-empty string")
        if any(character in " \t\r\n" for character in policy):
            raise QualityError(f"{context}[{index}] contains whitespace")
        policies.append(policy)
    if policies != sorted(policies):
        raise QualityError(f"{context} must be sorted")
    if len(policies) != len(set(policies)):
        raise QualityError(f"{context} must not contain duplicates")
    return policies


def _validate_requested_roster(raw: Any, context: str, bot_count: int) -> list[dict[str, Any]]:
    if not isinstance(raw, list):
        raise QualityError(f"{context} must be an array")
    if len(raw) != bot_count:
        raise QualityError(f"{context} count must equal bot_count")
    roster: list[dict[str, Any]] = []
    for index, item in enumerate(raw):
        fields = _object(item, f"{context}[{index}]")
        roster.append({
            "roster_index": _strict_integer(fields.get("roster_index"),
                                             f"{context}[{index}].roster_index", minimum=0),
            "requested_name": _string(fields, "requested_name", f"{context}[{index}]"),
            "external_skill": _strict_integer(fields.get("external_skill"),
                                               f"{context}[{index}].external_skill", minimum=0, maximum=7),
            "identity_fragment": _string(fields, "identity_fragment", f"{context}[{index}]", nonempty=True),
        })
    indexes = [entry["roster_index"] for entry in roster]
    if indexes != list(range(bot_count)):
        raise QualityError(f"{context} indexes must be unique, contiguous, and ordered from zero")
    fragments = [entry["identity_fragment"] for entry in roster]
    if len(fragments) != len(set(fragments)):
        raise QualityError(f"{context} identity fragments must be unique")
    for index, entry in enumerate(roster):
        try:
            name_hex = entry["requested_name"].encode("utf-8").hex()
        except UnicodeEncodeError as exc:
            raise QualityError(f"{context}[{index}].requested_name must be valid UTF-8") from exc
        expected_fragment = (
            f"participant-v1:index={entry['roster_index']};external_skill={entry['external_skill']};"
            f"requested_name_hex={name_hex}")
        if entry["identity_fragment"] != expected_fragment:
            raise QualityError(f"{context}[{index}].identity_fragment is not canonical")
    names = [_ascii_lower(entry["requested_name"]) for entry in roster if entry["requested_name"]]
    if len(names) != len(set(names)):
        raise QualityError(f"{context} non-empty requested names must be unique case-insensitively")
    return roster


def _validate_actual_roster(raw: Any, context: str, bot_count: int, *, complete: bool) -> list[dict[str, Any]]:
    if not isinstance(raw, list):
        raise QualityError(f"{context} must be an array")
    if len(raw) > bot_count:
        raise QualityError(f"{context} count exceeds bot_count")
    if complete and len(raw) != bot_count:
        raise QualityError(f"{context} count must equal bot_count for a complete run")
    roster: list[dict[str, Any]] = []
    for index, item in enumerate(raw):
        fields = _object(item, f"{context}[{index}]")
        roster.append({
            "roster_index": _strict_integer(fields.get("roster_index"),
                                             f"{context}[{index}].roster_index", minimum=0),
            "identity": _string(fields, "identity", f"{context}[{index}]", nonempty=True),
            "actor": _string(fields, "actor", f"{context}[{index}]", nonempty=True),
            "player_name": _string(fields, "player_name", f"{context}[{index}]", nonempty=True),
            "class": _string(fields, "class", f"{context}[{index}]", nonempty=True),
        })
    indexes = [entry["roster_index"] for entry in roster]
    if indexes != list(range(len(roster))):
        raise QualityError(f"{context} indexes must be unique, contiguous, and ordered from zero")
    for field in ("identity", "actor"):
        values = [entry[field] for entry in roster]
        if len(values) != len(set(values)):
            raise QualityError(f"{context} {field} values must be unique")
    names = [_ascii_lower(entry["player_name"]) for entry in roster]
    if len(names) != len(set(names)):
        raise QualityError(f"{context} player names must be unique case-insensitively")
    return roster


def _validate_manifest(path: Path) -> dict[str, Any]:
    raw = _load_json(path, "manifest")
    schema = raw.get("schema")
    if schema not in (MANIFEST_SCHEMA, MANIFEST_SCHEMA_V2, MANIFEST_SCHEMA_V3):
        raise QualityError(f"{path}: unsupported manifest schema {raw.get('schema')!r}")
    if raw.get("driver") != "bot-benchmark":
        raise QualityError(f"{path}: manifest driver must be 'bot-benchmark'")
    url = _string(raw, "url", "manifest", nonempty=True)
    output_directory = _string(raw, "output_directory", "manifest")
    seed = _integer(raw.get("seed"), "manifest.seed", minimum=0)
    max_ticks = _integer(raw.get("max_ticks"), "manifest.max_ticks", minimum=1)
    fixed_delta = _number(raw.get("fixed_delta"), "manifest.fixed_delta", minimum=0.0)
    if fixed_delta <= 0:
        raise QualityError("manifest.fixed_delta must be positive")
    difficulty = _integer(raw.get("difficulty"), "manifest.difficulty", minimum=0)
    if difficulty > 7:
        raise QualityError("manifest.difficulty must be at most 7")
    event_cap = _integer(raw.get("telemetry_event_cap"), "manifest.telemetry_event_cap", minimum=0)
    if event_cap != max_ticks + 2:
        raise QualityError(f"{path}: telemetry event cap does not equal max_ticks + 2")
    config_id = _string(raw, "config_id", "manifest", nonempty=True)
    bot_count = None
    requested_roster = None
    death_attribution_recent_window_seconds = None
    suicides_exact_semantics = None
    harmful_zone_escape_enabled = None
    walking_preflight_positive_dps_veto_enabled = None
    hazard_swim_egress_enabled = None
    hazard_swim_egress_live_enabled = None
    failed_navigation_avoidance_enabled = None
    falling_hazard_recovery_enabled = None
    falling_hazard_recovery_live_enabled = None
    targetless_move_to_timeout_enabled = None
    direct_actor_move_toward_timeout_enabled = None
    target_selection_observer_enabled = None
    pick_target_observer_enabled = None
    pick_target_predicate_mode = None
    warn_target_observer_enabled = None
    inventory_direct_reach_support_observer_enabled = None
    inventory_marker_direct_reach_safety_enabled = None
    native_path_commit_observer_enabled = None
    reachspec_capability_observer_enabled = None
    direct_reach_command_observer_enabled = None
    pawn_vision_cone_enabled = None
    pawn_vision_observer_enabled = None
    finite_move_command_guard_enabled = None
    shadow_policy_set = None
    build_identity = None
    if schema == MANIFEST_SCHEMA_V3:
        build_identity = _validate_build_identity(raw.get("build_identity"), "manifest.build_identity")
    if schema in (MANIFEST_SCHEMA_V2, MANIFEST_SCHEMA_V3):
        bot_count = _strict_integer(raw.get("bot_count"), "manifest.bot_count", minimum=1, maximum=16)
        requested_roster = _validate_requested_roster(raw.get("requested_roster"),
                                                      "manifest.requested_roster", bot_count)
        if "death_attribution_recent_window_seconds" in raw:
            death_attribution_recent_window_seconds = _number(
                raw.get("death_attribution_recent_window_seconds"),
                "manifest.death_attribution_recent_window_seconds", minimum=0.0)
        if "suicides_exact_semantics" in raw:
            suicides_exact_semantics = _string(raw, "suicides_exact_semantics", "manifest")
            if suicides_exact_semantics != "legacy_scoreboard_self_or_nonplayer_killer":
                raise QualityError(f"{path}: unsupported suicides_exact_semantics")
        if "harmful_zone_escape_enabled" in raw:
            harmful_zone_escape_enabled = _boolean(
                raw.get("harmful_zone_escape_enabled"),
                "manifest.harmful_zone_escape_enabled")
        if "walking_preflight_positive_dps_veto_enabled" in raw:
            walking_preflight_positive_dps_veto_enabled = _boolean(
                raw.get("walking_preflight_positive_dps_veto_enabled"),
                "manifest.walking_preflight_positive_dps_veto_enabled")
        if "hazard_swim_egress_enabled" in raw:
            hazard_swim_egress_enabled = _boolean(
                raw.get("hazard_swim_egress_enabled"), "manifest.hazard_swim_egress_enabled")
        if "hazard_swim_egress_live_enabled" in raw:
            hazard_swim_egress_live_enabled = _boolean(
                raw.get("hazard_swim_egress_live_enabled"),
                "manifest.hazard_swim_egress_live_enabled")
        if "failed_navigation_avoidance_enabled" in raw:
            failed_navigation_avoidance_enabled = _boolean(
                raw.get("failed_navigation_avoidance_enabled"),
                "manifest.failed_navigation_avoidance_enabled")
        if "falling_hazard_recovery_enabled" in raw:
            falling_hazard_recovery_enabled = _boolean(
                raw.get("falling_hazard_recovery_enabled"),
                "manifest.falling_hazard_recovery_enabled")
        if "falling_hazard_recovery_live_enabled" in raw:
            falling_hazard_recovery_live_enabled = _boolean(
                raw.get("falling_hazard_recovery_live_enabled"),
                "manifest.falling_hazard_recovery_live_enabled")
        if "targetless_move_to_timeout_enabled" in raw:
            targetless_move_to_timeout_enabled = _boolean(
                raw.get("targetless_move_to_timeout_enabled"),
                "manifest.targetless_move_to_timeout_enabled")
        if "direct_actor_move_toward_timeout_enabled" in raw:
            direct_actor_move_toward_timeout_enabled = _boolean(
                raw.get("direct_actor_move_toward_timeout_enabled"),
                "manifest.direct_actor_move_toward_timeout_enabled")
        if "target_selection_observer_enabled" in raw:
            target_selection_observer_enabled = _boolean(
                raw.get("target_selection_observer_enabled"),
                "manifest.target_selection_observer_enabled")
        if "pick_target_observer_enabled" in raw:
            pick_target_observer_enabled = _boolean(
                raw.get("pick_target_observer_enabled"),
                "manifest.pick_target_observer_enabled")
        if pick_target_observer_enabled is True:
            pick_target_predicate_mode = _string(
                raw, "pick_target_predicate_mode", "manifest", nonempty=True)
            if pick_target_predicate_mode not in ("stock", "fixed"):
                raise QualityError(f"{path}: PickTarget predicate mode is not recognized")
        elif "pick_target_predicate_mode" in raw:
            raise QualityError(
                f"{path}: PickTarget predicate mode requires the PickTarget observer")
        if "warn_target_observer_enabled" in raw:
            warn_target_observer_enabled = _boolean(
                raw.get("warn_target_observer_enabled"),
                "manifest.warn_target_observer_enabled")
            if warn_target_observer_enabled and pick_target_observer_enabled is not True:
                raise QualityError(f"{path}: WarnTarget observer requires PickTarget observer")
        if "inventory_direct_reach_support_observer_enabled" in raw:
            inventory_direct_reach_support_observer_enabled = _boolean(
                raw.get("inventory_direct_reach_support_observer_enabled"),
                "manifest.inventory_direct_reach_support_observer_enabled")
        if "inventory_marker_direct_reach_safety_enabled" in raw:
            inventory_marker_direct_reach_safety_enabled = _boolean(
                raw.get("inventory_marker_direct_reach_safety_enabled"),
                "manifest.inventory_marker_direct_reach_safety_enabled")
        if "native_path_commit_observer_enabled" in raw:
            native_path_commit_observer_enabled = _boolean(
                raw.get("native_path_commit_observer_enabled"),
                "manifest.native_path_commit_observer_enabled")
        if "reachspec_capability_observer_enabled" in raw:
            reachspec_capability_observer_enabled = _boolean(
                raw.get("reachspec_capability_observer_enabled"),
                "manifest.reachspec_capability_observer_enabled")
        if "direct_reach_command_observer_enabled" in raw:
            direct_reach_command_observer_enabled = _boolean(
                raw.get("direct_reach_command_observer_enabled"),
                "manifest.direct_reach_command_observer_enabled")
        if "pawn_vision_cone_enabled" in raw:
            pawn_vision_cone_enabled = _boolean(
                raw.get("pawn_vision_cone_enabled"), "manifest.pawn_vision_cone_enabled")
        if "pawn_vision_observer_enabled" in raw:
            pawn_vision_observer_enabled = _boolean(
                raw.get("pawn_vision_observer_enabled"),
                "manifest.pawn_vision_observer_enabled")
        if "finite_move_command_guard_enabled" in raw:
            finite_move_command_guard_enabled = _boolean(
                raw.get("finite_move_command_guard_enabled"),
                "manifest.finite_move_command_guard_enabled")
        if schema == MANIFEST_SCHEMA_V3:
            shadow_policy_set = _validate_shadow_policy_set(
                raw.get("shadow_policy_set"), "manifest.shadow_policy_set")
        elif "shadow_policy_set" in raw:
            raise QualityError(f"{path}: shadow_policy_set requires manifest v3")
    expected_id = _config_id(url, seed, max_ticks, fixed_delta, difficulty, bot_count,
                             requested_roster, harmful_zone_escape_enabled,
                             walking_preflight_positive_dps_veto_enabled,
                             hazard_swim_egress_enabled,
                             hazard_swim_egress_live_enabled,
                             failed_navigation_avoidance_enabled,
                             falling_hazard_recovery_enabled,
                             falling_hazard_recovery_live_enabled,
                             targetless_move_to_timeout_enabled,
                             direct_actor_move_toward_timeout_enabled,
                             target_selection_observer_enabled,
                             pick_target_observer_enabled,
                             pick_target_predicate_mode,
                             warn_target_observer_enabled,
                             inventory_direct_reach_support_observer_enabled,
                             inventory_marker_direct_reach_safety_enabled,
                             native_path_commit_observer_enabled,
                             reachspec_capability_observer_enabled,
                             direct_reach_command_observer_enabled,
                             pawn_vision_cone_enabled,
                             pawn_vision_observer_enabled,
                             shadow_policy_set,
                             finite_move_command_guard_enabled)
    if config_id != expected_id:
        raise QualityError(f"{path}: config_id does not match the manifest configuration")
    return {
        "config_id": config_id,
        "url": url,
        "output_directory": output_directory,
        "seed": seed,
        "max_ticks": max_ticks,
        "fixed_delta": fixed_delta,
        "difficulty": difficulty,
        "telemetry_event_cap": event_cap,
        "build_identity": build_identity,
        "schema": schema,
        "bot_count": bot_count,
        "requested_roster": requested_roster,
        "death_attribution_recent_window_seconds": death_attribution_recent_window_seconds,
        "suicides_exact_semantics": suicides_exact_semantics,
        "harmful_zone_escape_enabled": harmful_zone_escape_enabled,
        "walking_preflight_positive_dps_veto_enabled": walking_preflight_positive_dps_veto_enabled,
        "hazard_swim_egress_enabled": hazard_swim_egress_enabled,
        "hazard_swim_egress_live_enabled": hazard_swim_egress_live_enabled,
        "failed_navigation_avoidance_enabled": failed_navigation_avoidance_enabled,
        "falling_hazard_recovery_enabled": falling_hazard_recovery_enabled,
        "falling_hazard_recovery_live_enabled": falling_hazard_recovery_live_enabled,
        "targetless_move_to_timeout_enabled": targetless_move_to_timeout_enabled,
        "direct_actor_move_toward_timeout_enabled": direct_actor_move_toward_timeout_enabled,
        "target_selection_observer_enabled": target_selection_observer_enabled,
        "pick_target_observer_enabled": pick_target_observer_enabled,
        "pick_target_predicate_mode": pick_target_predicate_mode,
        "warn_target_observer_enabled": warn_target_observer_enabled,
        "inventory_direct_reach_support_observer_enabled": (
            inventory_direct_reach_support_observer_enabled),
        "inventory_marker_direct_reach_safety_enabled": (
            inventory_marker_direct_reach_safety_enabled),
        "native_path_commit_observer_enabled": native_path_commit_observer_enabled,
        "reachspec_capability_observer_enabled": reachspec_capability_observer_enabled,
        "direct_reach_command_observer_enabled": direct_reach_command_observer_enabled,
        "pawn_vision_cone_enabled": pawn_vision_cone_enabled,
        "pawn_vision_observer_enabled": pawn_vision_observer_enabled,
        "finite_move_command_guard_enabled": finite_move_command_guard_enabled,
        "shadow_policy_set": shadow_policy_set,
    }


def _validate_bot(raw: Any, context: str, schema: str,
                  pick_target_predicate_mode: str | None = None) -> dict[str, Any]:
    bot = _object(raw, context)
    if schema != TELEMETRY_SCHEMA_V2 and any(name in bot for name in (
            "walking_step_preflight_diagnostics", "falling_parity_realized_records",
            "vertical_pain_column_diagnostics", "hazard_water_egress_diagnostics",
            "hazard_death_partition_records", "move_stall_recovery_episodes",
            "move_stall_recovery_decisions",
            "walking_hitwall_dispatch_diagnostics",
            "inventory_direct_reach_support_diagnostics")):
        raise QualityError(
            f"{context}: observer record arrays require telemetry v2")
    result: dict[str, Any] = {
        "identity": _string(bot, "identity", context, nonempty=True),
        "actor": _string(bot, "actor", context, nonempty=True),
        "player_name": _string(bot, "player_name", context),
        "class": _string(bot, "class", context, nonempty=True),
        "state": _string(bot, "state", context),
        "health": _integer(bot.get("health"), f"{context}.health"),
    }
    for vector_name in ("position", "velocity"):
        vector = _object(bot.get(vector_name), f"{context}.{vector_name}")
        result[vector_name] = {
            axis: _number(vector.get(axis), f"{context}.{vector_name}.{axis}") for axis in "xyz"
        }
    if schema == TELEMETRY_SCHEMA_V2:
        result.update({
            "score": _number(bot.get("score"), f"{context}.score"),
            "pri_deaths": _number(bot.get("pri_deaths"), f"{context}.pri_deaths", minimum=0.0),
            "movement_intent": _boolean(bot.get("movement_intent"), f"{context}.movement_intent"),
            "in_hazard_zone": _boolean(bot.get("in_hazard_zone"), f"{context}.in_hazard_zone"),
        })
        for name in CORE_EXACT_COUNTERS:
            result[name] = _integer(bot.get(name), f"{context}.{name}", minimum=0)
        for name in OPTIONAL_EXACT_COUNTERS:
            if name in bot:
                result[name] = _integer(bot.get(name), f"{context}.{name}", minimum=0)
        for name in OPTIONAL_CUMULATIVE_NUMBERS:
            if name in bot:
                result[name] = _number(bot.get(name), f"{context}.{name}", minimum=0.0)
        for name in OPTIONAL_STATIC_METRICS:
            if name in bot:
                result[name] = _integer(bot.get(name), f"{context}.{name}", minimum=0)
        for label, names in (
                ("pain ledge", PAIN_LEDGE_EXACT_COUNTERS),
                ("wall adjust", WALL_ADJUST_EXACT_COUNTERS),
                ("failed navigation", FAILED_NAVIGATION_EXACT_COUNTERS),
                ("harmful-zone escape", HARMFUL_ZONE_ESCAPE_EXACT_COUNTERS),
                ("hazard swim egress", HAZARD_SWIM_EGRESS_EXACT_COUNTERS),
				("hazard swim egress planner handoff",
				 HAZARD_SWIM_EGRESS_PLANNER_HANDOFF_OUTCOME_COUNTERS),
                ("falling pre-move anchor", FALLING_PRE_MOVE_ANCHOR_COUNTERS),
                ("hazard swim egress live", HAZARD_SWIM_EGRESS_LIVE_COUNTERS),
                ("hazard swim egress direct navigation", HAZARD_SWIM_EGRESS_DIRECT_NAV_COUNTERS),
                ("hazard residence", HAZARD_RESIDENCE_COUNTERS),
                ("falling hazard recovery", FALLING_HAZARD_RECOVERY_COUNTERS),
                ("external-impulse fall witness", EXTERNAL_IMPULSE_FALL_WITNESS_COUNTERS),
                ("death attribution", DEATH_ATTRIBUTION_COUNTERS),
                ("damage attribution", DAMAGE_COUNTERS),
                ("falling seam shadow v1", FALLING_SEAM_SHADOW_COUNTERS),
                ("falling seam shadow detailed v2", FALLING_SEAM_DETAILED_COUNTERS),
                ("walking step preflight shadow", WALKING_STEP_PREFLIGHT_COUNTERS),
                ("walking HitWall dispatch", WALKING_HITWALL_DISPATCH_COUNTERS),
                ("move-stall recovery", MOVE_STALL_RECOVERY_EXACT_COUNTERS),
                ("persistent harmful fall", PERSISTENT_HARMFUL_FALL_COUNTERS),
                ("single harmful fall prefix", SINGLE_HARMFUL_FALL_PREFIX_COUNTERS),
                ("direct harmful-water entry", DIRECT_HARMFUL_WATER_ENTRY_COUNTERS),
                ("direct harmful-water entry certificate result",
                 DIRECT_HARMFUL_WATER_ENTRY_CERTIFICATE_RESULT_COUNTERS)):
            present = [name for name in names if name in result]
            if present and len(present) != len(names):
                raise QualityError(f"{context}: {label} counters must be provided as a complete group")
        confirmed_pickups_present = [
            name for name in CONFIRMED_PICKUP_COUNTERS if name in result]
        if confirmed_pickups_present and len(confirmed_pickups_present) != len(CONFIRMED_PICKUP_COUNTERS):
            raise QualityError(
                f"{context}: confirmed pickup counters must be provided as a complete group")
        if confirmed_pickups_present and result["confirmed_pickups_exact"] != sum(
                result[name] for name in CONFIRMED_PICKUP_COUNTERS[1:]):
            raise QualityError(
                f"{context}: confirmed pickup category counters do not partition confirmed pickups")
        navigation_coverage_present = [
            name for name in NAVIGATION_COVERAGE_COUNTERS if name in result]
        if navigation_coverage_present and len(navigation_coverage_present) != len(
                NAVIGATION_COVERAGE_COUNTERS):
            raise QualityError(
                f"{context}: navigation coverage counters must be provided as a complete group")
        if navigation_coverage_present and (
                result["navigation_coverage_visited_nodes_exact"] >
                result["navigation_coverage_catalog_nodes_exact"] or
                result["navigation_coverage_union_visited_nodes_exact"] >
                result["navigation_coverage_catalog_nodes_exact"]):
            raise QualityError(
                f"{context}: navigation coverage visited nodes exceed catalog nodes")
        if navigation_coverage_present and (
                result["navigation_coverage_visited_nodes_exact"] >
                result["navigation_coverage_union_visited_nodes_exact"]):
            raise QualityError(
                f"{context}: navigation coverage individual visited nodes exceed union visited nodes")
        falling_parity_present = {
            name for name in FALLING_PARITY_COUNTERS if name in result
        }
        valid_falling_parity_groups = (
            set(), set(FALLING_PARITY_LEGACY_COUNTERS), set(FALLING_PARITY_COUNTERS),
        )
        if falling_parity_present not in valid_falling_parity_groups:
            raise QualityError(
                f"{context}: falling parity shadow counters must be provided as the legacy "
                "or current complete group")
        if falling_parity_present == set(FALLING_PARITY_LEGACY_COUNTERS):
            result["falling_parity_realized_matched_landing_steps_exact"] = 0
        vertical_pain_present = {
            name for name in VERTICAL_PAIN_COLUMN_COUNTERS if name in result
        }
        valid_vertical_pain_groups = (
            set(), set(VERTICAL_PAIN_COLUMN_LEGACY_COUNTERS),
            set(VERTICAL_PAIN_COLUMN_COUNTERS),
        )
        if vertical_pain_present not in valid_vertical_pain_groups:
            raise QualityError(
                f"{context}: vertical pain column shadow counters must be provided as the "
                "legacy or current complete group")
        stall_field_names = set(
            MOVE_STALL_LEGACY_TELEMETRY_GROUP + MOVE_STALL_TELEMETRY_GROUP
            + MOVE_STALL_DIRECT_ACTOR_ATTRIBUTED_TELEMETRY_GROUP)
        stall_present = {name for name in stall_field_names if name in result}
        valid_stall_groups = (
            set(), set(MOVE_STALL_LEGACY_TELEMETRY_GROUP), set(MOVE_STALL_TELEMETRY_GROUP),
            set(MOVE_STALL_ATTRIBUTED_TELEMETRY_GROUP),
            set(MOVE_STALL_DIRECT_ACTOR_ATTRIBUTED_TELEMETRY_GROUP),
        )
        if stall_present not in valid_stall_groups:
            raise QualityError(
                f"{context}: move stall counters must be provided as the legacy or current complete group")
        if "move_stall_forced_replans_exact" in result:
            if result["move_stall_forced_replans_exact"] > result["move_stall_detections_exact"]:
                raise QualityError(f"{context}: move stall forced replans exceed detections")
        if "harmful_zone_escape_episodes_exact" in result:
            if result["harmful_zone_escape_successful_escapes_exact"] > \
                    result["harmful_zone_escape_episodes_exact"]:
                raise QualityError(f"{context}: harmful-zone escape successes exceed episodes")
            if result["harmful_zone_escape_forced_replans_exact"] > \
                    result["harmful_zone_escape_recovery_attempts_exact"]:
                raise QualityError(f"{context}: harmful-zone escape replans exceed recovery attempts")
        if "hazard_swim_egress_episodes_exact" in result:
            episodes = result["hazard_swim_egress_episodes_exact"]
            eligible = result["hazard_swim_egress_eligible_exact"]
            authorized = result["hazard_swim_egress_authorized_exact"]
            debounced = result["hazard_swim_egress_debounced_exact"]
            no_anchor = result["hazard_swim_egress_no_anchor_rejected_exact"]
            exited = result["hazard_swim_egress_exited_exact"]
            died_before_exit = result["hazard_swim_egress_died_before_exit_exact"]
            forced_replans = result["hazard_swim_egress_forced_replans_exact"]
            if eligible + no_anchor > episodes:
                raise QualityError(
                    f"{context}: hazard swim egress eligibility/no-anchor counts exceed episodes")
            if authorized + debounced > eligible:
                raise QualityError(
                    f"{context}: hazard swim egress authorized/debounced counts exceed eligibility")
            if exited + died_before_exit > episodes:
                raise QualityError(
                    f"{context}: hazard swim egress terminal outcomes exceed episodes")
            if died_before_exit > authorized:
                raise QualityError(
                    f"{context}: hazard swim egress deaths before exit exceed authorization")
            if forced_replans > authorized:
                raise QualityError(
                    f"{context}: hazard swim egress forced replans exceed authorization")
        if "hazard_swim_egress_forced_replan_same_command_reissued_exact" in result:
            if "hazard_swim_egress_forced_replans_exact" not in result:
                raise QualityError(
                    f"{context}: planner-handoff outcomes require forced-replan evidence")
            outcomes = sum(result[name] for name in
                HAZARD_SWIM_EGRESS_PLANNER_HANDOFF_OUTCOME_COUNTERS)
            if outcomes > result["hazard_swim_egress_forced_replans_exact"]:
                raise QualityError(
                    f"{context}: planner-handoff outcomes exceed forced replans")
        if "hazard_swim_egress_live_applies_exact" in result:
            live_applies = result["hazard_swim_egress_live_applies_exact"]
            live_active_ticks = result["hazard_swim_egress_live_active_ticks_exact"]
            live_probe_rejected = result["hazard_swim_egress_live_probe_rejected_exact"]
            live_successful_exits = result["hazard_swim_egress_live_successful_exits_exact"]
            # One authorized egress episode may first steer and later reject a
            # blocked re-probe. These are sequential observations, not a
            # disjoint partition of authorizations.
            if live_applies > result["hazard_swim_egress_authorized_exact"] \
                    or live_probe_rejected > result["hazard_swim_egress_authorized_exact"]:
                raise QualityError(
                    f"{context}: live swim egress applies or probe rejections exceed authorization")
            if live_active_ticks < live_applies:
                raise QualityError(
                    f"{context}: live swim egress active ticks are fewer than applies")
            if live_successful_exits > live_applies:
                raise QualityError(
                    f"{context}: live swim egress successful exits exceed applies")
        if "falling_hazard_recovery_promotions_exact" in result:
            promotions = result["falling_hazard_recovery_promotions_exact"]
            advance_calls = result["falling_hazard_recovery_advance_calls_exact"]
            context_rejected = result["falling_hazard_recovery_context_rejected_exact"]
            no_active_fall = result["falling_hazard_recovery_no_active_fall_episode_exact"]
            no_prefix = result["falling_hazard_recovery_no_prefix_exact"]
            eligible = result["falling_hazard_recovery_eligible_exact"]
            anchor_rejected = result["falling_hazard_recovery_anchor_rejected_exact"]
            probe_rejected = result["falling_hazard_recovery_probe_rejected_exact"]
            live_applies = result["falling_hazard_recovery_live_applies_exact"]
            active_ticks = result["falling_hazard_recovery_live_active_ticks_exact"]
            terminals = (
                result["falling_hazard_recovery_safe_landings_exact"]
                + result["falling_hazard_recovery_harmful_entries_exact"]
                + result["falling_hazard_recovery_deaths_exact"]
                + result["falling_hazard_recovery_timeouts_exact"])
            if eligible + anchor_rejected > promotions:
                raise QualityError(
                    f"{context}: falling-hazard recovery outcomes exceed promotions")
            if context_rejected > advance_calls or no_active_fall > context_rejected:
                raise QualityError(
                    f"{context}: falling-hazard recovery context diagnostics exceed advance calls")
            if no_prefix > advance_calls - context_rejected:
                raise QualityError(
                    f"{context}: falling-hazard recovery no-prefix diagnostics exceed valid contexts")
            if live_applies + probe_rejected > eligible:
                raise QualityError(
                    f"{context}: falling-hazard recovery applies/probe rejections exceed eligibility")
            if active_ticks < live_applies:
                raise QualityError(
                    f"{context}: falling-hazard recovery active ticks are fewer than applies")
            if terminals > live_applies:
                raise QualityError(
                    f"{context}: falling-hazard recovery terminal outcomes exceed applies")
        if "external_impulse_fall_harmful_witnesses_exact" in result:
            witnesses = result["external_impulse_fall_harmful_witnesses_exact"]
            no_air_control = result["external_impulse_fall_no_air_control_exact"]
            certified = result["external_impulse_fall_certified_exact"]
            uncertified = result["external_impulse_fall_uncertified_exact"]
            alternatives = result["external_impulse_fall_alternatives_tested_exact"]
            if no_air_control + certified + uncertified != witnesses:
                raise QualityError(
                    f"{context}: external-impulse fall witness decisions must partition witnesses")
            if alternatives < certified * 8 or alternatives < uncertified * 8:
                raise QualityError(
                    f"{context}: external-impulse fall alternatives are incomplete")
        if "hazard_swim_egress_direct_nav_probes_exact" in result:
            if result["hazard_swim_egress_direct_nav_safe_candidates_exact"] > \
                    result["hazard_swim_egress_direct_nav_probes_exact"]:
                raise QualityError(
                    f"{context}: direct safe navigation candidates exceed probes")
        if "hazard_residence_episodes_exact" in result:
            episodes = result["hazard_residence_episodes_exact"]
            terminals = (
                result["hazard_residence_cleared_exact"]
                + result["hazard_residence_deaths_exact"]
                + result["hazard_residence_life_boundary_censored_exact"]
                + result["hazard_residence_run_end_censored_exact"]
                + result["hazard_residence_unknown_exact"])
            if terminals > episodes:
                raise QualityError(
                    f"{context}: hazard residence terminal outcomes exceed episodes")
            if result["hazard_residence_candidates_observed_exact"] > episodes:
                raise QualityError(
                    f"{context}: hazard residence candidates exceed episodes")
            if result["hazard_residence_candidate_other_commands_exact"] > \
                    result["hazard_residence_candidates_observed_exact"]:
                raise QualityError(
                    f"{context}: hazard residence other commands exceed candidates")
            if result["hazard_residence_candidate_other_commands_exact"] > \
                    result["hazard_residence_command_changes_exact"]:
                raise QualityError(
                    f"{context}: hazard residence other commands exceed command changes")
        if "falling_pre_move_anchor_captures_exact" in result:
            if result["falling_pre_move_anchor_uses_exact"] > \
                    result["falling_pre_move_anchor_captures_exact"]:
                raise QualityError(
                    f"{context}: falling pre-move anchor uses exceed captures")
        if "direct_self_kills" in result:
            primary_attributions = sum(result[name] for name in DEATH_ATTRIBUTION_COUNTERS[:-1])
            if primary_attributions != result["deaths_exact"]:
                raise QualityError(
                    f"{context}: primary death attribution counters do not equal deaths_exact")
            if result["recent_enemy_momentum_contributed_environmental_deaths_proxy"] > \
                    result["recent_enemy_contributed_environmental_deaths_proxy"]:
                raise QualityError(
                    f"{context}: momentum-contributed deaths exceed enemy-contributed deaths")
        if "damage_taken_exact" in result:
            if result["damage_taken_exact"] != (
                    result["damage_taken_from_other_participants_exact"]
                    + result["damage_taken_from_self_exact"]
                    + result["damage_taken_from_nonparticipants_exact"]):
                raise QualityError(
                    f"{context}: damage source counters do not equal damage taken")
        if "falling_seam_detections_exact" in result:
            candidates = result["horizontal_corner_candidate_probes_exact"]
            detections = result["falling_seam_detections_exact"]
            if candidates > 3 * detections:
                raise QualityError(
                    f"{context}: horizontal corner candidates exceed three per falling seam detection")
            if result["horizontal_corner_authorized_escapes_exact"] > detections:
                raise QualityError(
                    f"{context}: horizontal corner authorized escapes exceed falling seam detections")
            for name, label in (
                    ("horizontal_corner_authorized_escapes_exact", "authorized escapes"),
                    ("horizontal_corner_target_progress_rejects_exact", "target-progress rejects"),
                    ("horizontal_corner_unknown_or_unsafe_support_exact",
                     "unknown-or-unsafe-support results")):
                if result[name] > candidates:
                    raise QualityError(
                        f"{context}: horizontal corner {label} exceed candidate probes")
            classified = (
                result["horizontal_corner_authorized_escapes_exact"]
                + result["horizontal_corner_target_progress_rejects_exact"]
                + result["horizontal_corner_unknown_or_unsafe_support_exact"])
            if classified != candidates:
                raise QualityError(
                    f"{context}: horizontal corner classifications do not partition candidate probes")
        if "falling_seam_episodes_exact" in result:
            if "falling_seam_detections_exact" not in result:
                raise QualityError(
                    f"{context}: falling seam detailed v2 counters require the complete v1 rollup group")
            detections = result["falling_seam_detections_exact"]
            episodes = result["falling_seam_episodes_exact"]
            invalid_geometry = result["falling_seam_invalid_geometry_rejects_exact"]
            authorizable_episodes = result["falling_seam_authorizable_episodes_exact"]
            probes = result["horizontal_corner_candidate_probes_exact"]
            authorized_candidates = result["horizontal_corner_authorized_candidates_exact"]
            if episodes > detections:
                raise QualityError(f"{context}: falling seam episodes exceed detections")
            if detections > 0 and episodes == 0:
                raise QualityError(
                    f"{context}: falling seam detections require at least one episode")
            if invalid_geometry > detections:
                raise QualityError(
                    f"{context}: falling seam invalid-geometry rejects exceed detections")
            candidate_sets = detections - invalid_geometry
            if probes < candidate_sets or probes > 3 * candidate_sets:
                raise QualityError(
                    f"{context}: horizontal corner probes are outside one-to-three per candidate-bearing detection")
            detailed_outcomes = sum(result[name] for name in (
                "horizontal_corner_authorized_candidates_exact",
                "horizontal_corner_blocked_sweep_candidates_exact",
                "horizontal_corner_no_static_walkable_support_candidates_exact",
                "horizontal_corner_pain_support_candidates_exact",
                "horizontal_corner_no_active_movement_intent_or_target_candidates_exact",
                "horizontal_corner_true_target_regression_candidates_exact",
                "horizontal_corner_unknown_evidence_candidates_exact",
            ))
            if detailed_outcomes != probes:
                raise QualityError(
                    f"{context}: detailed horizontal corner outcomes do not partition candidate probes")
            if authorized_candidates > candidate_sets:
                raise QualityError(
                    f"{context}: authorized horizontal corner candidates exceed candidate-bearing detections")
            if authorized_candidates != result["horizontal_corner_authorized_escapes_exact"]:
                raise QualityError(
                    f"{context}: detailed authorized candidates do not match v1 authorized escapes")
            if authorizable_episodes > episodes or authorizable_episodes > authorized_candidates:
                raise QualityError(
                    f"{context}: authorizable falling seam episodes exceed their episode or authorization bounds")
            if authorized_candidates > 0 and authorizable_episodes == 0:
                raise QualityError(
                    f"{context}: authorized horizontal corner candidates require an authorizable episode")
        if "walking_step_preflight_observations_exact" in result:
            observations = result["walking_step_preflight_observations_exact"]
            unsupported = result["walking_step_preflight_unsupported_endpoints_exact"]
            no_decisions = result["walking_step_preflight_no_decisions_exact"]
            provisional = result["walking_step_preflight_provisional_authorizations_exact"]
            authorizations = result[
                "walking_step_preflight_post_mayfall_confirmed_authorizations_exact"]
            episodes = result["walking_step_preflight_authorizable_episodes_exact"]
            if no_decisions + provisional != observations:
                raise QualityError(
                    f"{context}: walking step preflight decisions do not partition observations")
            if sum(result[name] for name in WALKING_STEP_PREFLIGHT_REASON_COUNTERS) != observations:
                raise QualityError(
                    f"{context}: walking step preflight reasons do not partition observations")
            harmful = result["walking_step_preflight_reason_harmful_pain_fall_exact"]
            if harmful != provisional:
                raise QualityError(
                    f"{context}: walking step harmful-pain reasons do not equal provisional authorizations")
            if authorizations > provisional:
                raise QualityError(
                    f"{context}: walking step confirmed authorizations exceed provisional authorizations")
            if episodes > authorizations:
                raise QualityError(
                    f"{context}: walking step authorizable episodes exceed authorizations")
            if authorizations > unsupported or unsupported > observations:
                raise QualityError(
                    f"{context}: walking step authorization/unsupported-endpoint bounds are invalid")
        if "walking_step_preflight_positive_dps_veto_eligible_exact" in result:
            eligible = result["walking_step_preflight_positive_dps_veto_eligible_exact"]
            applied = result["walking_step_preflight_positive_dps_veto_applied_exact"]
            debounced = result["walking_step_preflight_positive_dps_veto_debounced_exact"]
            forced_replans = result[
                "walking_step_preflight_positive_dps_veto_forced_replans_exact"]
            rollback_rejected = result[
                "walking_step_preflight_positive_dps_veto_rollback_rejected_exact"]
            if applied + debounced > eligible:
                raise QualityError(
                    f"{context}: positive-DPS veto applied/debounced counts exceed eligibility")
            if forced_replans != applied:
                raise QualityError(
                    f"{context}: positive-DPS veto forced replans do not equal applied vetoes")
            if rollback_rejected > eligible - debounced:
                raise QualityError(
                    f"{context}: positive-DPS veto rollback rejections exceed authorized attempts")
        if "falling_parity_realized_episodes_exact" in result:
            episodes = result["falling_parity_realized_episodes_exact"]
            realized_steps = result["falling_parity_realized_steps_exact"]
            if sum(result[name] for name in FALLING_PARITY_REALIZED_STEP_COUNTERS) \
                    != realized_steps:
                raise QualityError(
                    f"{context}: falling parity realized step outcomes do not partition realized steps")
            pain_entries = result["falling_parity_realized_pain_entries_exact"]
            deaths = result["falling_parity_realized_deaths_exact"]
            landings = result["falling_parity_realized_landings_exact"]
            continuity_losses = result[
                "falling_parity_realized_continuity_losses_exact"]
            if pain_entries > episodes:
                raise QualityError(
                    f"{context}: falling parity realized pain entries exceed episodes")
            if deaths + landings + continuity_losses > episodes:
                raise QualityError(
                    f"{context}: falling parity realized terminal outcomes exceed episodes")
        if "vertical_pain_column_episodes_started_exact" in result:
            episodes_started = result["vertical_pain_column_episodes_started_exact"]
            episodes_completed = result["vertical_pain_column_episodes_completed_exact"]
            if episodes_completed > episodes_started:
                raise QualityError(
                    f"{context}: vertical pain column completed episodes exceed started episodes")
            if sum(result[name] for name in VERTICAL_PAIN_COLUMN_OUTCOME_COUNTERS) != \
                    episodes_completed:
                raise QualityError(
                    f"{context}: vertical pain column outcomes do not partition completed episodes")
            if "vertical_pain_column_generation_capacity_exhaustions_exact" in result:
                if episodes_started - episodes_completed > 1:
                    raise QualityError(
                        f"{context}: current vertical pain column telemetry has more than one "
                        "active episode")
                if result["vertical_pain_column_generation_capacity_exhaustions_exact"] \
                        > episodes_started:
                    raise QualityError(
                        f"{context}: vertical pain column capacity exhaustions exceed starts")
        if "persistent_harmful_fall_candidates_started_exact" in result:
            candidates = result["persistent_harmful_fall_candidates_started_exact"]
            promotions = result["persistent_harmful_fall_promotions_exact"]
            resets = result["persistent_harmful_fall_resets_exact"]
            entries = result["persistent_harmful_fall_confirmed_harmful_entries_exact"]
            samples = result["persistent_harmful_fall_observed_lead_samples_exact"]
            if promotions > candidates or resets > candidates:
                raise QualityError(
                    f"{context}: persistent harmful fall promotions/resets exceed candidates")
            if entries > promotions or samples > entries:
                raise QualityError(
                    f"{context}: persistent harmful fall entries/samples exceed promotions")
        if "single_harmful_fall_prefix_candidates_started_exact" in result:
            candidates = result["single_harmful_fall_prefix_candidates_started_exact"]
            promotions = result["single_harmful_fall_prefix_promotions_exact"]
            resets = result["single_harmful_fall_prefix_resets_exact"]
            entries = result["single_harmful_fall_prefix_confirmed_harmful_entries_exact"]
            samples = result["single_harmful_fall_prefix_observed_lead_samples_exact"]
            if promotions > candidates or resets > candidates:
                raise QualityError(
                    f"{context}: single harmful fall prefix promotions/resets exceed candidates")
            if entries > promotions or samples > entries:
                raise QualityError(
                    f"{context}: single harmful fall prefix entries/samples exceed promotions")
        if "direct_harmful_water_entry_candidates_exact" in result:
            candidates = result["direct_harmful_water_entry_candidates_exact"]
            confirmed = result["direct_harmful_water_entry_confirmed_exact"]
            no_harm = result["direct_harmful_water_entry_confirmed_no_harm_exact"]
            unresolved = result["direct_harmful_water_entry_unresolved_exact"]
            samples = result["direct_harmful_water_entry_lead_samples_exact"]
            if confirmed + no_harm + unresolved > candidates:
                raise QualityError(
                    f"{context}: direct harmful-water entry outcomes exceed candidates")
            if samples > confirmed:
                raise QualityError(
                    f"{context}: direct harmful-water entry lead samples exceed confirmations")
            if ("direct_harmful_water_entry_certificate_certified_exact" in result
                    and result["direct_harmful_water_entry_certificate_certified_exact"]
                    != candidates):
                raise QualityError(
                    f"{context}: direct harmful-water certificate certifications do not equal candidates")
        if "walking_step_preflight_diagnostics" in bot:
            if "walking_step_preflight_observations_exact" not in result:
                raise QualityError(
                    f"{context}: walking step preflight diagnostics require the complete counter group")
            result["walking_step_preflight_diagnostics"] = \
                _walking_step_preflight_diagnostics(
                    bot.get("walking_step_preflight_diagnostics"),
                    f"{context}.walking_step_preflight_diagnostics")
        if "walking_hitwall_dispatch_diagnostics" in bot:
            if "walking_hitwall_dispatch_observations_exact" not in result:
                raise QualityError(
                    f"{context}: walking HitWall dispatch diagnostics require the complete counter group")
            result["walking_hitwall_dispatch_diagnostics"] = \
                _walking_hitwall_dispatch_diagnostics(
                    bot.get("walking_hitwall_dispatch_diagnostics"),
                    f"{context}.walking_hitwall_dispatch_diagnostics")
        elif "walking_hitwall_dispatch_observations_exact" in result:
            raise QualityError(
                f"{context}: walking HitWall dispatch counters require diagnostics")
        if "walking_step_preflight_positive_dps_veto_actions" in bot:
            if "walking_step_preflight_positive_dps_veto_action_overflows_exact" not in result:
                raise QualityError(
                    f"{context}: positive-DPS veto action records require their overflow counter")
            result["walking_step_preflight_positive_dps_veto_actions"] = \
                _walking_step_preflight_positive_dps_veto_actions(
                    bot.get("walking_step_preflight_positive_dps_veto_actions"),
                    f"{context}.walking_step_preflight_positive_dps_veto_actions")
        if "falling_parity_realized_records" in bot:
            if "falling_parity_realized_episodes_exact" not in result:
                raise QualityError(
                    f"{context}: falling parity realized records require the complete counter group")
            result["falling_parity_realized_records"] = \
                _falling_parity_realized_records(
                    bot.get("falling_parity_realized_records"),
                    f"{context}.falling_parity_realized_records")
        elif "falling_parity_realized_episodes_exact" in result:
            raise QualityError(
                f"{context}: falling parity counter group requires falling_parity_realized_records")
        if "vertical_pain_column_diagnostics" in bot:
            if "vertical_pain_column_generation_capacity_exhaustions_exact" not in result:
                raise QualityError(
                    f"{context}: vertical pain column diagnostics require the current counter group")
            result["vertical_pain_column_diagnostics"] = \
                _vertical_pain_column_diagnostics(
                    bot.get("vertical_pain_column_diagnostics"),
                    f"{context}.vertical_pain_column_diagnostics")
        elif "vertical_pain_column_generation_capacity_exhaustions_exact" in result:
            raise QualityError(
                f"{context}: current vertical pain column counters require "
                "vertical_pain_column_diagnostics")
        if "hazard_water_egress_diagnostics" in bot:
            if "hazard_swim_egress_episodes_exact" not in result \
                    or HAZARD_WATER_EGRESS_DIAGNOSTIC_OVERFLOW_COUNTER not in result:
                raise QualityError(
                    f"{context}: hazard-water egress diagnostics require episode and overflow counters")
            result["hazard_water_egress_diagnostics"] = \
                _hazard_water_egress_diagnostics(
                    bot.get("hazard_water_egress_diagnostics"),
                    f"{context}.hazard_water_egress_diagnostics")
        elif HAZARD_WATER_EGRESS_DIAGNOSTIC_OVERFLOW_COUNTER in result:
            raise QualityError(
                f"{context}: hazard-water egress overflow counter requires diagnostics")
        if "hazard_death_partition_records" in bot:
            if "direct_self_kills" not in result:
                raise QualityError(
                    f"{context}: hazard death partition records require the complete "
                    "death-attribution counter group")
            result["hazard_death_partition_records"] = \
                _hazard_death_partition_records(
                    bot.get("hazard_death_partition_records"),
                    f"{context}.hazard_death_partition_records")
        if "move_stall_recovery_episodes" in bot:
            if "move_stall_recovery_episodes_exact" not in result:
                raise QualityError(
                    f"{context}: move-stall recovery records require the complete counter group")
            result["move_stall_recovery_episodes"] = _move_stall_recovery_episodes(
                bot.get("move_stall_recovery_episodes"),
                f"{context}.move_stall_recovery_episodes")
        elif "move_stall_recovery_episodes_exact" in result:
            raise QualityError(
                f"{context}: move-stall recovery counter group requires records")
        if "move_stall_recovery_decisions" in bot:
            if MOVE_STALL_DECISION_RECORD_OVERFLOW_COUNTER not in result:
                raise QualityError(
                    f"{context}: move-stall decision records require an overflow counter")
            result["move_stall_recovery_decisions"] = _move_stall_recovery_decisions(
                bot.get("move_stall_recovery_decisions"),
                f"{context}.move_stall_recovery_decisions")
        elif MOVE_STALL_DECISION_RECORD_OVERFLOW_COUNTER in result:
            raise QualityError(
                f"{context}: move-stall decision overflow counter requires records")
        target_selection_present = [name for name in TARGET_SELECTION_COUNTERS if name in result]
        if target_selection_present and len(target_selection_present) != len(TARGET_SELECTION_COUNTERS):
            raise QualityError(f"{context}: target-selection counters must be provided as a complete group")
        if "target_selection_records" in bot:
            if len(target_selection_present) != len(TARGET_SELECTION_COUNTERS):
                raise QualityError(f"{context}: target-selection records require the complete counter group")
            records = bot.get("target_selection_records")
            if not isinstance(records, list):
                raise QualityError(f"{context}.target_selection_records must be an array")
            parsed_records = []
            for index, record in enumerate(records):
                record_context = f"{context}.target_selection_records[{index}]"
                item = _object(record, record_context)
                outcome = _string(item, "outcome", record_context, nonempty=True)
                if outcome not in ("accepted_target_change", "accepted_same_target",
                                   "rejected_or_unchanged"):
                    raise QualityError(f"{record_context}.outcome is not recognized")
                parsed_records.append({
                    "sequence": _integer(item.get("sequence"), f"{record_context}.sequence", minimum=1),
                    "contract_id": _string(item, "contract_id", record_context, nonempty=True),
                    "bot_id": _string(item, "bot_id", record_context, nonempty=True),
                    "previous_target_id": _string(item, "previous_target_id", record_context),
                    "requested_target_id": _string(item, "requested_target_id", record_context, nonempty=True),
                    "observed_target_id": _string(item, "observed_target_id", record_context),
                    "outcome": outcome,
                })
            result["target_selection_records"] = parsed_records
        elif target_selection_present:
            raise QualityError(f"{context}: target-selection counter group requires records")
        pick_target_present = [name for name in PICK_TARGET_COUNTERS if name in result]
        if pick_target_present and len(pick_target_present) != len(PICK_TARGET_COUNTERS):
            raise QualityError(f"{context}: PickTarget counters must be provided as a complete group")
        if "pick_target_records" in bot:
            if len(pick_target_present) != len(PICK_TARGET_COUNTERS):
                raise QualityError(f"{context}: PickTarget records require the complete counter group")
            records = bot.get("pick_target_records")
            if not isinstance(records, list):
                raise QualityError(f"{context}.pick_target_records must be an array")
            parsed_records = []
            for index, record in enumerate(records):
                record_context = f"{context}.pick_target_records[{index}]"
                item = _object(record, record_context)
                provenance_fields = (
                    "observer_tick", "caller_invocation_token", "source_life_id",
                    "selected_life_id", "source_actor_index", "selected_actor_index",
                )
                provenance_present = [name in item for name in provenance_fields]
                if any(provenance_present) and not all(provenance_present):
                    raise QualityError(f"{record_context}: PickTarget provenance must be complete")
                parsed = {
                    "sequence": _integer(item.get("sequence"),
                                         f"{record_context}.sequence", minimum=1),
                    "candidate_pawns": _strict_integer(item.get("candidate_pawns"),
                                                        f"{record_context}.candidate_pawns", minimum=0),
                    "self_rejects": _strict_integer(item.get("self_rejects"),
                                                     f"{record_context}.self_rejects", minimum=0),
                    "dead_rejects": _strict_integer(item.get("dead_rejects"),
                                                     f"{record_context}.dead_rejects", minimum=0),
                    "living_candidates": _strict_integer(item.get("living_candidates"),
                                                          f"{record_context}.living_candidates", minimum=0),
                    "living_skipped_by_current_predicate": _strict_integer(
                        item.get("living_skipped_by_current_predicate"),
                        f"{record_context}.living_skipped_by_current_predicate", minimum=0),
                    "team_rejects": _strict_integer(item.get("team_rejects"),
                                                     f"{record_context}.team_rejects", minimum=0),
                    "living_geometry_eligible": _strict_integer(
                        item.get("living_geometry_eligible"),
                        f"{record_context}.living_geometry_eligible", minimum=0),
                    "living_line_of_sight_eligible": _strict_integer(
                        item.get("living_line_of_sight_eligible"),
                        f"{record_context}.living_line_of_sight_eligible", minimum=0),
                    "returned_target": _boolean(item.get("returned_target"),
                                                f"{record_context}.returned_target"),
                    "returned_living_target": _boolean(item.get("returned_living_target"),
                                                       f"{record_context}.returned_living_target"),
                    "no_result_with_living_line_of_sight_candidate": _boolean(
                        item.get("no_result_with_living_line_of_sight_candidate"),
                        f"{record_context}.no_result_with_living_line_of_sight_candidate"),
                    "integrity_valid": _boolean(item.get("integrity_valid"),
                                                f"{record_context}.integrity_valid"),
                    "caller_class": _string(item, "caller_class", record_context),
                    "caller_function": _string(item, "caller_function", record_context),
                    "selected_actor": _string(item, "selected_actor", record_context),
                    "selected_class": _string(item, "selected_class", record_context),
                    "observer_tick": (_integer(item.get("observer_tick"),
                                                 f"{record_context}.observer_tick", minimum=0)
                                      if all(provenance_present) else None),
                    "caller_invocation_token": (_integer(item.get("caller_invocation_token"),
                                                           f"{record_context}.caller_invocation_token", minimum=0)
                                                if all(provenance_present) else None),
                    "source_life_id": (_integer(item.get("source_life_id"),
                                                  f"{record_context}.source_life_id", minimum=0)
                                       if all(provenance_present) else None),
                    "selected_life_id": (_integer(item.get("selected_life_id"),
                                                    f"{record_context}.selected_life_id", minimum=0)
                                         if all(provenance_present) else None),
                    "source_actor_index": (_strict_integer(item.get("source_actor_index"),
                                                             f"{record_context}.source_actor_index", minimum=-1)
                                           if all(provenance_present) else None),
                    "selected_actor_index": (_strict_integer(item.get("selected_actor_index"),
                                                               f"{record_context}.selected_actor_index", minimum=-1)
                                             if all(provenance_present) else None),
                }
                if parsed["self_rejects"] + parsed["dead_rejects"] + \
                        parsed["living_candidates"] != parsed["candidate_pawns"]:
                    raise QualityError(f"{record_context}: candidate partition does not reconcile")
                if pick_target_predicate_mode is None:
                    raise QualityError(
                        f"{record_context}: PickTarget records require declared predicate mode provenance")
                if parsed["team_rejects"] > parsed["living_candidates"] \
                        or parsed["living_geometry_eligible"] > \
                        parsed["living_candidates"] - parsed["team_rejects"] \
                        or parsed["living_line_of_sight_eligible"] > parsed["living_geometry_eligible"]:
                    raise QualityError(f"{record_context}: living-candidate eligibility is inconsistent")
                if parsed["returned_living_target"] and not parsed["returned_target"]:
                    raise QualityError(f"{record_context}: a living result requires a result")
                if pick_target_predicate_mode == "fixed":
                    if parsed["living_skipped_by_current_predicate"] != 0:
                        raise QualityError(
                            f"{record_context}: fixed PickTarget predicate must not skip living pawns")
                    if parsed["returned_target"] and not parsed["returned_living_target"]:
                        raise QualityError(
                            f"{record_context}: fixed PickTarget predicate must return a living pawn")
                    if parsed["no_result_with_living_line_of_sight_candidate"]:
                        raise QualityError(
                            f"{record_context}: fixed PickTarget predicate missed a living LOS candidate")
                elif pick_target_predicate_mode == "stock":
                    if parsed["living_skipped_by_current_predicate"] != parsed["living_candidates"]:
                        raise QualityError(
                            f"{record_context}: stock PickTarget predicate must skip every living pawn")
                    if parsed["returned_living_target"]:
                        raise QualityError(
                            f"{record_context}: stock PickTarget predicate must not return a living pawn")
                else:
                    raise QualityError(
                        f"{record_context}: PickTarget predicate mode is not recognized")
                if not parsed["returned_target"] and (parsed["selected_actor"] or parsed["selected_class"]):
                    raise QualityError(f"{record_context}: absent result must not identify a selection")
                if parsed["returned_target"] and (not parsed["selected_actor"] or not parsed["selected_class"]):
                    raise QualityError(f"{record_context}: result requires actor and class")
                parsed_records.append(parsed)
            result["pick_target_records"] = parsed_records
        elif pick_target_present:
            raise QualityError(f"{context}: PickTarget counter group requires records")
        pawn_can_see_present = [name for name in PAWN_CAN_SEE_COUNTERS if name in result]
        if pawn_can_see_present and len(pawn_can_see_present) != len(PAWN_CAN_SEE_COUNTERS):
            raise QualityError(f"{context}: Pawn.CanSee counters must be provided as a complete group")
        if "pawn_can_see_records" in bot:
            if len(pawn_can_see_present) != len(PAWN_CAN_SEE_COUNTERS):
                raise QualityError(f"{context}: Pawn.CanSee records require the complete counter group")
            records = bot.get("pawn_can_see_records")
            if not isinstance(records, list):
                raise QualityError(f"{context}.pawn_can_see_records must be an array")
            parsed_records = []
            for index, record in enumerate(records):
                record_context = f"{context}.pawn_can_see_records[{index}]"
                item = _object(record, record_context)
                parsed = {
                    "sequence": _integer(item.get("sequence"),
                                         f"{record_context}.sequence", minimum=1),
                    "observer_tick": _integer(item.get("observer_tick"),
                                              f"{record_context}.observer_tick", minimum=0),
                    "caller_invocation_token": _integer(item.get("caller_invocation_token"),
                                                         f"{record_context}.caller_invocation_token", minimum=0),
                    "source_life_id": _integer(item.get("source_life_id"),
                                               f"{record_context}.source_life_id", minimum=0),
                    "target_life_id": _integer(item.get("target_life_id"),
                                               f"{record_context}.target_life_id", minimum=0),
                    "source_actor_index": _strict_integer(item.get("source_actor_index"),
                                                          f"{record_context}.source_actor_index", minimum=0),
                    "target_actor_index": _strict_integer(item.get("target_actor_index"),
                                                          f"{record_context}.target_actor_index", minimum=0),
                    "peripheral_vision": _number(item.get("peripheral_vision"),
                                                   f"{record_context}.peripheral_vision"),
                    "sight_radius_accepted": _boolean(item.get("sight_radius_accepted"),
                                                       f"{record_context}.sight_radius_accepted"),
                    "legacy_cone_accepted": _boolean(item.get("legacy_cone_accepted"),
                                                       f"{record_context}.legacy_cone_accepted"),
                    "corrected_cone_accepted": _boolean(item.get("corrected_cone_accepted"),
                                                          f"{record_context}.corrected_cone_accepted"),
                    "corrected_cone_selected": _boolean(item.get("corrected_cone_selected"),
                                                          f"{record_context}.corrected_cone_selected"),
                    "returned_visible": _boolean(item.get("returned_visible"),
                                                f"{record_context}.returned_visible"),
                    "integrity_valid": _boolean(item.get("integrity_valid"),
                                                f"{record_context}.integrity_valid"),
                    "caller_class": _string(item, "caller_class", record_context),
                    "caller_function": _string(item, "caller_function", record_context),
                    "target_actor": _string(item, "target_actor", record_context, nonempty=True),
                    "target_class": _string(item, "target_class", record_context, nonempty=True),
                }
                if parsed["returned_visible"] and not parsed["sight_radius_accepted"]:
                    raise QualityError(f"{record_context}: visible result exceeds sight radius")
                if parsed["returned_visible"] and not (
                        parsed["corrected_cone_accepted"] if parsed["corrected_cone_selected"]
                        else parsed["legacy_cone_accepted"]):
                    raise QualityError(f"{record_context}: visible result conflicts with selected cone")
                parsed_records.append(parsed)
            result["pawn_can_see_records"] = parsed_records
        elif pawn_can_see_present:
            raise QualityError(f"{context}: Pawn.CanSee counter group requires records")
        finite_move_guard_present = [
            name for name in FINITE_MOVE_COMMAND_GUARD_COUNTERS if name in result]
        if finite_move_guard_present and len(finite_move_guard_present) != len(
                FINITE_MOVE_COMMAND_GUARD_COUNTERS):
            raise QualityError(
                f"{context}: finite MoveTo command guard counters must be provided as a complete group")
        if "finite_move_command_guard_diagnostics" in bot:
            if len(finite_move_guard_present) != len(FINITE_MOVE_COMMAND_GUARD_COUNTERS):
                raise QualityError(
                    f"{context}: finite MoveTo command guard diagnostics require the complete counter group")
            records = bot.get("finite_move_command_guard_diagnostics")
            if not isinstance(records, list):
                raise QualityError(
                    f"{context}.finite_move_command_guard_diagnostics must be an array")
            parsed_records = []
            for index, record in enumerate(records):
                record_context = f"{context}.finite_move_command_guard_diagnostics[{index}]"
                item = _object(record, record_context)
                classes = {
                    axis: _string(item, f"requested_{axis}_class", record_context, nonempty=True)
                    for axis in "xyz"
                }
                if any(value not in ("finite", "nan", "negative_infinity", "positive_infinity")
                       for value in classes.values()):
                    raise QualityError(f"{record_context}: requested component classification is not recognized")
                if all(value == "finite" for value in classes.values()):
                    raise QualityError(f"{record_context}: guard diagnostic has no invalid component")
                parsed_records.append({
                    "sequence": _integer(item.get("sequence"), f"{record_context}.sequence", minimum=1),
                    "observer_tick": _integer(item.get("observer_tick"),
                                              f"{record_context}.observer_tick", minimum=0),
                    "life_id": _integer(item.get("life_id"), f"{record_context}.life_id", minimum=0),
                    "actor_index": _strict_integer(item.get("actor_index"),
                                                   f"{record_context}.actor_index", minimum=0),
                    "requested_x_class": classes["x"],
                    "requested_y_class": classes["y"],
                    "requested_z_class": classes["z"],
                    "prior_destination_finite": _boolean(item.get("prior_destination_finite"),
                                                          f"{record_context}.prior_destination_finite"),
                    "prior_focus_finite": _boolean(item.get("prior_focus_finite"),
                                                    f"{record_context}.prior_focus_finite"),
                })
            result["finite_move_command_guard_diagnostics"] = parsed_records
        elif finite_move_guard_present:
            raise QualityError(
                f"{context}: finite MoveTo command guard counter group requires diagnostics")
        warn_target_present = [name for name in WARN_TARGET_COUNTERS if name in result]
        if warn_target_present and len(warn_target_present) != len(WARN_TARGET_COUNTERS):
            raise QualityError(f"{context}: WarnTarget counters must be provided as a complete group")
        if "warn_target_records" in bot:
            if len(warn_target_present) != len(WARN_TARGET_COUNTERS):
                raise QualityError(f"{context}: WarnTarget records require the complete counter group")
            records = bot.get("warn_target_records")
            if not isinstance(records, list):
                raise QualityError(f"{context}.warn_target_records must be an array")
            parsed_records = []
            for index, record in enumerate(records):
                record_context = f"{context}.warn_target_records[{index}]"
                item = _object(record, record_context)
                provenance_fields = (
                    "observer_tick", "caller_invocation_token", "receiver_life_id",
                    "receiver_actor_index", "shooter_actor_index",
                )
                provenance_present = [name in item for name in provenance_fields]
                if any(provenance_present) and not all(provenance_present):
                    raise QualityError(f"{record_context}: WarnTarget provenance must be complete")
                event = _string(item, "event", record_context, nonempty=True)
                if event not in ("warn_target", "try_to_duck"):
                    raise QualityError(f"{record_context}.event is not recognized")
                nested = _integer(item.get("nested_warn_target_sequence"),
                                  f"{record_context}.nested_warn_target_sequence", minimum=0)
                nested_exact = _boolean(item.get("nested_warn_target_exact"),
                                        f"{record_context}.nested_warn_target_exact")
                if nested_exact != (nested != 0):
                    raise QualityError(f"{record_context}: nested WarnTarget link is inconsistent")
                if event == "warn_target" and (nested != 0 or nested_exact):
                    raise QualityError(f"{record_context}: WarnTarget must not link to itself")
                parsed_records.append({
                    "sequence": _integer(item.get("sequence"), f"{record_context}.sequence", minimum=1),
                    "nested_warn_target_sequence": nested,
                    "event": event,
                    "contract_id": _string(item, "contract_id", record_context, nonempty=True),
                    "receiver_id": _string(item, "receiver_id", record_context, nonempty=True),
                    "receiver_state": _string(item, "receiver_state", record_context),
                    "shooter_id": _string(item, "shooter_id", record_context),
                    "nested_warn_target_exact": nested_exact,
                    "integrity_valid": _boolean(item.get("integrity_valid"),
                                                f"{record_context}.integrity_valid"),
                    "observer_tick": (_integer(item.get("observer_tick"),
                                                 f"{record_context}.observer_tick", minimum=0)
                                      if all(provenance_present) else None),
                    "caller_invocation_token": (_integer(item.get("caller_invocation_token"),
                                                           f"{record_context}.caller_invocation_token", minimum=0)
                                                if all(provenance_present) else None),
                    "receiver_life_id": (_integer(item.get("receiver_life_id"),
                                                    f"{record_context}.receiver_life_id", minimum=0)
                                         if all(provenance_present) else None),
                    "receiver_actor_index": (_strict_integer(item.get("receiver_actor_index"),
                                                               f"{record_context}.receiver_actor_index", minimum=-1)
                                             if all(provenance_present) else None),
                    "shooter_actor_index": (_strict_integer(item.get("shooter_actor_index"),
                                                              f"{record_context}.shooter_actor_index", minimum=-1)
                                            if all(provenance_present) else None),
                })
            result["warn_target_records"] = parsed_records
            if "try_to_duck_outcome_records" in bot:
                records = bot.get("try_to_duck_outcome_records")
                if not isinstance(records, list):
                    raise QualityError(f"{context}.try_to_duck_outcome_records must be an array")
                if "warn_target_observation_overflows_exact" not in result:
                    raise QualityError(f"{context}: TryToDuck outcome records require WarnTarget counters")
                parsed_outcomes = []
                for index, record in enumerate(records):
                    record_context = f"{context}.try_to_duck_outcome_records[{index}]"
                    item = _object(record, record_context)
                    requested = _object(item.get("requested_duck_dir"), f"{record_context}.requested_duck_dir")
                    velocity = _object(item.get("post_velocity"), f"{record_context}.post_velocity")
                    parsed_outcomes.append({
                        "sequence": _integer(item.get("sequence"), f"{record_context}.sequence", minimum=1),
                        "nested_warn_target_sequence": _integer(item.get("nested_warn_target_sequence"), f"{record_context}.nested_warn_target_sequence", minimum=1),
                        "observer_tick": _integer(item.get("observer_tick"), f"{record_context}.observer_tick", minimum=0),
                        "caller_invocation_token": _integer(item.get("caller_invocation_token"), f"{record_context}.caller_invocation_token", minimum=0),
                        "receiver_life_id": _integer(item.get("receiver_life_id"), f"{record_context}.receiver_life_id", minimum=0),
                        "receiver_actor_index": _strict_integer(item.get("receiver_actor_index"), f"{record_context}.receiver_actor_index", minimum=-1),
                        "requested_duck_dir": {axis: _number(requested.get(axis), f"{record_context}.requested_duck_dir.{axis}") for axis in "xyz"},
                        "requested_reversed": _boolean(item.get("requested_reversed"), f"{record_context}.requested_reversed"),
                        "post_velocity": {axis: _number(velocity.get(axis), f"{record_context}.post_velocity.{axis}") for axis in "xyz"},
                        "post_physics_mode": _string(item, "post_physics_mode", record_context, nonempty=True),
                        "post_state": _string(item, "post_state", record_context),
                        "post_latent_action": _string(item, "post_latent_action", record_context),
                        "integrity_valid": _boolean(item.get("integrity_valid"), f"{record_context}.integrity_valid"),
                    })
                result["try_to_duck_outcome_overflows_exact"] = _integer(
                    bot.get("try_to_duck_outcome_overflows_exact"),
                    f"{context}.try_to_duck_outcome_overflows_exact", minimum=0)
                result["try_to_duck_outcome_records"] = parsed_outcomes
            if "warning_dodge_launch_records" in bot:
                launch_counter_present = [name for name in WARNING_DODGE_LAUNCH_COUNTERS if name in result]
                if len(launch_counter_present) != len(WARNING_DODGE_LAUNCH_COUNTERS):
                    raise QualityError(f"{context}: warning-dodge launch counters must be provided as a complete group")
                records = bot.get("warning_dodge_launch_records")
                if not isinstance(records, list):
                    raise QualityError(f"{context}.warning_dodge_launch_records must be an array")
                parsed_launches = []
                for index, record in enumerate(records):
                    record_context = f"{context}.warning_dodge_launch_records[{index}]"
                    item = _object(record, record_context)
                    location = _object(item.get("launch_location"), f"{record_context}.launch_location")
                    velocity = _object(item.get("launch_velocity"), f"{record_context}.launch_velocity")
                    acceleration = _object(item.get("launch_acceleration"), f"{record_context}.launch_acceleration")
                    parsed_launches.append({
                        "launch_token": _integer(item.get("launch_token"), f"{record_context}.launch_token", minimum=1),
                        "sequence": _integer(item.get("sequence"), f"{record_context}.sequence", minimum=1),
                        "nested_warn_target_sequence": _integer(item.get("nested_warn_target_sequence"), f"{record_context}.nested_warn_target_sequence", minimum=1),
                        "observer_tick": _integer(item.get("observer_tick"), f"{record_context}.observer_tick", minimum=0),
                        "caller_invocation_token": _integer(item.get("caller_invocation_token"), f"{record_context}.caller_invocation_token", minimum=0),
                        "receiver_life_id": _integer(item.get("receiver_life_id"), f"{record_context}.receiver_life_id", minimum=0),
                        "receiver_actor_index": _strict_integer(item.get("receiver_actor_index"), f"{record_context}.receiver_actor_index", minimum=-1),
                        "launch_location": {axis: _number(location.get(axis), f"{record_context}.launch_location.{axis}") for axis in "xyz"},
                        "launch_velocity": {axis: _number(velocity.get(axis), f"{record_context}.launch_velocity.{axis}") for axis in "xyz"},
                        "launch_acceleration": {axis: _number(acceleration.get(axis), f"{record_context}.launch_acceleration.{axis}") for axis in "xyz"},
                        "move_target_name": _string(item, "move_target_name", record_context),
                        "route_head_name": _string(item, "route_head_name", record_context),
                        "post_state": _string(item, "post_state", record_context),
                        "post_latent_action": _string(item, "post_latent_action", record_context),
                        "integrity_valid": _boolean(item.get("integrity_valid"), f"{record_context}.integrity_valid"),
                    })
                result["warning_dodge_launches_exact"] = _integer(
                    bot.get("warning_dodge_launches_exact"),
                    f"{context}.warning_dodge_launches_exact", minimum=0)
                result["warning_dodge_launch_overflows_exact"] = _integer(
                    bot.get("warning_dodge_launch_overflows_exact"),
                    f"{context}.warning_dodge_launch_overflows_exact", minimum=0)
                result["warning_dodge_launch_records"] = parsed_launches
            if "warning_dodge_terminal_records" in bot:
                terminal_counter_present = [name for name in WARNING_DODGE_TERMINAL_COUNTERS
                                            if name in result]
                if len(terminal_counter_present) != len(WARNING_DODGE_TERMINAL_COUNTERS):
                    raise QualityError(f"{context}: warning-dodge terminal counters must be provided as a complete group")
                records = bot.get("warning_dodge_terminal_records")
                if not isinstance(records, list):
                    raise QualityError(f"{context}.warning_dodge_terminal_records must be an array")
                parsed_terminals = []
                for index, record in enumerate(records):
                    record_context = f"{context}.warning_dodge_terminal_records[{index}]"
                    item = _object(record, record_context)
                    outcome = _string(item, "outcome", record_context, nonempty=True)
                    reason = _string(item, "unknown_reason", record_context)
                    if outcome not in ("harmful_water_exit", "harmful_water_death", "unknown"):
                        raise QualityError(f"{record_context}.outcome is not recognized")
                    if outcome == "unknown":
                        if reason not in ("life_boundary", "physics_transition", "command_transition",
                                          "superseded_launch", "timeout", "run_end",
                                          "unproven_water_terminal"):
                            raise QualityError(f"{record_context}.unknown_reason is not recognized")
                    elif reason:
                        raise QualityError(f"{record_context}: known warning-dodge terminal must not have an unknown reason")
                    water_sequence = _integer(item.get("water_egress_sequence"),
                                              f"{record_context}.water_egress_sequence", minimum=0)
                    if (outcome != "unknown") != (water_sequence != 0):
                        raise QualityError(f"{record_context}: water terminal linkage is inconsistent")
                    parsed_terminals.append({
                        "launch_token": _integer(item.get("launch_token"), f"{record_context}.launch_token", minimum=1),
                        "launch_sequence": _integer(item.get("launch_sequence"), f"{record_context}.launch_sequence", minimum=1),
                        "receiver_life_id": _integer(item.get("receiver_life_id"), f"{record_context}.receiver_life_id", minimum=1),
                        "receiver_actor_index": _strict_integer(item.get("receiver_actor_index"), f"{record_context}.receiver_actor_index", minimum=0),
                        "terminal_tick": _integer(item.get("terminal_tick"), f"{record_context}.terminal_tick", minimum=0),
                        "water_egress_sequence": water_sequence,
                        "outcome": outcome,
                        "unknown_reason": reason,
                        "integrity_valid": _boolean(item.get("integrity_valid"), f"{record_context}.integrity_valid"),
                    })
                result["warning_dodge_terminal_outcomes_exact"] = _integer(
                    bot.get("warning_dodge_terminal_outcomes_exact"),
                    f"{context}.warning_dodge_terminal_outcomes_exact", minimum=0)
                result["warning_dodge_terminal_unknown_exact"] = _integer(
                    bot.get("warning_dodge_terminal_unknown_exact"),
                    f"{context}.warning_dodge_terminal_unknown_exact", minimum=0)
                result["warning_dodge_terminal_overflows_exact"] = _integer(
                    bot.get("warning_dodge_terminal_overflows_exact"),
                    f"{context}.warning_dodge_terminal_overflows_exact", minimum=0)
                result["warning_dodge_terminal_records"] = parsed_terminals
        elif warn_target_present:
            raise QualityError(f"{context}: WarnTarget counter group requires records")
        if "direct_reach_command_records" in bot:
            records = bot.get("direct_reach_command_records")
            if not isinstance(records, list):
                raise QualityError(f"{context}.direct_reach_command_records must be an array")
            parsed_records = []
            for index, record in enumerate(records):
                record_context = f"{context}.direct_reach_command_records[{index}]"
                item = _object(record, record_context)
                link_status = _string(item, "link_status", record_context, nonempty=True)
                activation_tick = item.get("activation_tick")
                terminal = item.get("terminal")
                hazard_terminal = item.get("hazard_terminal_exact")
                if link_status == "same_life_exact":
                    parsed_activation_tick = _integer(
                        activation_tick, f"{record_context}.activation_tick", minimum=0)
                    parsed_terminal = _string(item, "terminal", record_context, nonempty=True)
                    parsed_hazard_terminal = _boolean(
                        hazard_terminal, f"{record_context}.hazard_terminal_exact")
                else:
                    if activation_tick is not None or terminal is not None or hazard_terminal is not None:
                        raise QualityError(
                            f"{record_context}: unlinked command must not carry terminal provenance")
                    parsed_activation_tick = None
                    parsed_terminal = None
                    parsed_hazard_terminal = None
                parsed_records.append({
                    "sequence": _integer(item.get("sequence"),
                                         f"{record_context}.sequence", minimum=1),
                    "life_id": _integer(item.get("life_id"),
                                        f"{record_context}.life_id", minimum=0),
                    "target_actor_index": _strict_integer(item.get("target_actor_index"),
                                                            f"{record_context}.target_actor_index", minimum=-1),
                    "link_status": link_status,
                    "activation_tick": parsed_activation_tick,
                    "terminal": parsed_terminal,
                    "hazard_terminal_exact": parsed_hazard_terminal,
                })
            result["direct_reach_command_records"] = parsed_records
        inventory_direct_reach_present = [name for name in INVENTORY_DIRECT_REACH_SUPPORT_COUNTERS
                                          if name in result]
        if (inventory_direct_reach_present
                and len(inventory_direct_reach_present)
                != len(INVENTORY_DIRECT_REACH_SUPPORT_COUNTERS)):
            raise QualityError(
                f"{context}: inventory direct-reach support counters must be a complete group")
        if "inventory_direct_reach_support_diagnostics" in bot:
            if len(inventory_direct_reach_present) != len(INVENTORY_DIRECT_REACH_SUPPORT_COUNTERS):
                raise QualityError(
                    f"{context}: inventory direct-reach diagnostics require the complete counter group")
            records = bot.get("inventory_direct_reach_support_diagnostics")
            if not isinstance(records, list):
                raise QualityError(
                    f"{context}.inventory_direct_reach_support_diagnostics must be an array")
            parsed_records = []
            outcomes = {
                "safe_supported", "safe_unsupported_no_observed_hazard",
                "unsafe_harmful_foot_zone", "unsafe_unsupported_over_harmful", "unavailable",
            }
            for index, record in enumerate(records):
                record_context = f"{context}.inventory_direct_reach_support_diagnostics[{index}]"
                item = _object(record, record_context)
                outcome = _string(item, "outcome", record_context, nonempty=True)
                if outcome not in outcomes:
                    raise QualityError(f"{record_context}.outcome is not recognized")
                support_fraction = _number(item.get("support_fraction"),
                                           f"{record_context}.support_fraction", minimum=0.0)
                if support_fraction > 1.0:
                    raise QualityError(f"{record_context}.support_fraction must be at most one")
                parsed_records.append({
                    "source_pawn_actor": _string(item, "source_pawn_actor", record_context,
                                                   nonempty=True),
                    "target_actor": _string(item, "target_actor", record_context, nonempty=True),
                    "target_class": _string(item, "target_class", record_context, nonempty=True),
                    "sequence": _integer(item.get("sequence"), f"{record_context}.sequence", minimum=1),
                    "walking_simulation_iterations": _integer(
                        item.get("walking_simulation_iterations"),
                        f"{record_context}.walking_simulation_iterations", minimum=1),
                    "support_fraction": support_fraction,
                    "support_normal_z": _number(item.get("support_normal_z"),
                                                f"{record_context}.support_normal_z"),
                    "walkable_support": _boolean(item.get("walkable_support"),
                                                  f"{record_context}.walkable_support"),
                    "harmful_foot_zone": _boolean(item.get("harmful_foot_zone"),
                                                   f"{record_context}.harmful_foot_zone"),
                    "harmful_below": _boolean(item.get("harmful_below"),
                                                f"{record_context}.harmful_below"),
                    "outcome": outcome,
                })
            result["inventory_direct_reach_support_diagnostics"] = parsed_records
        elif inventory_direct_reach_present:
            raise QualityError(
                f"{context}: inventory direct-reach support counter group requires diagnostics")
        if "move_stall_navigation_forced_replans_exact" in result:
            attributed_replans = (
                result["move_stall_navigation_forced_replans_exact"]
                + result["move_stall_targetless_move_to_timeouts_exact"]
                + result.get("move_stall_direct_actor_move_toward_timeouts_exact", 0))
            if attributed_replans != result["move_stall_forced_replans_exact"]:
                raise QualityError(
                    f"{context}: attributed move stall recoveries do not equal forced replans")
            if result["move_stall_episode_resets_exact"] > result["move_stall_detections_exact"]:
                raise QualityError(f"{context}: move stall episode resets exceed detections")
        if "move_stall_recovery_episodes_exact" in result:
            terminals = sum(result[name] for name in MOVE_STALL_RECOVERY_OUTCOME_COUNTERS)
            if terminals > result["move_stall_recovery_episodes_exact"]:
                raise QualityError(
                    f"{context}: move-stall recovery terminal outcomes exceed episodes")
        if "physics_mode" in bot:
            result["physics_mode"] = _string(bot, "physics_mode", context)
            if result["physics_mode"] not in PHYSICS_MODES:
                raise QualityError(f"{context}.physics_mode is not recognized")
        if "latent_action" in bot:
            result["latent_action"] = _string(bot, "latent_action", context)
            if result["latent_action"] not in LATENT_ACTIONS:
                raise QualityError(f"{context}.latent_action is not recognized")
        for vector_name in ("acceleration", "destination"):
            if vector_name in bot:
                vector = _object(bot.get(vector_name), f"{context}.{vector_name}")
                result[vector_name] = {
                    axis: _number(vector.get(axis), f"{context}.{vector_name}.{axis}")
                    for axis in "xyz"
                }
        if "move_timer" in bot:
            result["move_timer"] = _number(bot.get("move_timer"), f"{context}.move_timer")
        for name in ("move_target_identity", "move_target_name"):
            if name in bot:
                result[name] = _string(bot, name, context)
        if ("move_target_identity" in result) != ("move_target_name" in result):
            raise QualityError(f"{context}: move target identity and name must be provided together")
        if "move_target_identity" in result:
            identity, name = result["move_target_identity"], result["move_target_name"]
            if bool(identity) != bool(name):
                raise QualityError(f"{context}: move target identity and name must both be empty or non-empty")
            if identity and not identity.startswith(("pri:", "actor:")):
                raise QualityError(f"{context}.move_target_identity must start with pri: or actor:")
    return result


def _load_events(path: Path, manifest: dict[str, Any]) -> list[dict[str, Any]]:
    if not path.is_file():
        raise QualityError(f"missing telemetry: {path}")
    events: list[dict[str, Any]] = []
    try:
        handle = path.open("r", encoding="utf-8-sig")
    except OSError as exc:
        raise QualityError(f"cannot read telemetry {path}: {exc}") from exc
    with handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                raise QualityError(f"{path}:{line_number}: blank telemetry line")
            try:
                raw = _object(json.loads(line), f"telemetry line {line_number}")
            except json.JSONDecodeError as exc:
                raise QualityError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
            context = f"{path}:{line_number}"
            schema = raw.get("schema")
            if schema not in (TELEMETRY_SCHEMA, TELEMETRY_SCHEMA_V2):
                raise QualityError(f"{context}: unsupported telemetry schema {raw.get('schema')!r}")
            seq = _integer(raw.get("seq"), f"{context}.seq", minimum=0)
            tick = _integer(raw.get("tick"), f"{context}.tick", minimum=0)
            seconds = _number(raw.get("simulated_seconds"), f"{context}.simulated_seconds", minimum=0.0)
            event_type = _string(raw, "type", context, nonempty=True)
            status = _string(raw, "status", context, nonempty=True)
            event = {
                "seq": seq,
                "tick": tick,
                "simulated_seconds": seconds,
                "type": event_type,
                "map": _string(raw, "map", context),
                "status": status,
                "failure_reason": _string(raw, "failure_reason", context),
                "schema": schema,
                "bots": [_validate_bot(item, f"{context}.bots[{index}]", schema,
                                       manifest.get("pick_target_predicate_mode"))
                         for index, item in enumerate(raw.get("bots", []))],
            }
            observer_requested = manifest.get("target_selection_observer_enabled") is True
            if observer_requested:
                observer = _object(raw.get("target_selection_observer"),
                                   f"{context}.target_selection_observer")
                if _boolean(observer.get("requested"),
                            f"{context}.target_selection_observer.requested") is not True:
                    raise QualityError(f"{context}: target-selection observer must be requested")
                observer_status = _string(observer, "status",
                                          f"{context}.target_selection_observer", nonempty=True)
                if observer_status not in ("active", "disabled_contract_mismatch",
                                           "disabled_integrity_failure"):
                    raise QualityError(f"{context}: target-selection observer status is not recognized")
                event["target_selection_observer"] = {
                    "status": observer_status,
                    "reason": _string(observer, "reason",
                                      f"{context}.target_selection_observer"),
                }
            elif "target_selection_observer" in raw:
                raise QualityError(f"{context}: target-selection observer telemetry is present while disabled")
            pick_target_observer_requested = manifest.get("pick_target_observer_enabled") is True
            if pick_target_observer_requested:
                observer = _object(raw.get("pick_target_observer"),
                                   f"{context}.pick_target_observer")
                if _boolean(observer.get("requested"),
                            f"{context}.pick_target_observer.requested") is not True:
                    raise QualityError(f"{context}: PickTarget observer must be requested")
                if _string(observer, "status", f"{context}.pick_target_observer", nonempty=True) \
                        != "active":
                    raise QualityError(f"{context}: PickTarget observer is not active")
                event["pick_target_observer"] = {"status": "active"}
            elif "pick_target_observer" in raw:
                raise QualityError(f"{context}: PickTarget observer telemetry is present while disabled")
            pawn_vision_observer_requested = manifest.get("pawn_vision_observer_enabled") is True
            if pawn_vision_observer_requested:
                observer = _object(raw.get("pawn_vision_observer"),
                                   f"{context}.pawn_vision_observer")
                if _boolean(observer.get("requested"),
                            f"{context}.pawn_vision_observer.requested") is not True:
                    raise QualityError(f"{context}: Pawn.CanSee observer must be requested")
                if _string(observer, "status", f"{context}.pawn_vision_observer", nonempty=True) \
                        != "active":
                    raise QualityError(f"{context}: Pawn.CanSee observer is not active")
                event["pawn_vision_observer"] = {"status": "active"}
            elif "pawn_vision_observer" in raw:
                raise QualityError(f"{context}: Pawn.CanSee observer telemetry is present while disabled")
            warn_target_observer_requested = manifest.get("warn_target_observer_enabled") is True
            if warn_target_observer_requested:
                observer = _object(raw.get("warn_target_observer"),
                                   f"{context}.warn_target_observer")
                if _boolean(observer.get("requested"),
                            f"{context}.warn_target_observer.requested") is not True:
                    raise QualityError(f"{context}: WarnTarget observer must be requested")
                observer_status = _string(observer, "status", f"{context}.warn_target_observer",
                                          nonempty=True)
                if observer_status not in ("active", "disabled_contract_mismatch",
                                           "disabled_integrity_failure"):
                    raise QualityError(f"{context}: WarnTarget observer status is not recognized")
                event["warn_target_observer"] = {
                    "status": observer_status,
                    "reason": _string(observer, "reason", f"{context}.warn_target_observer"),
                }
            elif "warn_target_observer" in raw:
                raise QualityError(f"{context}: WarnTarget observer telemetry is present while disabled")
            inventory_direct_reach_observer_requested = (
                manifest.get("inventory_direct_reach_support_observer_enabled") is True)
            if inventory_direct_reach_observer_requested:
                observer = _object(raw.get("inventory_direct_reach_support_observer"),
                                   f"{context}.inventory_direct_reach_support_observer")
                if _boolean(observer.get("requested"),
                            f"{context}.inventory_direct_reach_support_observer.requested") is not True:
                    raise QualityError(
                        f"{context}: inventory direct-reach support observer must be requested")
                if _string(observer, "status", f"{context}.inventory_direct_reach_support_observer",
                           nonempty=True) != "active":
                    raise QualityError(
                        f"{context}: inventory direct-reach support observer is not active")
                event["inventory_direct_reach_support_observer"] = {"status": "active"}
            elif "inventory_direct_reach_support_observer" in raw:
                raise QualityError(
                    f"{context}: inventory direct-reach support observer telemetry is present while disabled")
            if not isinstance(raw.get("bots"), list):
                raise QualityError(f"{context}.bots must be an array")
            if raw.get("config_id") != manifest["config_id"]:
                raise QualityError(f"{context}: config_id differs from manifest")
            order = [(bot["identity"], bot["actor"]) for bot in event["bots"]]
            if order != sorted(order):
                raise QualityError(f"{context}: bots are not ordered by identity and actor")
            identities = [bot["identity"] for bot in event["bots"]]
            if len(identities) != len(set(identities)):
                raise QualityError(f"{context}: duplicate bot identity")
            events.append(event)
    if not events:
        raise QualityError(f"empty telemetry: {path}")
    schemas = {event["schema"] for event in events}
    if len(schemas) != 1:
        raise QualityError(f"{path}: telemetry schema changed during the run")
    if len(events) > manifest["telemetry_event_cap"]:
        raise QualityError(f"{path}: telemetry exceeds the manifest event cap")
    for index, event in enumerate(events):
        if event["seq"] != index:
            raise QualityError(f"{path}: sequence {event['seq']} found where {index} was expected")
        if index and event["tick"] < events[index - 1]["tick"]:
            raise QualityError(f"{path}: tick regressed at sequence {index}")
        if index and event["simulated_seconds"] < events[index - 1]["simulated_seconds"]:
            raise QualityError(f"{path}: simulated time regressed at sequence {index}")
        if not _close(event["simulated_seconds"], event["tick"] * manifest["fixed_delta"]):
            raise QualityError(f"{path}: simulated time does not match tick * fixed_delta at sequence {index}")
    if manifest.get("target_selection_observer_enabled") is True:
        observer_values = {(event["target_selection_observer"]["status"],
                            event["target_selection_observer"]["reason"])
                           for event in events}
        if len(observer_values) != 1:
            raise QualityError(f"{path}: target-selection observer state changed during the run")
        observer_status, observer_reason = next(iter(observer_values))
        if observer_status == "active":
            if observer_reason:
                raise QualityError(f"{path}: active target-selection observer has a reason")
            totals: dict[str, dict[str, int]] = {}
            sequences: dict[str, int] = {}
            for event in events:
                for bot in event["bots"]:
                    if any(name not in bot for name in TARGET_SELECTION_COUNTERS):
                        raise QualityError(f"{path}: active target-selection observer lacks counters")
                    for record in bot["target_selection_records"]:
                        if record["bot_id"] != bot["identity"]:
                            raise QualityError(f"{path}: target-selection record bot identity differs from owner")
                        expected = sequences.get(bot["identity"], 0) + 1
                        if record["sequence"] != expected:
                            raise QualityError(f"{path}: target-selection record sequence is not contiguous")
                        sequences[bot["identity"]] = expected
                        bucket = totals.setdefault(bot["identity"], {
                            "accepted_target_change": 0, "accepted_same_target": 0,
                            "rejected_or_unchanged": 0})
                        bucket[record["outcome"]] += 1
            for bot in events[-1]["bots"]:
                counts = totals.get(bot["identity"], {
                    "accepted_target_change": 0, "accepted_same_target": 0,
                    "rejected_or_unchanged": 0})
                outcome_total = sum(counts.values())
                if outcome_total != (bot["target_selection_accepted_target_changes_exact"]
                                     + bot["target_selection_accepted_same_target_exact"]
                                     + bot["target_selection_rejected_or_unchanged_exact"]):
                    raise QualityError(f"{path}: target-selection records do not reconcile outcomes")
                if bot["target_selection_outermost_calls_exact"] != (
                        outcome_total + bot["target_selection_missing_results_exact"]
                        + bot["target_selection_record_overflows_exact"]):
                    raise QualityError(f"{path}: target-selection outermost calls do not reconcile")
                if (bot["target_selection_missing_results_exact"]
                        or bot["target_selection_record_overflows_exact"]
                        or bot["target_selection_integrity_failures_exact"]):
                    raise QualityError(f"{path}: active target-selection observer has incomplete evidence")
        elif not observer_reason:
            raise QualityError(f"{path}: disabled target-selection observer has no reason")
    if manifest.get("pick_target_observer_enabled") is True:
        totals: dict[str, dict[str, int]] = {}
        sequences: dict[str, int] = {}
        record_count: dict[str, int] = {}
        for event in events:
            for bot in event["bots"]:
                if any(name not in bot for name in PICK_TARGET_COUNTERS):
                    raise QualityError(f"{path}: active PickTarget observer lacks counters")
                for record in bot["pick_target_records"]:
                    prior = sequences.get(bot["identity"], 0)
                    if record["sequence"] <= prior:
                        raise QualityError(f"{path}: PickTarget record sequence did not increase")
                    sequences[bot["identity"]] = record["sequence"]
                    record_count[bot["identity"]] = record_count.get(bot["identity"], 0) + 1
                    bucket = totals.setdefault(bot["identity"], {
                        "pick_target_candidates_exact": 0,
                        "pick_target_self_rejects_exact": 0,
                        "pick_target_dead_rejects_exact": 0,
                        "pick_target_living_candidates_exact": 0,
                        "pick_target_living_skipped_by_current_predicate_exact": 0,
                        "pick_target_team_rejects_exact": 0,
                        "pick_target_living_geometry_eligible_exact": 0,
                        "pick_target_living_line_of_sight_eligible_exact": 0,
                        "pick_target_returned_targets_exact": 0,
                        "pick_target_returned_living_targets_exact": 0,
                        "pick_target_no_result_with_living_line_of_sight_candidate_exact": 0,
                        "pick_target_integrity_failures_exact": 0,
                    })
                    bucket["pick_target_candidates_exact"] += record["candidate_pawns"]
                    bucket["pick_target_self_rejects_exact"] += record["self_rejects"]
                    bucket["pick_target_dead_rejects_exact"] += record["dead_rejects"]
                    bucket["pick_target_living_candidates_exact"] += \
                        record["living_candidates"]
                    bucket["pick_target_living_skipped_by_current_predicate_exact"] += \
                        record["living_skipped_by_current_predicate"]
                    bucket["pick_target_team_rejects_exact"] += record["team_rejects"]
                    bucket["pick_target_living_geometry_eligible_exact"] += \
                        record["living_geometry_eligible"]
                    bucket["pick_target_living_line_of_sight_eligible_exact"] += \
                        record["living_line_of_sight_eligible"]
                    bucket["pick_target_returned_targets_exact"] += int(record["returned_target"])
                    bucket["pick_target_returned_living_targets_exact"] += \
                        int(record["returned_living_target"])
                    bucket["pick_target_no_result_with_living_line_of_sight_candidate_exact"] += \
                        int(record["no_result_with_living_line_of_sight_candidate"])
                    bucket["pick_target_integrity_failures_exact"] += int(not record["integrity_valid"])
        for bot in events[-1]["bots"]:
            counts = totals.get(bot["identity"], {
                name: 0 for name in PICK_TARGET_COUNTERS
                if name not in ("pick_target_observations_exact",
                                "pick_target_observation_overflows_exact")
            })
            records = record_count.get(bot["identity"], 0)
            overflow = bot["pick_target_observation_overflows_exact"]
            if bot["pick_target_observations_exact"] != records + overflow:
                raise QualityError(f"{path}: PickTarget observations do not reconcile records")
            if overflow or bot["pick_target_integrity_failures_exact"]:
                raise QualityError(f"{path}: active PickTarget observer has incomplete evidence")
            for counter, observed in counts.items():
                if bot[counter] != observed:
                    raise QualityError(f"{path}: PickTarget records do not reconcile {counter}")
            if records and sequences[bot["identity"]] != records:
                raise QualityError(f"{path}: PickTarget record sequence is not contiguous")
    if manifest.get("pawn_vision_observer_enabled") is True:
        totals: dict[str, dict[str, int]] = {}
        sequences: dict[str, int] = {}
        record_count: dict[str, int] = {}
        selected_cone = manifest.get("pawn_vision_cone_enabled")
        if selected_cone is None:
            raise QualityError(f"{path}: Pawn.CanSee observer requires pawn vision cone provenance")
        for event in events:
            for bot in event["bots"]:
                if any(name not in bot for name in PAWN_CAN_SEE_COUNTERS):
                    raise QualityError(f"{path}: active Pawn.CanSee observer lacks counters")
                for record in bot["pawn_can_see_records"]:
                    prior = sequences.get(bot["identity"], 0)
                    if record["sequence"] <= prior:
                        raise QualityError(f"{path}: Pawn.CanSee record sequence did not increase")
                    if record["corrected_cone_selected"] is not selected_cone:
                        raise QualityError(f"{path}: Pawn.CanSee record cone mode differs from manifest")
                    sequences[bot["identity"]] = record["sequence"]
                    record_count[bot["identity"]] = record_count.get(bot["identity"], 0) + 1
                    bucket = totals.setdefault(bot["identity"], {
                        "pawn_can_see_returned_visible_exact": 0,
                        "pawn_can_see_legacy_corrected_divergences_exact": 0,
                        "pawn_can_see_integrity_failures_exact": 0,
                    })
                    bucket["pawn_can_see_returned_visible_exact"] += int(record["returned_visible"])
                    bucket["pawn_can_see_legacy_corrected_divergences_exact"] += int(
                        record["legacy_cone_accepted"] != record["corrected_cone_accepted"])
                    bucket["pawn_can_see_integrity_failures_exact"] += int(not record["integrity_valid"])
        for bot in events[-1]["bots"]:
            counts = totals.get(bot["identity"], {
                "pawn_can_see_returned_visible_exact": 0,
                "pawn_can_see_legacy_corrected_divergences_exact": 0,
                "pawn_can_see_integrity_failures_exact": 0,
            })
            records = record_count.get(bot["identity"], 0)
            overflow = bot["pawn_can_see_observation_overflows_exact"]
            if bot["pawn_can_see_observations_exact"] != records + overflow:
                raise QualityError(f"{path}: Pawn.CanSee observations do not reconcile records")
            if overflow or bot["pawn_can_see_integrity_failures_exact"]:
                raise QualityError(f"{path}: active Pawn.CanSee observer has incomplete evidence")
            for counter, observed in counts.items():
                if bot[counter] != observed:
                    raise QualityError(f"{path}: Pawn.CanSee records do not reconcile {counter}")
            if records and sequences[bot["identity"]] != records:
                raise QualityError(f"{path}: Pawn.CanSee record sequence is not contiguous")
    finite_move_guard_enabled = manifest.get("finite_move_command_guard_enabled") is True
    for event in events:
        for bot in event["bots"]:
            fields_present = any(name in bot for name in FINITE_MOVE_COMMAND_GUARD_COUNTERS) \
                or "finite_move_command_guard_diagnostics" in bot
            if finite_move_guard_enabled:
                if any(name not in bot for name in FINITE_MOVE_COMMAND_GUARD_COUNTERS) \
                        or "finite_move_command_guard_diagnostics" not in bot:
                    raise QualityError(f"{path}: enabled finite MoveTo command guard lacks complete evidence")
            elif fields_present:
                raise QualityError(f"{path}: finite MoveTo command guard telemetry is present while disabled")
    if finite_move_guard_enabled:
        sequences: dict[str, int] = {}
        record_counts: dict[str, int] = {}
        for event in events:
            for bot in event["bots"]:
                for record in bot["finite_move_command_guard_diagnostics"]:
                    prior = sequences.get(bot["identity"], 0)
                    if record["sequence"] <= prior:
                        raise QualityError(
                            f"{path}: finite MoveTo command guard record sequence did not increase")
                    sequences[bot["identity"]] = record["sequence"]
                    record_counts[bot["identity"]] = record_counts.get(bot["identity"], 0) + 1
                    if not record["prior_destination_finite"] or not record["prior_focus_finite"]:
                        raise QualityError(
                            f"{path}: finite MoveTo command guard did not preserve finite state")
        for bot in events[-1]["bots"]:
            records = record_counts.get(bot["identity"], 0)
            overflow = bot["finite_move_command_guard_diagnostic_overflows_exact"]
            rejections = bot["finite_move_command_guard_rejections_exact"]
            if rejections != records + overflow:
                raise QualityError(f"{path}: finite MoveTo command guard diagnostics do not reconcile")
            if rejections or overflow:
                raise QualityError(f"{path}: finite MoveTo command guard rejected an invalid command")
    if manifest.get("warn_target_observer_enabled") is True:
        observer_values = {(event["warn_target_observer"]["status"],
                            event["warn_target_observer"]["reason"])
                           for event in events}
        if len(observer_values) != 1:
            raise QualityError(f"{path}: WarnTarget observer state changed during the run")
        observer_status, observer_reason = next(iter(observer_values))
        if observer_status != "active":
            if not observer_reason:
                raise QualityError(f"{path}: disabled WarnTarget observer has no reason")
        else:
            if observer_reason:
                raise QualityError(f"{path}: active WarnTarget observer has a reason")
            counts: dict[str, dict[str, int]] = {}
            sequences: dict[str, int] = {}
            warns: dict[str, set[int]] = {}
            tries: dict[str, dict[int, dict[str, Any]]] = {}
            outcome_counts: dict[str, int] = {}
            launch_counts: dict[str, int] = {}
            launches_by_token: dict[str, dict[int, dict[str, Any]]] = {}
            terminal_counts: dict[str, dict[str, int]] = {}
            terminal_tokens: dict[str, set[int]] = {}
            water_terminals: dict[str, set[tuple[int, int, str]]] = {}
            for event in events:
                for bot in event["bots"]:
                    if any(name not in bot for name in WARN_TARGET_COUNTERS):
                        raise QualityError(f"{path}: active WarnTarget observer lacks counters")
                    for record in bot["warn_target_records"]:
                        expected = sequences.get(bot["identity"], 0) + 1
                        if record["sequence"] != expected:
                            raise QualityError(f"{path}: WarnTarget record sequence is not contiguous")
                        if record["receiver_id"] != bot["identity"]:
                            raise QualityError(f"{path}: WarnTarget record receiver differs from owner")
                        sequences[bot["identity"]] = expected
                        bucket = counts.setdefault(bot["identity"], {"warn_target": 0, "try_to_duck": 0,
                                                                       "nested": 0, "invalid": 0})
                        bucket[record["event"]] += 1
                        if not record["integrity_valid"]:
                            bucket["invalid"] += 1
                        if record["event"] == "warn_target":
                            warns.setdefault(bot["identity"], set()).add(record["sequence"])
                        elif record["nested_warn_target_exact"]:
                            if record["nested_warn_target_sequence"] not in warns.get(bot["identity"], set()):
                                raise QualityError(f"{path}: TryToDuck link lacks its retained WarnTarget record")
                            bucket["nested"] += 1
                        if record["event"] == "try_to_duck":
                            tries.setdefault(bot["identity"], {})[record["sequence"]] = record
                    if "try_to_duck_outcome_records" not in bot:
                        raise QualityError(f"{path}: active WarnTarget observer lacks TryToDuck outcomes")
                    if bot.get("try_to_duck_outcome_overflows_exact", 0) != 0:
                        raise QualityError(f"{path}: TryToDuck outcome evidence overflowed")
                    for outcome in bot["try_to_duck_outcome_records"]:
                        linked = tries.get(bot["identity"], {}).get(outcome["sequence"])
                        if linked is None or not outcome["integrity_valid"]:
                            raise QualityError(f"{path}: TryToDuck outcome lacks a valid retained call")
                        if any(outcome[name] != linked[name] for name in (
                                "nested_warn_target_sequence", "observer_tick",
                                "caller_invocation_token", "receiver_life_id",
                                "receiver_actor_index")):
                            raise QualityError(f"{path}: TryToDuck outcome provenance differs from its call")
                        outcome_counts[bot["identity"]] = outcome_counts.get(bot["identity"], 0) + 1
                    if "warning_dodge_launch_records" in bot:
                        if bot.get("warning_dodge_launch_overflows_exact", 0) != 0:
                            raise QualityError(f"{path}: warning-dodge launch evidence overflowed")
                        for launch in bot["warning_dodge_launch_records"]:
                            linked = tries.get(bot["identity"], {}).get(launch["sequence"])
                            if linked is None or not launch["integrity_valid"]:
                                raise QualityError(f"{path}: warning-dodge launch lacks a valid retained TryToDuck call")
                            if any(launch[name] != linked[name] for name in (
                                    "nested_warn_target_sequence", "observer_tick",
                                    "caller_invocation_token", "receiver_life_id",
                                    "receiver_actor_index")):
                                raise QualityError(f"{path}: warning-dodge launch provenance differs from TryToDuck")
                            tokens = launches_by_token.setdefault(bot["identity"], {})
                            if launch["launch_token"] in tokens:
                                raise QualityError(f"{path}: warning-dodge launch token was reused")
                            tokens[launch["launch_token"]] = launch
                            launch_counts[bot["identity"]] = launch_counts.get(bot["identity"], 0) + 1
                    if "warning_dodge_terminal_records" not in bot:
                        raise QualityError(f"{path}: active WarnTarget observer lacks warning-dodge terminal records")
                    if bot.get("warning_dodge_terminal_overflows_exact", 0) != 0:
                        raise QualityError(f"{path}: warning-dodge terminal evidence overflowed")
                    for diagnostic in bot.get("hazard_water_egress_diagnostics", []):
                        if diagnostic["terminal"] in ("primary_zone_cleared", "death_before_exit") \
                                and diagnostic["damage_per_second"] > 0.0:
                            water_terminals.setdefault(bot["identity"], set()).add(
                                (diagnostic["sequence"], diagnostic["life_id"], diagnostic["terminal"]))
                    for terminal in bot["warning_dodge_terminal_records"]:
                        launch = launches_by_token.get(bot["identity"], {}).get(terminal["launch_token"])
                        if launch is None or not terminal["integrity_valid"]:
                            raise QualityError(f"{path}: warning-dodge terminal lacks a valid retained launch")
                        if any(terminal[name] != launch[mapped] for name, mapped in (
                                ("launch_sequence", "sequence"),
                                ("receiver_life_id", "receiver_life_id"),
                                ("receiver_actor_index", "receiver_actor_index"))):
                            raise QualityError(f"{path}: warning-dodge terminal provenance differs from launch")
                        if terminal["terminal_tick"] < launch["observer_tick"]:
                            raise QualityError(f"{path}: warning-dodge terminal predates its launch")
                        used = terminal_tokens.setdefault(bot["identity"], set())
                        if terminal["launch_token"] in used:
                            raise QualityError(f"{path}: warning-dodge launch has multiple terminals")
                        used.add(terminal["launch_token"])
                        if terminal["outcome"] == "harmful_water_exit":
                            expected_water = (terminal["water_egress_sequence"],
                                              terminal["receiver_life_id"], "primary_zone_cleared")
                            if expected_water not in water_terminals.get(bot["identity"], set()):
                                raise QualityError(f"{path}: warning-dodge water exit lacks a harmful-water witness")
                        elif terminal["outcome"] == "harmful_water_death":
                            expected_water = (terminal["water_egress_sequence"],
                                              terminal["receiver_life_id"], "death_before_exit")
                            if expected_water not in water_terminals.get(bot["identity"], set()):
                                raise QualityError(f"{path}: warning-dodge water death lacks a harmful-water witness")
                        counts_for_bot = terminal_counts.setdefault(bot["identity"], {
                            "known": 0, "unknown": 0})
                        counts_for_bot["unknown" if terminal["outcome"] == "unknown" else "known"] += 1
            for bot in events[-1]["bots"]:
                bucket = counts.get(bot["identity"], {"warn_target": 0, "try_to_duck": 0,
                                                        "nested": 0, "invalid": 0})
                records = sequences.get(bot["identity"], 0)
                if bot["warn_target_observations_exact"] != bucket["warn_target"] or \
                        bot["try_to_duck_observations_exact"] != bucket["try_to_duck"] or \
                        bot["warn_target_exact_nested_try_to_duck_links_exact"] != bucket["nested"]:
                    raise QualityError(f"{path}: WarnTarget records do not reconcile counters")
                if records + bot["warn_target_observation_overflows_exact"] != \
                        bot["warn_target_observations_exact"] + bot["try_to_duck_observations_exact"]:
                    raise QualityError(f"{path}: WarnTarget observations do not reconcile records")
                if bot["warn_target_observation_overflows_exact"] or \
                        bot["warn_target_integrity_failures_exact"] or bucket["invalid"]:
                    raise QualityError(f"{path}: active WarnTarget observer has incomplete evidence")
                if outcome_counts.get(bot["identity"], 0) != bucket["try_to_duck"]:
                    raise QualityError(f"{path}: TryToDuck calls do not reconcile outcome records")
                if "warning_dodge_launch_records" in bot and \
                        bot["warning_dodge_launches_exact"] != launch_counts.get(bot["identity"], 0):
                    raise QualityError(f"{path}: warning-dodge launch records do not reconcile counters")
                terminal_bucket = terminal_counts.get(bot["identity"], {"known": 0, "unknown": 0})
                if bot["warning_dodge_terminal_outcomes_exact"] != terminal_bucket["known"] \
                        or bot["warning_dodge_terminal_unknown_exact"] != terminal_bucket["unknown"]:
                    raise QualityError(f"{path}: warning-dodge terminal records do not reconcile counters")
                if (terminal_bucket["known"] + terminal_bucket["unknown"]
                        != launch_counts.get(bot["identity"], 0)):
                    raise QualityError(f"{path}: every warning-dodge launch requires exactly one terminal")
    if manifest.get("inventory_direct_reach_support_observer_enabled") is True:
        totals: dict[str, dict[str, int]] = {}
        sequences: dict[str, int] = {}
        for event in events:
            for bot in event["bots"]:
                if any(name not in bot for name in INVENTORY_DIRECT_REACH_SUPPORT_COUNTERS):
                    raise QualityError(
                        f"{path}: active inventory direct-reach observer lacks counters")
                for record in bot["inventory_direct_reach_support_diagnostics"]:
                    if record["source_pawn_actor"] != bot["actor"]:
                        raise QualityError(
                            f"{path}: inventory direct-reach record owner differs from bot actor")
                    expected = sequences.get(bot["identity"], 0) + 1
                    if record["sequence"] != expected:
                        raise QualityError(
                            f"{path}: inventory direct-reach record sequence is not contiguous")
                    sequences[bot["identity"]] = expected
                    bucket = totals.setdefault(bot["identity"], {
                        "safe_supported": 0,
                        "safe_unsupported_no_observed_hazard": 0,
                        "unsafe_harmful_foot_zone": 0,
                        "unsafe_unsupported_over_harmful": 0,
                        "unavailable": 0,
                    })
                    bucket[record["outcome"]] += 1
        for bot in events[-1]["bots"]:
            counts = totals.get(bot["identity"], {
                "safe_supported": 0,
                "safe_unsupported_no_observed_hazard": 0,
                "unsafe_harmful_foot_zone": 0,
                "unsafe_unsupported_over_harmful": 0,
                "unavailable": 0,
            })
            emitted = sum(counts.values())
            if bot["inventory_direct_reach_support_observations_exact"] != (
                    emitted + bot["inventory_direct_reach_support_diagnostic_overflows_exact"]):
                raise QualityError(
                    f"{path}: inventory direct-reach observations do not reconcile diagnostics")
            expected_counters = {
                "safe_supported": "inventory_direct_reach_support_safe_supported_exact",
                "safe_unsupported_no_observed_hazard": (
                    "inventory_direct_reach_support_safe_unsupported_no_observed_hazard_exact"),
                "unsafe_harmful_foot_zone": (
                    "inventory_direct_reach_support_unsafe_harmful_foot_zone_exact"),
                "unsafe_unsupported_over_harmful": (
                    "inventory_direct_reach_support_unsafe_unsupported_over_harmful_exact"),
                "unavailable": "inventory_direct_reach_support_unavailable_exact",
            }
            for outcome, counter in expected_counters.items():
                if counts[outcome] != bot[counter]:
                    raise QualityError(
                        f"{path}: inventory direct-reach diagnostics do not reconcile {outcome}")
            if bot["inventory_direct_reach_support_diagnostic_overflows_exact"]:
                raise QualityError(
                    f"{path}: active inventory direct-reach observer overflowed its diagnostics")
    if events[0]["schema"] == TELEMETRY_SCHEMA_V2:
        previous: dict[str, dict[str, Any]] = {}
        counters = CORE_EXACT_COUNTERS + OPTIONAL_CUMULATIVE_METRICS
        optional_fields = (
            OPTIONAL_CUMULATIVE_METRICS + OPTIONAL_STATIC_METRICS
            + OPTIONAL_DIAGNOSTIC_FIELDS)
        optional_presence: set[str] | None = None
        for event in events:
            participant_damage_dealt = 0
            participant_damage_taken = 0
            navigation_coverage_union_values: set[int] = set()
            for bot in event["bots"]:
                present = {name for name in optional_fields if name in bot}
                if optional_presence is None:
                    optional_presence = present
                elif present != optional_presence:
                    raise QualityError(
                        f"{path}: optional telemetry field availability changed at sequence {event['seq']}")
                prior = previous.get(bot["identity"])
                if prior:
                    for name in counters:
                        if name in bot and bot[name] < prior[name]:
                            raise QualityError(
                                f"{path}: {name} regressed for {bot['identity']} at sequence {event['seq']}")
                    if ("navigation_coverage_catalog_nodes_exact" in bot and
                            bot["navigation_coverage_catalog_nodes_exact"] !=
                            prior["navigation_coverage_catalog_nodes_exact"]):
                        raise QualityError(
                            f"{path}: navigation coverage catalog changed for {bot['identity']} "
                            f"at sequence {event['seq']}")
                if bot["environmental_deaths_exact"] > bot["suicides_exact"]:
                    raise QualityError(
                        f"{path}: environmental deaths exceed suicides for {bot['identity']}")
                if bot["suicides_exact"] > bot["deaths_exact"] or \
                        bot["hazard_exposed_deaths_proxy"] > bot["deaths_exact"]:
                    raise QualityError(f"{path}: death subcounter exceeds deaths_exact for {bot['identity']}")
                if "pain_ledge_repeat_vetoes_exact" in bot and \
                        bot["pain_ledge_repeat_vetoes_exact"] > bot["pain_ledge_vetoes_exact"]:
                    raise QualityError(f"{path}: pain ledge repeats exceed vetoes for {bot['identity']}")
                if "pain_ledge_recovery_attempts_exact" in bot and \
                        bot["pain_ledge_recovery_attempts_exact"] > bot["pain_ledge_vetoes_exact"]:
                    raise QualityError(f"{path}: pain ledge attempts exceed vetoes for {bot['identity']}")
                if "pain_ledge_recovery_escapes_exact" in bot and \
                        bot["pain_ledge_recovery_escapes_exact"] > bot["pain_ledge_recovery_attempts_exact"]:
                    raise QualityError(f"{path}: pain ledge escapes exceed attempts for {bot['identity']}")
                if "pain_ledge_recovery_active_hitwall_events_exact" in bot and \
                        bot["pain_ledge_recovery_active_hitwall_events_exact"] > bot["hit_wall_events_exact"]:
                    raise QualityError(
                        f"{path}: pain-ledge recovery contacts exceed HitWall events")
                if "wall_adjust_repeats_exact" in bot and \
                        bot["wall_adjust_repeats_exact"] > bot["wall_adjust_calls_exact"]:
                    raise QualityError(f"{path}: wall adjust repeats exceed calls for {bot['identity']}")
                if "wall_adjust_recovery_successes_exact" in bot and \
                        bot["wall_adjust_recovery_successes_exact"] > bot["wall_adjust_recovery_attempts_exact"]:
                    raise QualityError(f"{path}: wall adjust successes exceed attempts for {bot['identity']}")
                if "damage_taken_exact" in bot:
                    participant_damage_dealt += bot["damage_dealt_to_other_participants_exact"]
                    participant_damage_taken += bot["damage_taken_from_other_participants_exact"]
                if "navigation_coverage_union_visited_nodes_exact" in bot:
                    navigation_coverage_union_values.add(
                        bot["navigation_coverage_union_visited_nodes_exact"])
                previous[bot["identity"]] = bot
            if optional_presence and "damage_taken_exact" in optional_presence and \
                    participant_damage_dealt != participant_damage_taken:
                raise QualityError(
                    f"{path}: inter-participant damage dealt/taken does not reconcile at sequence {event['seq']}")
            if len(navigation_coverage_union_values) > 1:
                raise QualityError(
                    f"{path}: navigation coverage union differs between bots at sequence {event['seq']}")
        if optional_presence and "walking_step_preflight_diagnostics" in optional_presence:
            _validate_walking_step_preflight_diagnostic_stream(events, path)
        if optional_presence and "walking_hitwall_dispatch_diagnostics" in optional_presence:
            _validate_walking_hitwall_dispatch_diagnostic_stream(events, path)
        if optional_presence and "walking_step_preflight_positive_dps_veto_actions" in optional_presence:
            _validate_positive_dps_veto_action_stream(events, path)
        if optional_presence and "falling_parity_realized_records" in optional_presence:
            _validate_falling_parity_realized_record_stream(events, path)
        if optional_presence and "vertical_pain_column_diagnostics" in optional_presence:
            _validate_vertical_pain_column_diagnostic_stream(events, path)
        if optional_presence and "hazard_water_egress_diagnostics" in optional_presence:
            _validate_hazard_water_egress_diagnostic_stream(events, path)
        if optional_presence and "move_stall_recovery_episodes" in optional_presence:
            _validate_move_stall_recovery_episode_stream(events, path)
        if optional_presence and "move_stall_recovery_decisions" in optional_presence:
            _validate_move_stall_recovery_decision_stream(events, path)
        if optional_presence and "hazard_death_partition_records" in optional_presence:
            _validate_hazard_death_partition_stream(events, path)
    if events[0]["type"] != "run_start" or events[0]["tick"] != 0:
        raise QualityError(f"{path}: first event must be run_start at tick zero")
    if events[-1]["type"] != "run_result":
        raise QualityError(f"{path}: last event must be run_result")
    for index, event in enumerate(events[1:-1], 1):
        if event["type"] != "tick":
            raise QualityError(f"{path}: event {index} must be a tick event")
        if event["tick"] != index:
            raise QualityError(f"{path}: tick event {index} has tick {event['tick']}")
        if event["status"] != "running" or event["failure_reason"]:
            raise QualityError(f"{path}: tick event {index} is not a clean running sample")
    if events[-1]["tick"] != len(events) - 2:
        raise QualityError(f"{path}: final tick does not match the number of tick samples")
    if events[-1]["tick"] > manifest["max_ticks"]:
        raise QualityError(f"{path}: final tick exceeds max_ticks")
    maps = {event["map"] for event in events}
    if len(maps) != 1:
        raise QualityError(f"{path}: map changed during the run")
    return events


def _validate_ai_frame_timing(raw: Any, context: str) -> dict[str, Any]:
    timing = _object(raw, context)
    if _string(timing, "schema", context, nonempty=True) != "surreal-bot-ai-frame-timing-v1":
        raise QualityError(f"{context}: unsupported timing schema")
    if _string(timing, "scope", context, nonempty=True) != "benchmark_observation_policy_driver_sampling":
        raise QualityError(f"{context}: timing scope is not the benchmark-only scope")
    if _string(timing, "clock", context, nonempty=True) != "host_steady_clock_performance_only":
        raise QualityError(f"{context}: timing clock is not the scoped in-engine performance clock")
    if (_string(timing, "behavioral_determinism", context, nonempty=True)
            != "not_behavioral_evidence"):
        raise QualityError(f"{context}: timing must not claim behavioral determinism")
    result = {
        "sample_count": _integer(timing.get("sample_count"), f"{context}.sample_count", minimum=0),
        "histogram_bucket_overflows_exact": _integer(
            timing.get("histogram_bucket_overflows_exact"),
            f"{context}.histogram_bucket_overflows_exact", minimum=0),
        "bucket_max_microseconds": _integer(
            timing.get("bucket_max_microseconds"), f"{context}.bucket_max_microseconds", minimum=1),
        "max_microseconds": _integer(
            timing.get("max_microseconds"), f"{context}.max_microseconds", minimum=0),
    }
    for name in ("p50_microseconds", "p95_microseconds", "p99_microseconds"):
        value = timing.get(name)
        result[name] = None if value is None else _integer(value, f"{context}.{name}", minimum=0)
        if result[name] is not None and result[name] > result["max_microseconds"]:
            raise QualityError(f"{context}.{name} exceeds max_microseconds")
    if result["sample_count"] == 0:
        if any(result[name] is not None for name in
               ("p50_microseconds", "p95_microseconds", "p99_microseconds")):
            raise QualityError(f"{context}: empty timing sample has a percentile")
    elif result["p50_microseconds"] is None:
        raise QualityError(f"{context}: populated timing sample lacks p50")
    return result


def _validate_summary(path: Path, manifest: dict[str, Any], events: list[dict[str, Any]]) -> dict[str, Any]:
    raw = _load_json(path, "summary")
    expected_schema = (SUMMARY_SCHEMA_V4 if manifest["schema"] == MANIFEST_SCHEMA_V3 else
                       SUMMARY_SCHEMA_V2 if manifest["schema"] == MANIFEST_SCHEMA_V2 else SUMMARY_SCHEMA)
    allowed_schemas = ((SUMMARY_SCHEMA_V2, SUMMARY_SCHEMA_V3)
                       if expected_schema == SUMMARY_SCHEMA_V2 else (SUMMARY_SCHEMA_V4,)
                       if expected_schema == SUMMARY_SCHEMA_V4 else (SUMMARY_SCHEMA,))
    if raw.get("schema") not in allowed_schemas:
        raise QualityError(f"{path}: unsupported summary schema {raw.get('schema')!r}")
    status = _string(raw, "status", "summary", nonempty=True)
    if status not in ("complete", "failed"):
        raise QualityError(f"{path}: unsupported summary status {status!r}")
    exit_code = _integer(raw.get("exit_code"), "summary.exit_code")
    ticks = _integer(raw.get("ticks"), "summary.ticks", minimum=0)
    seconds = _number(raw.get("simulated_seconds"), "summary.simulated_seconds", minimum=0.0)
    config = _object(raw.get("config"), "summary.config")
    comparisons = {
        "url": _string(config, "url", "summary.config", nonempty=True),
        "seed": _integer(config.get("seed"), "summary.config.seed", minimum=0),
        "max_ticks": _integer(config.get("max_ticks"), "summary.config.max_ticks", minimum=1),
        "fixed_delta": _number(config.get("fixed_delta"), "summary.config.fixed_delta", minimum=0.0),
        "difficulty": _integer(config.get("difficulty"), "summary.config.difficulty", minimum=0),
    }
    if manifest["harmful_zone_escape_enabled"] is not None:
        comparisons["harmful_zone_escape_enabled"] = _boolean(
            config.get("harmful_zone_escape_enabled"),
            "summary.config.harmful_zone_escape_enabled")
    if manifest["walking_preflight_positive_dps_veto_enabled"] is not None:
        comparisons["walking_preflight_positive_dps_veto_enabled"] = _boolean(
            config.get("walking_preflight_positive_dps_veto_enabled"),
            "summary.config.walking_preflight_positive_dps_veto_enabled")
    if manifest["hazard_swim_egress_enabled"] is not None:
        comparisons["hazard_swim_egress_enabled"] = _boolean(
            config.get("hazard_swim_egress_enabled"),
            "summary.config.hazard_swim_egress_enabled")
    if manifest["hazard_swim_egress_live_enabled"] is not None:
        comparisons["hazard_swim_egress_live_enabled"] = _boolean(
            config.get("hazard_swim_egress_live_enabled"),
            "summary.config.hazard_swim_egress_live_enabled")
    if manifest["failed_navigation_avoidance_enabled"] is not None:
        comparisons["failed_navigation_avoidance_enabled"] = _boolean(
            config.get("failed_navigation_avoidance_enabled"),
            "summary.config.failed_navigation_avoidance_enabled")
    if manifest["falling_hazard_recovery_enabled"] is not None:
        comparisons["falling_hazard_recovery_enabled"] = _boolean(
            config.get("falling_hazard_recovery_enabled"),
            "summary.config.falling_hazard_recovery_enabled")
    if manifest["falling_hazard_recovery_live_enabled"] is not None:
        comparisons["falling_hazard_recovery_live_enabled"] = _boolean(
            config.get("falling_hazard_recovery_live_enabled"),
            "summary.config.falling_hazard_recovery_live_enabled")
    if manifest["targetless_move_to_timeout_enabled"] is not None:
        comparisons["targetless_move_to_timeout_enabled"] = _boolean(
            config.get("targetless_move_to_timeout_enabled"),
            "summary.config.targetless_move_to_timeout_enabled")
    if manifest["direct_actor_move_toward_timeout_enabled"] is not None:
        comparisons["direct_actor_move_toward_timeout_enabled"] = _boolean(
            config.get("direct_actor_move_toward_timeout_enabled"),
            "summary.config.direct_actor_move_toward_timeout_enabled")
    if manifest["native_path_commit_observer_enabled"] is not None:
        comparisons["native_path_commit_observer_enabled"] = _boolean(
            config.get("native_path_commit_observer_enabled"),
            "summary.config.native_path_commit_observer_enabled")
    if manifest["reachspec_capability_observer_enabled"] is not None:
        comparisons["reachspec_capability_observer_enabled"] = _boolean(
            config.get("reachspec_capability_observer_enabled"),
            "summary.config.reachspec_capability_observer_enabled")
    if manifest["direct_reach_command_observer_enabled"] is not None:
        comparisons["direct_reach_command_observer_enabled"] = _boolean(
            config.get("direct_reach_command_observer_enabled"),
            "summary.config.direct_reach_command_observer_enabled")
    if manifest["pawn_vision_cone_enabled"] is not None:
        comparisons["pawn_vision_cone_enabled"] = _boolean(
            config.get("pawn_vision_cone_enabled"), "summary.config.pawn_vision_cone_enabled")
    if manifest["pawn_vision_observer_enabled"] is not None:
        comparisons["pawn_vision_observer_enabled"] = _boolean(
            config.get("pawn_vision_observer_enabled"),
            "summary.config.pawn_vision_observer_enabled")
    if manifest["finite_move_command_guard_enabled"] is not None:
        comparisons["finite_move_command_guard_enabled"] = _boolean(
            config.get("finite_move_command_guard_enabled"),
            "summary.config.finite_move_command_guard_enabled")
    if manifest["pick_target_predicate_mode"] is not None:
        comparisons["pick_target_predicate_mode"] = _string(
            config, "pick_target_predicate_mode", "summary.config", nonempty=True)
    if manifest["shadow_policy_set"] is not None:
        comparisons["shadow_policy_set"] = _validate_shadow_policy_set(
            config.get("shadow_policy_set"), "summary.config.shadow_policy_set")
    requested_roster = None
    actual_roster = None
    if expected_schema in (SUMMARY_SCHEMA_V2, SUMMARY_SCHEMA_V4):
        summary_bot_count = _strict_integer(config.get("bot_count"), "summary.config.bot_count",
                                            minimum=1, maximum=16)
        if summary_bot_count != manifest["bot_count"]:
            raise QualityError(f"{path}: summary config bot_count differs from manifest")
        requested_roster = _validate_requested_roster(
            raw.get("requested_roster"), "summary.requested_roster", summary_bot_count)
        if requested_roster != manifest["requested_roster"]:
            raise QualityError(f"{path}: summary requested_roster differs from manifest")
        actual_roster = _validate_actual_roster(
            raw.get("actual_roster"), "summary.actual_roster", summary_bot_count, complete=status == "complete")
    _string(config, "output_directory", "summary.config")
    for name, value in comparisons.items():
        expected = manifest[name]
        equal = _close(value, expected) if name == "fixed_delta" else value == expected
        if not equal:
            raise QualityError(f"{path}: summary config {name} differs from manifest")
    final = events[-1]
    failure_reason = _string(raw, "failure_reason", "summary")
    map_name = _string(raw, "map", "summary")
    summary_strings = ("game", "version") if expected_schema in (SUMMARY_SCHEMA_V2, SUMMARY_SCHEMA_V4) else (
        "game", "version", "bot_class", "bot_name")
    for name in summary_strings:
        _string(raw, name, "summary")
    if ticks != final["tick"] or not _close(seconds, final["simulated_seconds"]):
        raise QualityError(f"{path}: summary time does not match final telemetry")
    if status != final["status"] or failure_reason != final["failure_reason"]:
        raise QualityError(f"{path}: summary result differs from final telemetry")
    if map_name != final["map"]:
        raise QualityError(f"{path}: summary map differs from telemetry")
    if (status == "complete") != (exit_code == 0):
        raise QualityError(f"{path}: completion status and exit code disagree")
    if actual_roster is not None:
        actual_by_identity = {entry["identity"]: entry for entry in actual_roster}
        for event_index, event in enumerate(events):
            for bot in event["bots"]:
                actual = actual_by_identity.get(bot["identity"])
                if actual is None:
                    raise QualityError(
                        f"{path}: telemetry event {event_index} bot identity is absent from actual_roster")
                for field in ("actor", "player_name", "class"):
                    if bot[field] != actual[field]:
                        raise QualityError(
                            f"{path}: telemetry event {event_index} bot {field} differs from actual_roster")
        if status == "complete" and {bot["identity"] for bot in events[0]["bots"]} != set(actual_by_identity):
            raise QualityError(f"{path}: initial telemetry bot identities differ from actual_roster")
    ai_frame_timing = (_validate_ai_frame_timing(raw.get("ai_frame_timing"), "summary.ai_frame_timing")
                       if raw.get("schema") in (SUMMARY_SCHEMA_V3, SUMMARY_SCHEMA_V4) else None)
    if manifest["schema"] == MANIFEST_SCHEMA_V3:
        if _validate_build_identity(raw.get("build_identity"), "summary.build_identity") != manifest["build_identity"]:
            raise QualityError(f"{path}: summary build identity differs from manifest")
    if ai_frame_timing is not None and ai_frame_timing["sample_count"] > ticks:
        raise QualityError(f"{path}: timing sample count exceeds simulated tick count")
    return {
        "status": status,
        "exit_code": exit_code,
        "ticks": ticks,
        "simulated_seconds": seconds,
        "map": map_name,
        "failure_reason": failure_reason,
        "requested_roster": requested_roster,
        "actual_roster": actual_roster,
        "ai_frame_timing": ai_frame_timing,
    }


def _initial_layout(summary: dict[str, Any], events: list[dict[str, Any]]) -> dict[str, Any] | None:
    requested = summary["requested_roster"]
    actual = summary["actual_roster"]
    if requested is None or actual is None:
        return None
    if summary["status"] != "complete" or len(requested) != len(actual):
        return None
    start_bots = {bot["identity"]: bot for bot in events[0]["bots"]}
    if len(start_bots) != len(events[0]["bots"]):
        raise QualityError("initial telemetry contains duplicate participant identities")
    requested_by_index = {entry["roster_index"]: entry for entry in requested}
    participants = []
    for participant in actual:
        roster_index = participant["roster_index"]
        bot = start_bots.get(participant["identity"])
        if bot is None:
            raise QualityError("initial telemetry is missing an actual roster participant")
        if "physics_mode" not in bot:
            return None
        participants.append({
            "roster_index": roster_index,
            "external_skill": requested_by_index[roster_index]["external_skill"],
            "class": participant["class"],
            "player_name": participant["player_name"],
            "position": bot["position"],
            "velocity": bot["velocity"],
            "physics_mode": bot["physics_mode"],
            "state": bot["state"],
        })
    participants.sort(key=lambda item: item["roster_index"])
    canonical = json.dumps(participants, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
    return {
        "fingerprint": "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest(),
        "participants": participants,
    }


def _load_metadata(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    raw = _load_json(path, "quality metadata")
    if raw.get("schema") != METADATA_SCHEMA:
        raise QualityError(f"{path}: unsupported quality metadata schema {raw.get('schema')!r}")
    variant = _string(raw, "variant", "quality metadata", nonempty=True)
    pair_id = raw.get("pair_id")
    role = raw.get("comparison_role")
    if (pair_id is None) != (role is None):
        raise QualityError(f"{path}: pair_id and comparison_role must be provided together")
    if pair_id is not None:
        if not isinstance(pair_id, str) or not pair_id:
            raise QualityError(f"{path}: pair_id must be a non-empty string")
        if role not in ("baseline", "candidate"):
            raise QualityError(f"{path}: comparison_role must be baseline or candidate")
    layout_id = raw.get("start_layout_id")
    expected_layout_fingerprint = raw.get("expected_initial_layout_fingerprint")
    if (layout_id is None) != (expected_layout_fingerprint is None):
        raise QualityError(
            f"{path}: start_layout_id and expected_initial_layout_fingerprint must be provided together")
    if layout_id is not None:
        if not isinstance(layout_id, str) or not layout_id:
            raise QualityError(f"{path}: start_layout_id must be a non-empty string")
        expected_layout_fingerprint = _layout_fingerprint(
            expected_layout_fingerprint, "quality metadata.expected_initial_layout_fingerprint")
    return {
        "variant": variant,
        "pair_id": pair_id,
        "comparison_role": role,
        "start_layout_id": layout_id,
        "expected_initial_layout_fingerprint": expected_layout_fingerprint,
    }


def _distance(left: dict[str, float], right: dict[str, float]) -> float:
    return math.sqrt(sum((right[axis] - left[axis]) ** 2 for axis in "xyz"))


def _counter_fraction(numerator: int | float | None,
                      denominator: int | float | None) -> float | None:
    if numerator is None or denominator is None or denominator <= 0:
        return None
    return numerator / denominator


def _reconcile_post_mayfall_harmful_parity_deaths(
        events: list[dict[str, Any]], path: Path) -> dict[str, dict[str, int] | None]:
    """Return only exact command-stable harmful-fall/death correlations.

    This deliberately is not an avoidable-suicide metric: it lacks proof of a
    safe alternative and of no external intervention.  Missing arrays or either
    bounded record stream overflowing produce null metrics rather than zero.
    """
    witnesses: defaultdict[str, set[tuple[str, int, int, int]]] = defaultdict(set)
    parity_started: defaultdict[str, set[tuple[str, int, int, int]]] = defaultdict(set)
    parity_died: defaultdict[str, set[tuple[str, int, int, int]]] = defaultdict(set)
    partition_claims: defaultdict[
        str, defaultdict[tuple[str, int, int, int], list[str]]] = defaultdict(
            lambda: defaultdict(list))
    complete: dict[str, bool] = {}

    for event in events:
        for bot in event["bots"]:
            identity, actor = bot["identity"], bot["actor"]
            required = (
                "walking_step_preflight_diagnostics",
                "falling_parity_realized_records",
                "hazard_death_partition_records",
                "walking_step_preflight_diagnostic_overflows_exact",
                "falling_parity_realized_record_overflows_exact",
            )
            if any(name not in bot for name in required) \
                    or bot["walking_step_preflight_diagnostic_overflows_exact"] != 0 \
                    or bot["falling_parity_realized_record_overflows_exact"] != 0:
                complete[identity] = False
                continue
            complete.setdefault(identity, True)
            for diagnostic in bot["walking_step_preflight_diagnostics"]:
                if diagnostic["source_pawn_actor"] != actor:
                    raise QualityError(f"{path}: causal preflight witness actor differs from {identity}")
                if diagnostic["phase"] != "post_mayfall_confirmation" \
                        or diagnostic["transition_outcome"] != "begin_falling" \
                        or diagnostic["reason"] \
                        != "walking_step_preflight_reason_harmful_pain_fall_exact" \
                        or diagnostic["movement_command_token"] == 0:
                    continue
                witnesses[identity].add((actor, diagnostic["life_generation"],
                                         diagnostic["invocation_token"],
                                         diagnostic["walking_iteration"]))
            for parity in bot["falling_parity_realized_records"]:
                if parity["source_pawn_actor"] != actor:
                    raise QualityError(f"{path}: causal parity record actor differs from {identity}")
                key = (actor, parity["life_generation"], parity["invocation_token"],
                       parity["walking_iteration"])
                if parity["outcome"] == "episode_started":
                    parity_started[identity].add(key)
                elif parity["outcome"] == "died":
                    parity_died[identity].add(key)
            for partition in bot["hazard_death_partition_records"]:
                if partition["source_pawn_actor"] != actor:
                    raise QualityError(f"{path}: causal death partition actor differs from {identity}")
                if partition["falling_parity_terminal_known"]:
                    key = (actor, partition["falling_parity_life_generation"],
                           partition["falling_parity_invocation_token"],
                           partition["falling_parity_walking_iteration"])
                    partition_claims[identity][key].append(partition["attribution"])

    result: dict[str, dict[str, int] | None] = {}
    identities = {bot["identity"] for event in events for bot in event["bots"]}
    for identity in identities:
        if not complete.get(identity, False):
            result[identity] = None
            continue
        for key in witnesses[identity]:
            if key not in parity_started[identity]:
                # Older v2 artifacts can carry the later command token while
                # lacking the parity lifecycle stream. The causal answer is
                # unknown, not a zero and not grounds to reject the complete
                # non-causal benchmark artifact.
                complete[identity] = False
                break
        if not complete[identity]:
            result[identity] = None
            continue
        for key in parity_died[identity]:
            claims = partition_claims[identity].get(key, [])
            if len(claims) != 1:
                raise QualityError(f"{path}: causal parity death must have exactly one partition claim")
        for key, claims in partition_claims[identity].items():
            if key not in parity_died[identity] or len(claims) != 1:
                raise QualityError(f"{path}: causal partition claim must match exactly one parity death")
        died = witnesses[identity] & parity_died[identity]
        result[identity] = {
            "post_mayfall_harmful_begin_falling_command_witnesses_exact": len(
                witnesses[identity]),
            "post_mayfall_harmful_begin_falling_command_witness_parity_deaths_exact": len(died),
            "post_mayfall_harmful_begin_falling_command_witness_unassisted_environmental_deaths_exact": sum(
                partition_claims[identity][key][0] == "unassisted_environmental_death"
                for key in died),
        }
    return result


def _bot_metrics(events: list[dict[str, Any]],
                 causal_harmful_fall: dict[str, dict[str, int] | None] | None = None) \
        -> dict[str, dict[str, Any]]:
    telemetry_v2 = events[0]["schema"] == TELEMETRY_SCHEMA_V2
    identities = sorted({bot["identity"] for event in events for bot in event["bots"]})
    final_bots = {bot["identity"]: bot for bot in events[-1]["bots"]}
    metrics: dict[str, dict[str, Any]] = {}
    for identity in identities:
        samples = [(index, event["simulated_seconds"], bot)
                   for index, event in enumerate(events)
                   for bot in event["bots"] if bot["identity"] == identity]
        distance = active = no_progress = observed = health_loss = 0.0
        intent_no_progress = hazard_exposure = 0.0
        longest_no_progress = current_no_progress = 0.0
        longest_intent_no_progress = current_intent_no_progress = 0.0
        stuck_events = discontinuities = 0
        intent_stuck_events = hazard_entries = 0
        stuck_latched = False
        intent_stuck_latched = False
        health_values = [bot["health"] for _, _, bot in samples]
        for (previous_index, previous_time, previous), (index, now, current) in zip(samples, samples[1:]):
            if index != previous_index + 1 or current["actor"] != previous["actor"]:
                current_no_progress = 0.0
                current_intent_no_progress = 0.0
                stuck_latched = False
                intent_stuck_latched = False
                discontinuities += 1
                continue
            delta = now - previous_time
            if delta <= 0:
                continue
            health_loss += max(0, previous["health"] - current["health"])
            if telemetry_v2:
                if previous["in_hazard_zone"]:
                    hazard_exposure += delta
                if not previous["in_hazard_zone"] and current["in_hazard_zone"]:
                    hazard_entries += 1
            if previous["health"] <= 0 or current["health"] <= 0:
                current_no_progress = 0.0
                current_intent_no_progress = 0.0
                stuck_latched = False
                intent_stuck_latched = False
                continue
            travelled = _distance(previous["position"], current["position"])
            distance += travelled
            observed += delta
            if travelled >= DISTANCE_EPSILON:
                active += delta
                current_no_progress = 0.0
                stuck_latched = False
            else:
                no_progress += delta
                current_no_progress += delta
                longest_no_progress = max(longest_no_progress, current_no_progress)
                if current_no_progress + 1e-12 >= STUCK_WINDOW_SECONDS and not stuck_latched:
                    stuck_events += 1
                    stuck_latched = True
            if telemetry_v2:
                movement_intent = previous["movement_intent"] or current["movement_intent"]
                if movement_intent and travelled < DISTANCE_EPSILON:
                    intent_no_progress += delta
                    current_intent_no_progress += delta
                    longest_intent_no_progress = max(
                        longest_intent_no_progress, current_intent_no_progress)
                    if (current_intent_no_progress + 1e-12 >= STUCK_WINDOW_SECONDS and
                            not intent_stuck_latched):
                        intent_stuck_events += 1
                        intent_stuck_latched = True
                else:
                    current_intent_no_progress = 0.0
                    intent_stuck_latched = False
        final = final_bots.get(identity)
        first = samples[0][2]
        exact: dict[str, int | float | None] = {}
        if telemetry_v2:
            for name in CORE_EXACT_COUNTERS + OPTIONAL_CUMULATIVE_METRICS:
                exact[name] = (
                    final[name] - first[name]
                    if final is not None and name in first and name in final else None)
            kills = exact["kills_exact"]
            deaths = exact["deaths_exact"]
        else:
            exact = {name: None for name in CORE_EXACT_COUNTERS + OPTIONAL_CUMULATIVE_METRICS}
            kills = deaths = None
        parity_episodes = exact.get("falling_parity_realized_episodes_exact")
        parity_steps = exact.get("falling_parity_realized_steps_exact")
        parity_matched = exact.get("falling_parity_realized_matched_steps_exact")
        parity_matched_landings = exact.get(
            "falling_parity_realized_matched_landing_steps_exact")
        parity_mismatched = exact.get("falling_parity_realized_mismatches_exact")
        parity_matched_total = (
            parity_matched + parity_matched_landings
            if parity_matched is not None and parity_matched_landings is not None else None)
        parity_comparable = (
            parity_matched_total + parity_mismatched
            if parity_matched_total is not None and parity_mismatched is not None else None)
        column_true_positive = exact.get(
            "vertical_pain_column_true_positive_outcomes_exact")
        column_false_positive = exact.get(
            "vertical_pain_column_false_positive_outcomes_exact")
        column_false_negative = exact.get(
            "vertical_pain_column_false_negative_outcomes_exact")
        column_true_negative = exact.get(
            "vertical_pain_column_true_negative_outcomes_exact")
        column_ambiguous = exact.get(
            "vertical_pain_column_ambiguous_outcomes_exact")
        column_unknown = exact.get(
            "vertical_pain_column_unknown_outcomes_exact")
        column_started = exact.get("vertical_pain_column_episodes_started_exact")
        column_completed = exact.get("vertical_pain_column_episodes_completed_exact")
        column_overflows = exact.get("vertical_pain_column_diagnostic_overflows_exact")
        column_capacity = exact.get(
            "vertical_pain_column_generation_capacity_exhaustions_exact")
        column_labeled = (
            column_true_positive + column_false_positive + column_false_negative
            + column_true_negative
            if all(value is not None for value in (
                column_true_positive, column_false_positive,
                column_false_negative, column_true_negative)) else None)
        recovery_episodes = exact.get("move_stall_recovery_episodes_exact")
        recovery_excluded = exact.get(
            "move_stall_recovery_excluded_intentional_stops_exact")
        recovery_overflows = exact.get(
            "move_stall_recovery_episode_record_overflows_exact")
        recovery_denominator = (
            recovery_episodes - recovery_excluded
            if recovery_episodes is not None and recovery_excluded is not None
            and recovery_overflows == 0 else None)
        recovery_clear_within_2 = exact.get(
            "move_stall_recovery_cleared_within_2_seconds_exact")
        recovery_clear_after_2 = exact.get(
            "move_stall_recovery_cleared_after_2_seconds_within_5_seconds_exact")
        recovery_replanned = exact.get(
            "move_stall_recovery_replanned_within_5_seconds_exact")
        metrics[identity] = {
            "identity": identity,
            "player_name": first["player_name"],
            "class": first["class"],
            "distance_traveled": distance,
            "observed_alive_seconds": observed,
            "active_movement_seconds": active,
            "active_movement_fraction": active / observed if observed > 0 else None,
            "no_progress_seconds_proxy": no_progress,
            "longest_no_progress_seconds_proxy": longest_no_progress,
            "stuck_events_proxy": stuck_events,
            "movement_intent_no_progress_seconds_proxy": intent_no_progress if telemetry_v2 else None,
            "longest_movement_intent_no_progress_seconds_proxy": (
                longest_intent_no_progress if telemetry_v2 else None),
            "movement_intent_stuck_events_proxy": intent_stuck_events if telemetry_v2 else None,
            **(causal_harmful_fall.get(identity) if causal_harmful_fall
               and causal_harmful_fall.get(identity) is not None else {
                   "post_mayfall_harmful_begin_falling_command_witnesses_exact": None,
                   "post_mayfall_harmful_begin_falling_command_witness_parity_deaths_exact": None,
                   "post_mayfall_harmful_begin_falling_command_witness_unassisted_environmental_deaths_exact": None,
               }),
            **exact,
            "navigation_coverage_visited_nodes_exact": (
                final.get("navigation_coverage_visited_nodes_exact")
                if telemetry_v2 and final is not None else None),
            "navigation_coverage_catalog_nodes_exact": (
                final.get("navigation_coverage_catalog_nodes_exact")
                if telemetry_v2 and final is not None else None),
            "navigation_coverage_union_visited_nodes_exact": (
                final.get("navigation_coverage_union_visited_nodes_exact")
                if telemetry_v2 and final is not None else None),
            "navigation_coverage_fraction": _counter_fraction(
                final.get("navigation_coverage_visited_nodes_exact")
                if telemetry_v2 and final is not None else None,
                final.get("navigation_coverage_catalog_nodes_exact")
                if telemetry_v2 and final is not None else None),
            "navigation_coverage_union_fraction": _counter_fraction(
                final.get("navigation_coverage_union_visited_nodes_exact")
                if telemetry_v2 and final is not None else None,
                final.get("navigation_coverage_catalog_nodes_exact")
                if telemetry_v2 and final is not None else None),
            "suicide_to_kill_ratio": (
                exact["suicides_exact"] / kills if telemetry_v2 and kills and kills > 0 else None),
            "match_score_initial": first.get("score"),
            "match_score_final": final.get("score") if telemetry_v2 and final is not None else None,
            "match_score_delta": (
                final["score"] - first["score"] if telemetry_v2 and final is not None else None),
            "pri_deaths_delta": (
                final["pri_deaths"] - first["pri_deaths"] if telemetry_v2 and final is not None else None),
            "hazard_exposure_seconds": hazard_exposure if telemetry_v2 else None,
            "hazard_entries": hazard_entries if telemetry_v2 else None,
            "hazard_exposed_death_fraction_proxy": (
                exact["hazard_exposed_deaths_proxy"] / deaths
                if telemetry_v2 and deaths and deaths > 0 else None),
            "health_loss_observed": health_loss,
            "minimum_health_observed": min(health_values),
            "final_health_observed": final["health"] if final is not None else None,
            "survived_to_final_sample": final["health"] > 0 if final is not None else None,
            "actor_or_observation_discontinuities": discontinuities,
            "recoverable_movement_episode_clear_within_2s_fraction": _counter_fraction(
                recovery_clear_within_2, recovery_denominator),
            "recoverable_movement_episode_clear_or_replanned_within_5s_fraction": (
                _counter_fraction(
                    recovery_clear_within_2 + recovery_clear_after_2 + recovery_replanned
                    if all(value is not None for value in (
                        recovery_clear_within_2, recovery_clear_after_2, recovery_replanned))
                    else None,
                    recovery_denominator)),
            "falling_parity_realized_episode_completion_fraction": _counter_fraction(
                (exact.get("falling_parity_realized_deaths_exact")
                 + exact.get("falling_parity_realized_landings_exact")
                 + exact.get("falling_parity_realized_continuity_losses_exact")
                 if exact.get("falling_parity_realized_deaths_exact") is not None
                 and exact.get("falling_parity_realized_landings_exact") is not None
                 and exact.get("falling_parity_realized_continuity_losses_exact") is not None
                 else None), parity_episodes),
            "falling_parity_realized_comparable_step_fraction": _counter_fraction(
                parity_comparable, parity_steps),
            "falling_parity_realized_matched_step_fraction": _counter_fraction(
                parity_matched_total, parity_comparable),
            "falling_parity_realized_mismatch_fraction": _counter_fraction(
                parity_mismatched, parity_comparable),
            "falling_parity_realized_unknown_step_fraction": _counter_fraction(
                exact.get("falling_parity_realized_unknowns_exact"), parity_steps),
            "vertical_pain_column_precision": _counter_fraction(
                column_true_positive,
                column_true_positive + column_false_positive
                if column_true_positive is not None and column_false_positive is not None
                else None),
            "vertical_pain_column_recall": _counter_fraction(
                column_true_positive,
                column_true_positive + column_false_negative
                if column_true_positive is not None and column_false_negative is not None
                else None),
            "vertical_pain_column_false_positive_rate": _counter_fraction(
                column_false_positive,
                column_false_positive + column_true_negative
                if column_false_positive is not None and column_true_negative is not None
                else None),
            "vertical_pain_column_labeled_episode_fraction": _counter_fraction(
                column_labeled, column_completed),
            "vertical_pain_column_episode_completion_fraction": _counter_fraction(
                column_completed, column_started),
            "vertical_pain_column_unknown_outcome_fraction": _counter_fraction(
                column_unknown, column_completed),
            "vertical_pain_column_ambiguous_outcome_fraction": _counter_fraction(
                column_ambiguous, column_completed),
            "vertical_pain_column_diagnostic_coverage_fraction": _counter_fraction(
                (column_started + column_completed + column_capacity - column_overflows
                 if all(value is not None for value in (
                     column_started, column_completed, column_capacity, column_overflows))
                 else None),
                (column_started + column_completed + column_capacity
                 if all(value is not None for value in (
                     column_started, column_completed, column_capacity)) else None)),
            "vertical_pain_column_generation_capacity_exhaustion_rate":
                _counter_fraction(column_capacity, column_started),
        }
    return metrics


def _run_metrics(bots: dict[str, dict[str, Any]], completion: bool) -> dict[str, Any]:
    values = list(bots.values())
    observed = sum(bot["observed_alive_seconds"] for bot in values)
    active = sum(bot["active_movement_seconds"] for bot in values)
    survival = [bot["survived_to_final_sample"] for bot in values]
    def sum_available(name: str) -> float | int | None:
        materialized = [bot[name] for bot in values if bot[name] is not None]
        return sum(materialized) if materialized else None

    kills = sum_available("kills_exact")
    deaths = sum_available("deaths_exact")
    suicides = sum_available("suicides_exact")
    hazard_deaths = sum_available("hazard_exposed_deaths_proxy")
    participant_damage_dealt = sum_available("damage_dealt_to_other_participants_exact")
    participant_damage_taken = sum_available("damage_taken_from_other_participants_exact")
    result = {
        "distance_traveled": sum(bot["distance_traveled"] for bot in values),
        "active_movement_seconds": active,
        "active_movement_fraction": active / observed if observed > 0 else None,
        "no_progress_seconds_proxy": sum(bot["no_progress_seconds_proxy"] for bot in values),
        "longest_no_progress_seconds_proxy": max(
            (bot["longest_no_progress_seconds_proxy"] for bot in values), default=0.0),
        "stuck_events_proxy": sum(bot["stuck_events_proxy"] for bot in values),
        "movement_intent_no_progress_seconds_proxy": sum_available(
            "movement_intent_no_progress_seconds_proxy"),
        "longest_movement_intent_no_progress_seconds_proxy": max(
            (bot["longest_movement_intent_no_progress_seconds_proxy"] for bot in values
             if bot["longest_movement_intent_no_progress_seconds_proxy"] is not None), default=None),
        "movement_intent_stuck_events_proxy": sum_available("movement_intent_stuck_events_proxy"),
        "kills_exact": kills,
        "deaths_exact": deaths,
        "suicides_exact": suicides,
        "environmental_deaths_exact": sum_available("environmental_deaths_exact"),
        "suicide_to_kill_ratio": suicides / kills if kills and kills > 0 else None,
        "match_score_delta": sum_available("match_score_delta"),
        "hit_wall_events_exact": sum_available("hit_wall_events_exact"),
        "hazard_exposure_seconds": sum_available("hazard_exposure_seconds"),
        "hazard_entries": sum_available("hazard_entries"),
        "hazard_exposed_deaths_proxy": hazard_deaths,
        "hazard_exposed_death_fraction_proxy": (
            hazard_deaths / deaths if deaths and deaths > 0 else None),
        "damage_taken_exact": sum_available("damage_taken_exact"),
        "damage_taken_from_other_participants_exact": participant_damage_taken,
        "damage_taken_from_self_exact": sum_available("damage_taken_from_self_exact"),
        "damage_taken_from_nonparticipants_exact": sum_available(
            "damage_taken_from_nonparticipants_exact"),
        "damage_dealt_to_other_participants_exact": participant_damage_dealt,
        "damage_efficiency_to_other_participants": (
            participant_damage_dealt / participant_damage_taken
            if participant_damage_dealt is not None
            and participant_damage_taken is not None
            and participant_damage_taken > 0 else None),
        "health_loss_observed": sum(bot["health_loss_observed"] for bot in values),
        "minimum_health_observed": min((bot["minimum_health_observed"] for bot in values), default=None),
        "survived_to_final_sample": all(survival) if survival and all(item is not None for item in survival) else None,
        "completion": completion,
        "post_mayfall_harmful_begin_falling_command_witnesses_exact": sum_available(
            "post_mayfall_harmful_begin_falling_command_witnesses_exact"),
        "post_mayfall_harmful_begin_falling_command_witness_parity_deaths_exact": sum_available(
            "post_mayfall_harmful_begin_falling_command_witness_parity_deaths_exact"),
        "post_mayfall_harmful_begin_falling_command_witness_unassisted_environmental_deaths_exact": sum_available(
            "post_mayfall_harmful_begin_falling_command_witness_unassisted_environmental_deaths_exact"),
    }
    result.update({name: sum_available(name) for name in OPTIONAL_CUMULATIVE_METRICS})
    recovery_episodes = result.get("move_stall_recovery_episodes_exact")
    recovery_excluded = result.get(
        "move_stall_recovery_excluded_intentional_stops_exact")
    recovery_overflows = result.get(
        "move_stall_recovery_episode_record_overflows_exact")
    recovery_denominator = (
        recovery_episodes - recovery_excluded
        if recovery_episodes is not None and recovery_excluded is not None
        and recovery_overflows == 0 else None)
    recovery_clear_within_2 = result.get(
        "move_stall_recovery_cleared_within_2_seconds_exact")
    recovery_clear_after_2 = result.get(
        "move_stall_recovery_cleared_after_2_seconds_within_5_seconds_exact")
    recovery_replanned = result.get(
        "move_stall_recovery_replanned_within_5_seconds_exact")
    result.update({
        "recoverable_movement_episode_clear_within_2s_fraction": _counter_fraction(
            recovery_clear_within_2, recovery_denominator),
        "recoverable_movement_episode_clear_or_replanned_within_5s_fraction": (
            _counter_fraction(
                recovery_clear_within_2 + recovery_clear_after_2 + recovery_replanned
                if all(value is not None for value in (
                    recovery_clear_within_2, recovery_clear_after_2, recovery_replanned))
                else None,
                recovery_denominator)),
    })
    navigation_visited = sum_available("navigation_coverage_visited_nodes_exact")
    navigation_catalog = sum_available("navigation_coverage_catalog_nodes_exact")
    navigation_union_values = {
        bot["navigation_coverage_union_visited_nodes_exact"] for bot in values
        if bot["navigation_coverage_union_visited_nodes_exact"] is not None
    }
    navigation_catalog_values = {
        bot["navigation_coverage_catalog_nodes_exact"] for bot in values
        if bot["navigation_coverage_catalog_nodes_exact"] is not None
    }
    navigation_union = (
        next(iter(navigation_union_values)) if len(navigation_union_values) == 1 else None)
    navigation_union_catalog = (
        next(iter(navigation_catalog_values)) if len(navigation_catalog_values) == 1 else None)
    result.update({
        "navigation_coverage_visited_nodes_exact": navigation_visited,
        "navigation_coverage_catalog_nodes_exact": navigation_catalog,
        "navigation_coverage_union_visited_nodes_exact": navigation_union,
        "navigation_coverage_fraction": _counter_fraction(navigation_visited, navigation_catalog),
        "navigation_coverage_union_fraction": _counter_fraction(navigation_union, navigation_union_catalog),
    })
    parity_episodes = result.get("falling_parity_realized_episodes_exact")
    parity_steps = result.get("falling_parity_realized_steps_exact")
    parity_matched = result.get("falling_parity_realized_matched_steps_exact")
    parity_matched_landings = result.get(
        "falling_parity_realized_matched_landing_steps_exact")
    parity_mismatched = result.get("falling_parity_realized_mismatches_exact")
    parity_matched_total = (
        parity_matched + parity_matched_landings
        if parity_matched is not None and parity_matched_landings is not None else None)
    parity_comparable = (
        parity_matched_total + parity_mismatched
        if parity_matched_total is not None and parity_mismatched is not None else None)
    result.update({
        "falling_parity_realized_episode_completion_fraction": _counter_fraction(
            (result.get("falling_parity_realized_deaths_exact")
             + result.get("falling_parity_realized_landings_exact")
             + result.get("falling_parity_realized_continuity_losses_exact")
             if result.get("falling_parity_realized_deaths_exact") is not None
             and result.get("falling_parity_realized_landings_exact") is not None
             and result.get("falling_parity_realized_continuity_losses_exact") is not None
             else None), parity_episodes),
        "falling_parity_realized_comparable_step_fraction": _counter_fraction(
            parity_comparable, parity_steps),
        "falling_parity_realized_matched_step_fraction": _counter_fraction(
            parity_matched_total, parity_comparable),
        "falling_parity_realized_mismatch_fraction": _counter_fraction(
            parity_mismatched, parity_comparable),
        "falling_parity_realized_unknown_step_fraction": _counter_fraction(
            result.get("falling_parity_realized_unknowns_exact"), parity_steps),
    })
    true_positive = result.get("vertical_pain_column_true_positive_outcomes_exact")
    false_positive = result.get("vertical_pain_column_false_positive_outcomes_exact")
    false_negative = result.get("vertical_pain_column_false_negative_outcomes_exact")
    true_negative = result.get("vertical_pain_column_true_negative_outcomes_exact")
    ambiguous = result.get("vertical_pain_column_ambiguous_outcomes_exact")
    unknown = result.get("vertical_pain_column_unknown_outcomes_exact")
    column_started = result.get("vertical_pain_column_episodes_started_exact")
    column_completed = result.get("vertical_pain_column_episodes_completed_exact")
    column_overflows = result.get("vertical_pain_column_diagnostic_overflows_exact")
    column_capacity = result.get(
        "vertical_pain_column_generation_capacity_exhaustions_exact")
    labeled = (
        true_positive + false_positive + false_negative + true_negative
        if all(value is not None for value in (
            true_positive, false_positive, false_negative, true_negative)) else None)
    result.update({
        "vertical_pain_column_precision": _counter_fraction(
            true_positive,
            true_positive + false_positive
            if true_positive is not None and false_positive is not None else None),
        "vertical_pain_column_recall": _counter_fraction(
            true_positive,
            true_positive + false_negative
            if true_positive is not None and false_negative is not None else None),
        "vertical_pain_column_false_positive_rate": _counter_fraction(
            false_positive,
            false_positive + true_negative
            if false_positive is not None and true_negative is not None else None),
        "vertical_pain_column_labeled_episode_fraction": _counter_fraction(
            labeled, column_completed),
        "vertical_pain_column_episode_completion_fraction": _counter_fraction(
            column_completed, column_started),
        "vertical_pain_column_unknown_outcome_fraction": _counter_fraction(
            unknown, column_completed),
        "vertical_pain_column_ambiguous_outcome_fraction": _counter_fraction(
            ambiguous, column_completed),
        "vertical_pain_column_diagnostic_coverage_fraction": _counter_fraction(
            (column_started + column_completed + column_capacity - column_overflows
             if all(value is not None for value in (
                 column_started, column_completed, column_capacity, column_overflows))
             else None),
            (column_started + column_completed + column_capacity
             if all(value is not None for value in (
                 column_started, column_completed, column_capacity)) else None)),
        "vertical_pain_column_generation_capacity_exhaustion_rate":
            _counter_fraction(column_capacity, column_started),
        "persistent_harmful_fall_promotion_fraction": _counter_fraction(
            result.get("persistent_harmful_fall_promotions_exact"),
            result.get("persistent_harmful_fall_candidates_started_exact")),
        "persistent_harmful_fall_confirmed_entry_fraction": _counter_fraction(
            result.get("persistent_harmful_fall_confirmed_harmful_entries_exact"),
            result.get("persistent_harmful_fall_promotions_exact")),
        "persistent_harmful_fall_mean_observed_lead_milliseconds": (
            result["persistent_harmful_fall_observed_lead_milliseconds_exact"]
            / result["persistent_harmful_fall_observed_lead_samples_exact"]
            if result.get("persistent_harmful_fall_observed_lead_samples_exact") is not None
            and result["persistent_harmful_fall_observed_lead_samples_exact"] > 0
            and result.get("persistent_harmful_fall_observed_lead_milliseconds_exact")
            is not None else None),
        "single_harmful_fall_prefix_promotion_fraction": _counter_fraction(
            result.get("single_harmful_fall_prefix_promotions_exact"),
            result.get("single_harmful_fall_prefix_candidates_started_exact")),
        "single_harmful_fall_prefix_confirmed_entry_fraction": _counter_fraction(
            result.get("single_harmful_fall_prefix_confirmed_harmful_entries_exact"),
            result.get("single_harmful_fall_prefix_promotions_exact")),
        "single_harmful_fall_prefix_mean_observed_lead_milliseconds": (
            result["single_harmful_fall_prefix_observed_lead_milliseconds_exact"]
            / result["single_harmful_fall_prefix_observed_lead_samples_exact"]
            if result.get("single_harmful_fall_prefix_observed_lead_samples_exact") is not None
            and result["single_harmful_fall_prefix_observed_lead_samples_exact"] > 0
            and result.get("single_harmful_fall_prefix_observed_lead_milliseconds_exact")
            is not None else None),
    })
    return result


def analyze_run(path: Path) -> dict[str, Any]:
    run_path = path.resolve()
    if not run_path.is_dir():
        raise QualityError(f"run path is not a directory: {run_path}")
    manifest = _validate_manifest(run_path / "manifest.json")
    events = _load_events(run_path / "events.jsonl", manifest)
    has_death_attribution = any(
        "direct_self_kills" in bot for event in events for bot in event["bots"])
    if has_death_attribution:
        if manifest["death_attribution_recent_window_seconds"] is None:
            raise QualityError(
                f"{run_path}: attributed death telemetry requires "
                "manifest.death_attribution_recent_window_seconds")
        if manifest["suicides_exact_semantics"] is None:
            raise QualityError(
                f"{run_path}: attributed death telemetry requires manifest.suicides_exact_semantics")
    summary = _validate_summary(run_path / "summary.json", manifest, events)
    metadata = _load_metadata(run_path / "quality-metadata.json")
    initial_layout = _initial_layout(summary, events)
    if metadata and metadata["start_layout_id"] is not None:
        if initial_layout is None:
            raise QualityError(f"{run_path}: declared start layout requires a complete observed initial layout")
        if initial_layout["fingerprint"] != metadata["expected_initial_layout_fingerprint"]:
            raise QualityError(f"{run_path}: observed initial-layout fingerprint differs from quality metadata")
    causal_harmful_fall = _reconcile_post_mayfall_harmful_parity_deaths(events, run_path)
    bots = _bot_metrics(events, causal_harmful_fall)
    for identity, bot in bots.items():
        if not any(bot.get(name) is None for name in
                   HAZARD_SWIM_EGRESS_PLANNER_HANDOFF_OUTCOME_COUNTERS):
            outcomes = sum(bot[name] for name in
                HAZARD_SWIM_EGRESS_PLANNER_HANDOFF_OUTCOME_COUNTERS)
            if outcomes != bot["hazard_swim_egress_forced_replans_exact"]:
                raise QualityError(
                    f"{run_path}: final planner-handoff outcomes must partition forced replans for {identity}")
        if not any(bot.get(name) is None for name in HAZARD_RESIDENCE_COUNTERS):
            terminals = sum(bot[name] for name in HAZARD_RESIDENCE_COUNTERS[1:6])
            if terminals != bot["hazard_residence_episodes_exact"]:
                raise QualityError(
                    f"{run_path}: final hazard-residence outcomes must partition episodes for {identity}")
    completion = summary["status"] == "complete" and summary["exit_code"] == 0
    metrics = _run_metrics(bots, completion)
    timing = summary["ai_frame_timing"]
    metrics["ai_frame_p95_ms"] = (
        timing["p95_microseconds"] / 1000.0
        if completion and timing is not None and timing["sample_count"] == summary["ticks"]
        and timing["p95_microseconds"] is not None else None)
    return {
        "path": str(run_path),
        "metadata": metadata,
        "initial_layout": initial_layout,
        "variant": metadata["variant"] if metadata else run_path.name,
        "config": {
            "url": manifest["url"], "seed": manifest["seed"], "max_ticks": manifest["max_ticks"],
            "fixed_delta": manifest["fixed_delta"], "difficulty": manifest["difficulty"],
            "map": summary["map"], "initial_bot_count": len(events[0]["bots"]),
            "requested_roster": manifest["requested_roster"],
            "harmful_zone_escape_enabled": manifest["harmful_zone_escape_enabled"],
            "walking_preflight_positive_dps_veto_enabled": (
                manifest["walking_preflight_positive_dps_veto_enabled"]),
            "hazard_swim_egress_enabled": manifest["hazard_swim_egress_enabled"],
            "hazard_swim_egress_live_enabled": manifest["hazard_swim_egress_live_enabled"],
            "failed_navigation_avoidance_enabled": (
                manifest["failed_navigation_avoidance_enabled"]),
            "falling_hazard_recovery_enabled": manifest["falling_hazard_recovery_enabled"],
            "falling_hazard_recovery_live_enabled": (
                manifest["falling_hazard_recovery_live_enabled"]),
            "targetless_move_to_timeout_enabled": (
                manifest["targetless_move_to_timeout_enabled"]),
            "direct_actor_move_toward_timeout_enabled": (
                manifest["direct_actor_move_toward_timeout_enabled"]),
            "native_path_commit_observer_enabled": (
                manifest["native_path_commit_observer_enabled"]),
            "reachspec_capability_observer_enabled": (
                manifest["reachspec_capability_observer_enabled"]),
            "pawn_vision_cone_enabled": manifest["pawn_vision_cone_enabled"],
            "pawn_vision_observer_enabled": manifest["pawn_vision_observer_enabled"],
            "shadow_policy_set": manifest["shadow_policy_set"],
            "death_attribution_recent_window_seconds": (
                manifest["death_attribution_recent_window_seconds"]),
            "suicides_exact_semantics": manifest["suicides_exact_semantics"],
            "start_layout": {
                "id": metadata["start_layout_id"] if metadata else None,
                "expected_fingerprint": (
                    metadata["expected_initial_layout_fingerprint"] if metadata else None),
                "observed_fingerprint": initial_layout["fingerprint"] if initial_layout else None,
            },
        },
        "result": summary,
        "metrics": metrics,
        "bots": [bots[key] for key in sorted(bots)],
        "validation": {
            "status": "passed",
            "telemetry_events": len(events),
            "optional_telemetry_fields": sorted(
                name for name in (
                    OPTIONAL_CUMULATIVE_METRICS + OPTIONAL_STATIC_METRICS
                    + OPTIONAL_DIAGNOSTIC_FIELDS)
                if any(name in bot for event in events for bot in event["bots"])),
        },
    }


def _numeric(value: Any) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    result = float(value)
    return result if math.isfinite(result) else None


def _describe(values: Iterable[float]) -> dict[str, Any]:
    materialized = list(values)
    return {
        "count": len(materialized),
        "mean": statistics.fmean(materialized) if materialized else None,
        "median": statistics.median(materialized) if materialized else None,
        "minimum": min(materialized) if materialized else None,
        "maximum": max(materialized) if materialized else None,
    }


def aggregate_variants(runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for run in runs:
        grouped[run["variant"]].append(run)
    result = []
    for variant, group in sorted(grouped.items()):
        metrics = {}
        for name in METRIC_DIRECTIONS:
            values = [numeric for run in group if (numeric := _numeric(run["metrics"].get(name))) is not None]
            metrics[name] = _describe(values)
        result.append({"variant": variant, "runs": len(group), "metrics": metrics})
    return result


def _comparison_signature(run: dict[str, Any]) -> tuple[Any, ...]:
    config = run["config"]
    base = tuple(config[name] for name in (
        "url", "seed", "max_ticks", "fixed_delta", "difficulty", "map", "initial_bot_count"))
    roster = config["requested_roster"]
    roster_signature = None if roster is None else tuple(
        (entry["roster_index"], entry["requested_name"], entry["external_skill"], entry["identity_fragment"])
        for entry in roster)
    return (*base, roster_signature)


def paired_comparisons(runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    paired: dict[str, dict[str, dict[str, Any]]] = defaultdict(dict)
    for run in runs:
        metadata = run["metadata"]
        if not metadata or metadata["pair_id"] is None:
            continue
        pair_id = metadata["pair_id"]
        role = metadata["comparison_role"]
        if role in paired[pair_id]:
            raise QualityError(f"pair {pair_id!r} has more than one {role} run")
        paired[pair_id][role] = run
    comparisons: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for pair_id, roles in sorted(paired.items()):
        if set(roles) != {"baseline", "candidate"}:
            raise QualityError(f"pair {pair_id!r} must contain exactly one baseline and one candidate")
        baseline, candidate = roles["baseline"], roles["candidate"]
        if _comparison_signature(baseline) != _comparison_signature(candidate):
            raise QualityError(f"pair {pair_id!r} has incomparable run configurations")
        baseline_layout = baseline["config"]["start_layout"]
        candidate_layout = candidate["config"]["start_layout"]
        if baseline_layout != candidate_layout:
            raise QualityError(f"pair {pair_id!r} has mismatched declared or observed start layouts")
        deltas = {}
        for name in METRIC_DIRECTIONS:
            left, right = _numeric(baseline["metrics"].get(name)), _numeric(candidate["metrics"].get(name))
            deltas[name] = right - left if left is not None and right is not None else None
        comparisons[(baseline["variant"], candidate["variant"])].append({
            "pair_id": pair_id,
            "baseline_path": baseline["path"],
            "candidate_path": candidate["path"],
            "candidate_minus_baseline": deltas,
        })
    result = []
    for (baseline_variant, candidate_variant), pairs in sorted(comparisons.items()):
        metrics = {}
        for name, direction in METRIC_DIRECTIONS.items():
            values = [pair["candidate_minus_baseline"][name] for pair in pairs
                      if pair["candidate_minus_baseline"][name] is not None]
            preferred = [value if direction == "higher" else -value for value in values] if direction else []
            metrics[name] = {
                "preferred_direction": direction,
                "candidate_minus_baseline": _describe(values),
                "candidate_wins": sum(value > 0 for value in preferred) if direction else None,
                "ties": sum(value == 0 for value in preferred) if direction else None,
                "candidate_losses": sum(value < 0 for value in preferred) if direction else None,
            }
        result.append({
            "baseline_variant": baseline_variant,
            "candidate_variant": candidate_variant,
            "pair_count": len(pairs),
            "metrics": metrics,
            "pairs": pairs,
        })
    return result


def analyze(paths: list[Path]) -> dict[str, Any]:
    if not paths:
        raise QualityError("at least one run directory is required")
    resolved = [path.resolve() for path in paths]
    if len(resolved) != len(set(resolved)):
        raise QualityError("the same run directory was provided more than once")
    runs = [analyze_run(path) for path in resolved]
    return {
        "schema": REPORT_SCHEMA,
        "tool": {"name": Path(__file__).name, "version": TOOL_VERSION},
        "thresholds": {
            "movement_distance_per_sample": DISTANCE_EPSILON,
            "stuck_window_seconds": STUCK_WINDOW_SECONDS,
        },
        "runs": runs,
        "variant_aggregates": aggregate_variants(runs),
        "paired_comparisons": paired_comparisons(runs),
        "metric_availability": {
            "available": list(METRIC_DIRECTIONS),
            "optional_counter_metrics_present": {
                name: any(run["metrics"].get(name) is not None for run in runs)
                for name in OPTIONAL_CUMULATIVE_METRICS + OPTIONAL_STATIC_METRICS
            },
            "death_attribution_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in DEATH_ATTRIBUTION_COUNTERS
            ),
            "damage_attribution_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in DAMAGE_COUNTERS
            ),
            "falling_seam_shadow_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in FALLING_SEAM_SHADOW_COUNTERS
            ),
            "falling_seam_detailed_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in FALLING_SEAM_DETAILED_COUNTERS
            ),
            "walking_step_preflight_shadow_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in WALKING_STEP_PREFLIGHT_COUNTERS
            ),
            "falling_parity_shadow_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in FALLING_PARITY_COUNTERS
            ),
            "vertical_pain_column_shadow_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in VERTICAL_PAIN_COLUMN_LEGACY_COUNTERS
            ),
            "unavailable_until_telemetry_is_extended": FUTURE_METRICS,
            "composite_quality_score": None,
        },
        "interpretation": (
            "Telemetry-v2 combat counters come from authoritative Killed and HitWall script-call boundaries. "
            "suicides_exact retains legacy scoreboard self-or-nonplayer-killer semantics. Optional causal death "
            "attribution is a complete monotonic partition of deaths_exact; enemy and momentum environmental "
            "contribution fields remain explicitly labeled proxies. "
            "optional pain-ledge and wall-adjust counters are validated as monotonic and reported when present. "
            "Optional move-stall detections, recovery counters, and cumulative eligible seconds are validated as "
            "a legacy, aggregate-recovery, or attributed-recovery complete monotonic group and reported when present. "
            "Optional failed-navigation activations, safeguard suppressions, and route-penalty applications are "
            "validated as a complete monotonic group and reported when present. "
            "Optional confirmed pickups are ownership-transfer observations; their categories partition only "
            "confirmed pickups, while consumed source items without a confirmed transfer remain a separate, "
            "neutral diagnostic. Optional navigation coverage reports per-bot terminal visited/catalog node "
            "counts and their weighted aggregate fraction, plus a shared terminal union count when every bot "
            "reports the same catalog; these are observations, not gameplay-quality scores. "
            "Optional falling-seam shadow v1 rollups and detailed v2 episode, geometry, and candidate outcome "
            "counters are validated as complete monotonic groups and reported when present; they describe a "
            "read-only policy probe and do not prove that any movement was applied. "
            "Optional walking-step preflight observations, decisions, debounced authorizable episodes, and exact "
            "reason counters are validated as a complete monotonic partition; this observer does not veto movement. "
            "Optional realized falling-parity episodes and step outcomes are validated as a complete monotonic "
            "partition with strictly correlated sampled records; callback barriers, continuity losses, and unknown "
            "steps are distinct from matches and mismatches. Optional "
            "vertical pain-column episode outcomes are validated as an exclusive partition; precision and recall "
            "exclude ambiguous or unknown episodes. The current vertical pain-column group additionally validates "
            "strict start/terminal/capacity diagnostics, opaque deterministic zone identities, lifecycle ordering, "
            "and exact record-overflow reconciliation while retaining legacy counter-only telemetry-v2 support. "
            "Physics, latent-action, acceleration, destination, move-timer, and move-target diagnostics are "
            "validated when present and remain available in the source event stream. "
            "PRI score/deaths are sampled persistent game counters. Hazard-exposed death and movement-intent "
            "stuck values remain explicitly labeled proxies. Distance and activity have no preferred direction. "
            "No composite quality score is emitted because damage attribution, accuracy, objectives, and calibrated "
            "opponent strength are still unavailable. Telemetry-v1 inputs remain supported with v2-only metrics null."
        ),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runs", nargs="+", help="Run directories containing manifest.json, events.jsonl, and summary.json")
    parser.add_argument("--output", required=True, help="New JSON report path")
    args = parser.parse_args(argv)
    output = Path(args.output).resolve()
    if output.exists():
        parser.error(f"output already exists: {output}")
    try:
        report = analyze([Path(value) for value in args.runs])
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    except (QualityError, OSError) as exc:
        parser.error(str(exc))
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
