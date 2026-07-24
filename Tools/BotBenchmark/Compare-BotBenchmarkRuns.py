#!/usr/bin/env python3
"""Fail-closed deterministic equivalence check for two benchmark runs."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import re
import sys
from pathlib import Path
from typing import Any, NamedTuple


REPORT_SCHEMA = "surreal-bot-benchmark-equivalence-v1"
REQUIRED_ARTIFACTS = ("manifest.json", "events.jsonl", "summary.json")
SUPPORTED_SCHEMAS = {
    "manifest.json": {
        "surreal-bot-benchmark-manifest-v1",
        "surreal-bot-benchmark-manifest-v2",
    },
    "events.jsonl": {
        "surreal-bot-benchmark-telemetry-v1",
        "surreal-bot-benchmark-telemetry-v2",
    },
    "summary.json": {
        "surreal-bot-benchmark-summary-v1",
        "surreal-bot-benchmark-summary-v2",
    },
    "shadow-manifest.json": {"surreal-bot-benchmark-shadow-manifest-v1"},
    "shadow-decisions.jsonl": {"surreal-bot-benchmark-shadow-event-v1"},
}
SUPPORTED_SHADOW_ARTIFACTS = {"shadow-manifest.json", "shadow-decisions.jsonl"}

# These fields define structure, configuration, ordering, or participant identity.
# Letting an ignore option remove any of them would turn a mismatch into a false pass.
PROTECTED_FIELDS = {
    "schema", "driver", "config_id", "benchmark_config_id", "url", "seed",
    "max_ticks", "fixed_delta", "difficulty", "bot_count", "telemetry_event_cap",
    "harmful_zone_escape_enabled",
    "requested_roster", "actual_roster", "config", "index", "roster_index",
    "identity", "actor", "player_name", "class", "seq", "tick",
    "simulated_seconds", "type", "map", "status", "failure_reason", "exit_code",
    "bots", "participants",
}
INTEGER_STRING = re.compile(r"-?(?:0|[1-9][0-9]*)\Z")
COUNTER_FIELD = re.compile(r"[A-Za-z_][A-Za-z0-9_]*_exact\Z")
DIAGNOSTIC_FIELD = re.compile(r"[A-Za-z_][A-Za-z0-9_]*_diagnostics\Z")
KNOWN_RECORD_DIAGNOSTIC_FIELDS = {"falling_parity_realized_records"}


class ComparisonError(Exception):
    pass


class Artifact(NamedTuple):
    name: str
    raw: bytes
    documents: list[dict[str, Any]]
    jsonl: bool


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def _pointer_token(value: str) -> str:
    return value.replace("~", "~0").replace("/", "~1")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ComparisonError(f"duplicate JSON object field {key!r}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise ComparisonError(f"non-finite JSON number {value!r}")


def _strict_int(value: Any, context: str, minimum: int | None = None) -> int:
    if isinstance(value, bool):
        raise ComparisonError(f"{context}: expected an integer")
    if isinstance(value, int):
        result = value
    elif isinstance(value, str) and INTEGER_STRING.fullmatch(value):
        result = int(value)
    else:
        raise ComparisonError(f"{context}: expected an integer or canonical integer string")
    if minimum is not None and result < minimum:
        raise ComparisonError(f"{context}: expected a value of at least {minimum}")
    return result


def _number(value: Any, context: str, *, positive: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ComparisonError(f"{context}: expected a finite JSON number")
    result = float(value)
    if not math.isfinite(result):
        raise ComparisonError(f"{context}: expected a finite JSON number")
    if positive and result <= 0.0:
        raise ComparisonError(f"{context}: expected a positive number")
    return result


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ComparisonError(f"{context}: expected a JSON object")
    return value


def _array(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise ComparisonError(f"{context}: expected a JSON array")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise ComparisonError(f"{context}: expected a non-empty string")
    return value


def _read_artifact(path: Path, name: str) -> Artifact:
    try:
        if not path.is_file():
            raise ComparisonError(f"{path}: missing required artifact")
        raw = path.read_bytes()
    except OSError as exc:
        raise ComparisonError(f"{path}: cannot read artifact: {exc}") from exc
    if not raw:
        raise ComparisonError(f"{path}: artifact is empty")
    try:
        text = raw.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise ComparisonError(f"{path}: artifact is not valid UTF-8: {exc}") from exc

    jsonl = path.suffix == ".jsonl"
    source_lines = text.splitlines() if jsonl else [text]
    if jsonl and (not source_lines or any(not line.strip() for line in source_lines)):
        raise ComparisonError(f"{path}: JSONL must contain non-empty lines only")
    documents: list[dict[str, Any]] = []
    for index, source in enumerate(source_lines, 1):
        context = f"{path}:line {index}" if jsonl else str(path)
        try:
            document = json.loads(source, object_pairs_hook=_unique_object,
                                  parse_constant=_reject_constant)
        except (json.JSONDecodeError, ValueError) as exc:
            raise ComparisonError(f"{context}: invalid JSON: {exc}") from exc
        documents.append(_object(document, context))
    return Artifact(name=name, raw=raw, documents=documents, jsonl=jsonl)


def _schema(document: dict[str, Any], artifact_name: str, context: str) -> str:
    schema = _string(document.get("schema"), f"{context}.schema")
    supported = SUPPORTED_SCHEMAS.get(artifact_name)
    if supported is None:
        raise ComparisonError(f"{context}: unsupported shadow artifact {artifact_name!r}")
    if schema not in supported:
        raise ComparisonError(f"{context}: unsupported schema {schema!r}")
    return schema


def _validate_roster(value: Any, context: str, index_name: str) -> list[dict[str, Any]]:
    roster = _array(value, context)
    unique_values: dict[str, set[str]] = {
        "identity": set(), "identity_fragment": set(), "actor": set(), "player_name": set(),
    }
    for index, raw in enumerate(roster):
        item = _object(raw, f"{context}[{index}]")
        if _strict_int(item.get(index_name), f"{context}[{index}].{index_name}", 0) != index:
            raise ComparisonError(f"{context}[{index}].{index_name}: roster order is not contiguous")
        for field, seen in unique_values.items():
            if field in item:
                field_value = _string(item[field], f"{context}[{index}].{field}")
                if field_value in seen:
                    raise ComparisonError(f"{context}: duplicate {field} {field_value!r}")
                seen.add(field_value)
    return roster


def _validate_run(run: Path, artifacts: dict[str, Artifact]) -> None:
    manifest = artifacts["manifest.json"].documents[0]
    summary = artifacts["summary.json"].documents[0]
    events = artifacts["events.jsonl"].documents
    manifest_schema = _schema(manifest, "manifest.json", f"{run}/manifest.json")
    summary_schema = _schema(summary, "summary.json", f"{run}/summary.json")
    expected_summary = manifest_schema.replace("manifest", "summary")
    if summary_schema != expected_summary:
        raise ComparisonError(f"{run}: manifest and summary schema versions differ")

    if manifest.get("driver") != "bot-benchmark":
        raise ComparisonError(f"{run}/manifest.json: driver must be 'bot-benchmark'")
    config_id = _string(manifest.get("config_id"), f"{run}/manifest.json.config_id")
    _string(manifest.get("url"), f"{run}/manifest.json.url")
    _strict_int(manifest.get("seed"), f"{run}/manifest.json.seed", 0)
    _strict_int(manifest.get("max_ticks"), f"{run}/manifest.json.max_ticks", 1)
    _number(manifest.get("fixed_delta"), f"{run}/manifest.json.fixed_delta", positive=True)
    difficulty = _strict_int(manifest.get("difficulty"), f"{run}/manifest.json.difficulty", 0)
    if difficulty > 7:
        raise ComparisonError(f"{run}/manifest.json.difficulty: expected a value at most 7")
    event_cap = _strict_int(
        manifest.get("telemetry_event_cap"), f"{run}/manifest.json.telemetry_event_cap", 1)
    if "output_directory" in manifest and not isinstance(manifest["output_directory"], str):
        raise ComparisonError(f"{run}/manifest.json.output_directory: expected a string")
    config_fields = ("url", "seed", "max_ticks", "fixed_delta", "difficulty")
    if manifest_schema.endswith("v2"):
        bot_count = _strict_int(manifest.get("bot_count"), f"{run}/manifest.json.bot_count", 1)
        if bot_count > 16:
            raise ComparisonError(f"{run}/manifest.json.bot_count: expected a value at most 16")
        requested = _validate_roster(
            manifest.get("requested_roster"), f"{run}/manifest.json.requested_roster", "roster_index")
        if len(requested) != bot_count:
            raise ComparisonError(f"{run}/manifest.json: bot_count differs from requested_roster length")
        if "harmful_zone_escape_enabled" in manifest:
            if not isinstance(manifest["harmful_zone_escape_enabled"], bool):
                raise ComparisonError(
                    f"{run}/manifest.json.harmful_zone_escape_enabled: expected a boolean")
            config_fields += ("harmful_zone_escape_enabled",)

    if summary.get("status") != "complete" or _strict_int(
            summary.get("exit_code"), f"{run}/summary.json.exit_code") != 0:
        raise ComparisonError(f"{run}/summary.json: run is not complete and successful")
    summary_ticks = _strict_int(summary.get("ticks"), f"{run}/summary.json.ticks", 0)
    summary_seconds = _number(
        summary.get("simulated_seconds"), f"{run}/summary.json.simulated_seconds")
    if summary_ticks > _strict_int(manifest["max_ticks"], f"{run}/manifest.json.max_ticks"):
        raise ComparisonError(f"{run}/summary.json: ticks exceed manifest max_ticks")
    if summary.get("failure_reason") != "":
        raise ComparisonError(f"{run}/summary.json: complete run has a failure reason")
    _string(summary.get("map"), f"{run}/summary.json.map")
    summary_config = _object(summary.get("config"), f"{run}/summary.json.config")
    _string(summary_config.get("url"), f"{run}/summary.json.config.url")
    _strict_int(summary_config.get("seed"), f"{run}/summary.json.config.seed", 0)
    _strict_int(summary_config.get("max_ticks"), f"{run}/summary.json.config.max_ticks", 1)
    _number(summary_config.get("fixed_delta"), f"{run}/summary.json.config.fixed_delta", positive=True)
    summary_difficulty = _strict_int(
        summary_config.get("difficulty"), f"{run}/summary.json.config.difficulty", 0)
    if summary_difficulty > 7:
        raise ComparisonError(f"{run}/summary.json.config.difficulty: expected a value at most 7")
    if "harmful_zone_escape_enabled" in config_fields and not isinstance(
            summary_config.get("harmful_zone_escape_enabled"), bool):
        raise ComparisonError(
            f"{run}/summary.json.config.harmful_zone_escape_enabled: expected a boolean")
    for field in config_fields:
        if summary_config.get(field) != manifest.get(field):
            raise ComparisonError(f"{run}: summary.config.{field} differs from manifest.{field}")
    if "output_directory" in manifest and summary_config.get("output_directory") != manifest["output_directory"]:
        raise ComparisonError(f"{run}: summary.config.output_directory differs from manifest")

    actual_identities: list[str] | None = None
    if manifest_schema.endswith("v2"):
        if summary.get("requested_roster") != manifest.get("requested_roster"):
            raise ComparisonError(f"{run}: summary requested_roster differs from manifest")
        if summary_config.get("bot_count") != manifest.get("bot_count"):
            raise ComparisonError(f"{run}: summary.config.bot_count differs from manifest.bot_count")
        _strict_int(summary_config.get("bot_count"), f"{run}/summary.json.config.bot_count", 1)
        actual = _validate_roster(
            summary.get("actual_roster"), f"{run}/summary.json.actual_roster", "roster_index")
        if len(actual) != len(manifest["requested_roster"]):
            raise ComparisonError(f"{run}/summary.json: actual_roster is incomplete")
        actual_identities = [_string(item.get("identity"), "actual_roster.identity") for item in actual]

    event_schema: str | None = None
    expected_event_schema = "surreal-bot-benchmark-telemetry-" + manifest_schema.rsplit("-", 1)[-1]
    previous_tick = -1
    if len(events) > event_cap:
        raise ComparisonError(f"{run}/events.jsonl: line count exceeds telemetry_event_cap")
    for index, event in enumerate(events):
        context = f"{run}/events.jsonl:line {index + 1}"
        current_schema = _schema(event, "events.jsonl", context)
        if current_schema != expected_event_schema:
            raise ComparisonError(f"{context}: telemetry schema version differs from manifest")
        if event_schema is not None and current_schema != event_schema:
            raise ComparisonError(f"{context}: event schema changed within the stream")
        event_schema = current_schema
        if event.get("config_id") != config_id:
            raise ComparisonError(f"{context}: config_id differs from manifest")
        if _strict_int(event.get("seq"), f"{context}.seq", 0) != index:
            raise ComparisonError(f"{context}: seq does not match line order")
        tick = _strict_int(event.get("tick"), f"{context}.tick", 0)
        if tick > _strict_int(manifest["max_ticks"], f"{run}/manifest.json.max_ticks"):
            raise ComparisonError(f"{context}: tick exceeds manifest max_ticks")
        if tick < previous_tick:
            raise ComparisonError(f"{context}: tick order regressed")
        previous_tick = tick
        seconds = _number(event.get("simulated_seconds"), f"{context}.simulated_seconds")
        expected_seconds = tick * _number(
            manifest["fixed_delta"], f"{run}/manifest.json.fixed_delta", positive=True)
        if not math.isclose(seconds, expected_seconds, rel_tol=1.0e-6, abs_tol=1.0e-6):
            raise ComparisonError(f"{context}: simulated_seconds differs from tick * fixed_delta")
        _string(event.get("map"), f"{context}.map")
        if not isinstance(event.get("status"), str) or not isinstance(event.get("failure_reason"), str):
            raise ComparisonError(f"{context}: status and failure_reason must be strings")
        if actual_identities is not None:
            bots = _array(event.get("bots"), f"{context}.bots")
            identities = [_string(_object(bot, f"{context}.bots").get("identity"),
                                  f"{context}.bots.identity") for bot in bots]
            if identities != actual_identities:
                raise ComparisonError(f"{context}: bot roster/order differs from actual_roster")
    if events[0].get("type") != "run_start" or events[-1].get("type") != "run_result":
        raise ComparisonError(f"{run}/events.jsonl: stream must start with run_start and end with run_result")
    if events[-1].get("status") != "complete":
        raise ComparisonError(f"{run}/events.jsonl: final run_result is not complete")
    if events[-1].get("failure_reason") != "":
        raise ComparisonError(f"{run}/events.jsonl: complete run_result has a failure reason")
    if previous_tick != summary_ticks:
        raise ComparisonError(f"{run}: summary ticks differ from final telemetry tick")
    event_maps = {event.get("map") for event in events}
    if len(event_maps) != 1 or summary.get("map") not in event_maps:
        raise ComparisonError(f"{run}: telemetry and summary maps differ")
    if not math.isclose(_number(events[-1]["simulated_seconds"], "final telemetry time"),
                        summary_seconds, rel_tol=1.0e-6, abs_tol=1.0e-6):
        raise ComparisonError(f"{run}: summary time differs from final telemetry time")

    shadow_manifest = artifacts.get("shadow-manifest.json")
    shadow_events = artifacts.get("shadow-decisions.jsonl")
    if (shadow_manifest is None) != (shadow_events is None):
        raise ComparisonError(f"{run}: shadow-manifest.json and shadow-decisions.jsonl must appear together")
    if shadow_manifest is not None and shadow_events is not None:
        document = shadow_manifest.documents[0]
        _schema(document, "shadow-manifest.json", f"{run}/shadow-manifest.json")
        if document.get("benchmark_config_id") != config_id:
            raise ComparisonError(f"{run}/shadow-manifest.json: benchmark_config_id differs from manifest")
        shadow_roster = _validate_roster(
            document.get("participants"), f"{run}/shadow-manifest.json.participants", "roster_index")
        shadow_identities = [_string(item.get("identity"), "shadow participant identity")
                             for item in shadow_roster]
        if actual_identities is not None and shadow_identities != actual_identities:
            raise ComparisonError(f"{run}/shadow-manifest.json: participant roster/order differs")
        prior_tick = -1
        for index, event in enumerate(shadow_events.documents):
            context = f"{run}/shadow-decisions.jsonl:line {index + 1}"
            _schema(event, "shadow-decisions.jsonl", context)
            if event.get("benchmark_config_id") != config_id:
                raise ComparisonError(f"{context}: benchmark_config_id differs from manifest")
            if _strict_int(event.get("seq"), f"{context}.seq", 0) != index:
                raise ComparisonError(f"{context}: seq does not match line order")
            tick = _strict_int(event.get("tick"), f"{context}.tick", 0)
            if tick < prior_tick:
                raise ComparisonError(f"{context}: tick order regressed")
            prior_tick = tick
            event_participants = _validate_roster(
                event.get("participants"), f"{context}.participants", "roster_index")
            event_identities = [_string(item.get("identity"), "shadow participant identity")
                                for item in event_participants]
            if event_identities != shadow_identities:
                raise ComparisonError(f"{context}: participant roster/order differs from shadow manifest")


def _discover(run: Path) -> dict[str, Artifact]:
    try:
        resolved = run.resolve(strict=True)
    except OSError as exc:
        raise ComparisonError(f"run directory does not exist: {run}") from exc
    if not resolved.is_dir():
        raise ComparisonError(f"run path is not a directory: {resolved}")
    names = set(REQUIRED_ARTIFACTS)
    for child in resolved.iterdir():
        if child.is_file() and child.name.startswith("shadow-"):
            if child.suffix not in {".json", ".jsonl"}:
                raise ComparisonError(f"{child}: unsupported shadow artifact extension")
            if child.name not in SUPPORTED_SHADOW_ARTIFACTS:
                raise ComparisonError(f"{child}: unsupported shadow artifact name")
            names.add(child.name)
    artifacts = {name: _read_artifact(resolved / name, name) for name in sorted(names)}
    _validate_run(resolved, artifacts)
    return artifacts


def _ignored_value(field: str, value: Any, context: str) -> None:
    # A/B runs normally live in distinct directories. This one run-local string
    # is permitted only when the caller names it and remains fully audited.
    if field == "output_directory" and isinstance(value, str):
        return
    if isinstance(value, bool):
        raise ComparisonError(f"{context}: ignored counter field must be an integer value")
    if isinstance(value, int):
        return
    if isinstance(value, str) and INTEGER_STRING.fullmatch(value):
        return
    raise ComparisonError(
        f"{context}: ignored fields are limited to integer-valued behavior-neutral counters")


def _normalize(value: Any, ignored: set[str], ignored_diagnostics: set[str],
               pointer: str, line: int | None,
               occurrences: dict[str, list[dict[str, Any]]]) -> Any:
    if isinstance(value, dict):
        result: dict[str, Any] = {}
        for key in sorted(value):
            child_pointer = f"{pointer}/{_pointer_token(key)}"
            if key in ignored:
                _ignored_value(key, value[key], child_pointer)
                occurrence = {"pointer": child_pointer, "value": value[key]}
                if line is not None:
                    occurrence["line"] = line
                occurrences.setdefault(key, []).append(occurrence)
            elif key in ignored_diagnostics:
                occurrence = {"pointer": child_pointer, "value": value[key]}
                if line is not None:
                    occurrence["line"] = line
                occurrences.setdefault(key, []).append(occurrence)
            else:
                result[key] = _normalize(
                    value[key], ignored, ignored_diagnostics, child_pointer, line, occurrences)
        return result
    if isinstance(value, list):
        return [_normalize(item, ignored, ignored_diagnostics, f"{pointer}/{index}", line,
                           occurrences)
                for index, item in enumerate(value)]
    if isinstance(value, float) and value == 0.0:
        return 0.0
    return value


def _normalized_artifact(artifact: Artifact, ignored: set[str], ignored_diagnostics: set[str]) -> tuple[list[dict[str, Any]], bytes, dict[str, Any]]:
    occurrences: dict[str, list[dict[str, Any]]] = {}
    documents = [
        _normalize(copy.deepcopy(document), ignored, ignored_diagnostics, "",
                   index + 1 if artifact.jsonl else None, occurrences)
        for index, document in enumerate(artifact.documents)
    ]
    normalized = b"".join(_canonical(document) for document in documents)
    audit: dict[str, Any] = {}
    for field, entries in sorted(occurrences.items()):
        if field == "output_directory":
            allowed_pointers = {
                "manifest.json": {"/output_directory"},
                "summary.json": {"/config/output_directory"},
            }.get(artifact.name, set())
            if any(entry["pointer"] not in allowed_pointers for entry in entries):
                raise ComparisonError(
                    f"{artifact.name}: output_directory ignore occurred outside its run-local metadata path")
        elif not artifact.jsonl:
            raise ComparisonError(
                f"{artifact.name}: counter and diagnostic ignores are limited to JSONL streams")
        digest_bytes = b"".join(_canonical(entry) for entry in entries)
        audit[field] = {
            "occurrences": len(entries),
            "occurrences_sha256": _sha256(digest_bytes),
            "first": entries[0],
            "last": entries[-1],
        }
    return documents, normalized, audit


def _mismatch(left: Any, right: Any, pointer: str = "") -> dict[str, Any] | None:
    if type(left) is not type(right):
        return {"location": pointer or "/", "kind": "type", "left": left, "right": right}
    if isinstance(left, dict):
        left_keys, right_keys = set(left), set(right)
        if left_keys != right_keys:
            return {
                "location": pointer or "/", "kind": "object_fields",
                "left_only": sorted(left_keys - right_keys),
                "right_only": sorted(right_keys - left_keys),
            }
        for key in sorted(left):
            mismatch = _mismatch(left[key], right[key], f"{pointer}/{_pointer_token(key)}")
            if mismatch is not None:
                return mismatch
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return {"location": pointer or "/", "kind": "array_length",
                    "left": len(left), "right": len(right)}
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            mismatch = _mismatch(left_item, right_item, f"{pointer}/{index}")
            if mismatch is not None:
                return mismatch
        return None
    if left != right:
        return {"location": pointer or "/", "kind": "value", "left": left, "right": right}
    return None


def compare_runs(left_run: Path, right_run: Path, ignore_fields: list[str],
                 ignore_diagnostic_fields: list[str] | None = None) -> dict[str, Any]:
    ignore_diagnostic_fields = ignore_diagnostic_fields or []
    if len(ignore_fields) != len(set(ignore_fields)):
        raise ComparisonError("ignore fields must be unique")
    for field in ignore_fields:
        if not field or field.strip() != field:
            raise ComparisonError("ignore fields must be non-empty names without surrounding whitespace")
        if field in PROTECTED_FIELDS:
            raise ComparisonError(f"cannot ignore protected structural field {field!r}")
        if field != "output_directory" and COUNTER_FIELD.fullmatch(field) is None:
            raise ComparisonError(
                f"ignored counter field {field!r} must be a safe identifier ending in '_exact'")
    if len(ignore_diagnostic_fields) != len(set(ignore_diagnostic_fields)):
        raise ComparisonError("ignored diagnostic fields must be unique")
    for field in ignore_diagnostic_fields:
        if not field or field.strip() != field:
            raise ComparisonError(
                "ignored diagnostic fields must be non-empty names without surrounding whitespace")
        if field in PROTECTED_FIELDS:
            raise ComparisonError(f"cannot ignore protected structural field {field!r}")
        if DIAGNOSTIC_FIELD.fullmatch(field) is None \
                and field not in KNOWN_RECORD_DIAGNOSTIC_FIELDS:
            raise ComparisonError(
                f"ignored diagnostic field {field!r} must be a safe identifier ending in "
                f"'_diagnostics' or a recognized bounded record array")
    overlap = set(ignore_fields) & set(ignore_diagnostic_fields)
    if overlap:
        raise ComparisonError(f"fields cannot use both ignore modes: {', '.join(sorted(overlap))}")
    ignored = set(ignore_fields)
    ignored_diagnostics = set(ignore_diagnostic_fields)
    try:
        left_resolved = left_run.resolve(strict=True)
        right_resolved = right_run.resolve(strict=True)
    except OSError as exc:
        raise ComparisonError("both run directories must exist") from exc
    if left_resolved == right_resolved:
        raise ComparisonError("left and right run directories must be distinct")
    left = _discover(left_resolved)
    right = _discover(right_resolved)
    left_names, right_names = set(left), set(right)
    all_names = sorted(left_names | right_names)
    report: dict[str, Any] = {
        "schema": REPORT_SCHEMA,
        "equivalent": True,
        "left_run": str(left_run.resolve()),
        "right_run": str(right_run.resolve()),
        "ignore_fields": ignore_fields,
        "ignore_diagnostic_fields": ignore_diagnostic_fields,
        "artifacts": [],
    }
    if left_names != right_names:
        report["equivalent"] = False
        report["mismatch"] = {
            "kind": "artifact_set", "location": "/",
            "left_only": sorted(left_names - right_names),
            "right_only": sorted(right_names - left_names),
        }

    used_ignores: set[str] = set()
    for name in all_names:
        artifact_report: dict[str, Any] = {"path": name}
        if name not in left or name not in right:
            artifact_report["equivalent"] = False
            artifact_report["missing_from"] = "left" if name not in left else "right"
            report["artifacts"].append(artifact_report)
            continue
        left_documents, left_normalized, left_audit = _normalized_artifact(
            left[name], ignored, ignored_diagnostics)
        right_documents, right_normalized, right_audit = _normalized_artifact(
            right[name], ignored, ignored_diagnostics)
        used_ignores.update(left_audit)
        used_ignores.update(right_audit)
        artifact_report.update({
            "line_count": len(left_documents) if left[name].jsonl else None,
            "left": {
                "size": len(left[name].raw), "raw_sha256": _sha256(left[name].raw),
                "normalized_sha256": _sha256(left_normalized), "ignored": left_audit,
            },
            "right": {
                "size": len(right[name].raw), "raw_sha256": _sha256(right[name].raw),
                "normalized_sha256": _sha256(right_normalized), "ignored": right_audit,
            },
        })
        if left[name].jsonl and len(left_documents) != len(right_documents):
            mismatch = {"kind": "line_count", "location": "/",
                        "left": len(left_documents), "right": len(right_documents)}
        else:
            mismatch = None
            for index, (left_document, right_document) in enumerate(
                    zip(left_documents, right_documents)):
                mismatch = _mismatch(left_document, right_document)
                if mismatch is not None:
                    if left[name].jsonl:
                        mismatch["line"] = index + 1
                    break
        artifact_report["equivalent"] = mismatch is None
        if mismatch is not None:
            artifact_report["mismatch"] = mismatch
            report["equivalent"] = False
            report.setdefault("mismatch", {"artifact": name, **mismatch})
        report["artifacts"].append(artifact_report)

    unused = [field for field in ignore_fields if field not in used_ignores]
    unused_diagnostics = [field for field in ignore_diagnostic_fields if field not in used_ignores]
    if unused:
        raise ComparisonError(f"ignore fields were not present in either run: {', '.join(unused)}")
    if unused_diagnostics:
        raise ComparisonError(
            "ignored diagnostic fields were not present in either run: " +
            ", ".join(unused_diagnostics))
    return report


def _write_report(report: dict[str, Any], output: str | None,
                  left_run: Path, right_run: Path) -> None:
    rendered = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if output is None or output == "-":
        sys.stdout.write(rendered)
        return
    target = Path(output).resolve()
    for run in (left_run.resolve(), right_run.resolve()):
        if target == run or run in target.parents:
            raise ComparisonError("refusing to write a report inside either immutable QA run")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(rendered, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left_run", type=Path)
    parser.add_argument("right_run", type=Path)
    parser.add_argument("--ignore-field", action="append", default=[], metavar="NAME",
                        help="explicit counter field to remove (repeatable); output_directory is also permitted")
    parser.add_argument("--ignore-diagnostic-field", action="append", default=[], metavar="NAME",
                        help="explicit *_diagnostics or recognized bounded record JSON payload "
                             "to remove (repeatable)")
    parser.add_argument("--output", help="write JSON report here; defaults to stdout")
    args = parser.parse_args(argv)
    try:
        report = compare_runs(
            args.left_run, args.right_run, args.ignore_field, args.ignore_diagnostic_field)
        _write_report(report, args.output, args.left_run, args.right_run)
        return 0 if report["equivalent"] else 1
    except ComparisonError as exc:
        error_report = {"schema": REPORT_SCHEMA, "equivalent": False, "error": str(exc)}
        try:
            _write_report(error_report, args.output, args.left_run, args.right_run)
        except ComparisonError as output_exc:
            sys.stderr.write(f"comparison failed: {exc}; report failed: {output_exc}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
