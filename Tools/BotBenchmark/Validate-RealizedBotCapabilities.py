#!/usr/bin/env python3
"""Fail-closed validation for a benchmark run's live bot-capability witness."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path
from typing import Any


ARTIFACT_NAME = "bot-realized-capabilities.json"
SCHEMA = "surreal-bot-realized-capability-observation-v1"
SAMPLE = "post_spawn_pre_tick"
SUMMARY_SCHEMAS = (
    "surreal-bot-benchmark-summary-v2",
    "surreal-bot-benchmark-summary-v3",
)
MOVEMENT_FIELDS = (
    "ground_speed", "water_speed", "air_speed", "jump_z", "max_step_height", "accel_rate",
)
CAPABILITY_FIELDS = ("walk", "jump", "swim", "fly", "open_doors", "special")


class CapabilityError(ValueError):
    pass


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CapabilityError(f"duplicate JSON object field {key!r}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise CapabilityError(f"non-finite JSON number {value!r}")


def _load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object,
                           parse_constant=_reject_constant)
    except OSError as exc:
        raise CapabilityError(f"cannot read {path}: {exc}") from exc
    except (json.JSONDecodeError, CapabilityError) as exc:
        raise CapabilityError(f"invalid JSON in {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise CapabilityError(f"{path}: root must be an object")
    return value


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CapabilityError(f"{context}: expected an object")
    return value


def _array(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise CapabilityError(f"{context}: expected an array")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise CapabilityError(f"{context}: expected a non-empty string")
    return value


def _integer(value: Any, context: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise CapabilityError(f"{context}: expected an integer of at least {minimum}")
    return value


def _nonnegative_number(value: Any, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CapabilityError(f"{context}: expected a finite non-negative number")
    result = float(value)
    if not math.isfinite(result) or result < 0.0:
        raise CapabilityError(f"{context}: expected a finite non-negative number")
    return result


def _exact_keys(value: dict[str, Any], expected: tuple[str, ...], context: str) -> None:
    actual = set(value)
    wanted = set(expected)
    if actual != wanted:
        missing = sorted(wanted - actual)
        extra = sorted(actual - wanted)
        raise CapabilityError(f"{context}: fields differ (missing={missing}, extra={extra})")


def _actual_roster(summary: dict[str, Any], context: str) -> list[dict[str, str]]:
    if summary.get("schema") not in SUMMARY_SCHEMAS:
        raise CapabilityError(f"{context}: requires a supported bot-benchmark summary schema")
    if summary.get("status") != "complete" or summary.get("exit_code") != 0:
        raise CapabilityError(f"{context}: requires a complete successful benchmark")
    raw = _array(summary.get("actual_roster"), f"{context}.actual_roster")
    if not raw:
        raise CapabilityError(f"{context}.actual_roster: expected at least one participant")
    result: list[dict[str, str]] = []
    identities: set[str] = set()
    actors: set[str] = set()
    for index, item in enumerate(raw):
        participant = _object(item, f"{context}.actual_roster[{index}]")
        if _integer(participant.get("roster_index"),
                    f"{context}.actual_roster[{index}].roster_index") != index:
            raise CapabilityError(f"{context}.actual_roster: roster_index is not contiguous")
        identity = _string(participant.get("identity"), f"{context}.actual_roster[{index}].identity")
        actor = _string(participant.get("actor"), f"{context}.actual_roster[{index}].actor")
        class_name = _string(participant.get("class"), f"{context}.actual_roster[{index}].class")
        if identity in identities or actor in actors:
            raise CapabilityError(f"{context}.actual_roster: duplicate participant identity or actor")
        identities.add(identity)
        actors.add(actor)
        result.append({"identity": identity, "actor": actor, "class": class_name})
    return result


def validate_run(run: Path) -> dict[str, Any]:
    """Validate a witness and return its stable identity summary.

    This intentionally does not infer reachability from movement values or capability
    bits.  It only proves that the pre-tick witness belongs to this exact completed
    benchmark roster.
    """
    run = run.resolve()
    if not run.is_dir():
        raise CapabilityError(f"run directory does not exist: {run}")
    manifest = _load(run / "manifest.json")
    if manifest.get("schema") != "surreal-bot-benchmark-manifest-v2" or manifest.get("driver") != "bot-benchmark":
        raise CapabilityError(f"{run}/manifest.json: requires bot-benchmark manifest v2")
    bot_count = _integer(manifest.get("bot_count"), f"{run}/manifest.json.bot_count", 1)
    summary = _load(run / "summary.json")
    roster = _actual_roster(summary, f"{run}/summary.json")
    if len(roster) != bot_count:
        raise CapabilityError(f"{run}: manifest bot_count differs from actual_roster length")

    artifact_path = run / ARTIFACT_NAME
    artifact = _load(artifact_path)
    _exact_keys(artifact, ("schema", "sample", "participants"), str(artifact_path))
    if artifact.get("schema") != SCHEMA:
        raise CapabilityError(f"{artifact_path}.schema: expected {SCHEMA!r}")
    if artifact.get("sample") != SAMPLE:
        raise CapabilityError(f"{artifact_path}.sample: expected {SAMPLE!r}")
    participants = _array(artifact.get("participants"), f"{artifact_path}.participants")
    if len(participants) != len(roster):
        raise CapabilityError(f"{artifact_path}: participant count differs from actual_roster")

    normalized: list[dict[str, Any]] = []
    for index, expected in enumerate(roster):
        item = _object(participants[index], f"{artifact_path}.participants[{index}]")
        _exact_keys(item, ("identity", "actor", "class", "movement", "capabilities"),
                    f"{artifact_path}.participants[{index}]")
        for field in ("identity", "actor", "class"):
            if _string(item.get(field), f"{artifact_path}.participants[{index}].{field}") != expected[field]:
                raise CapabilityError(f"{artifact_path}: participant {field} differs from actual_roster")
        movement = _object(item.get("movement"), f"{artifact_path}.participants[{index}].movement")
        _exact_keys(movement, MOVEMENT_FIELDS, f"{artifact_path}.participants[{index}].movement")
        capabilities = _object(item.get("capabilities"), f"{artifact_path}.participants[{index}].capabilities")
        _exact_keys(capabilities, CAPABILITY_FIELDS,
                    f"{artifact_path}.participants[{index}].capabilities")
        normalized_movement = {
            field: _nonnegative_number(movement[field], f"movement.{field}")
            for field in MOVEMENT_FIELDS}
        normalized_capabilities: dict[str, bool] = {}
        for field in CAPABILITY_FIELDS:
            if not isinstance(capabilities[field], bool):
                raise CapabilityError(f"capabilities.{field}: expected a boolean")
            normalized_capabilities[field] = capabilities[field]
        normalized.append({
            "identity": expected["identity"], "actor": expected["actor"], "class": expected["class"],
            "movement": normalized_movement, "capabilities": normalized_capabilities,
        })
    canonical = json.dumps(normalized, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    return {"schema": SCHEMA, "sample": SAMPLE, "participants": len(normalized),
            "participants_sha256": hashlib.sha256(canonical).hexdigest().upper()}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path, help="benchmark run directory")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(validate_run(args.run), indent=2, sort_keys=True, allow_nan=False))
    except CapabilityError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
