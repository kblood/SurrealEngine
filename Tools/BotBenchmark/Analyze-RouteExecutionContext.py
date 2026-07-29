#!/usr/bin/env python3
"""Join read-only route execution with a validated map graph and bot capabilities.

This reports the currently observed first hop of an active route.  It is not a
path-selection oracle: dynamic collision, reach-flag semantics, endpoint
eligibility, and the native path search remain outside its scope.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from collections import Counter
from pathlib import Path
from typing import Any, Iterator


SCHEMA = "surreal-bot-route-execution-context-v1"
ROUTE_SCHEMA = "surreal-bot-route-execution-observation-v1"
FLAG_TO_CAPABILITY = {
    "walk": "walk", "fly": "fly", "swim": "swim", "jump": "jump",
    "door": "open_doors", "special": "special",
}


class ContextError(ValueError):
    pass


def _load_module(filename: str, module_name: str) -> Any:
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise ContextError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ContextError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ContextError(f"{path}: root must be an object")
    return value


def _tick_events(path: Path) -> Iterator[dict[str, Any]]:
    try:
        with path.open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise ContextError(f"{path}:{line_number}: event must be an object")
                if value.get("type") == "tick":
                    yield value
    except (OSError, json.JSONDecodeError) as exc:
        raise ContextError(f"cannot read {path}: {exc}") from exc


def _route_records(path: Path) -> Iterator[dict[str, Any]]:
    try:
        with path.open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise ContextError(f"{path}:{line_number}: route record must be an object")
                yield value
    except (OSError, json.JSONDecodeError) as exc:
        raise ContextError(f"cannot read {path}: {exc}") from exc


def _map_from_url(value: Any) -> str:
    if not isinstance(value, str) or not value:
        raise ContextError("benchmark manifest url must be a non-empty string")
    return value.split("?", 1)[0]


def _identifier(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise ContextError(f"{context} must be a non-empty string")
    return value


def _exact_counter(value: Any, context: str) -> int:
    if isinstance(value, int) and not isinstance(value, bool) and value >= 0:
        return value
    if not isinstance(value, str) or not value.isdecimal():
        raise ContextError(f"{context} must be an unsigned decimal string")
    return int(value)


def _node(reference: Any, nodes: dict[str, dict[str, Any]], context: str) -> dict[str, Any]:
    if not isinstance(reference, dict):
        raise ContextError(f"{context} must be an object")
    name = _identifier(reference.get("name"), f"{context}.name")
    class_name = _identifier(reference.get("class"), f"{context}.class")
    node = nodes.get(name)
    if node is None:
        raise ContextError(f"{context}.name {name!r} is not a catalog navigation point")
    # The catalog preserves package-qualified classes while the runtime route
    # observer serializes the UE1 class name.  Both representations are stable
    # only when their unqualified class component agrees.
    if node["class"].rsplit(".", 1)[-1] != class_name.rsplit(".", 1)[-1]:
        raise ContextError(f"{context}.class does not match catalog navigation point {name!r}")
    return node


def _first_hop(
    participant: dict[str, Any], nodes: dict[str, dict[str, Any]],
    specs_by_edge: dict[tuple[int, int], list[dict[str, Any]]], capabilities: dict[str, bool],
) -> tuple[str, dict[str, Any] | None]:
    cache = participant.get("route_cache")
    if not isinstance(cache, list):
        raise ContextError("route_cache must be an array")
    if len(cache) < 2:
        return "no_first_hop", None
    start = _node(cache[0], nodes, "route_cache[0]")
    end = _node(cache[1], nodes, "route_cache[1]")
    matching = specs_by_edge.get((start["actor_index"], end["actor_index"]), [])
    if not matching:
        return "no_directed_reachspec", {"from": start["name"], "to": end["name"]}
    if len(matching) != 1:
        return "ambiguous_directed_reachspec", {"from": start["name"], "to": end["name"]}
    spec = matching[0]
    flags = spec["reach_flag_names"]
    unknown = spec["unknown_reach_flags"] != 0
    opaque = "player_only" in flags
    missing = sorted(
        flag for flag, capability in FLAG_TO_CAPABILITY.items()
        if flag in flags and not capabilities[capability])
    return "resolved", {
        "index": spec["index"], "from": start["name"], "to": end["name"],
        "reach_flag_names": flags, "unknown_reach_flags": unknown,
        "player_only_opaque": opaque, "missing_capability_flags": missing,
    }


def analyze(catalog_path: Path, run: Path) -> dict[str, Any]:
    map_validator = _load_module("Validate-MapCatalog.py", "surreal_map_catalog_validation")
    capability_validator = _load_module(
        "Validate-RealizedBotCapabilities.py", "surreal_realized_capability_validation")
    try:
        catalog = _load_json(catalog_path)
        map_validator.validate_catalog(catalog)
        capability_validator.validate_run(run)
    except (ContextError, map_validator.CatalogError, capability_validator.CapabilityError) as exc:
        raise ContextError(str(exc)) from exc

    manifest = _load_json(run / "manifest.json")
    config_id = _identifier(manifest.get("config_id"), "manifest.config_id")
    map_name = _map_from_url(manifest.get("url"))
    if catalog["map"].casefold() != map_name.casefold():
        raise ContextError(f"catalog map {catalog['map']!r} does not match benchmark map {map_name!r}")
    witness = _load_json(run / capability_validator.ARTIFACT_NAME)
    capability_by_identity = {
        item["identity"]: item["capabilities"] for item in witness["participants"]
    }
    nodes: dict[str, dict[str, Any]] = {}
    for node in catalog["navigation_points"]:
        if node["name"] in nodes:
            raise ContextError(f"catalog has duplicate navigation point name {node['name']!r}")
        nodes[node["name"]] = node
    specs_by_edge: dict[tuple[int, int], list[dict[str, Any]]] = {}
    for spec in catalog["reachspecs"]:
        specs_by_edge.setdefault((spec["start_actor_index"], spec["end_actor_index"]), []).append(spec)

    counts: Counter[str] = Counter()
    per_bot: dict[str, Counter[str]] = {identity: Counter() for identity in capability_by_identity}
    death_contexts: list[dict[str, Any]] = []
    previous_deaths = {identity: 0 for identity in capability_by_identity}
    last_active_first_hop: dict[str, dict[str, Any] | None] = {
        identity: None for identity in capability_by_identity
    }
    event_iter = _tick_events(run / "events.jsonl")
    try:
        expected_sequence = 0
        for route in _route_records(run / "route-execution.jsonl"):
            if route.get("schema") != ROUTE_SCHEMA:
                raise ContextError("route record has an unexpected schema")
            if _exact_counter(route.get("seq"), "route.seq") != expected_sequence:
                raise ContextError("route sequence is not contiguous")
            expected_sequence += 1
            if route.get("benchmark_config_id") != config_id:
                raise ContextError("route benchmark_config_id does not match manifest.config_id")
            try:
                event = next(event_iter)
            except StopIteration as exc:
                raise ContextError("route records exceed tick telemetry") from exc
            tick = _exact_counter(route.get("tick"), "route.tick")
            if _exact_counter(event.get("tick"), "event.tick") != tick:
                raise ContextError("route tick does not match tick telemetry")
            route_people = route.get("participants")
            event_people = event.get("bots")
            if not isinstance(route_people, list) or not isinstance(event_people, list):
                raise ContextError("route participants and event bots must be arrays")
            event_by_identity = {item.get("identity"): item for item in event_people}
            identities = [_identifier(item.get("identity"), "route participant identity") for item in route_people]
            if len(set(identities)) != len(identities) or set(identities) != set(capability_by_identity):
                raise ContextError("route participants do not exactly match realized capability participants")
            if set(event_by_identity) != set(capability_by_identity):
                raise ContextError("tick telemetry bots do not exactly match realized capability participants")
            counts["route_samples_exact"] += len(route_people)
            for participant in route_people:
                identity = participant["identity"]
                live = event_by_identity[identity]
                available = participant.get("available") is True
                active = available and live.get("latent_action") == "MoveToward" and bool(participant.get("move_target"))
                if active:
                    counts["active_move_toward_samples_exact"] += 1
                    per_bot[identity]["active_move_toward_samples_exact"] += 1
                    status, first_hop = _first_hop(
                        participant, nodes, specs_by_edge, capability_by_identity[identity])
                    counts[f"first_hop_{status}_exact"] += 1
                    per_bot[identity][f"first_hop_{status}_exact"] += 1
                    if status == "resolved":
                        last_active_first_hop[identity] = {
                            "tick": tick, "observed_route_first_hop": first_hop,
                        }
                else:
                    status, first_hop = "inactive", None
                deaths = _exact_counter(live.get("deaths_exact"), f"tick {tick} {identity}.deaths_exact")
                if deaths < previous_deaths[identity]:
                    raise ContextError(f"tick {tick} {identity}.deaths_exact regressed")
                if deaths > previous_deaths[identity]:
                    prior_hop = last_active_first_hop[identity]
                    death_contexts.append({
                        "identity": identity, "tick": str(tick),
                        "death_counter_delta_exact": str(deaths - previous_deaths[identity]),
                        "in_hazard_zone": live.get("in_hazard_zone"),
                        "latent_action": live.get("latent_action"),
                        "route_activity": status, "observed_route_first_hop": first_hop,
                        "last_active_route_first_hop": prior_hop,
                        "ticks_since_last_active_route_first_hop": (
                            tick - prior_hop["tick"] if prior_hop is not None else None),
                    })
                previous_deaths[identity] = deaths
        try:
            next(event_iter)
        except StopIteration:
            pass
        else:
            raise ContextError("tick telemetry exceeds route records")
    finally:
        event_iter.close()
    counts["death_counter_increments_exact"] = sum(
        int(item["death_counter_delta_exact"]) for item in death_contexts)
    return {
        "schema": SCHEMA,
        "catalog": {"map": catalog["map"], "map_package_sha1": catalog["map_package"]["sha1"]},
        "benchmark": {"map": map_name, "config_id": config_id},
        "counts": dict(sorted(counts.items())),
        "participants": [{"identity": identity, "counts": dict(sorted(per_bot[identity].items()))}
                         for identity in sorted(per_bot)],
        "death_contexts": death_contexts,
        "interpretation": {
            "selection_safe": False,
            "reason": "Observed route-cache first hops are correlated with static reachspecs only; dynamic collision, native endpoint selection, reach-flag semantics, player-only eligibility, and traversal state are not established.",
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        report = analyze(args.catalog, args.run)
        encoded = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
        if args.output is None:
            print(encoded, end="")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(encoded, encoding="utf-8")
    except ContextError as exc:
        print(f"error: {exc}", file=__import__("sys").stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
