#!/usr/bin/env python3
"""Fail-closed validation of native path-cache commit provenance."""

from __future__ import annotations

import argparse
import importlib.util
import json
from collections import Counter
from pathlib import Path
from typing import Any


SCHEMA = "surreal-native-path-commit-analysis-v1"
ROUTE_SCHEMA = "surreal-bot-route-execution-observation-v1"


class PathCommitError(ValueError):
    pass


def _module(filename: str, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise PathCommitError(f"could not load {filename}")
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def _json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise PathCommitError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PathCommitError(f"{path} root must be an object")
    return value


def _nonnegative(value: Any, context: str) -> int:
    if isinstance(value, int) and not isinstance(value, bool) and value >= 0:
        return value
    if isinstance(value, str) and value.isdecimal():
        return int(value)
    raise PathCommitError(f"{context} must be an unsigned decimal")


def _map_name(url: Any) -> str:
    if not isinstance(url, str) or not url:
        raise PathCommitError("manifest.url must be non-empty")
    return url.split("?", 1)[0]


def _tick_events(path: Path):
    try:
        with path.open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise PathCommitError(f"event telemetry line {line_number} is not an object")
                if value.get("type") == "tick":
                    yield value
    except (OSError, json.JSONDecodeError) as exc:
        raise PathCommitError(f"cannot read event telemetry: {exc}") from exc


def analyze(catalog_path: Path, run: Path) -> dict[str, Any]:
    map_validator = _module("Validate-MapCatalog.py", "path_commit_catalog")
    capabilities = _module("Validate-RealizedBotCapabilities.py", "path_commit_capabilities")
    try:
        catalog = _json(catalog_path)
        map_validator.validate_catalog(catalog)
        capabilities.validate_run(run)
    except (PathCommitError, map_validator.CatalogError, capabilities.CapabilityError) as exc:
        raise PathCommitError(str(exc)) from exc
    manifest = _json(run / "manifest.json")
    config_id = manifest.get("config_id")
    if not isinstance(config_id, str) or not config_id:
        raise PathCommitError("manifest.config_id must be non-empty")
    if catalog["map"].casefold() != _map_name(manifest.get("url")).casefold():
        raise PathCommitError("catalog map does not match benchmark manifest")
    witness = _json(run / capabilities.ARTIFACT_NAME)
    expected = {item["identity"] for item in witness["participants"]}
    specs = catalog["reachspecs"]
    nodes = {point["name"]: point for point in catalog["navigation_points"]}
    counts: Counter[str] = Counter()
    sequences = {identity: 0 for identity in expected}
    previous_deaths = {identity: 0 for identity in expected}
    previous_hazard = {identity: False for identity in expected}
    last_native_commit: dict[str, dict[str, Any] | None] = {identity: None for identity in expected}
    death_contexts: list[dict[str, Any]] = []
    hazard_entry_contexts: list[dict[str, Any]] = []
    event_iter = _tick_events(run / "events.jsonl")
    try:
        stream = (run / "route-execution.jsonl").open(encoding="utf-8")
    except OSError as exc:
        raise PathCommitError(f"cannot read route telemetry: {exc}") from exc
    def validate_record(identity: str, tick: int, record: dict[str, Any]) -> None:
        sequence = _nonnegative(record.get("sequence"), "native path commit sequence")
        if sequence != sequences[identity] + 1:
            raise PathCommitError(f"{identity} native path commit sequence is not contiguous")
        sequences[identity] = sequence
        if record.get("origin") not in {"find_path_toward", "find_best_inventory_path"}:
            raise PathCommitError("native path commit has an invalid origin")
        if record.get("phase") != "pre_special_cache_commit":
            raise PathCommitError("native path commit has an invalid phase")
        for field in ("raw_endpoint_cost", "adjusted_endpoint_cost", "failed_navigation_penalty_applications"):
            _nonnegative(record.get(field), f"native path commit {field}")
        cache_clear, truncated = record.get("cache_clear"), record.get("truncated_by_route_cache")
        nodes_record, edges = record.get("nodes"), record.get("edges")
        if not isinstance(cache_clear, bool) or not isinstance(truncated, bool):
            raise PathCommitError("native path commit booleans are invalid")
        if not isinstance(nodes_record, list) or not isinstance(edges, list):
            raise PathCommitError("native path commit nodes and edges must be arrays")
        if cache_clear:
            if nodes_record or edges or truncated:
                raise PathCommitError("cache-clear path commit must be empty and untruncated")
            counts["cache_clears_exact"] += 1
            return
        if not nodes_record or len(nodes_record) > 16 or len(edges) != len(nodes_record) - 1:
            raise PathCommitError("native path commit has an invalid bounded node/edge shape")
        names: list[str] = []
        for node in nodes_record:
            if not isinstance(node, dict) or not isinstance(node.get("name"), str) or not isinstance(node.get("class"), str):
                raise PathCommitError("native path commit node is invalid")
            catalog_node = nodes.get(node["name"])
            if catalog_node is None or catalog_node["class"].rsplit(".", 1)[-1] != node["class"].rsplit(".", 1)[-1]:
                raise PathCommitError("native path commit node does not match catalog")
            names.append(node["name"])
        for index, edge in enumerate(edges):
            if not isinstance(edge, dict):
                raise PathCommitError("native path commit edge is invalid")
            spec_index = edge.get("reachspec_index")
            if not isinstance(spec_index, int) or spec_index < 0 or spec_index >= len(specs):
                raise PathCommitError("native path commit reachspec index is invalid")
            spec = specs[spec_index]
            if edge.get("start_node") != names[index] or edge.get("end_node") != names[index + 1]:
                raise PathCommitError("native path commit edge does not bind adjacent committed nodes")
            if spec["start_actor_index"] != nodes[names[index]]["actor_index"] or spec["end_actor_index"] != nodes[names[index + 1]]["actor_index"]:
                raise PathCommitError("native path commit edge does not match catalog reachspec endpoints")
            for field, catalog_field in (("distance", "distance"), ("collision_radius", "collision_radius"),
                                         ("collision_height", "collision_height"), ("reach_flags_raw", "reach_flags"),
                                         ("unknown_reach_flags", "unknown_reach_flags"), ("pruned", "pruned")):
                if edge.get(field) != spec[catalog_field]:
                    raise PathCommitError(f"native path commit edge {field} differs from catalog")
            if edge["pruned"]:
                raise PathCommitError("native path commit selected a pruned reachspec")
        counts["committed_paths_exact"] += 1
        counts["committed_edges_exact"] += len(edges)
        counts["truncated_commits_exact"] += int(truncated)
        last_native_commit[identity] = {"tick": str(tick), "origin": record["origin"],
                                        "first_edge": edges[0] if edges else None}

    try:
        with stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise PathCommitError(f"route telemetry line {line_number}: {exc}") from exc
                if row.get("schema") != ROUTE_SCHEMA or row.get("benchmark_config_id") != config_id:
                    raise PathCommitError(f"route telemetry line {line_number} has incompatible identity")
                tick = _nonnegative(row.get("tick"), "route telemetry tick")
                try:
                    event = next(event_iter)
                except StopIteration as exc:
                    raise PathCommitError("route telemetry exceeds tick events") from exc
                if _nonnegative(event.get("tick"), "event telemetry tick") != tick:
                    raise PathCommitError("route and event telemetry ticks do not reconcile")
                participants, event_bots = row.get("participants"), event.get("bots")
                if not isinstance(participants, list) or not isinstance(event_bots, list):
                    raise PathCommitError("route participants and event bots must be arrays")
                identities = {item.get("identity") for item in participants}
                event_by_identity = {item.get("identity"): item for item in event_bots}
                if identities != expected or len(identities) != len(participants):
                    raise PathCommitError("route participant identities do not match capability witness")
                if set(event_by_identity) != expected or len(event_by_identity) != len(event_bots):
                    raise PathCommitError("event participant identities do not match capability witness")
                for participant in participants:
                    identity = participant["identity"]
                    if _nonnegative(participant.get("native_path_commit_overflows_exact"),
                                    f"{identity}.native_path_commit_overflows_exact") != 0:
                        raise PathCommitError(f"{identity} has native path-commit record overflow")
                    records = participant.get("native_path_commits")
                    if not isinstance(records, list):
                        raise PathCommitError(f"{identity}.native_path_commits must be an array")
                    for record in records:
                        if not isinstance(record, dict):
                            raise PathCommitError("native path commit must be an object")
                        validate_record(identity, tick, record)
                    live = event_by_identity[identity]
                    in_hazard = live.get("in_hazard_zone")
                    if not isinstance(in_hazard, bool):
                        raise PathCommitError(f"event {identity}.in_hazard_zone must be a boolean")
                    if in_hazard and not previous_hazard[identity]:
                        last = last_native_commit[identity]
                        hazard_entry_contexts.append({"identity": identity, "tick": str(tick),
                            "latent_action": live.get("latent_action"), "last_native_path_commit": last,
                            "ticks_since_last_native_path_commit": tick - int(last["tick"]) if last else None})
                    previous_hazard[identity] = in_hazard
                    deaths = _nonnegative(live.get("deaths_exact"), f"event {identity}.deaths_exact")
                    if deaths < previous_deaths[identity]:
                        raise PathCommitError(f"event {identity}.deaths_exact regressed")
                    if deaths > previous_deaths[identity]:
                        last = last_native_commit[identity]
                        death_contexts.append({"identity": identity, "tick": str(tick),
                            "death_counter_delta_exact": str(deaths - previous_deaths[identity]),
                            "in_hazard_zone": live.get("in_hazard_zone"), "latent_action": live.get("latent_action"),
                            "last_native_path_commit": last,
                            "ticks_since_last_native_path_commit": tick - int(last["tick"]) if last else None})
                    previous_deaths[identity] = deaths
        try:
            next(event_iter)
        except StopIteration:
            pass
        else:
            raise PathCommitError("tick events exceed route telemetry")
    finally:
        event_iter.close()
    return {"schema": SCHEMA, "catalog": {"map": catalog["map"], "map_package_sha1": catalog["map_package"]["sha1"]},
            "benchmark": {"config_id": config_id, "map": _map_name(manifest["url"])},
            "counts": dict(sorted(counts.items())), "per_participant_final_sequence": sequences,
            "death_contexts": death_contexts, "hazard_entry_contexts": hazard_entry_contexts,
            "qualified": True}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path); parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        encoded = json.dumps(analyze(args.catalog, args.run), indent=2, sort_keys=True) + "\n"
        if args.output: args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(encoded, encoding="utf-8")
        else: print(encoded, end="")
    except PathCommitError as exc:
        print(f"error: {exc}", file=__import__("sys").stderr); return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
