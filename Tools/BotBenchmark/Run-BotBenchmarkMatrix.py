#!/usr/bin/env python3
"""Run a deterministic matrix against separate unified-engine bot variants."""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import importlib.util
import json
import math
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


MATRIX_SCHEMA = "surreal-bot-benchmark-matrix-v1"
RESULT_SCHEMA = "surreal-bot-benchmark-matrix-results-v1"
METADATA_SCHEMA = "surreal-bot-quality-run-metadata-v1"
INVOCATION_SCHEMA = "surreal-bot-benchmark-invocation-v1"
PROVENANCE_SCHEMA = "surreal-bot-benchmark-provenance-v1"
TOOL_VERSION = 7
MAX_CONCURRENCY = 64
MAX_BOT_COUNT = 16


class MatrixError(ValueError):
    pass


@dataclass(frozen=True)
class Variant:
    id: str
    executable: Path
    comparison_role: str | None
    build_preset: str | None = None
    hazard_swim_egress_enabled: bool = False
    hazard_swim_egress_live_enabled: bool = False
    failed_navigation_avoidance_enabled: bool = False
    falling_hazard_recovery_enabled: bool = False
    falling_hazard_recovery_live_enabled: bool = False
    targetless_move_to_timeout_enabled: bool = False
    direct_actor_move_toward_timeout_enabled: bool = False
    target_selection_observer_enabled: bool = False
    can_fire_at_enemy_observer_enabled: bool = False
    inventory_direct_reach_support_observer_enabled: bool = False
    native_path_commit_observer_enabled: bool = False
    pawn_vision_cone_enabled: bool = False
    pawn_vision_observer_enabled: bool = False
    vector_nonfinite_observer_enabled: bool = False
    finite_move_command_guard_enabled: bool = False
    pick_reg_destination_zero_divide_guard_enabled: bool = False
    walking_hitwall_minhitwall_candidate_enabled: bool = False
    movement_command_provenance_observer_enabled: bool = False
    hazard_residence_command_transition_ledger_observer_enabled: bool = False
    hazard_residence_preentry_causal_slice_observer_enabled: bool = False


@dataclass(frozen=True)
class StartLayout:
    id: str
    seed: int
    expected_fingerprint: str


@dataclass(frozen=True)
class MatrixConfig:
    source: Path
    game_family: str
    game_root: Path
    variants: tuple[Variant, ...]
    map_urls: tuple[str, ...]
    seeds: tuple[int, ...]
    start_layouts: tuple[StartLayout, ...] | None
    max_ticks: int
    fixed_delta: float
    difficulty: int
    bot_count: int
    per_bot_skills: tuple[int, ...] | None
    requested_names: tuple[str, ...] | None
    harmful_zone_escape_enabled: bool
    walking_preflight_positive_dps_veto_enabled: bool
    concurrency: int
    timeout_seconds: float
    repetitions: int
    provenance_mode: str
    evidence_classification: str | None
    environment_allowlist: tuple[str, ...]
    game_manifest: Path | None


@dataclass(frozen=True)
class MatrixCase:
    ordinal: int
    run_id: str
    pair_id: str | None
    variant: Variant
    map_url: str
    seed: int
    repetition: int
    start_layout: StartLayout | None


@dataclass(frozen=True)
class LaunchResult:
    exit_code: int | None
    timed_out: bool
    wall_seconds: float
    error: str | None = None


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise MatrixError(f"{context} must be a JSON object")
    return value


def _string(fields: dict[str, Any], name: str, context: str) -> str:
    value = fields.get(name)
    if not isinstance(value, str) or not value:
        raise MatrixError(f"{context}.{name} must be a non-empty string")
    return value


def _integer(value: Any, context: str, minimum: int, maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise MatrixError(f"{context} must be an integer")
    if value < minimum or (maximum is not None and value > maximum):
        end = f" and {maximum}" if maximum is not None else ""
        raise MatrixError(f"{context} must be between {minimum}{end}")
    return value


def _number(value: Any, context: str, minimum: float, maximum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise MatrixError(f"{context} must be a finite number")
    result = float(value)
    if not math.isfinite(result) or result < minimum or (maximum is not None and result > maximum):
        end = f" and {maximum}" if maximum is not None else ""
        raise MatrixError(f"{context} must be finite and between {minimum}{end}")
    return result


def _ascii_lower(value: str) -> str:
    return "".join(chr(ord(character) + 32) if "A" <= character <= "Z" else character
                   for character in value)


def _resolve_existing(path_text: str, base: Path, context: str, *, directory: bool) -> Path:
    path = Path(path_text)
    if not path.is_absolute():
        path = base / path
    path = path.resolve()
    valid = path.is_dir() if directory else path.is_file()
    if not valid:
        kind = "directory" if directory else "file"
        raise MatrixError(f"{context} is not an existing {kind}: {path}")
    return path


def _optional_nonempty_string(fields: dict[str, Any], name: str, context: str) -> str | None:
    value = fields.get(name)
    if value is None:
        return None
    if not isinstance(value, str) or not value or value != value.strip():
        raise MatrixError(f"{context}.{name} must be a non-empty trimmed string when present")
    return value


def _layout_fingerprint(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.startswith("sha256:"):
        raise MatrixError(f"{context} must be a sha256: fingerprint")
    digest = value.removeprefix("sha256:")
    if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
        raise MatrixError(f"{context} must be a lowercase sha256: fingerprint")
    return value


def load_matrix(path: Path) -> MatrixConfig:
    source = path.resolve()
    try:
        raw = _object(json.loads(source.read_text(encoding="utf-8-sig")), "matrix manifest")
    except FileNotFoundError as exc:
        raise MatrixError(f"matrix manifest does not exist: {source}") from exc
    except json.JSONDecodeError as exc:
        raise MatrixError(f"invalid matrix manifest JSON: {exc}") from exc
    if raw.get("schema") != MATRIX_SCHEMA:
        raise MatrixError(f"unsupported matrix schema {raw.get('schema')!r}")

    game = _object(raw.get("game"), "matrix.game")
    game_family = _string(game, "family", "matrix.game")
    game_root = _resolve_existing(_string(game, "root", "matrix.game"), source.parent, "matrix.game.root",
                                  directory=True)
    game_manifest_text = _optional_nonempty_string(game, "manifest", "matrix.game")
    game_manifest = None
    if game_manifest_text is not None:
        game_manifest = _resolve_existing(
            game_manifest_text, source.parent, "matrix.game.manifest", directory=False)

    provenance_raw = raw.get("provenance")
    provenance_mode = "development"
    evidence_classification = None
    environment_allowlist: tuple[str, ...] = ()
    environment_allowlist_present = False
    if provenance_raw is not None:
        provenance = _object(provenance_raw, "matrix.provenance")
        unknown_provenance = sorted(
            set(provenance) - {"mode", "classification", "environment_allowlist"})
        if unknown_provenance:
            raise MatrixError(
                "matrix.provenance contains unknown fields: " + ", ".join(unknown_provenance))
        provenance_mode = provenance.get("mode", "development")
        if provenance_mode not in ("development", "release"):
            raise MatrixError("matrix.provenance.mode must be development or release")
        evidence_classification = _optional_nonempty_string(
            provenance, "classification", "matrix.provenance")
        if evidence_classification is not None and evidence_classification not in ("tuning", "heldout"):
            raise MatrixError("matrix.provenance.classification must be tuning or heldout")
        environment_allowlist_present = "environment_allowlist" in provenance
        environment_raw = provenance.get("environment_allowlist", [])
        if (not isinstance(environment_raw, list)
                or any(not isinstance(value, str) or not value or value != value.strip()
                       or "=" in value or "\x00" in value for value in environment_raw)):
            raise MatrixError(
                "matrix.provenance.environment_allowlist must be an array of non-empty environment names")
        if len(environment_raw) != len(set(environment_raw)):
            raise MatrixError("matrix.provenance.environment_allowlist contains duplicates")
        environment_allowlist = tuple(environment_raw)

    variants_raw = raw.get("variants")
    if not isinstance(variants_raw, list) or not variants_raw:
        raise MatrixError("matrix.variants must be a non-empty array")
    variants: list[Variant] = []
    for index, item in enumerate(variants_raw):
        fields = _object(item, f"matrix.variants[{index}]")
        variant_id = _string(fields, "id", f"matrix.variants[{index}]")
        executable = _resolve_existing(
            _string(fields, "executable", f"matrix.variants[{index}]"), source.parent,
            f"matrix.variants[{index}].executable", directory=False)
        role = fields.get("comparison_role")
        if role is not None and role not in ("baseline", "candidate"):
            raise MatrixError(f"matrix.variants[{index}].comparison_role must be baseline or candidate")
        build_preset = _optional_nonempty_string(fields, "build_preset", f"matrix.variants[{index}]")
        hazard_swim_egress_enabled = fields.get("hazard_swim_egress_enabled", False)
        if not isinstance(hazard_swim_egress_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].hazard_swim_egress_enabled must be a boolean")
        hazard_swim_egress_live_enabled = fields.get("hazard_swim_egress_live_enabled", False)
        if not isinstance(hazard_swim_egress_live_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].hazard_swim_egress_live_enabled must be a boolean")
        failed_navigation_avoidance_enabled = fields.get("failed_navigation_avoidance_enabled", False)
        if not isinstance(failed_navigation_avoidance_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].failed_navigation_avoidance_enabled must be a boolean")
        falling_hazard_recovery_enabled = fields.get("falling_hazard_recovery_enabled", False)
        if not isinstance(falling_hazard_recovery_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].falling_hazard_recovery_enabled must be a boolean")
        falling_hazard_recovery_live_enabled = fields.get("falling_hazard_recovery_live_enabled", False)
        if not isinstance(falling_hazard_recovery_live_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].falling_hazard_recovery_live_enabled must be a boolean")
        targetless_move_to_timeout_enabled = fields.get("targetless_move_to_timeout_enabled", False)
        if not isinstance(targetless_move_to_timeout_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].targetless_move_to_timeout_enabled must be a boolean")
        direct_actor_move_toward_timeout_enabled = fields.get(
            "direct_actor_move_toward_timeout_enabled", False)
        if not isinstance(direct_actor_move_toward_timeout_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].direct_actor_move_toward_timeout_enabled must be a boolean")
        target_selection_observer_enabled = fields.get("target_selection_observer_enabled", False)
        if not isinstance(target_selection_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].target_selection_observer_enabled must be a boolean")
        can_fire_at_enemy_observer_enabled = fields.get("can_fire_at_enemy_observer_enabled", False)
        if not isinstance(can_fire_at_enemy_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].can_fire_at_enemy_observer_enabled must be a boolean")
        inventory_direct_reach_support_observer_enabled = fields.get(
            "inventory_direct_reach_support_observer_enabled", False)
        if not isinstance(inventory_direct_reach_support_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].inventory_direct_reach_support_observer_enabled must be a boolean")
        native_path_commit_observer_enabled = fields.get("native_path_commit_observer_enabled", False)
        if not isinstance(native_path_commit_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].native_path_commit_observer_enabled must be a boolean")
        pawn_vision_cone_enabled = fields.get("pawn_vision_cone_enabled", False)
        if not isinstance(pawn_vision_cone_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].pawn_vision_cone_enabled must be a boolean")
        pawn_vision_observer_enabled = fields.get("pawn_vision_observer_enabled", False)
        if not isinstance(pawn_vision_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].pawn_vision_observer_enabled must be a boolean")
        vector_nonfinite_observer_enabled = fields.get("vector_nonfinite_observer_enabled", False)
        if not isinstance(vector_nonfinite_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].vector_nonfinite_observer_enabled must be a boolean")
        finite_move_command_guard_enabled = fields.get("finite_move_command_guard_enabled", False)
        if not isinstance(finite_move_command_guard_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].finite_move_command_guard_enabled must be a boolean")
        pick_reg_destination_zero_divide_guard_enabled = fields.get(
            "pick_reg_destination_zero_divide_guard_enabled", False)
        if not isinstance(pick_reg_destination_zero_divide_guard_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].pick_reg_destination_zero_divide_guard_enabled must be a boolean")
        walking_hitwall_minhitwall_candidate_enabled = fields.get(
            "walking_hitwall_minhitwall_candidate_enabled", False)
        if not isinstance(walking_hitwall_minhitwall_candidate_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].walking_hitwall_minhitwall_candidate_enabled must be a boolean")
        movement_command_provenance_observer_enabled = fields.get(
            "movement_command_provenance_observer_enabled", False)
        if not isinstance(movement_command_provenance_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].movement_command_provenance_observer_enabled must be a boolean")
        if movement_command_provenance_observer_enabled and not native_path_commit_observer_enabled:
            raise MatrixError(
                f"matrix.variants[{index}].movement-command provenance requires native_path_commit_observer_enabled")
        # The ledger consumes the exact movement provenance stream; it cannot be
        # enabled independently without making reconciliation ambiguous.
        hazard_residence_command_transition_ledger_observer_enabled = fields.get(
            "hazard_residence_command_transition_ledger_observer_enabled", False)
        if not isinstance(hazard_residence_command_transition_ledger_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].hazard_residence_command_transition_ledger_observer_enabled must be a boolean")
        if (hazard_residence_command_transition_ledger_observer_enabled
                and not movement_command_provenance_observer_enabled):
            raise MatrixError(
                f"matrix.variants[{index}].hazard-residence command-transition ledger requires movement_command_provenance_observer_enabled")
        hazard_residence_preentry_causal_slice_observer_enabled = fields.get(
            "hazard_residence_preentry_causal_slice_observer_enabled", False)
        if not isinstance(hazard_residence_preentry_causal_slice_observer_enabled, bool):
            raise MatrixError(
                f"matrix.variants[{index}].hazard_residence_preentry_causal_slice_observer_enabled must be a boolean")
        if (hazard_residence_preentry_causal_slice_observer_enabled
                and (not hazard_residence_command_transition_ledger_observer_enabled
                     or not native_path_commit_observer_enabled)):
            raise MatrixError(
                f"matrix.variants[{index}].hazard-residence pre-entry causal slice requires native_path_commit_observer_enabled and hazard_residence_command_transition_ledger_observer_enabled")
        variants.append(Variant(
            variant_id, executable, role, build_preset, hazard_swim_egress_enabled,
            hazard_swim_egress_live_enabled, failed_navigation_avoidance_enabled,
            falling_hazard_recovery_enabled, falling_hazard_recovery_live_enabled,
            targetless_move_to_timeout_enabled, direct_actor_move_toward_timeout_enabled,
            target_selection_observer_enabled, can_fire_at_enemy_observer_enabled,
            inventory_direct_reach_support_observer_enabled,
            native_path_commit_observer_enabled,
            pawn_vision_cone_enabled, pawn_vision_observer_enabled,
            vector_nonfinite_observer_enabled,
            finite_move_command_guard_enabled,
            pick_reg_destination_zero_divide_guard_enabled,
            walking_hitwall_minhitwall_candidate_enabled,
            movement_command_provenance_observer_enabled,
            hazard_residence_command_transition_ledger_observer_enabled,
            hazard_residence_preentry_causal_slice_observer_enabled))
    ids = [variant.id for variant in variants]
    if len(ids) != len(set(ids)):
        raise MatrixError("matrix variant IDs must be unique")
    roles = [variant.comparison_role for variant in variants if variant.comparison_role]
    if roles and sorted(roles) != ["baseline", "candidate"]:
        raise MatrixError("paired comparison requires exactly one baseline and one candidate variant")

    map_urls = raw.get("map_urls")
    if (not isinstance(map_urls, list) or not map_urls
            or any(not isinstance(value, str) or not value for value in map_urls)):
        raise MatrixError("matrix.map_urls must be a non-empty array of non-empty strings")
    if len(map_urls) != len(set(map_urls)):
        raise MatrixError("matrix.map_urls contains duplicates; use repetitions instead")
    start_layouts_raw = raw.get("start_layouts")
    start_layouts: tuple[StartLayout, ...] | None = None
    if start_layouts_raw is not None:
        if "seeds" in raw:
            raise MatrixError("matrix.start_layouts and matrix.seeds are mutually exclusive")
        if not isinstance(start_layouts_raw, list) or not start_layouts_raw:
            raise MatrixError("matrix.start_layouts must be a non-empty array")
        parsed_layouts: list[StartLayout] = []
        for index, item in enumerate(start_layouts_raw):
            fields = _object(item, f"matrix.start_layouts[{index}]")
            unknown = sorted(set(fields) - {"id", "seed", "expected_fingerprint"})
            if unknown:
                raise MatrixError(
                    f"matrix.start_layouts[{index}] contains unknown fields: " + ", ".join(unknown))
            parsed_layouts.append(StartLayout(
                _string(fields, "id", f"matrix.start_layouts[{index}]"),
                _integer(fields.get("seed"), f"matrix.start_layouts[{index}].seed", 0),
                _layout_fingerprint(
                    fields.get("expected_fingerprint"),
                    f"matrix.start_layouts[{index}].expected_fingerprint")))
        layout_ids = [layout.id for layout in parsed_layouts]
        if len(layout_ids) != len(set(layout_ids)):
            raise MatrixError("matrix.start_layouts contains duplicate ids")
        layout_seeds = [layout.seed for layout in parsed_layouts]
        if len(layout_seeds) != len(set(layout_seeds)):
            raise MatrixError("matrix.start_layouts contains duplicate seeds")
        start_layouts = tuple(parsed_layouts)
        parsed_seeds = tuple(layout.seed for layout in start_layouts)
    else:
        seeds = raw.get("seeds")
        if not isinstance(seeds, list) or not seeds:
            raise MatrixError("matrix.seeds must be a non-empty array")
        parsed_seeds = tuple(_integer(value, f"matrix.seeds[{index}]", 0) for index, value in enumerate(seeds))
        if len(parsed_seeds) != len(set(parsed_seeds)):
            raise MatrixError("matrix.seeds contains duplicates; use repetitions instead")

    max_ticks = _integer(raw.get("max_ticks"), "matrix.max_ticks", 1, 10_000_000)
    fixed_delta = _number(raw.get("fixed_delta"), "matrix.fixed_delta", 0.0, 1.0)
    if fixed_delta == 0:
        raise MatrixError("matrix.fixed_delta must be positive")
    difficulty = _integer(raw.get("difficulty"), "matrix.difficulty", 0, 7)
    bot_count = _integer(raw.get("bot_count", 1), "matrix.bot_count", 1, MAX_BOT_COUNT)

    per_bot_skills_raw = raw.get("per_bot_skills")
    per_bot_skills = None
    if per_bot_skills_raw is not None:
        if not isinstance(per_bot_skills_raw, list):
            raise MatrixError("matrix.per_bot_skills must be an array")
        if len(per_bot_skills_raw) != bot_count:
            raise MatrixError("matrix.per_bot_skills count must match matrix.bot_count")
        per_bot_skills = tuple(
            _integer(value, f"matrix.per_bot_skills[{index}]", 0, 7)
            for index, value in enumerate(per_bot_skills_raw))

    requested_names_raw = raw.get("requested_names")
    requested_names = None
    if requested_names_raw is not None:
        if not isinstance(requested_names_raw, list):
            raise MatrixError("matrix.requested_names must be an array")
        if len(requested_names_raw) != bot_count:
            raise MatrixError("matrix.requested_names count must match matrix.bot_count")
        if any(not isinstance(value, str) or not value or value != value.strip() or "," in value
               for value in requested_names_raw):
            raise MatrixError("matrix.requested_names entries must be non-empty trimmed strings without commas")
        normalized_names = [_ascii_lower(value) for value in requested_names_raw]
        if len(normalized_names) != len(set(normalized_names)):
            raise MatrixError("matrix.requested_names entries must be unique case-insensitively")
        requested_names = tuple(requested_names_raw)

    harmful_zone_escape_enabled = raw.get("harmful_zone_escape_enabled", False)
    if not isinstance(harmful_zone_escape_enabled, bool):
        raise MatrixError("matrix.harmful_zone_escape_enabled must be a boolean")
    walking_preflight_positive_dps_veto_enabled = raw.get(
        "walking_preflight_positive_dps_veto_enabled", False)
    if not isinstance(walking_preflight_positive_dps_veto_enabled, bool):
        raise MatrixError("matrix.walking_preflight_positive_dps_veto_enabled must be a boolean")

    concurrency = _integer(raw.get("concurrency", 1), "matrix.concurrency", 1, MAX_CONCURRENCY)
    timeout_seconds = _number(raw.get("timeout_seconds", 120), "matrix.timeout_seconds", 0.0)
    if timeout_seconds == 0:
        raise MatrixError("matrix.timeout_seconds must be positive")
    repetitions = _integer(raw.get("repetitions", 1), "matrix.repetitions", 1, 10_000)
    if provenance_mode == "release":
        if evidence_classification is None:
            raise MatrixError("release provenance requires matrix.provenance.classification")
        if not environment_allowlist_present:
            raise MatrixError("release provenance requires matrix.provenance.environment_allowlist")
        if game_manifest is None:
            raise MatrixError("release provenance requires matrix.game.manifest")
        missing_presets = [variant.id for variant in variants if variant.build_preset is None]
        if missing_presets:
            raise MatrixError(
                "release provenance requires build_preset for variants: " + ", ".join(missing_presets))
    return MatrixConfig(
        source=source,
        game_family=game_family,
        game_root=game_root,
        variants=tuple(variants),
        map_urls=tuple(map_urls),
        seeds=parsed_seeds,
        start_layouts=start_layouts,
        max_ticks=max_ticks,
        fixed_delta=fixed_delta,
        difficulty=difficulty,
        bot_count=bot_count,
        per_bot_skills=per_bot_skills,
        requested_names=requested_names,
        harmful_zone_escape_enabled=harmful_zone_escape_enabled,
        walking_preflight_positive_dps_veto_enabled=walking_preflight_positive_dps_veto_enabled,
        concurrency=concurrency,
        timeout_seconds=timeout_seconds,
        repetitions=repetitions,
        provenance_mode=provenance_mode,
        evidence_classification=evidence_classification,
        environment_allowlist=environment_allowlist,
        game_manifest=game_manifest,
    )


def _slug(value: str, limit: int = 32) -> str:
    result = "".join(character.lower() if character.isalnum() else "-" for character in value)
    result = "-".join(filter(None, result.split("-")))[:limit].rstrip("-")
    return result or "case"


def _digest(fields: list[Any], length: int = 12) -> str:
    canonical = json.dumps(fields, ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:length]


def expand_cases(config: MatrixConfig) -> list[MatrixCase]:
    cases: list[MatrixCase] = []
    paired = any(variant.comparison_role for variant in config.variants)
    ordinal = 0
    for map_index, map_url in enumerate(config.map_urls):
        layouts = config.start_layouts or tuple(None for _ in config.seeds)
        for layout_index, start_layout in enumerate(layouts):
            seed = start_layout.seed if start_layout is not None else config.seeds[layout_index]
            for repetition in range(config.repetitions):
                shared = [config.game_family, map_index, map_url, seed, repetition,
                          config.max_ticks, config.fixed_delta, config.difficulty,
                          config.bot_count, config.per_bot_skills, config.requested_names,
                          config.harmful_zone_escape_enabled,
                          config.walking_preflight_positive_dps_veto_enabled]
                if start_layout is not None:
                    shared.extend((start_layout.id, start_layout.expected_fingerprint))
                pair_id = f"case-{_digest(shared, 16)}" if paired else None
                for variant in config.variants:
                    identity = [*shared, variant.id, str(variant.executable),
                                variant.hazard_swim_egress_enabled,
                                variant.hazard_swim_egress_live_enabled,
                                variant.failed_navigation_avoidance_enabled,
                                variant.falling_hazard_recovery_enabled,
                                 variant.falling_hazard_recovery_live_enabled,
                                 variant.targetless_move_to_timeout_enabled,
                                 variant.direct_actor_move_toward_timeout_enabled,
                                 variant.target_selection_observer_enabled,
                                 variant.can_fire_at_enemy_observer_enabled,
                                  variant.inventory_direct_reach_support_observer_enabled,
                                  variant.native_path_commit_observer_enabled,
                                 variant.pawn_vision_cone_enabled,
                                 variant.pawn_vision_observer_enabled,
                                  variant.vector_nonfinite_observer_enabled,
                                  variant.finite_move_command_guard_enabled,
                                  variant.pick_reg_destination_zero_divide_guard_enabled,
                                  variant.walking_hitwall_minhitwall_candidate_enabled,
                                  variant.movement_command_provenance_observer_enabled,
                                  variant.hazard_residence_command_transition_ledger_observer_enabled,
                                  variant.hazard_residence_preentry_causal_slice_observer_enabled]
                    run_id = (
                        f"{ordinal:06d}-{_slug(variant.id)}-{_slug(map_url)}-"
                        f"s{seed}" + (f"-l{_slug(start_layout.id)}" if start_layout else "") +
                        f"-r{repetition}-{_digest(identity)}"
                    )
                    cases.append(MatrixCase(ordinal, run_id, pair_id if variant.comparison_role else None,
                                            variant, map_url, seed, repetition, start_layout))
                    ordinal += 1
    run_ids = [case.run_id for case in cases]
    if len(run_ids) != len(set(run_ids)):
        raise MatrixError("case expansion produced colliding run IDs")
    return cases


def command_for(config: MatrixConfig, case: MatrixCase, run_directory: Path) -> list[str]:
    command = [
        str(case.variant.executable),
        "--autoplay",
        "--headless-driver=bot-benchmark",
        f"--botbench-url={case.map_url}",
        f"--botbench-output={run_directory}",
        f"--botbench-seed={case.seed}",
        f"--botbench-ticks={config.max_ticks}",
        f"--botbench-fixed-delta={format(config.fixed_delta, '.17g')}",
        f"--botbench-difficulty={config.difficulty}",
        f"--botbench-bots={config.bot_count}",
        "--botbench-harmful-zone-escape=" + (
            "1" if config.harmful_zone_escape_enabled else "0"),
        "--botbench-walking-preflight-positive-dps-veto=" + (
            "1" if config.walking_preflight_positive_dps_veto_enabled else "0"),
        "--botbench-hazard-swim-egress=" + (
            "1" if case.variant.hazard_swim_egress_enabled else "0"),
        "--botbench-hazard-swim-egress-live=" + (
            "1" if case.variant.hazard_swim_egress_live_enabled else "0"),
        "--botbench-failed-navigation-avoidance=" + (
            "1" if case.variant.failed_navigation_avoidance_enabled else "0"),
        "--botbench-falling-hazard-recovery=" + (
            "1" if case.variant.falling_hazard_recovery_enabled else "0"),
        "--botbench-falling-hazard-recovery-live=" + (
            "1" if case.variant.falling_hazard_recovery_live_enabled else "0"),
        "--botbench-targetless-move-to-timeout=" + (
            "1" if case.variant.targetless_move_to_timeout_enabled else "0"),
        "--botbench-direct-actor-move-toward-timeout=" + (
            "1" if case.variant.direct_actor_move_toward_timeout_enabled else "0"),
        "--botbench-target-selection-observer=" + (
            "1" if case.variant.target_selection_observer_enabled else "0"),
        "--botbench-can-fire-at-enemy-observer=" + (
            "1" if case.variant.can_fire_at_enemy_observer_enabled else "0"),
        "--botbench-inventory-direct-reach-support-observer=" + (
            "1" if case.variant.inventory_direct_reach_support_observer_enabled else "0"),
        "--botbench-native-path-commit-observer=" + (
            "1" if case.variant.native_path_commit_observer_enabled else "0"),
        "--botbench-pawn-vision-cone=" + (
            "1" if case.variant.pawn_vision_cone_enabled else "0"),
        "--botbench-pawn-vision-observer=" + (
            "1" if case.variant.pawn_vision_observer_enabled else "0"),
        "--botbench-vector-nonfinite-observer=" + (
            "1" if case.variant.vector_nonfinite_observer_enabled else "0"),
        "--botbench-finite-move-command-guard=" + (
            "1" if case.variant.finite_move_command_guard_enabled else "0"),
        "--botbench-pick-reg-destination-zero-divide-guard=" + (
            "1" if case.variant.pick_reg_destination_zero_divide_guard_enabled else "0"),
        "--botbench-walking-hitwall-minhitwall-candidate=" + (
            "1" if case.variant.walking_hitwall_minhitwall_candidate_enabled else "0"),
        "--botbench-movement-command-provenance-observer=" + (
            "1" if case.variant.movement_command_provenance_observer_enabled else "0"),
        "--botbench-hazard-residence-command-transition-ledger-observer=" + (
            "1" if case.variant.hazard_residence_command_transition_ledger_observer_enabled else "0"),
        "--botbench-hazard-residence-preentry-causal-slice-observer=" + (
            "1" if case.variant.hazard_residence_preentry_causal_slice_observer_enabled else "0"),
    ]
    if config.per_bot_skills is not None:
        command.append("--botbench-skills=" + ",".join(str(value) for value in config.per_bot_skills))
    if config.requested_names is not None:
        command.append("--botbench-names=" + ",".join(config.requested_names))
    command.append(str(config.game_root))
    return command


def _launch(command: list[str], timeout: float, stdout_path: Path, stderr_path: Path) -> LaunchResult:
    started = time.monotonic()
    try:
        with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open("w", encoding="utf-8") as stderr:
            process = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=timeout, check=False, shell=False)
        return LaunchResult(process.returncode, False, time.monotonic() - started)
    except subprocess.TimeoutExpired:
        return LaunchResult(None, True, time.monotonic() - started, f"timed out after {timeout:g} seconds")
    except OSError as exc:
        return LaunchResult(None, False, time.monotonic() - started, f"process launch failed: {exc}")


def _load_analyzer() -> Any:
    path = Path(__file__).with_name("Analyze-BotQuality.py")
    spec = importlib.util.spec_from_file_location("surreal_bot_quality", path)
    if spec is None or spec.loader is None:
        raise MatrixError(f"could not load analyzer: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_realized_capability_validator() -> Any:
    path = Path(__file__).with_name("Validate-RealizedBotCapabilities.py")
    spec = importlib.util.spec_from_file_location("surreal_bot_realized_capabilities", path)
    if spec is None or spec.loader is None:
        raise MatrixError(f"could not load realized-capability validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_quality_gate_evaluator() -> Any:
    path = Path(__file__).with_name("Evaluate-BotQualityGate.py")
    spec = importlib.util.spec_from_file_location("surreal_bot_quality_gate", path)
    if spec is None or spec.loader is None:
        raise MatrixError(f"could not load quality gate evaluator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def _utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z")


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def _file_provenance(path: Path) -> dict[str, Any]:
    resolved = path.resolve()
    return {
        "path": str(resolved),
        "size": resolved.stat().st_size,
        "sha256": _sha256_file(resolved),
    }


def _git_output(repo: Path, *arguments: str) -> bytes:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo), *arguments], stderr=subprocess.PIPE, shell=False)
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = ""
        if isinstance(exc, subprocess.CalledProcessError):
            detail = exc.stderr.decode("utf-8", errors="replace").strip()
        raise MatrixError(f"could not capture Git source provenance: {detail or exc}") from exc


def _source_provenance(start: Path, *, hash_untracked_contents: bool = True) -> dict[str, Any]:
    repository = Path(_git_output(start, "rev-parse", "--show-toplevel").decode().strip()).resolve()
    commit = _git_output(repository, "rev-parse", "HEAD").decode().strip()
    tree = _git_output(repository, "rev-parse", "HEAD^{tree}").decode().strip()
    branch_text = _git_output(repository, "branch", "--show-current").decode().strip()
    status = _git_output(repository, "status", "--porcelain=v1", "-z", "--untracked-files=all")
    diff = _git_output(repository, "diff", "--binary", "HEAD", "--")

    state_digest = hashlib.sha256()
    state_digest.update(b"git-status-v1\0")
    state_digest.update(status)
    state_digest.update(b"\0git-diff-head-binary\0")
    state_digest.update(diff)
    if hash_untracked_contents:
        for entry in sorted(part for part in status.split(b"\0") if part.startswith(b"?? ")):
            relative_bytes = entry[3:]
            relative = Path(relative_bytes.decode("utf-8", errors="surrogateescape"))
            untracked = repository / relative
            state_digest.update(b"\0untracked\0")
            state_digest.update(relative_bytes)
            state_digest.update(b"\0")
            if untracked.is_symlink():
                state_digest.update(os.readlink(untracked).encode("utf-8", errors="surrogateescape"))
            else:
                with untracked.open("rb") as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        state_digest.update(block)

    return {
        "repository": str(repository),
        "branch": branch_text or None,
        "commit": commit,
        "tree": tree,
        "dirty": bool(status),
        "diff_sha256": state_digest.hexdigest().upper(),
        "untracked_contents_hashed": hash_untracked_contents,
    }


def _preflight_provenance(
    config: MatrixConfig,
    invocation: list[str],
    environment: dict[str, str],
    quality_gates: Path | None = None,
) -> dict[str, Any]:
    utc_start = _utc_now()
    tool_path = Path(__file__).resolve()
    try:
        source: dict[str, Any] = _source_provenance(
            tool_path.parent,
            hash_untracked_contents=config.provenance_mode == "release")
        source["available"] = True
    except (MatrixError, OSError) as exc:
        if config.provenance_mode == "release":
            raise MatrixError(f"release provenance requires Git source state: {exc}") from exc
        source = {"available": False, "error": str(exc)}
    if config.provenance_mode == "release" and source.get("branch") is None:
        raise MatrixError("release provenance requires an attached Git branch")
    variants = []
    for variant in config.variants:
        variants.append({
            "id": variant.id,
            "build_preset": variant.build_preset,
            "falling_hazard_recovery_enabled": variant.falling_hazard_recovery_enabled,
            "falling_hazard_recovery_live_enabled": variant.falling_hazard_recovery_live_enabled,
            "targetless_move_to_timeout_enabled": variant.targetless_move_to_timeout_enabled,
            "direct_actor_move_toward_timeout_enabled": (
                variant.direct_actor_move_toward_timeout_enabled),
            "target_selection_observer_enabled": variant.target_selection_observer_enabled,
            "can_fire_at_enemy_observer_enabled": variant.can_fire_at_enemy_observer_enabled,
            "inventory_direct_reach_support_observer_enabled": (
                variant.inventory_direct_reach_support_observer_enabled),
            "native_path_commit_observer_enabled": variant.native_path_commit_observer_enabled,
            "pawn_vision_cone_enabled": variant.pawn_vision_cone_enabled,
            "pawn_vision_observer_enabled": variant.pawn_vision_observer_enabled,
            "vector_nonfinite_observer_enabled": variant.vector_nonfinite_observer_enabled,
            "finite_move_command_guard_enabled": variant.finite_move_command_guard_enabled,
            "pick_reg_destination_zero_divide_guard_enabled": (
                variant.pick_reg_destination_zero_divide_guard_enabled),
            "walking_hitwall_minhitwall_candidate_enabled": (
                variant.walking_hitwall_minhitwall_candidate_enabled),
            "movement_command_provenance_observer_enabled": (
                variant.movement_command_provenance_observer_enabled),
			"hazard_residence_command_transition_ledger_observer_enabled": (
				variant.hazard_residence_command_transition_ledger_observer_enabled),
			"hazard_residence_preentry_causal_slice_observer_enabled": (
				variant.hazard_residence_preentry_causal_slice_observer_enabled),
            "executable": _file_provenance(variant.executable),
        })
    game_manifest = _file_provenance(config.game_manifest) if config.game_manifest else None
    return {
        "schema": PROVENANCE_SCHEMA,
        "tool_version": TOOL_VERSION,
        "mode": config.provenance_mode,
        "classification": config.evidence_classification,
        "utc_start": utc_start,
        "utc_end": None,
        "source": source,
        "tool": _file_provenance(tool_path),
        "runner_invocation": {
            "working_directory": str(Path.cwd().resolve()),
            "argv": invocation,
            "environment_allowlist": list(config.environment_allowlist),
            "environment": {
                name: {"present": name in environment, "value": environment.get(name)}
                for name in config.environment_allowlist
            },
        },
        "scenario_manifest": _file_provenance(config.source),
        "game_manifest": game_manifest,
        "quality_gates": _file_provenance(quality_gates) if quality_gates else None,
        "game": {"family": config.game_family, "root": str(config.game_root)},
        "variants": variants,
        "child_artifacts": [],
    }


def _artifact_provenance(output: Path) -> list[dict[str, Any]]:
    excluded = {"provenance.json"}
    artifacts = []
    for path in sorted((item for item in output.rglob("*") if item.is_file()),
                       key=lambda item: item.relative_to(output).as_posix()):
        relative = path.relative_to(output).as_posix()
        if relative in excluded:
            continue
        record = _file_provenance(path)
        record["path"] = relative
        artifacts.append(record)
    return artifacts


def _run_case(
    config: MatrixConfig,
    case: MatrixCase,
    runs_directory: Path,
    launcher: Callable[[list[str], float, Path, Path], LaunchResult],
    validator: Callable[[Path], Any],
    require_realized_capabilities: bool,
) -> dict[str, Any]:
    run_directory = runs_directory / case.run_id
    run_directory.mkdir(parents=False, exist_ok=False)
    command = command_for(config, case, run_directory)
    metadata: dict[str, Any] = {
        "schema": METADATA_SCHEMA,
        "variant": case.variant.id,
        "falling_hazard_recovery_enabled": case.variant.falling_hazard_recovery_enabled,
        "falling_hazard_recovery_live_enabled": case.variant.falling_hazard_recovery_live_enabled,
        "targetless_move_to_timeout_enabled": case.variant.targetless_move_to_timeout_enabled,
        "direct_actor_move_toward_timeout_enabled": (
            case.variant.direct_actor_move_toward_timeout_enabled),
        "target_selection_observer_enabled": case.variant.target_selection_observer_enabled,
        "can_fire_at_enemy_observer_enabled": case.variant.can_fire_at_enemy_observer_enabled,
        "inventory_direct_reach_support_observer_enabled": (
            case.variant.inventory_direct_reach_support_observer_enabled),
        "native_path_commit_observer_enabled": case.variant.native_path_commit_observer_enabled,
        "pawn_vision_cone_enabled": case.variant.pawn_vision_cone_enabled,
        "vector_nonfinite_observer_enabled": case.variant.vector_nonfinite_observer_enabled,
        "pawn_vision_observer_enabled": case.variant.pawn_vision_observer_enabled,
        "finite_move_command_guard_enabled": case.variant.finite_move_command_guard_enabled,
        "pick_reg_destination_zero_divide_guard_enabled": (
            case.variant.pick_reg_destination_zero_divide_guard_enabled),
        "walking_hitwall_minhitwall_candidate_enabled": (
            case.variant.walking_hitwall_minhitwall_candidate_enabled),
        "movement_command_provenance_observer_enabled": (
            case.variant.movement_command_provenance_observer_enabled),
		"hazard_residence_command_transition_ledger_observer_enabled": (
			case.variant.hazard_residence_command_transition_ledger_observer_enabled),
		"hazard_residence_preentry_causal_slice_observer_enabled": (
			case.variant.hazard_residence_preentry_causal_slice_observer_enabled),
    }
    if case.start_layout is not None:
        metadata.update({
            "start_layout_id": case.start_layout.id,
            "expected_initial_layout_fingerprint": case.start_layout.expected_fingerprint,
        })
    if case.variant.comparison_role:
        metadata.update({"pair_id": case.pair_id, "comparison_role": case.variant.comparison_role})
    _write_json(run_directory / "quality-metadata.json", metadata)
    _write_json(run_directory / "invocation.json", {
        "schema": INVOCATION_SCHEMA,
        "run_id": case.run_id,
        "variant": case.variant.id,
        "variant_executable": str(case.variant.executable),
        "game_family": config.game_family,
        "game_root": str(config.game_root),
        "map_url": case.map_url,
        "seed": str(case.seed),
        "repetition": case.repetition,
        "start_layout_id": case.start_layout.id if case.start_layout else None,
        "expected_initial_layout_fingerprint": (
            case.start_layout.expected_fingerprint if case.start_layout else None),
        "bot_count": config.bot_count,
        "per_bot_skills": config.per_bot_skills,
        "requested_names": config.requested_names,
        "harmful_zone_escape_enabled": config.harmful_zone_escape_enabled,
        "walking_preflight_positive_dps_veto_enabled": (
            config.walking_preflight_positive_dps_veto_enabled),
        "hazard_swim_egress_enabled": case.variant.hazard_swim_egress_enabled,
        "hazard_swim_egress_live_enabled": case.variant.hazard_swim_egress_live_enabled,
        "failed_navigation_avoidance_enabled": (
            case.variant.failed_navigation_avoidance_enabled),
        "falling_hazard_recovery_enabled": case.variant.falling_hazard_recovery_enabled,
        "falling_hazard_recovery_live_enabled": case.variant.falling_hazard_recovery_live_enabled,
        "targetless_move_to_timeout_enabled": case.variant.targetless_move_to_timeout_enabled,
        "direct_actor_move_toward_timeout_enabled": (
            case.variant.direct_actor_move_toward_timeout_enabled),
        "target_selection_observer_enabled": case.variant.target_selection_observer_enabled,
        "can_fire_at_enemy_observer_enabled": case.variant.can_fire_at_enemy_observer_enabled,
        "inventory_direct_reach_support_observer_enabled": (
            case.variant.inventory_direct_reach_support_observer_enabled),
        "native_path_commit_observer_enabled": case.variant.native_path_commit_observer_enabled,
        "pawn_vision_cone_enabled": case.variant.pawn_vision_cone_enabled,
        "pawn_vision_observer_enabled": case.variant.pawn_vision_observer_enabled,
        "vector_nonfinite_observer_enabled": case.variant.vector_nonfinite_observer_enabled,
        "finite_move_command_guard_enabled": case.variant.finite_move_command_guard_enabled,
        "pick_reg_destination_zero_divide_guard_enabled": (
            case.variant.pick_reg_destination_zero_divide_guard_enabled),
        "walking_hitwall_minhitwall_candidate_enabled": (
            case.variant.walking_hitwall_minhitwall_candidate_enabled),
        "movement_command_provenance_observer_enabled": (
            case.variant.movement_command_provenance_observer_enabled),
		"hazard_residence_command_transition_ledger_observer_enabled": (
			case.variant.hazard_residence_command_transition_ledger_observer_enabled),
		"hazard_residence_preentry_causal_slice_observer_enabled": (
			case.variant.hazard_residence_preentry_causal_slice_observer_enabled),
        "command": command,
    })
    launch = launcher(command, config.timeout_seconds, run_directory / "stdout.txt", run_directory / "stderr.txt")
    required_names = ("manifest.json", "events.jsonl", "summary.json")
    if require_realized_capabilities:
        required_names += ("bot-realized-capabilities.json",)
    required = {name: (run_directory / name).is_file() and (run_directory / name).stat().st_size > 0
                for name in required_names}
    errors: list[str] = []
    if launch.error:
        errors.append(launch.error)
    if launch.timed_out:
        errors.append("benchmark process timed out")
    if launch.exit_code != 0:
        errors.append(f"benchmark exit code was {launch.exit_code}")
    for name, present in required.items():
        if not present:
            errors.append(f"missing or empty {name}")
    observed_initial_layout_fingerprint = None
    if not errors:
        try:
            validation = validator(run_directory)
            if case.start_layout is not None:
                if not isinstance(validation, dict):
                    raise MatrixError("start-layout validation did not return an analysis object")
                initial_layout = validation.get("initial_layout")
                if not isinstance(initial_layout, dict):
                    raise MatrixError("start-layout validation did not return initial_layout")
                observed = initial_layout.get("fingerprint")
                if not isinstance(observed, str):
                    raise MatrixError("start-layout validation did not return an initial-layout fingerprint")
                observed_initial_layout_fingerprint = observed
                if observed != case.start_layout.expected_fingerprint:
                    raise MatrixError(
                        "observed initial-layout fingerprint differs from the declared layout")
        except Exception as exc:
            errors.append(f"structural validation failed: {exc}")
    return {
        "ordinal": case.ordinal,
        "run_id": case.run_id,
        "run_directory": str(run_directory),
        "variant": case.variant.id,
        "comparison_role": case.variant.comparison_role,
        "pair_id": case.pair_id,
        "map_url": case.map_url,
        "seed": str(case.seed),
        "repetition": case.repetition,
        "start_layout_id": case.start_layout.id if case.start_layout else None,
        "expected_initial_layout_fingerprint": (
            case.start_layout.expected_fingerprint if case.start_layout else None),
        "observed_initial_layout_fingerprint": observed_initial_layout_fingerprint,
        "bot_count": config.bot_count,
        "per_bot_skills": config.per_bot_skills,
        "requested_names": config.requested_names,
        "harmful_zone_escape_enabled": config.harmful_zone_escape_enabled,
        "walking_preflight_positive_dps_veto_enabled": (
            config.walking_preflight_positive_dps_veto_enabled),
        "hazard_swim_egress_enabled": case.variant.hazard_swim_egress_enabled,
        "hazard_swim_egress_live_enabled": case.variant.hazard_swim_egress_live_enabled,
        "failed_navigation_avoidance_enabled": (
            case.variant.failed_navigation_avoidance_enabled),
        "falling_hazard_recovery_enabled": case.variant.falling_hazard_recovery_enabled,
        "falling_hazard_recovery_live_enabled": case.variant.falling_hazard_recovery_live_enabled,
        "targetless_move_to_timeout_enabled": case.variant.targetless_move_to_timeout_enabled,
        "direct_actor_move_toward_timeout_enabled": (
            case.variant.direct_actor_move_toward_timeout_enabled),
        "target_selection_observer_enabled": case.variant.target_selection_observer_enabled,
        "can_fire_at_enemy_observer_enabled": case.variant.can_fire_at_enemy_observer_enabled,
        "inventory_direct_reach_support_observer_enabled": (
            case.variant.inventory_direct_reach_support_observer_enabled),
        "native_path_commit_observer_enabled": case.variant.native_path_commit_observer_enabled,
        "pawn_vision_cone_enabled": case.variant.pawn_vision_cone_enabled,
        "pawn_vision_observer_enabled": case.variant.pawn_vision_observer_enabled,
        "vector_nonfinite_observer_enabled": case.variant.vector_nonfinite_observer_enabled,
        "finite_move_command_guard_enabled": case.variant.finite_move_command_guard_enabled,
        "pick_reg_destination_zero_divide_guard_enabled": (
            case.variant.pick_reg_destination_zero_divide_guard_enabled),
        "walking_hitwall_minhitwall_candidate_enabled": (
            case.variant.walking_hitwall_minhitwall_candidate_enabled),
        "movement_command_provenance_observer_enabled": (
            case.variant.movement_command_provenance_observer_enabled),
		"hazard_residence_command_transition_ledger_observer_enabled": (
			case.variant.hazard_residence_command_transition_ledger_observer_enabled),
		"hazard_residence_preentry_causal_slice_observer_enabled": (
			case.variant.hazard_residence_preentry_causal_slice_observer_enabled),
        "exit_code": launch.exit_code,
        "timed_out": launch.timed_out,
        "wall_seconds": launch.wall_seconds,
        "required_outputs": required,
        "status": "passed" if not errors else "failed",
        "errors": errors,
    }


def dry_run_plan(config: MatrixConfig, output: Path) -> dict[str, Any]:
    cases = expand_cases(config)
    runs_directory = output.resolve() / "runs"
    return {
        "schema": RESULT_SCHEMA,
        "dry_run": True,
        "manifest": str(config.source),
        "output": str(output.resolve()),
        "case_count": len(cases),
        "cases": [{
            "ordinal": case.ordinal,
            "run_id": case.run_id,
            "variant": case.variant.id,
            "comparison_role": case.variant.comparison_role,
            "pair_id": case.pair_id,
            "map_url": case.map_url,
            "seed": str(case.seed),
            "repetition": case.repetition,
            "start_layout_id": case.start_layout.id if case.start_layout else None,
            "expected_initial_layout_fingerprint": (
                case.start_layout.expected_fingerprint if case.start_layout else None),
            "bot_count": config.bot_count,
            "per_bot_skills": config.per_bot_skills,
            "requested_names": config.requested_names,
            "harmful_zone_escape_enabled": config.harmful_zone_escape_enabled,
            "walking_preflight_positive_dps_veto_enabled": (
                config.walking_preflight_positive_dps_veto_enabled),
            "hazard_swim_egress_enabled": case.variant.hazard_swim_egress_enabled,
            "hazard_swim_egress_live_enabled": case.variant.hazard_swim_egress_live_enabled,
            "failed_navigation_avoidance_enabled": (
                case.variant.failed_navigation_avoidance_enabled),
            "falling_hazard_recovery_enabled": case.variant.falling_hazard_recovery_enabled,
            "falling_hazard_recovery_live_enabled": case.variant.falling_hazard_recovery_live_enabled,
            "targetless_move_to_timeout_enabled": case.variant.targetless_move_to_timeout_enabled,
            "direct_actor_move_toward_timeout_enabled": (
                case.variant.direct_actor_move_toward_timeout_enabled),
            "target_selection_observer_enabled": case.variant.target_selection_observer_enabled,
            "inventory_direct_reach_support_observer_enabled": (
                case.variant.inventory_direct_reach_support_observer_enabled),
            "native_path_commit_observer_enabled": case.variant.native_path_commit_observer_enabled,
            "pawn_vision_cone_enabled": case.variant.pawn_vision_cone_enabled,
            "vector_nonfinite_observer_enabled": case.variant.vector_nonfinite_observer_enabled,
            "pawn_vision_observer_enabled": case.variant.pawn_vision_observer_enabled,
            "finite_move_command_guard_enabled": case.variant.finite_move_command_guard_enabled,
        "pick_reg_destination_zero_divide_guard_enabled": (
                case.variant.pick_reg_destination_zero_divide_guard_enabled),
            "walking_hitwall_minhitwall_candidate_enabled": (
                case.variant.walking_hitwall_minhitwall_candidate_enabled),
            "movement_command_provenance_observer_enabled": (
                case.variant.movement_command_provenance_observer_enabled),
			"hazard_residence_command_transition_ledger_observer_enabled": (
				case.variant.hazard_residence_command_transition_ledger_observer_enabled),
			"hazard_residence_preentry_causal_slice_observer_enabled": (
				case.variant.hazard_residence_preentry_causal_slice_observer_enabled),
            "command": command_for(config, case, runs_directory / case.run_id),
        } for case in cases],
    }


def _validate_paired_start_layouts(rows: list[dict[str, Any]]) -> None:
    paired: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        pair_id = row["pair_id"]
        if pair_id is None or row["start_layout_id"] is None:
            continue
        paired.setdefault(pair_id, []).append(row)
    for pair_id, pair_rows in paired.items():
        if any(row["status"] != "passed" for row in pair_rows):
            continue
        layout_ids = {row["start_layout_id"] for row in pair_rows}
        expected = {row["expected_initial_layout_fingerprint"] for row in pair_rows}
        observed = {row["observed_initial_layout_fingerprint"] for row in pair_rows}
        if len(layout_ids) != 1 or len(expected) != 1 or len(observed) != 1:
            for row in pair_rows:
                row["status"] = "failed"
                row["errors"].append(
                    f"paired start-layout mismatch for pair {pair_id}")


def run_matrix(
    config: MatrixConfig,
    output: Path,
    *,
    analyze: bool = False,
    quality_gates: Path | None = None,
    invocation: list[str] | None = None,
    environment: dict[str, str] | None = None,
    launcher: Callable[[list[str], float, Path, Path], LaunchResult] = _launch,
    validator: Callable[[Path], Any] | None = None,
    aggregate_analyzer: Callable[[list[Path]], dict[str, Any]] | None = None,
) -> dict[str, Any]:
    if config.provenance_mode == "release" and invocation is None:
        raise MatrixError("release provenance requires an exact runner invocation")
    invocation = list(invocation) if invocation is not None else [str(Path(__file__).resolve())]
    if not invocation or any(not isinstance(value, str) or not value for value in invocation):
        raise MatrixError("runner invocation must contain non-empty string arguments")
    environment = dict(environment) if environment is not None else dict(os.environ)
    gate_evaluator = None
    gate_config = None
    if quality_gates is not None:
        quality_gates = quality_gates.resolve()
        if not quality_gates.is_file():
            raise MatrixError(f"quality gates is not an existing file: {quality_gates}")
        try:
            gate_evaluator = _load_quality_gate_evaluator()
            gate_config = gate_evaluator._load_json(quality_gates)
            if gate_config.get("schema") != gate_evaluator.CONFIG_SCHEMA:
                raise gate_evaluator.GateInputError(
                    f"gate config schema must be {gate_evaluator.CONFIG_SCHEMA!r}")
            gate_evaluator._validate_config(gate_config)
        except Exception as exc:
            raise MatrixError(f"invalid quality gates {quality_gates}: {exc}") from exc
    provenance = _preflight_provenance(config, invocation, environment, quality_gates)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runs_directory = output / "runs"
    runs_directory.mkdir()
    analyzer = None
    default_validator = validator is None
    analyze_quality = analyze or quality_gates is not None
    if validator is None or (analyze_quality and aggregate_analyzer is None):
        analyzer = _load_analyzer()
    if validator is None:
        capability_validator = _load_realized_capability_validator()

        def validator(run: Path) -> Any:
            analysis = analyzer.analyze_run(run)
            witness = capability_validator.validate_run(run)
            if not isinstance(analysis, dict):
                raise MatrixError("quality analysis did not return an object")
            analysis["realized_capabilities"] = witness
            return analysis
    aggregate_analyzer = aggregate_analyzer or (analyzer.analyze if analyzer else None)
    cases = expand_cases(config)
    rows: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=config.concurrency) as executor:
        futures = [executor.submit(
            _run_case, config, case, runs_directory, launcher, validator, default_validator)
            for case in cases]
        for future in concurrent.futures.as_completed(futures):
            rows.append(future.result())
    rows.sort(key=lambda row: row["ordinal"])
    _validate_paired_start_layouts(rows)
    passed = all(row["status"] == "passed" for row in rows)
    report: dict[str, Any] = {
        "schema": RESULT_SCHEMA,
        "tool_version": TOOL_VERSION,
        "manifest": str(config.source),
        "game_family": config.game_family,
        "status": "passed" if passed else "failed",
        "case_count": len(rows),
        "passed": sum(row["status"] == "passed" for row in rows),
        "failed": sum(row["status"] == "failed" for row in rows),
        "quality_analysis": None,
        "quality_gate_config": str(quality_gates) if quality_gates else None,
        "quality_gate_result": None,
        "quality_gate_status": None,
        "provenance": str(output / "provenance.json"),
        "runs": rows,
    }
    if analyze_quality and passed:
        assert aggregate_analyzer is not None
        try:
            quality = aggregate_analyzer([Path(row["run_directory"]) for row in rows])
            quality_path = output / "quality-analysis.json"
            _write_json(quality_path, quality)
            report["quality_analysis"] = str(quality_path)
            if gate_evaluator is not None:
                assert gate_config is not None
                gate_result = gate_evaluator.evaluate(quality, gate_config)
                gate_result_path = output / "quality-gate-result.json"
                _write_json(gate_result_path, gate_result)
                report["quality_gate_result"] = str(gate_result_path)
                report["quality_gate_status"] = gate_result["status"]
                if gate_result["status"] != "passed":
                    report["status"] = "failed"
        except Exception as exc:
            report["status"] = "failed"
            report["analysis_error"] = str(exc)
    _write_json(output / "matrix-results.json", report)
    try:
        provenance["child_artifacts"] = _artifact_provenance(output)
    except OSError as exc:
        report["status"] = "failed"
        report["provenance_error"] = f"could not hash child artifacts: {exc}"
        provenance["artifact_error"] = str(exc)
        _write_json(output / "matrix-results.json", report)
    provenance["matrix_status"] = report["status"]
    provenance["utc_end"] = _utc_now()
    _write_json(output / "provenance.json", provenance)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, help=f"{MATRIX_SCHEMA} JSON manifest")
    parser.add_argument("--output", required=True, help="New matrix output directory")
    parser.add_argument("--dry-run", action="store_true", help="Print expanded commands without creating output")
    parser.add_argument("--analyze", action="store_true", help="Write quality-analysis.json after all runs validate")
    parser.add_argument(
        "--quality-gates", type=Path,
        help="Evaluate this surreal-bot-quality-gates-v1 file after structural analysis; implies --analyze")
    args = parser.parse_args(argv)
    try:
        config = load_matrix(Path(args.manifest))
        output = Path(args.output)
        if args.dry_run:
            print(json.dumps(dry_run_plan(config, output), indent=2, allow_nan=False))
            return 0
        invocation_arguments = list(sys.argv[1:] if argv is None else argv)
        invocation = [sys.executable, str(Path(__file__).resolve()), *invocation_arguments]
        report = run_matrix(
            config, output, analyze=args.analyze, quality_gates=args.quality_gates,
            invocation=invocation, environment=dict(os.environ))
        print(output.resolve() / "matrix-results.json")
        return 0 if report["status"] == "passed" else 1
    except (MatrixError, OSError) as exc:
        parser.error(str(exc))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
