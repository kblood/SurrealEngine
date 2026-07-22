#!/usr/bin/env python3
"""Analyze role-swapped mixed-skill Surreal bot qualification matrices.

The manifest supplies semantics that matrix-results.json cannot infer: the
intended higher/lower tier and which tier occupied the candidate (first-bot)
slot. Repeated same-seed processes are validated and collapsed before any
statistics are calculated.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import re
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable


ORIENTATIONS = {"higher_candidate", "lower_candidate"}
DIGEST_RE = re.compile(r"^[0-9a-f]{16}$")
TOOL_VERSION = 2
ADJACENT_FAMILY = {(tier, tier - 1) for tier in range(1, 8)}
QUALIFICATION_PROTOCOL_ID = "surreal-bot-skill-qualification-v1"
QUALIFICATION_MAPS = ("DM-Morbias][", "DM-Deck16][", "DM-Phobos")
QUALIFICATION_SEEDS = (
    "104729", "130363", "155921", "181081", "207073", "233021",
    "259033", "285007", "311041", "337069", "363073", "389029",
    "415021", "441011", "467003", "493067", "519031", "545023",
    "571021", "597031", "623011", "649069", "675067", "701009",
)
QUALIFICATION_TICKS = 5400
QUALIFICATION_FIXED_DELTA = 1.0 / 60.0
QUALIFICATION_SECONDS = 90.0
QUALIFICATION_GAME_CLASS = "Botpack.DeathMatchPlus"
QUALIFICATION_CONTENT_PATHS = (
    "System/Core.u",
    "System/Engine.u",
    "System/BotPack.u",
    "System/UnrealShare.u",
    "System/UnrealI.u",
    "System/SE-User.ini",
    "System/SE-UnrealTournament.ini",
    "Maps/DM-Morbias][.unr",
    "Maps/DM-Deck16][.unr",
    "Maps/DM-Phobos.unr",
)
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
CRITICAL_FIELDS = (
    "candidate_score",
    "opponent_score",
    "candidate_deaths",
    "opponent_deaths",
    "candidate_first_nonstarter_weapon_tick",
    "opponent_first_nonstarter_weapon_tick",
    "candidate_damage_dealt_exact",
    "opponent_damage_dealt_exact",
    "self_damage_exact_total",
    "fatal_damage_kills_exact_total",
    "fatal_damage_deaths_exact_total",
    "hitscan_shots_total",
    "hitscan_hits_total",
    "projectile_launches_total",
    "projectile_hits_finalized_total",
    "projectile_misses_finalized_total",
    "firing_intent_seconds_total",
    "no_progress_seconds_proxy_total",
    "stuck_events_proxy_total",
)
REPETITION_EQUIVALENCE_FIELDS = CRITICAL_FIELDS + (
    "map", "skill", "opponent_skill", "seed", "requested_bots", "requested_ticks", "fixed_delta",
    "valid", "deterministic", "digest_fnv1a64", "protocol_id", "game_class", "scenario", "fixture_id",
    "engine_binary_sha256", "content_manifest_sha256", "candidate_profile_id", "opponent_profile_id",
    "damage_dealt_exact_total", "damage_taken_exact_total", "pri_deaths_total", "final_score_total",
    "candidate_score_margin", "candidate_death_advantage", "hitscan_accuracy", "projectile_finalized_accuracy",
)


class QualificationError(ValueError):
    """Input is structurally unsafe for confirmatory inference."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


@dataclass(frozen=True)
class InputSpec:
    label: str
    path: Path
    higher_tier: int
    lower_tier: int
    orientation: str


def require_number(row: dict[str, Any], field: str, context: str) -> float:
    value = row.get(field)
    if value is None or isinstance(value, bool):
        raise QualificationError(f"{context}: required numeric field '{field}' is missing")
    try:
        number = float(value)
    except (TypeError, ValueError) as exc:
        raise QualificationError(f"{context}: field '{field}' is not numeric: {value!r}") from exc
    if not math.isfinite(number):
        raise QualificationError(f"{context}: field '{field}' is not finite")
    return number


def require_integer(row: dict[str, Any], field: str, context: str, minimum: int | None = None,
                    maximum: int | None = None) -> int:
    value = row.get(field)
    if isinstance(value, bool) or not isinstance(value, int):
        raise QualificationError(f"{context}: field '{field}' must be a JSON integer")
    if minimum is not None and value < minimum:
        raise QualificationError(f"{context}: field '{field}' must be >= {minimum}")
    if maximum is not None and value > maximum:
        raise QualificationError(f"{context}: field '{field}' must be <= {maximum}")
    return value


def require_nonnegative_number(row: dict[str, Any], field: str, context: str) -> float:
    value = require_number(row, field, context)
    if value < 0:
        raise QualificationError(f"{context}: field '{field}' must be nonnegative")
    return value


def require_json_number(row: dict[str, Any], field: str, context: str) -> float:
    value = row.get(field)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise QualificationError(f"{context}: field '{field}' must be a JSON number")
    number = float(value)
    if not math.isfinite(number):
        raise QualificationError(f"{context}: field '{field}' is not finite")
    return number


def require_sha256(value: Any, field: str, context: str) -> str:
    text = str(value)
    if not SHA256_RE.fullmatch(text):
        raise QualificationError(f"{context}: field '{field}' must be a canonical lowercase SHA-256")
    return text


def require_exact_number(container: dict[str, Any], field: str, expected: float, context: str) -> float:
    value = require_json_number(container, field, context)
    if not math.isclose(value, expected, rel_tol=0, abs_tol=1e-12):
        raise QualificationError(f"{context}: field '{field}' must equal {expected!r}")
    return value


def resolve_declared_file(matrix_path: Path, value: Any, field: str, context: str) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise QualificationError(f"{context}: field '{field}' must name an immutable file")
    declared = Path(value)
    resolved = declared if declared.is_absolute() else matrix_path.parent / declared
    resolved = resolved.resolve()
    if not resolved.is_file():
        raise QualificationError(f"{context}: declared {field} does not exist: {resolved}")
    return resolved


def resolve_declared_directory(matrix_path: Path, value: Any, field: str, context: str) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise QualificationError(f"{context}: field '{field}' must name an existing directory")
    declared = Path(value)
    resolved = declared if declared.is_absolute() else matrix_path.parent / declared
    resolved = resolved.resolve()
    if not resolved.is_dir():
        raise QualificationError(f"{context}: declared {field} does not exist or is not a directory: {resolved}")
    return resolved


def resolve_protocol_content_file(game_root: Path, logical_path: str, context: str) -> Path:
    if "\\" in logical_path or Path(logical_path).is_absolute() or any(
        part in {"", ".", ".."} for part in logical_path.split("/")
    ):
        raise QualificationError(f"{context}: package path must be a canonical root-relative protocol path")
    current = game_root
    for component in logical_path.split("/"):
        if not current.is_dir():
            raise QualificationError(f"{context}: required content path is missing: {logical_path}")
        exact_matches = [entry for entry in current.iterdir() if entry.name == component]
        if len(exact_matches) != 1:
            raise QualificationError(
                f"{context}: required content path is missing or has wrong physical casing: {logical_path}"
            )
        current = exact_matches[0]
    resolved = current.resolve()
    try:
        resolved.relative_to(game_root)
    except ValueError as exc:
        raise QualificationError(f"{context}: package path escapes the canonical game_root: {logical_path}") from exc
    if not resolved.is_file():
        raise QualificationError(f"{context}: required content path is not a file: {logical_path}")
    return resolved


def validate_content_manifest(path: Path, game_root: Path, context: str) -> int:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise QualificationError(f"{context}: content manifest must be readable JSON") from exc
    if not isinstance(manifest, dict) or type(manifest.get("schema")) is not int or manifest["schema"] != 1:
        raise QualificationError(f"{context}: content manifest must be an object with integer schema=1")
    packages = manifest.get("packages")
    if not isinstance(packages, list):
        raise QualificationError(f"{context}: content manifest packages must be an array")
    required = set(QUALIFICATION_CONTENT_PATHS)
    seen_logical: set[str] = set()
    seen_resolved: set[str] = set()
    for index, package in enumerate(packages):
        package_context = f"{context}/packages[{index}]"
        if not isinstance(package, dict):
            raise QualificationError(f"{package_context}: package identity must be an object")
        logical_path = package.get("path")
        if not isinstance(logical_path, str):
            raise QualificationError(f"{package_context}: package path must be a string")
        if "\\" in logical_path or logical_path.startswith("/") or any(
            part in {"", ".", ".."} for part in logical_path.split("/")
        ):
            raise QualificationError(f"{package_context}: traversal, absolute paths, and aliases are forbidden")
        if logical_path in seen_logical:
            raise QualificationError(f"{package_context}: duplicate logical package identity: {logical_path}")
        seen_logical.add(logical_path)
        package_path = resolve_protocol_content_file(game_root, logical_path, package_context)
        canonical_path = str(package_path)
        if canonical_path in seen_resolved:
            raise QualificationError(f"{package_context}: duplicate resolved package identity")
        seen_resolved.add(canonical_path)
        expected_sha = require_sha256(package.get("sha256"), "sha256", package_context)
        if sha256_file(package_path) != expected_sha:
            raise QualificationError(f"{package_context}: package SHA-256 does not match the declared file")
    if seen_logical != required or len(packages) != len(QUALIFICATION_CONTENT_PATHS):
        missing = sorted(required - seen_logical)
        extra = sorted(seen_logical - required)
        raise QualificationError(
            f"{context}: content manifest must contain the exact protocol-required logical path set; "
            f"missing={missing}, extra={extra}"
        )
    return len(packages)


def optional_number(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    low = math.floor(position)
    high = math.ceil(position)
    if low == high:
        return ordered[low]
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def describe(values: Iterable[float]) -> dict[str, float | int | None]:
    data = [float(value) for value in values if math.isfinite(float(value))]
    return {
        "count": len(data),
        "mean": statistics.fmean(data) if data else None,
        "median": statistics.median(data) if data else None,
        "p10": percentile(data, 0.10),
        "p90": percentile(data, 0.90),
        "minimum": min(data) if data else None,
        "maximum": max(data) if data else None,
    }


def superiority(margins: Iterable[float]) -> float | None:
    data = list(margins)
    if not data:
        return None
    return sum(1.0 if value > 0 else 0.5 if value == 0 else 0.0 for value in data) / len(data)


def cliff_sign_delta(margins: Iterable[float]) -> float | None:
    data = list(margins)
    if not data:
        return None
    return (sum(value > 0 for value in data) - sum(value < 0 for value in data)) / len(data)


def average_tie_ranks(values: list[float]) -> list[float]:
    indexed = sorted(enumerate(values), key=lambda item: item[1])
    ranks = [0.0] * len(values)
    cursor = 0
    while cursor < len(indexed):
        end = cursor + 1
        while end < len(indexed) and indexed[end][1] == indexed[cursor][1]:
            end += 1
        rank = ((cursor + 1) + end) / 2.0
        for position in range(cursor, end):
            ranks[indexed[position][0]] = rank
        cursor = end
    return ranks


def paired_rank_biserial(margins: Iterable[float]) -> float | None:
    nonzero = [value for value in margins if value != 0]
    if not nonzero:
        return 0.0
    ranks = average_tie_ranks([abs(value) for value in nonzero])
    positive = sum(rank for rank, value in zip(ranks, nonzero) if value > 0)
    negative = sum(rank for rank, value in zip(ranks, nonzero) if value < 0)
    return (positive - negative) / (positive + negative)


def exact_binomial_upper_tail(wins: int, trials: int) -> float:
    if trials <= 0:
        return 1.0
    numerator = sum(math.comb(trials, value) for value in range(wins, trials + 1))
    return numerator / (2**trials)


def load_manifest(path: Path) -> tuple[list[InputSpec], dict[str, Any]]:
    root = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(root, dict) or root.get("schema") != 1:
        raise QualificationError("manifest must be an object with schema=1")
    mode = str(root.get("mode", "qualification"))
    if mode not in {"qualification", "synthetic_test"}:
        raise QualificationError("manifest.mode must be 'qualification' or 'synthetic_test'")
    root["mode"] = mode
    raw_inputs = root.get("inputs")
    if not isinstance(raw_inputs, list) or not raw_inputs:
        raise QualificationError("manifest.inputs must be a non-empty array")
    specs: list[InputSpec] = []
    labels: set[str] = set()
    for index, item in enumerate(raw_inputs):
        context = f"manifest input {index}"
        if not isinstance(item, dict):
            raise QualificationError(f"{context} must be an object")
        label = str(item.get("label", "")).strip()
        if not label or label in labels:
            raise QualificationError(f"{context} label must be non-empty and unique: {label!r}")
        labels.add(label)
        orientation = str(item.get("orientation", ""))
        if orientation not in ORIENTATIONS:
            raise QualificationError(f"{context} orientation must be one of {sorted(ORIENTATIONS)}")
        higher = require_integer(item, "higher_tier", context, 0, 7)
        lower = require_integer(item, "lower_tier", context, 0, 7)
        if not (0 <= lower < higher <= 7):
            raise QualificationError(f"{context} requires 0 <= lower_tier < higher_tier <= 7")
        raw_path = Path(str(item.get("path", "")))
        matrix_path = raw_path if raw_path.is_absolute() else path.parent / raw_path
        matrix_path = matrix_path.resolve()
        if matrix_path.is_dir():
            matrix_path /= "matrix-results.json"
        if not matrix_path.is_file():
            raise QualificationError(f"{context} matrix does not exist: {matrix_path}")
        specs.append(InputSpec(label, matrix_path, higher, lower, orientation))
    if mode == "qualification":
        observed = [(spec.higher_tier, spec.lower_tier, spec.orientation) for spec in specs]
        expected = [(high, low, orientation) for high, low in sorted(ADJACENT_FAMILY)
                    for orientation in sorted(ORIENTATIONS)]
        if len(observed) != len(expected) or set(observed) != set(expected):
            missing = sorted(set(expected) - set(observed))
            extra = sorted(set(observed) - set(expected))
            duplicates = sorted({item for item in observed if observed.count(item) > 1})
            raise QualificationError(
                "qualification mode requires the full seven adjacent-pair Holm family and exactly one input "
                "for every pair/orientation; "
                f"missing={missing}, extra={extra}, duplicates={duplicates}"
            )
    return specs, root


def expected_skills(spec: InputSpec) -> tuple[int, int]:
    if spec.orientation == "higher_candidate":
        return spec.higher_tier, spec.lower_tier
    return spec.lower_tier, spec.higher_tier


def matrix_provenance(root: dict[str, Any]) -> dict[str, Any]:
    configuration = root.get("configuration") if isinstance(root.get("configuration"), dict) else {}
    build_identity = None
    for container in (root, configuration):
        for field in ("binary_sha256", "build_commit", "base_commit", "build_id", "engine_version"):
            if container.get(field) not in (None, ""):
                build_identity = f"{field}:{container[field]}"
                break
        if build_identity is not None:
            break
    return {
        "engine_path": root.get("engine_path"),
        "game_root": root.get("game_root"),
        "build_identity": build_identity,
        "duration_seconds": configuration.get("seconds"),
        "configuration_ticks": configuration.get("ticks"),
        "configuration_fixed_delta": configuration.get("fixed_delta"),
        "bot_count": configuration.get("bots"),
        "bot_name": configuration.get("bot_name"),
        "fixture_id": configuration.get("fixture_id"),
        "scenario": configuration.get("scenario"),
        "candidate_profile_id": configuration.get("candidate_profile_id"),
        "opponent_profile_id": configuration.get("opponent_profile_id"),
        "protocol_id": configuration.get("protocol_id"),
        "game_class": configuration.get("game_class"),
        "engine_binary_sha256": root.get("engine_binary_sha256"),
        "content_manifest_sha256": root.get("content_manifest_sha256"),
    }


def canonical_critical(row: dict[str, Any]) -> tuple[Any, ...]:
    return tuple(row.get(field) for field in REPETITION_EQUIVALENCE_FIELDS)


def validate_row_semantics(row: dict[str, Any], context: str, *, qualification: bool) -> None:
    requested_bots = require_integer(row, "requested_bots", context, 2, 2)
    requested_ticks = require_integer(row, "requested_ticks", context, 1)
    require_exact_number(row, "fixed_delta", QUALIFICATION_FIXED_DELTA, context) if qualification else require_number(
        row, "fixed_delta", context
    )
    require_integer(row, "skill", context, 0, 7)
    require_integer(row, "opponent_skill", context, 0, 7)
    require_integer(row, "repetition", context, 1)
    if qualification and requested_ticks != QUALIFICATION_TICKS:
        raise QualificationError(f"{context}: field 'requested_ticks' must equal {QUALIFICATION_TICKS}")

    for field in ("candidate_score", "opponent_score"):
        require_integer(row, field, context)
    for field in (
        "candidate_deaths", "opponent_deaths", "fatal_damage_kills_exact_total",
        "fatal_damage_deaths_exact_total", "hitscan_shots_total", "hitscan_hits_total",
        "projectile_launches_total", "projectile_hits_finalized_total",
        "projectile_misses_finalized_total", "stuck_events_proxy_total",
    ):
        require_integer(row, field, context, 0)
    for field in (
        "candidate_damage_dealt_exact", "opponent_damage_dealt_exact", "self_damage_exact_total",
        "firing_intent_seconds_total", "no_progress_seconds_proxy_total",
    ):
        require_nonnegative_number(row, field, context)
    for field in ("candidate_first_nonstarter_weapon_tick", "opponent_first_nonstarter_weapon_tick"):
        if row.get(field) is not None:
            require_integer(row, field, context, 0, requested_ticks)

    if require_integer(row, "hitscan_hits_total", context, 0) > require_integer(row, "hitscan_shots_total", context, 0):
        raise QualificationError(f"{context}: hitscan hits cannot exceed shots")
    finalized = require_integer(row, "projectile_hits_finalized_total", context, 0) + require_integer(
        row, "projectile_misses_finalized_total", context, 0
    )
    if finalized > require_integer(row, "projectile_launches_total", context, 0):
        raise QualificationError(f"{context}: finalized projectiles cannot exceed launches")
    simulated_bot_seconds = requested_bots * requested_ticks * require_number(row, "fixed_delta", context)
    for field in ("firing_intent_seconds_total", "no_progress_seconds_proxy_total"):
        if require_number(row, field, context) > simulated_bot_seconds + 1e-9:
            raise QualificationError(f"{context}: field '{field}' exceeds total bot-seconds exposure")

    if not qualification:
        return
    for field, expected in (
        ("protocol_id", QUALIFICATION_PROTOCOL_ID), ("game_class", QUALIFICATION_GAME_CLASS),
        ("scenario", "skill-qualification"), ("fixture_id", ""),
    ):
        if row.get(field) != expected:
            raise QualificationError(f"{context}: field '{field}' must equal {expected!r}")
    require_sha256(row.get("engine_binary_sha256"), "engine_binary_sha256", context)
    require_sha256(row.get("content_manifest_sha256"), "content_manifest_sha256", context)
    for field in ("candidate_profile_id", "opponent_profile_id"):
        if not isinstance(row.get(field), str) or not row[field].strip():
            raise QualificationError(f"{context}: field '{field}' must be a stable non-empty identifier")

    dealt = require_nonnegative_number(row, "damage_dealt_exact_total", context)
    taken = require_nonnegative_number(row, "damage_taken_exact_total", context)
    self_damage = require_nonnegative_number(row, "self_damage_exact_total", context)
    participant_damage = require_number(row, "candidate_damage_dealt_exact", context) + require_number(
        row, "opponent_damage_dealt_exact", context
    )
    if not math.isclose(dealt, participant_damage, rel_tol=0, abs_tol=1e-9):
        raise QualificationError(f"{context}: participant damage does not reconcile with damage_dealt_exact_total")
    if not math.isclose(taken, dealt + self_damage, rel_tol=0, abs_tol=1e-9):
        raise QualificationError(f"{context}: damage taken does not reconcile with dealt plus self damage")
    pri_deaths = require_integer(row, "pri_deaths_total", context, 0)
    participant_deaths = require_integer(row, "candidate_deaths", context, 0) + require_integer(
        row, "opponent_deaths", context, 0
    )
    if pri_deaths != participant_deaths:
        raise QualificationError(f"{context}: participant deaths do not reconcile with pri_deaths_total")
    fatal_deaths = require_integer(row, "fatal_damage_deaths_exact_total", context, 0)
    fatal_kills = require_integer(row, "fatal_damage_kills_exact_total", context, 0)
    if fatal_deaths != fatal_kills:
        raise QualificationError(f"{context}: fatal damage kills and deaths do not reconcile")
    if fatal_deaths > pri_deaths:
        raise QualificationError(f"{context}: fatal damage deaths exceed PRI deaths")
    if fatal_kills > pri_deaths:
        raise QualificationError(f"{context}: fatal damage kills exceed PRI deaths")
    score_total = require_integer(row, "final_score_total", context)
    candidate_score = require_integer(row, "candidate_score", context)
    opponent_score = require_integer(row, "opponent_score", context)
    if score_total != candidate_score + opponent_score:
        raise QualificationError(f"{context}: participant scores do not reconcile with final_score_total")
    if require_integer(row, "candidate_score_margin", context) != candidate_score - opponent_score:
        raise QualificationError(f"{context}: candidate_score_margin does not reconcile")
    candidate_deaths = require_integer(row, "candidate_deaths", context, 0)
    opponent_deaths = require_integer(row, "opponent_deaths", context, 0)
    if require_integer(row, "candidate_death_advantage", context) != opponent_deaths - candidate_deaths:
        raise QualificationError(f"{context}: candidate_death_advantage does not reconcile")

    for numerator_field, denominator_field, accuracy_field in (
        ("hitscan_hits_total", "hitscan_shots_total", "hitscan_accuracy"),
        ("projectile_hits_finalized_total", None, "projectile_finalized_accuracy"),
    ):
        numerator = require_integer(row, numerator_field, context, 0)
        denominator = (finalized if denominator_field is None else require_integer(row, denominator_field, context, 0))
        observed = row.get(accuracy_field)
        if denominator == 0:
            if observed is not None:
                raise QualificationError(f"{context}: field '{accuracy_field}' must be null with zero exposure")
        elif not math.isclose(require_number(row, accuracy_field, context), numerator / denominator, rel_tol=0, abs_tol=1e-12):
            raise QualificationError(f"{context}: field '{accuracy_field}' does not reconcile with counts")


def validate_qualification_root(spec: InputSpec, root: dict[str, Any]) -> dict[str, Any]:
    context = spec.label
    if type(root.get("schema")) is not int or root["schema"] != 1:
        raise QualificationError(f"{context}: matrix schema must be the JSON integer 1")
    configuration = root.get("configuration")
    totals = root.get("totals")
    cases = root.get("cases")
    runs = root.get("runs")
    if not isinstance(configuration, dict) or not isinstance(totals, dict) or not isinstance(cases, list) or not isinstance(runs, list):
        raise QualificationError(f"{context}: qualification requires configuration, totals, cases, and runs")

    engine_path = resolve_declared_file(spec.path, root.get("engine_path"), "engine_path", context)
    engine_sha = require_sha256(root.get("engine_binary_sha256"), "engine_binary_sha256", context)
    if sha256_file(engine_path) != engine_sha:
        raise QualificationError(f"{context}: engine binary SHA-256 does not match the declared file")
    game_root = resolve_declared_directory(spec.path, root.get("game_root"), "game_root", context)
    content_path = resolve_declared_file(spec.path, root.get("content_manifest_path"), "content_manifest_path", context)
    content_sha = require_sha256(root.get("content_manifest_sha256"), "content_manifest_sha256", context)
    if sha256_file(content_path) != content_sha:
        raise QualificationError(f"{context}: content manifest SHA-256 does not match the declared file")
    content_package_count = validate_content_manifest(content_path, game_root, context)

    candidate_skill, opponent_skill = expected_skills(spec)
    expected_config = {
        "protocol_id": QUALIFICATION_PROTOCOL_ID, "game_class": QUALIFICATION_GAME_CLASS,
        "maps": list(QUALIFICATION_MAPS), "seeds": list(QUALIFICATION_SEEDS),
        "scenario": "skill-qualification", "fixture_id": "",
    }
    for field, expected in expected_config.items():
        if configuration.get(field) != expected:
            raise QualificationError(f"{context}: configuration.{field} must equal the canonical protocol value {expected!r}")
    if not isinstance(configuration.get("skills"), list) or len(configuration["skills"]) != 1 \
            or type(configuration["skills"][0]) is not int or configuration["skills"][0] != candidate_skill:
        raise QualificationError(f"{context}: configuration.skills must equal [{candidate_skill}] with integer typing")
    for field, expected in (("opponent_skill", opponent_skill), ("bots", 2), ("ticks", QUALIFICATION_TICKS)):
        if require_integer(configuration, field, f"{context}/configuration") != expected:
            raise QualificationError(f"{context}: configuration.{field} must equal {expected}")
    require_exact_number(configuration, "seconds", QUALIFICATION_SECONDS, f"{context}/configuration")
    require_exact_number(configuration, "fixed_delta", QUALIFICATION_FIXED_DELTA, f"{context}/configuration")
    runs_per_case = require_integer(configuration, "runs_per_case", f"{context}/configuration", 1)
    expected_keys = {(map_name, seed) for map_name in QUALIFICATION_MAPS for seed in QUALIFICATION_SEEDS}

    expected_case_count = len(expected_keys)
    expected_run_count = expected_case_count * runs_per_case
    for field, expected in (
        ("cases", expected_case_count), ("runs", expected_run_count),
        ("failed_runs", 0), ("nondeterministic_cases", 0),
    ):
        if require_integer(totals, field, f"{context}/totals", 0) != expected:
            raise QualificationError(f"{context}: totals.{field} must equal {expected}")
    if len(cases) != expected_case_count or len(runs) != expected_run_count:
        raise QualificationError(f"{context}: cases/runs arrays do not match declared Cartesian totals")

    case_by_key: dict[tuple[str, str], dict[str, Any]] = {}
    case_ids: set[str] = set()
    for index, case in enumerate(cases):
        case_context = f"{context}/case[{index}]"
        if not isinstance(case, dict):
            raise QualificationError(f"{case_context}: case must be an object")
        if not isinstance(case.get("map"), str) or not isinstance(case.get("seed"), str):
            raise QualificationError(f"{case_context}: map and seed must be strings")
        key = (case["map"], case["seed"])
        if key not in expected_keys or key in case_by_key:
            raise QualificationError(f"{case_context}: duplicate or noncanonical map/seed Cartesian cell {key}")
        if require_integer(case, "skill", case_context, 0, 7) != candidate_skill or require_integer(
            case, "opponent_skill", case_context, 0, 7
        ) != opponent_skill:
            raise QualificationError(f"{case_context}: case skill metadata mismatch")
        if case.get("fixture_id") != "":
            raise QualificationError(f"{case_context}: fixtures are forbidden by the qualification protocol")
        case_id = case.get("case_id")
        if not isinstance(case_id, str) or not case_id or case_id in case_ids:
            raise QualificationError(f"{case_context}: case_id must be non-empty and unique")
        case_ids.add(case_id)
        for field, expected in (("expected_runs", runs_per_case), ("valid_runs", runs_per_case),
                                ("unique_valid_digests", 1)):
            if require_integer(case, field, case_context, 0) != expected:
                raise QualificationError(f"{case_context}: {field} must equal {expected}")
        if case.get("deterministic") is not True or not DIGEST_RE.fullmatch(str(case.get("digest_fnv1a64", ""))):
            raise QualificationError(f"{case_context}: case must be deterministic with a canonical lowercase digest")
        case_by_key[key] = case
    if set(case_by_key) != expected_keys:
        raise QualificationError(f"{context}: cases do not cover the exact canonical Cartesian grid")

    repetitions: dict[tuple[str, str], set[int]] = defaultdict(set)
    run_ids: set[str] = set()
    for index, row in enumerate(runs):
        run_context = f"{context}/run[{index}]"
        if not isinstance(row, dict):
            raise QualificationError(f"{run_context}: run must be an object")
        if not isinstance(row.get("map"), str) or not isinstance(row.get("seed"), str):
            raise QualificationError(f"{run_context}: map and seed must be strings")
        key = (row["map"], row["seed"])
        if key not in case_by_key:
            raise QualificationError(f"{run_context}: run is outside the canonical Cartesian grid")
        repetition = require_integer(row, "repetition", run_context, 1, runs_per_case)
        if repetition in repetitions[key]:
            raise QualificationError(f"{run_context}: duplicate repetition for Cartesian cell {key}")
        repetitions[key].add(repetition)
        run_id = row.get("run_id")
        if not isinstance(run_id, str) or not run_id or run_id in run_ids:
            raise QualificationError(f"{run_context}: run_id must be non-empty and unique")
        run_ids.add(run_id)
        case = case_by_key[key]
        if row.get("case_id") != case["case_id"] or row.get("skill") != candidate_skill or row.get("opponent_skill") != opponent_skill:
            raise QualificationError(f"{run_context}: run/case linkage or skill metadata mismatch")
        if row.get("valid") is not True or row.get("deterministic") is not True:
            raise QualificationError(f"{run_context}: qualification run must be valid and deterministic")
        if row.get("digest_fnv1a64") != case.get("digest_fnv1a64"):
            raise QualificationError(f"{run_context}: run digest does not match its case digest")
        validate_row_semantics(row, run_context, qualification=True)
        if row.get("engine_binary_sha256") != engine_sha or row.get("content_manifest_sha256") != content_sha:
            raise QualificationError(f"{run_context}: run immutable identity does not match matrix root")
    expected_repetitions = set(range(1, runs_per_case + 1))
    if set(repetitions) != expected_keys or any(value != expected_repetitions for value in repetitions.values()):
        raise QualificationError(f"{context}: runs do not cover every canonical case/repetition exactly once")
    return {"engine_path": str(engine_path), "game_root": str(game_root),
            "content_manifest_path": str(content_path),
            "content_package_count": content_package_count}


def load_input(spec: InputSpec, mode: str = "synthetic_test") -> tuple[list[dict[str, Any]], dict[str, Any]]:
    root = json.loads(spec.path.read_text(encoding="utf-8-sig"))
    if not isinstance(root, dict) or not isinstance(root.get("runs"), list):
        raise QualificationError(f"{spec.label}: not a matrix-results document")
    if root.get("passed") is not True:
        raise QualificationError(f"{spec.label}: matrix root did not pass")
    qualification_paths = validate_qualification_root(spec, root) if mode == "qualification" else {}
    source_provenance = matrix_provenance(root)
    if mode == "qualification":
        source_provenance["engine_path"] = qualification_paths["engine_path"]
        source_provenance["game_root"] = qualification_paths["game_root"]
    candidate_skill, opponent_skill = expected_skills(spec)
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row_index, raw_row in enumerate(root["runs"]):
        if not isinstance(raw_row, dict):
            raise QualificationError(f"{spec.label}: run {row_index} is not an object")
        context = f"{spec.label}/{raw_row.get('run_id', row_index)}"
        if raw_row.get("valid") is not True or raw_row.get("deterministic") is not True:
            raise QualificationError(f"{context}: run is invalid or its case is nondeterministic")
        validate_row_semantics(raw_row, context, qualification=mode == "qualification")
        if raw_row.get("skill") != candidate_skill or raw_row.get("opponent_skill") != opponent_skill:
            raise QualificationError(
                f"{context}: row skills {raw_row.get('skill')}v{raw_row.get('opponent_skill')} "
                f"do not match explicit {spec.orientation} metadata"
            )
        digest = str(raw_row.get("digest_fnv1a64", ""))
        if not DIGEST_RE.fullmatch(digest):
            raise QualificationError(f"{context}: invalid semantic digest {digest!r}")
        map_name = str(raw_row.get("map", ""))
        seed = str(raw_row.get("seed", ""))
        if not map_name or not seed:
            raise QualificationError(f"{context}: map and seed are required")
        for field in CRITICAL_FIELDS:
            # Acquisition may legitimately be unobserved. All other fields are
            # required so exposure cannot disappear silently.
            if field in {"candidate_first_nonstarter_weapon_tick", "opponent_first_nonstarter_weapon_tick"}:
                continue
            require_number(raw_row, field, context)
        grouped[(map_name, seed)].append(raw_row)

    if not grouped:
        raise QualificationError(f"{spec.label}: matrix contains zero usable trials")

    selected: list[dict[str, Any]] = []
    collapsed = 0
    for (map_name, seed), repetitions in sorted(grouped.items()):
        digests = {str(row["digest_fnv1a64"]) for row in repetitions}
        if len(digests) != 1:
            raise QualificationError(f"{spec.label}/{map_name}/{seed}: repetition digests disagree")
        critical = {canonical_critical(row) for row in repetitions}
        if len(critical) != 1:
            raise QualificationError(f"{spec.label}/{map_name}/{seed}: repetition metrics disagree")
        repetition_ids = [int(row.get("repetition", 0)) for row in repetitions]
        if len(repetition_ids) != len(set(repetition_ids)):
            raise QualificationError(f"{spec.label}/{map_name}/{seed}: duplicate repetition identifier")
        chosen = min(repetitions, key=lambda row: int(row.get("repetition", 0)))
        row_build_identity = None
        for field in ("binary_sha256", "build_commit", "base_commit", "build_id", "engine_version"):
            if chosen.get(field) not in (None, ""):
                row_build_identity = f"{field}:{chosen[field]}"
                break
        if (
            row_build_identity is not None
            and source_provenance.get("build_identity") is not None
            and row_build_identity != source_provenance["build_identity"]
        ):
            raise QualificationError(f"{spec.label}/{map_name}/{seed}: row/matrix build identity mismatch")
        row_provenance = {
            **source_provenance,
            "build_identity": row_build_identity or source_provenance.get("build_identity"),
            "requested_ticks": int(require_number(chosen, "requested_ticks", spec.label)),
            "fixed_delta": require_number(chosen, "fixed_delta", spec.label),
            "row_fixture_id": chosen.get("fixture_id"),
            "row_scenario": chosen.get("scenario"),
            "candidate_profile_id": chosen.get("candidate_profile_id", source_provenance.get("candidate_profile_id")),
            "opponent_profile_id": chosen.get("opponent_profile_id", source_provenance.get("opponent_profile_id")),
        }
        selected.append({**chosen, "input_label": spec.label, "orientation": spec.orientation,
                         "higher_tier": spec.higher_tier, "lower_tier": spec.lower_tier,
                         "_provenance": row_provenance})
        collapsed += len(repetitions) - 1
    return selected, {
        "label": spec.label,
        "path": str(spec.path),
        "sha256": sha256_file(spec.path),
        "higher_tier": spec.higher_tier,
        "lower_tier": spec.lower_tier,
        "orientation": spec.orientation,
        "process_rows": len(root["runs"]),
        "independent_trials": len(selected),
        "determinism_duplicates_collapsed": collapsed,
        "matrix_provenance": source_provenance,
        **qualification_paths,
    }


def participant_values(row: dict[str, Any], spec_orientation: str) -> dict[str, float | None]:
    candidate = {
        "score": require_number(row, "candidate_score", str(row.get("run_id"))),
        "deaths": require_number(row, "candidate_deaths", str(row.get("run_id"))),
        "damage": require_number(row, "candidate_damage_dealt_exact", str(row.get("run_id"))),
        "weapon_tick": optional_number(row.get("candidate_first_nonstarter_weapon_tick")),
    }
    opponent = {
        "score": require_number(row, "opponent_score", str(row.get("run_id"))),
        "deaths": require_number(row, "opponent_deaths", str(row.get("run_id"))),
        "damage": require_number(row, "opponent_damage_dealt_exact", str(row.get("run_id"))),
        "weapon_tick": optional_number(row.get("opponent_first_nonstarter_weapon_tick")),
    }
    if spec_orientation == "higher_candidate":
        high, low = candidate, opponent
    else:
        high, low = opponent, candidate
    fixed_delta = require_number(row, "fixed_delta", str(row.get("run_id")))
    for participant in (high, low):
        tick = participant.pop("weapon_tick")
        participant["first_weapon_seconds"] = None if tick is None else tick * fixed_delta
    return {f"high_{key}": value for key, value in high.items()} | {
        f"low_{key}": value for key, value in low.items()
    }


def validate_provenance(rows: list[dict[str, Any]]) -> dict[str, Any]:
    if not rows:
        raise QualificationError("zero independent orientation trials")
    effective: list[dict[str, Any]] = []
    for row in rows:
        context = f"{row['input_label']}/{row.get('run_id')}"
        provenance = row["_provenance"]
        if provenance.get("configuration_ticks") is not None and int(provenance["configuration_ticks"]) != int(provenance["requested_ticks"]):
            raise QualificationError(f"{context}: row/config requested tick mismatch")
        if provenance.get("configuration_fixed_delta") is not None and not math.isclose(
            float(provenance["configuration_fixed_delta"]), float(provenance["fixed_delta"]), rel_tol=0, abs_tol=1e-12
        ):
            raise QualificationError(f"{context}: row/config fixed_delta mismatch")
        if provenance.get("bot_count") is not None and int(provenance["bot_count"]) != 2:
            raise QualificationError(f"{context}: matrix configuration bot count is not two")
        if provenance.get("duration_seconds") is not None:
            simulated = float(provenance["requested_ticks"]) * float(provenance["fixed_delta"])
            if abs(simulated - float(provenance["duration_seconds"])) > float(provenance["fixed_delta"]) + 1e-9:
                raise QualificationError(f"{context}: duration/ticks/fixed_delta provenance mismatch")
        fixture = provenance.get("row_fixture_id")
        if fixture is None:
            fixture = provenance.get("fixture_id")
        elif provenance.get("fixture_id") is not None and fixture != provenance.get("fixture_id"):
            raise QualificationError(f"{context}: row/config fixture mismatch")
        scenario = provenance.get("row_scenario")
        if scenario is None:
            scenario = provenance.get("scenario")
        elif provenance.get("scenario") is not None and scenario != provenance.get("scenario"):
            raise QualificationError(f"{context}: row/config scenario mismatch")
        effective.append({
            "requested_ticks": int(provenance["requested_ticks"]),
            "fixed_delta": float(provenance["fixed_delta"]),
            "duration_seconds": provenance.get("duration_seconds"),
            "engine_path": provenance.get("engine_path"),
            "game_root": provenance.get("game_root"),
            "build_identity": provenance.get("build_identity"),
            "bot_name": provenance.get("bot_name"),
            "fixture_id": fixture,
            "scenario": scenario,
            "protocol_id": provenance.get("protocol_id"),
            "game_class": provenance.get("game_class"),
            "engine_binary_sha256": provenance.get("engine_binary_sha256"),
            "content_manifest_sha256": provenance.get("content_manifest_sha256"),
        })

    equality_fields = tuple(effective[0])
    observed: dict[str, Any] = {}
    missing: list[str] = []
    for field in equality_fields:
        values = [item[field] for item in effective]
        present = [value for value in values if value is not None]
        if present and len(present) != len(values):
            raise QualificationError(f"provenance field '{field}' is only partially exposed across inputs")
        serialized = {json.dumps(value, sort_keys=True) for value in present}
        if len(serialized) > 1:
            raise QualificationError(f"crossover provenance mismatch for '{field}': {sorted(serialized)}")
        if present:
            observed[field] = present[0]
        else:
            observed[field] = None
            missing.append(field)
    return {"observed": observed, "missing_fields": missing, "equality_validated": True}


def join_crossovers(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    indexed: dict[tuple[int, int, str, str], dict[str, dict[str, Any]]] = defaultdict(dict)
    for row in rows:
        key = (int(row["higher_tier"]), int(row["lower_tier"]), str(row["map"]), str(row["seed"]))
        orientation = str(row["orientation"])
        if orientation in indexed[key]:
            raise QualificationError(f"duplicate independent input for {key} orientation {orientation}")
        indexed[key][orientation] = row

    units: list[dict[str, Any]] = []
    for key, orientations in sorted(indexed.items()):
        missing = ORIENTATIONS - orientations.keys()
        if missing:
            raise QualificationError(f"missing role-swapped orientation(s) {sorted(missing)} for {key}")
        high, low, map_name, seed = key
        first_provenance = orientations["higher_candidate"]["_provenance"]
        second_provenance = orientations["lower_candidate"]["_provenance"]
        profile_fields = ("candidate_profile_id", "opponent_profile_id")
        profile_values = [provenance.get(field) for provenance in (first_provenance, second_provenance) for field in profile_fields]
        profile_complete = all(first_provenance.get(field) not in (None, "") and second_provenance.get(field) not in (None, "")
                               for field in profile_fields)
        if any(value not in (None, "") for value in profile_values) and not profile_complete:
            raise QualificationError(f"profile provenance is only partially exposed for {key}")
        if profile_complete:
            for field in profile_fields:
                if first_provenance[field] != second_provenance[field]:
                    raise QualificationError(f"profile provenance mismatch for {key}: {field}")
        role_values: dict[str, dict[str, float | None]] = {}
        for orientation, row in orientations.items():
            role_values[orientation] = participant_values(row, orientation)

        high_score = sum(float(value["high_score"]) for value in role_values.values())
        low_score = sum(float(value["low_score"]) for value in role_values.values())
        high_deaths = sum(float(value["high_deaths"]) for value in role_values.values())
        low_deaths = sum(float(value["low_deaths"]) for value in role_values.values())
        high_damage = sum(float(value["high_damage"]) for value in role_values.values())
        low_damage = sum(float(value["low_damage"]) for value in role_values.values())
        damage_total = high_damage + low_damage
        high_weapon = [value["high_first_weapon_seconds"] for value in role_values.values()
                       if value["high_first_weapon_seconds"] is not None]
        low_weapon = [value["low_first_weapon_seconds"] for value in role_values.values()
                      if value["low_first_weapon_seconds"] is not None]
        exposure_fields = (
            "self_damage_exact_total", "fatal_damage_kills_exact_total", "fatal_damage_deaths_exact_total",
            "hitscan_shots_total", "hitscan_hits_total", "projectile_launches_total",
            "projectile_hits_finalized_total", "projectile_misses_finalized_total",
            "firing_intent_seconds_total", "no_progress_seconds_proxy_total", "stuck_events_proxy_total",
        )
        exposures = {field: sum(require_number(row, field, str(row.get("run_id"))) for row in orientations.values())
                     for field in exposure_fields}
        units.append({
            "higher_tier": high, "lower_tier": low, "map": map_name, "seed": seed,
            "score_margin": high_score - low_score,
            "death_advantage": low_deaths - high_deaths,
            "damage_margin": high_damage - low_damage,
            "damage_share": None if damage_total <= 0 else high_damage / damage_total,
            "higher_score": high_score, "lower_score": low_score,
            "higher_deaths": high_deaths, "lower_deaths": low_deaths,
            "higher_damage": high_damage, "lower_damage": low_damage,
            "higher_first_weapon_seconds_mean": statistics.fmean(high_weapon) if high_weapon else None,
            "lower_first_weapon_seconds_mean": statistics.fmean(low_weapon) if low_weapon else None,
            "higher_weapon_observations": len(high_weapon), "lower_weapon_observations": len(low_weapon),
            "stable_profile_identity": profile_complete,
            "candidate_profile_id": first_provenance.get("candidate_profile_id") if profile_complete else None,
            "opponent_profile_id": first_provenance.get("opponent_profile_id") if profile_complete else None,
            **exposures,
        })
    if not units:
        raise QualificationError("zero joined crossover units")
    return units


def bootstrap_clustered(
    units: list[dict[str, Any]], draws: int, seed: int,
    statistic: Callable[[list[dict[str, Any]]], float | None],
) -> dict[str, float | int | None]:
    clusters: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for unit in units:
        clusters[str(unit["seed"])].append(unit)
    keys = sorted(clusters)
    estimate = statistic(units)
    if not keys or estimate is None:
        return {"estimate": estimate, "lower_95": None, "upper_95": None, "draws": draws, "seed_clusters": len(keys)}
    rng = random.Random(seed)
    values: list[float] = []
    for _ in range(draws):
        sample: list[dict[str, Any]] = []
        for _cluster in keys:
            sample.extend(clusters[keys[rng.randrange(len(keys))]])
        value = statistic(sample)
        if value is not None and math.isfinite(value):
            values.append(value)
    return {
        "estimate": estimate,
        "lower_95": percentile(values, 0.025),
        "upper_95": percentile(values, 0.975),
        "draws": len(values),
        "seed_clusters": len(keys),
    }


def margin_summary(margins: list[float]) -> dict[str, Any]:
    return {
        "distribution": describe(margins),
        "wins": sum(value > 0 for value in margins),
        "ties": sum(value == 0 for value in margins),
        "losses": sum(value < 0 for value in margins),
        "superiority": superiority(margins),
        "cliff_sign_delta": cliff_sign_delta(margins),
        "paired_rank_biserial": paired_rank_biserial(margins),
    }


def stratum_summary(units: list[dict[str, Any]]) -> dict[str, Any]:
    margins = [float(unit["score_margin"]) for unit in units]
    damage_shares = [float(unit["damage_share"]) for unit in units if unit["damage_share"] is not None]
    summary = margin_summary(margins)
    summary["damage_share"] = describe(damage_shares)
    summary["material_reversal"] = bool(
        (summary["cliff_sign_delta"] is not None and summary["cliff_sign_delta"] <= -0.147)
        or (damage_shares and statistics.median(damage_shares) < 0.45)
    )
    return summary


def role_strata(rows: list[dict[str, Any]], high: int, low: int) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for orientation in sorted(ORIENTATIONS):
        selected = [row for row in rows if int(row["higher_tier"]) == high and int(row["lower_tier"]) == low
                    and row["orientation"] == orientation]
        margins: list[float] = []
        shares: list[float] = []
        for row in selected:
            values = participant_values(row, orientation)
            margins.append(float(values["high_score"]) - float(values["low_score"]))
            damage_total = float(values["high_damage"]) + float(values["low_damage"])
            if damage_total > 0:
                shares.append(float(values["high_damage"]) / damage_total)
        summary = margin_summary(margins)
        summary["damage_share"] = describe(shares)
        summary["material_reversal"] = bool(
            (summary["cliff_sign_delta"] is not None and summary["cliff_sign_delta"] <= -0.147)
            or (shares and statistics.median(shares) < 0.45)
        )
        result[orientation] = summary
    return result


def clustered_sign_test(units: list[dict[str, Any]]) -> dict[str, Any]:
    by_seed: dict[str, float] = defaultdict(float)
    for unit in units:
        by_seed[str(unit["seed"])] += float(unit["score_margin"])
    values = list(by_seed.values())
    wins = sum(value > 0 for value in values)
    losses = sum(value < 0 for value in values)
    ties = sum(value == 0 for value in values)
    return {
        "seed_clusters": len(values), "wins": wins, "ties": ties, "losses": losses,
        "one_sided_p": exact_binomial_upper_tail(wins, wins + losses),
    }


def analyze_rows(rows: list[dict[str, Any]], units: list[dict[str, Any]], draws: int, bootstrap_seed: int) -> list[dict[str, Any]]:
    pairs = sorted({(int(unit["higher_tier"]), int(unit["lower_tier"])) for unit in units})
    results: list[dict[str, Any]] = []
    for high, low in pairs:
        pair_units = [unit for unit in units if int(unit["higher_tier"]) == high and int(unit["lower_tier"]) == low]
        pair_seed = (bootstrap_seed * 1_000_003 + high * 101 + low) & ((1 << 63) - 1)
        sup_ci = bootstrap_clustered(pair_units, draws, pair_seed,
                                     lambda sample: superiority(float(unit["score_margin"]) for unit in sample))
        delta_ci = bootstrap_clustered(pair_units, draws, pair_seed + 1,
                                       lambda sample: cliff_sign_delta(float(unit["score_margin"]) for unit in sample))
        damage_ci = bootstrap_clustered(
            pair_units, draws, pair_seed + 2,
            lambda sample: statistics.median(values) if (values := [float(unit["damage_share"]) for unit in sample
                                                                    if unit["damage_share"] is not None]) else None,
        )
        score = margin_summary([float(unit["score_margin"]) for unit in pair_units])
        maps = {map_name: stratum_summary([unit for unit in pair_units if unit["map"] == map_name])
                for map_name in sorted({str(unit["map"]) for unit in pair_units})}
        positive_maps = sum(
            item["cliff_sign_delta"] is not None and item["cliff_sign_delta"] > 0
            for item in maps.values()
        )
        required_positive_maps = math.ceil(2 * len(maps) / 3) if maps else 0
        roles = role_strata(rows, high, low)
        sign_test = clustered_sign_test(pair_units)
        exposure_fields = [field for field in CRITICAL_FIELDS if field.endswith("_total")]
        exposures = {field: sum(float(unit[field]) for unit in pair_units) for field in exposure_fields}
        exposures.update({
            "crossover_units": len(pair_units),
            "unique_seeds": len({str(unit["seed"]) for unit in pair_units}),
            "maps": len({str(unit["map"]) for unit in pair_units}),
            "decisive_score_units": score["wins"] + score["losses"],
            "damage_exposed_units": sum(unit["damage_share"] is not None for unit in pair_units),
            "higher_weapon_observations": sum(int(unit["higher_weapon_observations"]) for unit in pair_units),
            "lower_weapon_observations": sum(int(unit["lower_weapon_observations"]) for unit in pair_units),
        })
        results.append({
            "higher_tier": high, "lower_tier": low,
            "score": score,
            "superiority_cluster_bootstrap": sup_ci,
            "cliff_delta_cluster_bootstrap": delta_ci,
            "damage_share_cluster_bootstrap": damage_ci,
            "seed_cluster_sign_test": sign_test,
            "holm_adjusted_one_sided_p": None,
            "holm_reject_at_0_05": False,
            "map_strata": maps,
            "role_strata": roles,
            "no_map_material_reversal": not any(item["material_reversal"] for item in maps.values()),
            "no_role_material_reversal": not any(item["material_reversal"] for item in roles.values()),
            "positive_map_point_effects": positive_maps,
            "required_positive_map_point_effects": required_positive_maps,
            "positive_map_requirement_pass": positive_maps >= required_positive_maps and required_positive_maps > 0,
            "exposure": exposures,
        })
    apply_holm(results)
    for result in results:
        sup = result["superiority_cluster_bootstrap"]
        delta = result["cliff_delta_cluster_bootstrap"]
        damage = result["damage_share_cluster_bootstrap"]
        result["statistical_rules_pass"] = bool(
            sup["estimate"] is not None and sup["estimate"] > 0.50
            and sup["lower_95"] is not None and sup["lower_95"] > 0.50
            and result["holm_reject_at_0_05"]
            and delta["estimate"] is not None and delta["estimate"] >= 0.147
            and delta["lower_95"] is not None and delta["lower_95"] > 0
            and damage["estimate"] is not None and damage["estimate"] > 0.50
            and damage["lower_95"] is not None and damage["lower_95"] >= 0.48
            and result["no_map_material_reversal"] and result["no_role_material_reversal"]
            and result["positive_map_requirement_pass"]
        )
        result["separated_pass"] = False
    return results


def apply_holm(results: list[dict[str, Any]]) -> None:
    ordered = sorted(enumerate(results), key=lambda item: item[1]["seed_cluster_sign_test"]["one_sided_p"])
    running = 0.0
    count = len(ordered)
    adjusted: dict[int, float] = {}
    for rank, (original_index, result) in enumerate(ordered):
        raw = float(result["seed_cluster_sign_test"]["one_sided_p"])
        running = max(running, min(1.0, (count - rank) * raw))
        adjusted[original_index] = running
    for index, result in enumerate(results):
        result["holm_adjusted_one_sided_p"] = adjusted[index]
        result["holm_reject_at_0_05"] = adjusted[index] < 0.05


def qualification_readiness(mode: str, units: list[dict[str, Any]], pair_results: list[dict[str, Any]]) -> dict[str, Any]:
    pair_keys = {(int(result["higher_tier"]), int(result["lower_tier"])) for result in pair_results}
    if mode == "qualification" and pair_keys != ADJACENT_FAMILY:
        missing = sorted(ADJACENT_FAMILY - pair_keys)
        extra = sorted(pair_keys - ADJACENT_FAMILY)
        raise QualificationError(
            f"qualification mode requires the full seven adjacent-pair Holm family; missing={missing}, extra={extra}"
        )

    per_pair: dict[str, Any] = {}
    blockers: list[str] = []
    for high, low in sorted(pair_keys):
        selected = [unit for unit in units if int(unit["higher_tier"]) == high and int(unit["lower_tier"]) == low]
        seeds = {str(unit["seed"]) for unit in selected}
        maps = {str(unit["map"]) for unit in selected}
        profiles_complete = all(bool(unit["stable_profile_identity"]) for unit in selected)
        canonical_grid = seeds == set(QUALIFICATION_SEEDS) and maps == set(QUALIFICATION_MAPS) and len(selected) == 72
        eligible = canonical_grid and profiles_complete
        key = f"{high}v{low}"
        pair_blockers: list[str] = []
        if len(seeds) < 24:
            pair_blockers.append(f"requires >=24 unique seeds; observed {len(seeds)}")
        if len(maps) < 3:
            pair_blockers.append(f"requires >=3 maps; observed {len(maps)}")
        if not profiles_complete:
            pair_blockers.append("stable candidate/opponent profile identity is missing or incomplete")
        if mode == "qualification" and not canonical_grid:
            pair_blockers.append("requires the exact canonical Morbias/Deck16/Phobos x S1 24-seed grid")
        per_pair[key] = {
            "unique_seeds": len(seeds), "maps": len(maps),
            "stable_profile_identity_complete": profiles_complete,
            "canonical_grid_complete": canonical_grid,
            "minimum_design_complete": eligible, "blockers": pair_blockers,
        }
        blockers.extend(f"{key}: {message}" for message in pair_blockers)

    if mode == "synthetic_test":
        blockers.insert(0, "synthetic_test mode is non-qualification and can never emit a qualified pass")
    eligible = mode == "qualification" and not blockers and bool(pair_results)
    return {
        "mode": mode,
        "qualification_claim_allowed": eligible,
        "full_adjacent_holm_family": pair_keys == ADJACENT_FAMILY,
        "required_unique_seeds_per_pair": 24,
        "required_maps_per_pair": 3,
        "stable_profile_identity_is_blocking": True,
        "per_pair": per_pair,
        "blockers": blockers,
    }


def analyze_manifest(
    manifest_path: Path, draws: int, bootstrap_seed: int,
    command_parameters: dict[str, Any] | None = None,
) -> dict[str, Any]:
    specs, manifest = load_manifest(manifest_path)
    rows: list[dict[str, Any]] = []
    audits: list[dict[str, Any]] = []
    for spec in specs:
        loaded, audit = load_input(spec, str(manifest["mode"]))
        rows.extend(loaded)
        audits.append(audit)
    provenance = validate_provenance(rows)
    units = join_crossovers(rows)
    pair_results = analyze_rows(rows, units, draws, bootstrap_seed)
    if not pair_results:
        raise QualificationError("zero tier pairs were produced")
    readiness = qualification_readiness(str(manifest["mode"]), units, pair_results)
    for result in pair_results:
        key = f"{result['higher_tier']}v{result['lower_tier']}"
        pair_ready = readiness["per_pair"][key]["minimum_design_complete"]
        result["qualification_readiness_pass"] = bool(readiness["qualification_claim_allowed"] and pair_ready)
        result["separated_pass"] = bool(result["statistical_rules_pass"] and result["qualification_readiness_pass"])
    tool_path = Path(__file__).resolve()
    return {
        "schema": 1,
        "tool": {"name": tool_path.name, "version": TOOL_VERSION, "path": str(tool_path), "sha256": sha256_file(tool_path)},
        "manifest": str(manifest_path.resolve()),
        "manifest_sha256": sha256_file(manifest_path),
        "mode": manifest["mode"],
        "command_parameters": command_parameters or {
            "manifest": str(manifest_path.resolve()), "bootstrap_draws": draws, "bootstrap_seed": bootstrap_seed,
        },
        "bootstrap": {"draws": draws, "seed": bootstrap_seed, "cluster": "seed"},
        "inputs": audits,
        "process_rows": sum(item["process_rows"] for item in audits),
        "independent_orientation_trials": len(rows),
        "determinism_duplicates_collapsed": sum(item["determinism_duplicates_collapsed"] for item in audits),
        "crossover_units": len(units),
        "pairs": pair_results,
        "readiness": readiness,
        "provenance": provenance,
        "claim_scope": "adjacent_skill_monotonicity",
        "godlike_evaluated": False,
        "godlike_qualified": False,
        "all_pairs_statistical_rules_pass": all(result["statistical_rules_pass"] for result in pair_results),
        "all_pairs_separated": bool(readiness["qualification_claim_allowed"] and pair_results
                                    and all(result["separated_pass"] for result in pair_results)),
        "validation": {
            "status": "passed",
            "duplicates_are_not_independent": True,
            "role_swap_join_complete": True,
            "manifest_metadata_explicit": True,
        },
        "interpretation_warning": (
            "Confirmatory statistics use one crossover unit per map/seed/tier pair and cluster bootstrap/sign tests by seed. "
            "The matrix's same-seed repetitions validate determinism and are collapsed, never counted as samples."
        ),
        "_units": units,
        "_manifest_metadata": manifest.get("metadata", {}),
    }


def write_outputs(output: Path, report: dict[str, Any]) -> None:
    output.mkdir(parents=True, exist_ok=False)
    units = report.pop("_units")
    metadata = report.pop("_manifest_metadata")
    if metadata:
        report["metadata"] = metadata
    (output / "qualification-analysis.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    unit_fields = list(units[0].keys()) if units else []
    with (output / "crossover-units.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=unit_fields)
        if unit_fields:
            writer.writeheader()
            writer.writerows(units)
    pair_fields = (
        "higher_tier", "lower_tier", "crossover_units", "unique_seeds", "score_wins", "score_ties", "score_losses",
        "superiority", "superiority_lower_95", "superiority_upper_95", "cliff_sign_delta", "paired_rank_biserial",
        "cliff_lower_95", "damage_share", "damage_share_lower_95", "one_sided_p", "holm_adjusted_p",
        "positive_map_point_effects", "required_positive_map_point_effects",
        "no_map_material_reversal", "no_role_material_reversal", "statistical_rules_pass",
        "qualification_readiness_pass", "separated_pass",
    )
    with (output / "pairs.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=pair_fields)
        writer.writeheader()
        for result in report["pairs"]:
            writer.writerow({
                "higher_tier": result["higher_tier"], "lower_tier": result["lower_tier"],
                "crossover_units": result["exposure"]["crossover_units"],
                "unique_seeds": result["exposure"]["unique_seeds"],
                "score_wins": result["score"]["wins"], "score_ties": result["score"]["ties"],
                "score_losses": result["score"]["losses"], "superiority": result["score"]["superiority"],
                "superiority_lower_95": result["superiority_cluster_bootstrap"]["lower_95"],
                "superiority_upper_95": result["superiority_cluster_bootstrap"]["upper_95"],
                "cliff_sign_delta": result["score"]["cliff_sign_delta"],
                "paired_rank_biserial": result["score"]["paired_rank_biserial"],
                "cliff_lower_95": result["cliff_delta_cluster_bootstrap"]["lower_95"],
                "damage_share": result["damage_share_cluster_bootstrap"]["estimate"],
                "damage_share_lower_95": result["damage_share_cluster_bootstrap"]["lower_95"],
                "one_sided_p": result["seed_cluster_sign_test"]["one_sided_p"],
                "holm_adjusted_p": result["holm_adjusted_one_sided_p"],
                "positive_map_point_effects": result["positive_map_point_effects"],
                "required_positive_map_point_effects": result["required_positive_map_point_effects"],
                "no_map_material_reversal": result["no_map_material_reversal"],
                "no_role_material_reversal": result["no_role_material_reversal"],
                "statistical_rules_pass": result["statistical_rules_pass"],
                "qualification_readiness_pass": result["qualification_readiness_pass"],
                "separated_pass": result["separated_pass"],
            })


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze explicit role-swapped bot skill qualification matrices.")
    parser.add_argument("--manifest", required=True, help="Qualification manifest JSON")
    parser.add_argument("--output", required=True, help="New output directory")
    parser.add_argument("--bootstrap-draws", type=int, default=10_000)
    parser.add_argument("--bootstrap-seed", type=int, default=7_436_991)
    args = parser.parse_args()
    if args.bootstrap_draws < 100:
        parser.error("--bootstrap-draws must be at least 100")
    output = Path(args.output).resolve()
    if output.exists():
        parser.error(f"output directory already exists: {output}")
    try:
        report = analyze_manifest(
            Path(args.manifest).resolve(), args.bootstrap_draws, args.bootstrap_seed,
            command_parameters={
                "argv": sys.argv[1:],
                "manifest": str(Path(args.manifest).resolve()),
                "output": str(output),
                "bootstrap_draws": args.bootstrap_draws,
                "bootstrap_seed": args.bootstrap_seed,
            },
        )
        write_outputs(output, report)
    except (QualificationError, FileNotFoundError, json.JSONDecodeError) as exc:
        parser.error(str(exc))
    print(output / "qualification-analysis.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
