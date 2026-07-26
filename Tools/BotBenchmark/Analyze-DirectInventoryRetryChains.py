#!/usr/bin/env python3
"""Fail-closed evidence analysis for stalled direct-inventory retry chains."""

from __future__ import annotations

import argparse
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Any


SCHEMA = "surreal-bot-benchmark-direct-inventory-retry-chain-v1"
MAX_WITNESSES = 64


class RetryChainError(ValueError):
    pass


def _json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RetryChainError(f"{path}: unreadable JSON") from error
    if not isinstance(value, dict):
        raise RetryChainError(f"{path}: expected an object")
    return value


def _count(value: Any, context: str) -> int:
    if isinstance(value, int) and not isinstance(value, bool) and value >= 0:
        return value
    if isinstance(value, str) and value.isascii() and value.isdecimal():
        return int(value)
    raise RetryChainError(f"{context}: expected an unsigned decimal")


def _finite(value: Any, context: str) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value):
        raise RetryChainError(f"{context}: expected a finite number")
    return float(value)


def _inventory_target(record: dict[str, Any], context: str) -> tuple[int, int, str, str, str, str]:
    is_stall = "move_target_is_inventory" in record
    required_bools = (("move_target_is_inventory", "marker_known", "marker_live") if is_stall
                      else ("reached", "target_is_inventory", "marker_known", "marker_live"))
    if any(not isinstance(record.get(field), bool) for field in required_bools):
        raise RetryChainError(f"{context}: inventory identity booleans are invalid")
    target = record.get("move_target_actor_index") if is_stall else record.get("target_actor_index")
    marker = record.get("marker_actor_index")
    target_name = record.get("target_name", record.get("move_target_name"))
    target_class = record.get("target_class", record.get("move_target_class"))
    marker_name = record.get("marker_name")
    marker_class = record.get("marker_class")
    if not all(isinstance(value, str) and value for value in
               (target_name, target_class, marker_name, marker_class)):
        raise RetryChainError(f"{context}: inventory identity names are invalid")
    return (_count(target, f"{context}: target actor index"),
            _count(marker, f"{context}: marker actor index"), target_name, target_class,
            marker_name, marker_class)


def _same_target(left: tuple[int, int, str, str, str, str],
                 right: tuple[int, int, str, str, str, str]) -> bool:
    return left == right


def _events(path: Path):
    try:
        with path.open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as error:
                    raise RetryChainError(f"events line {line_number}: invalid JSON") from error
                if not isinstance(event, dict):
                    raise RetryChainError(f"events line {line_number}: expected object")
                yield line_number, event
    except OSError as error:
        raise RetryChainError(f"{path}: unreadable") from error


def analyze(run: Path) -> dict[str, Any]:
    manifest = _json(run / "manifest.json")
    if manifest.get("direct_reach_command_observer_enabled") is not True:
        raise RetryChainError("direct-reach command observer must be explicitly enabled")
    if manifest.get("native_path_commit_observer_enabled") is not True:
        raise RetryChainError("native path-commit observer must be explicitly enabled")
    config_id = manifest.get("config_id")
    if not isinstance(config_id, str) or not config_id:
        raise RetryChainError("manifest.config_id must be non-empty")
    summary = _json(run / "summary.json")
    if summary.get("status") != "complete" or summary.get("exit_code") != 0:
        raise RetryChainError("summary is not complete and successful")
    identities = {item.get("identity") for item in summary.get("actual_roster", [])
                  if isinstance(item, dict)}
    if not identities or not all(isinstance(identity, str) and identity for identity in identities):
        raise RetryChainError("summary has no complete actual roster")

    direct: dict[str, list[dict[str, Any]]] = defaultdict(list)
    stalls: dict[str, list[dict[str, Any]]] = defaultdict(list)
    direct_sequences = {identity: 0 for identity in identities}
    stall_sequences = {identity: 0 for identity in identities}
    final_overflows = {identity: {"direct": 0, "stall": 0} for identity in identities}
    inventory_direct_verdicts_seen = 0

    for line_number, event in _events(run / "events.jsonl"):
        if event.get("type") != "tick":
            continue
        if event.get("direct_reach_command_observer") != {"requested": True, "status": "active"}:
            raise RetryChainError(f"events line {line_number}: direct-reach observer envelope is incomplete")
        bots = event.get("bots")
        if not isinstance(bots, list):
            raise RetryChainError(f"events line {line_number}: bots is not an array")
        seen: set[str] = set()
        for bot in bots:
            if not isinstance(bot, dict) or bot.get("identity") not in identities:
                raise RetryChainError(f"events line {line_number}: participant identity is invalid")
            identity = bot["identity"]
            if identity in seen:
                raise RetryChainError(f"events line {line_number}: duplicate participant")
            seen.add(identity)
            final_overflows[identity]["direct"] = _count(
                bot.get("direct_reach_command_overflows_exact"),
                f"events line {line_number} {identity}: direct overflow")
            final_overflows[identity]["stall"] = _count(
                bot.get("move_stall_recovery_decision_record_overflows_exact"),
                f"events line {line_number} {identity}: stall overflow")
            if final_overflows[identity]["direct"] != 0 or final_overflows[identity]["stall"] != 0:
                raise RetryChainError(f"{identity}: provenance record overflow")
            records = bot.get("direct_reach_command_records")
            decisions = bot.get("move_stall_recovery_decisions")
            if not isinstance(records, list) or not isinstance(decisions, list):
                raise RetryChainError(f"events line {line_number} {identity}: provenance arrays are invalid")
            for record in records:
                if not isinstance(record, dict):
                    raise RetryChainError(f"events line {line_number} {identity}: direct record is invalid")
                sequence = _count(record.get("sequence"), "direct record sequence")
                if sequence != direct_sequences[identity] + 1:
                    raise RetryChainError(f"{identity}: direct record sequence is not contiguous")
                direct_sequences[identity] = sequence
                native_tick = _count(record.get("native_tick"), "direct native tick")
                life = _count(record.get("life_id"), "direct life id")
                if record.get("caller_origin") != "script_actor_reachable":
                    continue
                if record.get("reached") is not True or record.get("link_status") != "same_life_exact":
                    continue
                key = _inventory_target(record, "direct record")
                if not all((record["target_is_inventory"], record["marker_known"], record["marker_live"])):
                    continue
                direct[identity].append({"native_tick": native_tick, "life_id": life, "target": key})
                inventory_direct_verdicts_seen += 1
            for decision in decisions:
                if not isinstance(decision, dict):
                    raise RetryChainError(f"events line {line_number} {identity}: stall decision is invalid")
                sequence = _count(decision.get("sequence"), "stall decision sequence")
                if sequence != stall_sequences[identity] + 1:
                    raise RetryChainError(f"{identity}: stall decision sequence is not contiguous")
                stall_sequences[identity] = sequence
                if decision.get("latent_mode") != "move_toward" or decision.get("decision") != "none":
                    continue
                if decision.get("move_target_live") is not True:
                    continue
                target = _inventory_target(decision, "stall decision")
                if decision.get("move_target_is_inventory") is not True or not all(
                        (decision["marker_known"], decision["marker_live"])):
                    continue
                seconds = _finite(decision.get("no_progress_seconds"), "stall no-progress seconds")
                displacement = _finite(decision.get("no_progress_displacement"), "stall displacement")
                radius = _finite(decision.get("progress_radius"), "stall progress radius")
                if seconds < 2.0 or radius <= 0.0 or displacement > radius:
                    continue
                stalls[identity].append({"native_tick": _count(decision.get("native_tick"), "stall native tick"),
                                         "life_id": _count(decision.get("life_id"), "stall life id"),
                                         "target": target})
        if seen != identities:
            raise RetryChainError(f"events line {line_number}: participant set is incomplete")
    for identity, values in final_overflows.items():
        if values["direct"] != 0 or values["stall"] != 0:
            raise RetryChainError(f"{identity}: provenance record overflow")

    route_commits: dict[str, dict[int, set[tuple[str, str]]]] = defaultdict(lambda: defaultdict(set))
    path_overflows = {identity: 0 for identity in identities}
    for line_number, row in _events(run / "route-execution.jsonl"):
        if row.get("schema") != "surreal-bot-route-execution-observation-v1":
            raise RetryChainError(f"route line {line_number}: schema is invalid")
        if row.get("benchmark_config_id") != config_id:
            raise RetryChainError(f"route line {line_number}: config identity is invalid")
        tick = _count(row.get("tick"), f"route line {line_number}: tick")
        participants = row.get("participants")
        if not isinstance(participants, list):
            raise RetryChainError(f"route line {line_number}: participants are invalid")
        seen: set[str] = set()
        for participant in participants:
            if not isinstance(participant, dict) or participant.get("identity") not in identities:
                raise RetryChainError(f"route line {line_number}: participant identity is invalid")
            identity = participant["identity"]
            if identity in seen:
                raise RetryChainError(f"route line {line_number}: duplicate participant")
            seen.add(identity)
            path_overflows[identity] = _count(participant.get("native_path_commit_overflows_exact"),
                                               f"route line {line_number} {identity}: path overflow")
            if path_overflows[identity] != 0:
                raise RetryChainError("native path-commit record overflow")
            commits = participant.get("native_path_commits")
            if not isinstance(commits, list):
                raise RetryChainError(f"route line {line_number} {identity}: commits are invalid")
            for commit in commits:
                if not isinstance(commit, dict):
                    raise RetryChainError(f"route line {line_number} {identity}: commit is invalid")
                if commit.get("origin") not in {"find_path_toward", "find_best_inventory_path"}:
                    raise RetryChainError(f"route line {line_number} {identity}: route origin is invalid")
                if commit.get("phase") != "pre_special_cache_commit":
                    raise RetryChainError(f"route line {line_number} {identity}: route phase is invalid")
                if not isinstance(commit.get("cache_clear"), bool) or not isinstance(
                        commit.get("truncated_by_route_cache"), bool):
                    raise RetryChainError(f"route line {line_number} {identity}: route cache state is invalid")
                if commit["cache_clear"]:
                    continue
                nodes = commit.get("nodes")
                edges = commit.get("edges")
                if (commit["truncated_by_route_cache"] or not isinstance(nodes, list) or not nodes
                        or not isinstance(edges, list) or len(edges) != len(nodes) - 1):
                    raise RetryChainError(f"route line {line_number} {identity}: route nodes are invalid")
                if any(not isinstance(edge, dict) or edge.get("pruned") is not False for edge in edges):
                    raise RetryChainError(f"route line {line_number} {identity}: route edge is invalid")
                endpoint = nodes[-1]
                if (not isinstance(endpoint, dict) or not isinstance(endpoint.get("name"), str)
                        or not endpoint["name"] or not isinstance(endpoint.get("class"), str)
                        or not endpoint["class"]):
                    raise RetryChainError(f"route line {line_number} {identity}: route endpoint is invalid")
                route_commits[identity][tick].add((endpoint["name"], endpoint["class"]))
        if seen != identities:
            raise RetryChainError(f"route line {line_number}: participant set is incomplete")
    if any(value != 0 for value in path_overflows.values()):
        raise RetryChainError("native path-commit record overflow")

    counts = {"inventory_direct_verdicts_exact": inventory_direct_verdicts_seen,
              "full_watchdog_stalls_exact": 0,
              "immediate_same_target_reissues_exact": 0, "marker_routes_committed_exact": 0,
              "qualifying_chains_exact": 0}
    witnesses: list[dict[str, Any]] = []
    for identity in sorted(identities):
        for stall in stalls[identity]:
            counts["full_watchdog_stalls_exact"] += 1
            before = [record for record in direct[identity]
                      if record["life_id"] == stall["life_id"]
                      and record["native_tick"] < stall["native_tick"]
                      and _same_target(record["target"], stall["target"])]
            if not before:
                continue
            reissues = [record for record in direct[identity]
                       if record["life_id"] == stall["life_id"]
                       and _same_target(record["target"], stall["target"])
                       and stall["native_tick"] <= record["native_tick"] <= stall["native_tick"] + 1]
            if not reissues:
                continue
            counts["immediate_same_target_reissues_exact"] += 1
            reissue = min(reissues, key=lambda record: record["native_tick"])
            marker = (stall["target"][4], stall["target"][5])
            if marker not in route_commits[identity].get(reissue["native_tick"], set()):
                continue
            counts["marker_routes_committed_exact"] += 1
            counts["qualifying_chains_exact"] += 1
            if len(witnesses) < MAX_WITNESSES:
                witnesses.append({"identity": identity, "life_id": str(stall["life_id"]),
                                  "target": stall["target"][2], "marker": marker[0],
                                  "direct_verdict_tick": str(max(record["native_tick"] for record in before)),
                                  "stall_tick": str(stall["native_tick"]),
                                  "reissue_tick": str(reissue["native_tick"])})
    qualified = counts["qualifying_chains_exact"] > 0
    return {"schema": SCHEMA, "benchmark": {"config_id": config_id, "map": manifest.get("url")},
            "counts": {key: str(value) for key, value in counts.items()}, "witnesses": witnesses,
            "witnesses_truncated": counts["qualifying_chains_exact"] > len(witnesses),
            "coverage": {"criterion": "at_least_one_exact_direct_inventory_retry_chain",
                         "qualified": qualified,
                         "verdict": "qualified" if qualified else "unqualified_no_exact_retry_chain"},
            "qualified": qualified}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path); parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        encoded = json.dumps(analyze(args.run), indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except RetryChainError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
