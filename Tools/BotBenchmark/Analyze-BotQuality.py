#!/usr/bin/env python3
"""Validate unified bot telemetry and report the quality signals it supports."""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


MANIFEST_SCHEMA = "surreal-bot-benchmark-manifest-v1"
MANIFEST_SCHEMA_V2 = "surreal-bot-benchmark-manifest-v2"
TELEMETRY_SCHEMA = "surreal-bot-benchmark-telemetry-v1"
TELEMETRY_SCHEMA_V2 = "surreal-bot-benchmark-telemetry-v2"
SUMMARY_SCHEMA = "surreal-bot-benchmark-summary-v1"
SUMMARY_SCHEMA_V2 = "surreal-bot-benchmark-summary-v2"
METADATA_SCHEMA = "surreal-bot-quality-run-metadata-v1"
REPORT_SCHEMA = "surreal-bot-quality-analysis-v1"
TOOL_VERSION = 17

DISTANCE_EPSILON = 0.25
STUCK_WINDOW_SECONDS = 2.0

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
    "move_stall_eligible_seconds": None,
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
}

FUTURE_METRICS = {
    "damage_dealt": "Attributed damage events are required.",
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
METRIC_DIRECTIONS.update({
    name: None for name in (
        WALKING_STEP_PREFLIGHT_COUNTERS + FALLING_PARITY_COUNTERS
        + VERTICAL_PAIN_COLUMN_COUNTERS)
})
OPTIONAL_EXACT_COUNTERS = (
    PAIN_LEDGE_EXACT_COUNTERS + WALL_ADJUST_EXACT_COUNTERS + MOVE_STALL_EXACT_COUNTERS
    + FAILED_NAVIGATION_EXACT_COUNTERS + HARMFUL_ZONE_ESCAPE_EXACT_COUNTERS
    + DEATH_ATTRIBUTION_COUNTERS
    + FALLING_SEAM_SHADOW_COUNTERS + FALLING_SEAM_DETAILED_COUNTERS
    + WALKING_STEP_PREFLIGHT_COUNTERS + FALLING_PARITY_COUNTERS
    + VERTICAL_PAIN_COLUMN_COUNTERS
)
OPTIONAL_CUMULATIVE_NUMBERS = ("move_stall_eligible_seconds",)
OPTIONAL_CUMULATIVE_METRICS = OPTIONAL_EXACT_COUNTERS + OPTIONAL_CUMULATIVE_NUMBERS
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
OPTIONAL_DIAGNOSTIC_FIELDS = (
    "physics_mode", "latent_action", "acceleration", "destination", "move_timer",
    "move_target_identity", "move_target_name", "walking_step_preflight_diagnostics",
    "falling_parity_realized_records", "vertical_pain_column_diagnostics",
)
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
WALKING_STEP_PREFLIGHT_PHASES = {
    "precommit_provisional", "post_mayfall_confirmation",
}
WALKING_STEP_PREFLIGHT_TRANSITIONS = {
    "abort", "restore_grounded", "begin_falling", "post_callback_evidence_changed",
    "post_callback_forecast_rejected",
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
        "landing_normal", "landing_zone", "hit_fractions",
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
        "hit_fractions": fractions,
    }


def _walking_step_preflight_diagnostic(value: Any, context: str) -> dict[str, Any]:
    fields = _exact_object(value, context, {
        "source_pawn_actor", "sequence", "life_generation", "invocation_token",
        "walking_iteration", "phase",
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
    return {
        "source_pawn_actor": _string(fields, "source_pawn_actor", context, nonempty=True),
        "sequence": _integer(fields.get("sequence"), f"{context}.sequence", minimum=0),
        "life_generation": _integer(
            fields.get("life_generation"), f"{context}.life_generation", minimum=0),
        "invocation_token": _integer(
            fields.get("invocation_token"), f"{context}.invocation_token", minimum=0),
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
        "fall_forecast": _walking_step_preflight_forecast(
            fields.get("fall_forecast"), f"{context}.fall_forecast"),
    }


def _walking_step_preflight_diagnostics(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        raise QualityError(f"{context} must be an array")
    return [
        _walking_step_preflight_diagnostic(item, f"{context}[{index}]")
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
        "elapsed_horizon", "precharged_elapsed",
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
        "actual_trajectory_unknown", "landing_collision",
    }
    capacity_fields = common | {"attempted_source"}
    expected_fields = {
        "start": start_fields,
        "terminal": terminal_fields,
        "generation_capacity_exceeded": capacity_fields,
    }.get(kind)
    if expected_fields is None:
        raise QualityError(f"{context}.kind is not recognized")
    fields = _exact_object(raw, context, expected_fields)
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
                            "elapsed_horizon",
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
        "origin", "predicted_unsupported_endpoint", "semantic_target",
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


def _load_json(path: Path, context: str) -> dict[str, Any]:
    try:
        return _object(json.loads(path.read_text(encoding="utf-8-sig")), context)
    except FileNotFoundError as exc:
        raise QualityError(f"missing {context}: {path}") from exc
    except json.JSONDecodeError as exc:
        raise QualityError(f"invalid {context} JSON at {path}: {exc}") from exc


def _config_id(url: str, seed: int, max_ticks: int, fixed_delta: float, difficulty: int,
               bot_count: int | None = None, requested_roster: list[dict[str, Any]] | None = None,
               harmful_zone_escape_enabled: bool | None = None) -> str:
    canonical_text = (
        f"url={url}\nseed={seed}\nmax_ticks={max_ticks}\n"
        f"fixed_delta={fixed_delta:.9f}\ndifficulty={difficulty}\n"
    )
    if bot_count is not None:
        canonical_text += f"bot_count={bot_count}\n"
        if harmful_zone_escape_enabled is not None:
            canonical_text += "harmful_zone_escape_enabled=" + (
                "1\n" if harmful_zone_escape_enabled else "0\n")
        assert requested_roster is not None
        canonical_text += "".join(f"roster={entry['identity_fragment']}\n" for entry in requested_roster)
    canonical = canonical_text.encode("utf-8")
    digest = 1469598103934665603
    for byte in canonical:
        digest ^= byte
        digest = (digest * 1099511628211) & ((1 << 64) - 1)
    return f"fnv1a64:{digest:016x}"


def _close(left: float, right: float) -> bool:
    return math.isclose(left, right, rel_tol=1e-7, abs_tol=1e-8)


def _ascii_lower(value: str) -> str:
    return "".join(chr(ord(character) + 32) if "A" <= character <= "Z" else character
                   for character in value)


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
    if schema not in (MANIFEST_SCHEMA, MANIFEST_SCHEMA_V2):
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
    if schema == MANIFEST_SCHEMA_V2:
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
    expected_id = _config_id(url, seed, max_ticks, fixed_delta, difficulty, bot_count,
                             requested_roster, harmful_zone_escape_enabled)
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
        "schema": schema,
        "bot_count": bot_count,
        "requested_roster": requested_roster,
        "death_attribution_recent_window_seconds": death_attribution_recent_window_seconds,
        "suicides_exact_semantics": suicides_exact_semantics,
        "harmful_zone_escape_enabled": harmful_zone_escape_enabled,
    }


def _validate_bot(raw: Any, context: str, schema: str) -> dict[str, Any]:
    bot = _object(raw, context)
    if schema != TELEMETRY_SCHEMA_V2 and any(name in bot for name in (
            "walking_step_preflight_diagnostics", "falling_parity_realized_records",
            "vertical_pain_column_diagnostics")):
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
        for label, names in (
                ("pain ledge", PAIN_LEDGE_EXACT_COUNTERS),
                ("wall adjust", WALL_ADJUST_EXACT_COUNTERS),
                ("failed navigation", FAILED_NAVIGATION_EXACT_COUNTERS),
                ("harmful-zone escape", HARMFUL_ZONE_ESCAPE_EXACT_COUNTERS),
                ("death attribution", DEATH_ATTRIBUTION_COUNTERS),
                ("falling seam shadow v1", FALLING_SEAM_SHADOW_COUNTERS),
                ("falling seam shadow detailed v2", FALLING_SEAM_DETAILED_COUNTERS),
                ("walking step preflight shadow", WALKING_STEP_PREFLIGHT_COUNTERS)):
            present = [name for name in names if name in result]
            if present and len(present) != len(names):
                raise QualityError(f"{context}: {label} counters must be provided as a complete group")
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
            + MOVE_STALL_ATTRIBUTED_TELEMETRY_GROUP)
        stall_present = {name for name in stall_field_names if name in result}
        valid_stall_groups = (
            set(), set(MOVE_STALL_LEGACY_TELEMETRY_GROUP), set(MOVE_STALL_TELEMETRY_GROUP),
            set(MOVE_STALL_ATTRIBUTED_TELEMETRY_GROUP),
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
        if "direct_self_kills" in result:
            primary_attributions = sum(result[name] for name in DEATH_ATTRIBUTION_COUNTERS[:-1])
            if primary_attributions != result["deaths_exact"]:
                raise QualityError(
                    f"{context}: primary death attribution counters do not equal deaths_exact")
            if result["recent_enemy_momentum_contributed_environmental_deaths_proxy"] > \
                    result["recent_enemy_contributed_environmental_deaths_proxy"]:
                raise QualityError(
                    f"{context}: momentum-contributed deaths exceed enemy-contributed deaths")
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
        if "walking_step_preflight_diagnostics" in bot:
            if "walking_step_preflight_observations_exact" not in result:
                raise QualityError(
                    f"{context}: walking step preflight diagnostics require the complete counter group")
            result["walking_step_preflight_diagnostics"] = \
                _walking_step_preflight_diagnostics(
                    bot.get("walking_step_preflight_diagnostics"),
                    f"{context}.walking_step_preflight_diagnostics")
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
        if "move_stall_navigation_forced_replans_exact" in result:
            attributed_replans = (
                result["move_stall_navigation_forced_replans_exact"]
                + result["move_stall_targetless_move_to_timeouts_exact"])
            if attributed_replans != result["move_stall_forced_replans_exact"]:
                raise QualityError(
                    f"{context}: attributed move stall recoveries do not equal forced replans")
            if result["move_stall_episode_resets_exact"] > result["move_stall_detections_exact"]:
                raise QualityError(f"{context}: move stall episode resets exceed detections")
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
                "bots": [_validate_bot(item, f"{context}.bots[{index}]", schema)
                         for index, item in enumerate(raw.get("bots", []))],
            }
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
    if events[0]["schema"] == TELEMETRY_SCHEMA_V2:
        previous: dict[str, dict[str, Any]] = {}
        counters = CORE_EXACT_COUNTERS + OPTIONAL_CUMULATIVE_METRICS
        optional_fields = OPTIONAL_CUMULATIVE_METRICS + OPTIONAL_DIAGNOSTIC_FIELDS
        optional_presence: set[str] | None = None
        for event in events:
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
                if "wall_adjust_repeats_exact" in bot and \
                        bot["wall_adjust_repeats_exact"] > bot["wall_adjust_calls_exact"]:
                    raise QualityError(f"{path}: wall adjust repeats exceed calls for {bot['identity']}")
                if "wall_adjust_recovery_successes_exact" in bot and \
                        bot["wall_adjust_recovery_successes_exact"] > bot["wall_adjust_recovery_attempts_exact"]:
                    raise QualityError(f"{path}: wall adjust successes exceed attempts for {bot['identity']}")
                previous[bot["identity"]] = bot
        if optional_presence and "walking_step_preflight_diagnostics" in optional_presence:
            _validate_walking_step_preflight_diagnostic_stream(events, path)
        if optional_presence and "falling_parity_realized_records" in optional_presence:
            _validate_falling_parity_realized_record_stream(events, path)
        if optional_presence and "vertical_pain_column_diagnostics" in optional_presence:
            _validate_vertical_pain_column_diagnostic_stream(events, path)
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


def _validate_summary(path: Path, manifest: dict[str, Any], events: list[dict[str, Any]]) -> dict[str, Any]:
    raw = _load_json(path, "summary")
    expected_schema = SUMMARY_SCHEMA_V2 if manifest["schema"] == MANIFEST_SCHEMA_V2 else SUMMARY_SCHEMA
    if raw.get("schema") != expected_schema:
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
    requested_roster = None
    actual_roster = None
    if expected_schema == SUMMARY_SCHEMA_V2:
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
    summary_strings = ("game", "version") if expected_schema == SUMMARY_SCHEMA_V2 else (
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
    return {
        "status": status,
        "exit_code": exit_code,
        "ticks": ticks,
        "simulated_seconds": seconds,
        "map": map_name,
        "failure_reason": failure_reason,
        "requested_roster": requested_roster,
        "actual_roster": actual_roster,
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
    return {"variant": variant, "pair_id": pair_id, "comparison_role": role}


def _distance(left: dict[str, float], right: dict[str, float]) -> float:
    return math.sqrt(sum((right[axis] - left[axis]) ** 2 for axis in "xyz"))


def _counter_fraction(numerator: int | float | None,
                      denominator: int | float | None) -> float | None:
    if numerator is None or denominator is None or denominator <= 0:
        return None
    return numerator / denominator


def _bot_metrics(events: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
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
            **exact,
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
        "health_loss_observed": sum(bot["health_loss_observed"] for bot in values),
        "minimum_health_observed": min((bot["minimum_health_observed"] for bot in values), default=None),
        "survived_to_final_sample": all(survival) if survival and all(item is not None for item in survival) else None,
        "completion": completion,
    }
    result.update({name: sum_available(name) for name in OPTIONAL_CUMULATIVE_METRICS})
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
    bots = _bot_metrics(events)
    completion = summary["status"] == "complete" and summary["exit_code"] == 0
    metrics = _run_metrics(bots, completion)
    return {
        "path": str(run_path),
        "metadata": metadata,
        "variant": metadata["variant"] if metadata else run_path.name,
        "config": {
            "url": manifest["url"], "seed": manifest["seed"], "max_ticks": manifest["max_ticks"],
            "fixed_delta": manifest["fixed_delta"], "difficulty": manifest["difficulty"],
            "map": summary["map"], "initial_bot_count": len(events[0]["bots"]),
            "requested_roster": manifest["requested_roster"],
            "harmful_zone_escape_enabled": manifest["harmful_zone_escape_enabled"],
            "death_attribution_recent_window_seconds": (
                manifest["death_attribution_recent_window_seconds"]),
            "suicides_exact_semantics": manifest["suicides_exact_semantics"],
        },
        "result": summary,
        "metrics": metrics,
        "bots": [bots[key] for key in sorted(bots)],
        "validation": {
            "status": "passed",
            "telemetry_events": len(events),
            "optional_telemetry_fields": sorted(
                name for name in OPTIONAL_CUMULATIVE_METRICS + OPTIONAL_DIAGNOSTIC_FIELDS
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
                for name in OPTIONAL_CUMULATIVE_METRICS
            },
            "death_attribution_metrics_present": all(
                any(run["metrics"].get(name) is not None for run in runs)
                for name in DEATH_ATTRIBUTION_COUNTERS
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
