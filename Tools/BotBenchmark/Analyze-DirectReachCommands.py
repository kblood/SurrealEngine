#!/usr/bin/env python3
"""Fail-closed validation for direct ActorReachable command provenance."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


SCHEMA = "surreal-bot-benchmark-direct-reach-command-v1"
COUNTERS = (
    "direct_reach_command_observations_exact",
    "direct_reach_command_successes_exact",
    "direct_reach_command_failures_exact",
    "direct_reach_command_same_life_exact_exact",
    "direct_reach_command_unlinked_exact",
    "direct_reach_command_overflows_exact",
)
LINK_STATUSES = {
    "same_life_exact",
    "not_reached",
    "unavailable_life_boundary",
    "unavailable_no_active_direct_command",
    "unavailable_route_head",
    "unavailable_target_replaced",
}


class DirectReachCommandError(Exception):
    pass


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise DirectReachCommandError(f"{path}: unreadable JSON") from error
    if not isinstance(value, dict):
        raise DirectReachCommandError(f"{path}: expected an object")
    return value


def _strict_count(value: Any, context: str) -> int:
    if not isinstance(value, str) or not value.isascii() or not value.isdecimal():
        raise DirectReachCommandError(f"{context}: expected a non-negative integer string")
    return int(value)


def analyze(run: Path) -> dict[str, Any]:
    manifest = _read_json(run / "manifest.json")
    if manifest.get("direct_reach_command_observer_enabled") is not True:
        raise DirectReachCommandError("manifest does not explicitly enable direct-reach command observer")
    summary = _read_json(run / "summary.json")
    if summary.get("status") != "complete" or summary.get("exit_code") != 0:
        raise DirectReachCommandError("summary is not complete and successful")
    expected = {item.get("identity") for item in summary.get("actual_roster", [])}
    if not expected or not all(isinstance(identity, str) and identity for identity in expected):
        raise DirectReachCommandError("summary has no complete actual roster")
    previous = {identity: {counter: 0 for counter in COUNTERS} for identity in expected}
    sequences = {identity: 0 for identity in expected}
    records = {identity: [] for identity in expected}
    event_path = run / "events.jsonl"
    try:
        lines = event_path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise DirectReachCommandError(f"{event_path}: unreadable") from error
    if not lines:
        raise DirectReachCommandError("events are empty")
    for event_index, line in enumerate(lines, 1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            raise DirectReachCommandError(f"events line {event_index}: invalid JSON") from error
        envelope = event.get("direct_reach_command_observer")
        if envelope != {"requested": True, "status": "active"}:
            raise DirectReachCommandError(f"events line {event_index}: observer envelope is incomplete")
        bots = event.get("bots")
        if not isinstance(bots, list):
            raise DirectReachCommandError(f"events line {event_index}: bots is not an array")
        seen: set[str] = set()
        for bot in bots:
            if not isinstance(bot, dict):
                raise DirectReachCommandError(f"events line {event_index}: bot is not an object")
            identity = bot.get("identity")
            if identity not in expected or identity in seen:
                raise DirectReachCommandError(f"events line {event_index}: participant identity is invalid")
            seen.add(identity)
            for counter in COUNTERS:
                current = _strict_count(bot.get(counter), f"events line {event_index} {identity} {counter}")
                if current < previous[identity][counter]:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: counter regressed")
                previous[identity][counter] = current
            payload = bot.get("direct_reach_command_records")
            if not isinstance(payload, list):
                raise DirectReachCommandError(f"events line {event_index} {identity}: records are not an array")
            for record in payload:
                if not isinstance(record, dict):
                    raise DirectReachCommandError(f"events line {event_index} {identity}: record is not an object")
                sequence = _strict_count(record.get("sequence"), "record sequence")
                if sequence != sequences[identity] + 1:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: record sequence is not contiguous")
                sequences[identity] = sequence
                if not isinstance(record.get("reached"), bool):
                    raise DirectReachCommandError(f"events line {event_index} {identity}: record reached is invalid")
                if record.get("link_status") not in LINK_STATUSES:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: record link status is invalid")
                if record["link_status"] == "same_life_exact" and not record["reached"]:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: false reach was linked")
                records[identity].append(record)
        if seen != expected:
            raise DirectReachCommandError(f"events line {event_index}: participant set is incomplete")
    totals: dict[str, int] = {counter: 0 for counter in COUNTERS}
    matched = 0
    for identity in sorted(expected):
        final = previous[identity]
        emitted = records[identity]
        if final["direct_reach_command_observations_exact"] != len(emitted):
            raise DirectReachCommandError(f"{identity}: observation counter does not reconcile with records")
        successes = sum(record["reached"] for record in emitted)
        matches = sum(record["link_status"] == "same_life_exact" for record in emitted)
        if final["direct_reach_command_successes_exact"] != successes:
            raise DirectReachCommandError(f"{identity}: success counter does not reconcile")
        if final["direct_reach_command_failures_exact"] + successes != len(emitted):
            raise DirectReachCommandError(f"{identity}: success/failure partition does not reconcile")
        if final["direct_reach_command_same_life_exact_exact"] != matches:
            raise DirectReachCommandError(f"{identity}: same-life counter does not reconcile")
        if final["direct_reach_command_unlinked_exact"] + matches != len(emitted):
            raise DirectReachCommandError(f"{identity}: link partition does not reconcile")
        if final["direct_reach_command_overflows_exact"] != 0:
            raise DirectReachCommandError(f"{identity}: observer record overflow")
        matched += matches
        for counter in COUNTERS:
            totals[counter] += final[counter]
    coverage = {
        "criterion": "at_least_one_same_life_exact",
        "same_life_exact": matched,
        "qualified": matched > 0,
        "verdict": "qualified" if matched > 0 else "unqualified_no_same_life_exact",
    }
    return {"schema": SCHEMA, "benchmark": {"config_id": manifest.get("config_id"),
            "map": manifest.get("url")}, "counts": totals,
            "per_participant_final_sequence": sequences, "coverage": coverage,
            "qualified": coverage["qualified"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = analyze(args.run)
    except DirectReachCommandError as error:
        parser.error(str(error))
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
