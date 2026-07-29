#!/usr/bin/env python3
"""Fail-closed audit of live capability snapshots on native ReachSpec commits."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any


SCHEMA = "surreal-reachspec-commit-capability-audit-v1"
ROUTE_SCHEMA = "surreal-bot-route-execution-observation-v1"
KNOWN_FLAGS = 127
CAPABILITIES = (
    ("walk", 1), ("fly", 2), ("swim", 4), ("jump", 8),
    ("open_doors", 16), ("special", 32), ("is_player", 64),
)


class ReachSpecCommitCapabilityError(ValueError):
    pass


def _module(filename: str, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise ReachSpecCommitCapabilityError(f"could not load {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReachSpecCommitCapabilityError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ReachSpecCommitCapabilityError(f"{path}: root must be an object")
    return value


def _count(value: Any, context: str, minimum: int = 0) -> int:
    if isinstance(value, bool):
        raise ReachSpecCommitCapabilityError(f"{context} must be an unsigned decimal")
    if isinstance(value, int) and value >= minimum:
        return value
    if isinstance(value, str) and value.isdecimal() and int(value) >= minimum:
        return int(value)
    raise ReachSpecCommitCapabilityError(f"{context} must be an unsigned decimal")


def _exact(value: Any, fields: set[str], context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ReachSpecCommitCapabilityError(f"{context} must be an object")
    if set(value) != fields:
        raise ReachSpecCommitCapabilityError(f"{context} fields differ")
    return value


def _snapshot(value: Any, tick: int, context: str) -> tuple[int, int]:
    snapshot = _exact(value, {"native_tick", "life_id", "capability_flags", "capabilities"}, context)
    if _count(snapshot["native_tick"], f"{context}.native_tick") != tick:
        raise ReachSpecCommitCapabilityError(f"{context}.native_tick does not match route tick")
    life_id = _count(snapshot["life_id"], f"{context}.life_id", 1)
    capabilities = _exact(snapshot["capabilities"], {name for name, _ in CAPABILITIES},
                          f"{context}.capabilities")
    flags = 0
    for name, bit in CAPABILITIES:
        if not isinstance(capabilities[name], bool):
            raise ReachSpecCommitCapabilityError(f"{context}.capabilities.{name} must be boolean")
        if capabilities[name]:
            flags |= bit
    if _count(snapshot["capability_flags"], f"{context}.capability_flags") != flags:
        raise ReachSpecCommitCapabilityError(f"{context}.capability_flags does not match booleans")
    return life_id, flags


def _expected(reach_flags: int, capability_flags: int) -> tuple[bool, int, int, int, str]:
    required = reach_flags & KNOWN_FLAGS
    invalid = reach_flags & ~KNOWN_FLAGS
    if invalid:
        return False, required, 0, invalid, "invalid_flags"
    if required == 0:
        return False, required, 0, 0, "no_requirements"
    missing = required & ~capability_flags
    if missing:
        return False, required, missing, 0, "missing_capabilities"
    return True, required, 0, 0, "eligible"


def _eligibility(value: Any, reach_flags: int, capability_flags: int, context: str) -> str:
    result = _exact(value, {"eligible", "required_flags", "capability_flags", "missing_flags",
                            "invalid_flags", "disposition"}, context)
    if not isinstance(result["eligible"], bool) or not isinstance(result["disposition"], str):
        raise ReachSpecCommitCapabilityError(f"{context} has invalid scalar fields")
    expected = _expected(reach_flags, capability_flags)
    actual = (result["eligible"], _count(result["required_flags"], f"{context}.required_flags"),
              _count(result["missing_flags"], f"{context}.missing_flags"),
              _count(result["invalid_flags"], f"{context}.invalid_flags"), result["disposition"])
    if _count(result["capability_flags"], f"{context}.capability_flags") != capability_flags:
        raise ReachSpecCommitCapabilityError(f"{context}.capability_flags does not match snapshot")
    if actual != expected:
        raise ReachSpecCommitCapabilityError(f"{context} does not match the UE1 capability model")
    return expected[-1]


def analyze(catalog: Path, run: Path) -> dict[str, Any]:
    native = _module("Analyze-NativePathCommits.py", "reachspec_commit_native_path")
    try:
        native_report = native.analyze(catalog, run)
    except native.PathCommitError as exc:
        raise ReachSpecCommitCapabilityError(str(exc)) from exc
    manifest = _load(run / "manifest.json")
    if manifest.get("reachspec_capability_observer_enabled") is not True:
        raise ReachSpecCommitCapabilityError("ReachSpec capability observer must be explicitly enabled")
    if manifest.get("native_path_commit_observer_enabled") is not True:
        raise ReachSpecCommitCapabilityError("ReachSpec capability observer requires native path commits")
    config_id = manifest.get("config_id")
    if not isinstance(config_id, str) or not config_id:
        raise ReachSpecCommitCapabilityError("manifest.config_id must be non-empty")
    summary = _load(run / "summary.json")
    summary_config = summary.get("config")
    if not isinstance(summary_config, dict):
        raise ReachSpecCommitCapabilityError("summary.config must be an object")
    for field in ("native_path_commit_observer_enabled", "reachspec_capability_observer_enabled"):
        if not isinstance(summary_config.get(field), bool):
            raise ReachSpecCommitCapabilityError(f"summary.config.{field} must be boolean")
        if summary_config[field] != manifest.get(field):
            raise ReachSpecCommitCapabilityError(f"summary.config.{field} does not match manifest")

    counts: Counter[str] = Counter()
    try:
        stream = (run / "route-execution.jsonl").open(encoding="utf-8")
    except OSError as exc:
        raise ReachSpecCommitCapabilityError(f"cannot read route telemetry: {exc}") from exc
    with stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                route = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ReachSpecCommitCapabilityError(f"route line {line_number}: {exc}") from exc
            if route.get("schema") != ROUTE_SCHEMA or route.get("benchmark_config_id") != config_id:
                raise ReachSpecCommitCapabilityError(f"route line {line_number} has incompatible identity")
            tick = _count(route.get("tick"), f"route line {line_number}.tick")
            participants = route.get("participants")
            if not isinstance(participants, list):
                raise ReachSpecCommitCapabilityError(f"route line {line_number}.participants must be an array")
            for participant_index, participant in enumerate(participants):
                if not isinstance(participant, dict):
                    raise ReachSpecCommitCapabilityError("route participant must be an object")
                commits = participant.get("native_path_commits")
                if not isinstance(commits, list):
                    raise ReachSpecCommitCapabilityError("native_path_commits must be an array")
                for commit_index, commit in enumerate(commits):
                    context = f"route line {line_number}.participants[{participant_index}].commits[{commit_index}]"
                    if not isinstance(commit, dict):
                        raise ReachSpecCommitCapabilityError(f"{context} must be an object")
                    if "reachspec_capability" not in commit:
                        raise ReachSpecCommitCapabilityError(f"{context} is missing live capability snapshot")
                    life_id, capability_flags = _snapshot(commit["reachspec_capability"], tick, context)
                    counts["capability_snapshots_exact"] += 1
                    counts["life_ids_exact"] += int(life_id > 0)
                    edges = commit.get("edges")
                    if not isinstance(edges, list):
                        raise ReachSpecCommitCapabilityError(f"{context}.edges must be an array")
                    for edge_index, edge in enumerate(edges):
                        if not isinstance(edge, dict):
                            raise ReachSpecCommitCapabilityError(f"{context}.edges[{edge_index}] must be an object")
                        reach_flags = _count(edge.get("reach_flags_raw"), f"{context}.edges[{edge_index}].reach_flags_raw")
                        if "reachspec_eligibility" not in edge:
                            raise ReachSpecCommitCapabilityError(f"{context}.edges[{edge_index}] is missing model result")
                        disposition = _eligibility(edge["reachspec_eligibility"], reach_flags,
                                                   capability_flags, f"{context}.edges[{edge_index}]")
                        counts["committed_edges_exact"] += 1
                        counts[f"{disposition}_edges_exact"] += 1
    counts.setdefault("capability_snapshots_exact", 0)
    counts.setdefault("committed_edges_exact", 0)
    coverage = {"criterion": "at_least_one_capability_evaluated_edge",
                "committed_edges_exact": counts["committed_edges_exact"],
                "qualified": counts["committed_edges_exact"] > 0,
                "verdict": "qualified" if counts["committed_edges_exact"] > 0
                else "unqualified_no_committed_edge"}
    return {"schema": SCHEMA, "native_path_commit": native_report,
            "counts": dict(sorted(counts.items())), "coverage": coverage,
            "qualified": coverage["qualified"]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        encoded = json.dumps(analyze(args.catalog, args.run), indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except ReachSpecCommitCapabilityError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
