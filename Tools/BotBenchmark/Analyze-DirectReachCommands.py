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
    "direct_reach_command_hazardous_deaths_exact",
    "direct_reach_command_nonhazard_deaths_exact",
    "direct_reach_command_cleared_exact",
    "direct_reach_command_life_boundary_censored_exact",
    "direct_reach_command_run_end_censored_exact",
    "direct_reach_command_command_replaced_exact",
)
LINK_STATUSES = {
    "same_life_exact",
    "not_reached",
    "unavailable_life_boundary",
    "unavailable_caller_origin",
    "unavailable_no_active_direct_command",
    "unavailable_route_head",
    "unavailable_target_replaced",
}
CALLER_ORIGINS = {
    "script_actor_reachable",
    "path_special_handling",
    "find_path_to_end_point",
    "find_random_dest",
    "unknown",
}
REJECT_REASONS = {
    "null_actor",
    "distance",
    "navpoint_reachspec",
    "pain_zone",
    "water",
    "trace",
    "check_location",
    "walk_simulation",
    "unsupported_physics",
    "reached",
}
TERMINALS = {
    "hazardous_death",
    "nonhazard_death",
    "cleared",
    "life_boundary_censor",
    "run_end_censor",
    "command_replaced",
}
TERMINAL_COUNTERS = {
    "hazardous_death": "direct_reach_command_hazardous_deaths_exact",
    "nonhazard_death": "direct_reach_command_nonhazard_deaths_exact",
    "cleared": "direct_reach_command_cleared_exact",
    "life_boundary_censor": "direct_reach_command_life_boundary_censored_exact",
    "run_end_censor": "direct_reach_command_run_end_censored_exact",
    "command_replaced": "direct_reach_command_command_replaced_exact",
}
TERMINAL_FIELDS = (
    "activation_tick",
    "terminal_tick",
    "terminal",
    "hazard_terminal_exact",
)


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


def _validate_same_life_terminal(record: dict[str, Any], context: str) -> None:
    if record.get("caller_origin") != "script_actor_reachable":
        raise DirectReachCommandError(f"{context}: same-life link has a non-script caller origin")
    native_tick = _strict_count(record.get("native_tick"), f"{context}: native_tick")
    activation_tick = _strict_count(record.get("activation_tick"),
                                    f"{context}: activation_tick")
    if activation_tick <= native_tick:
        raise DirectReachCommandError(
            f"{context}: activation is not from a later benchmark sample")
    terminal_tick = _strict_count(record.get("terminal_tick"),
                                  f"{context}: terminal_tick")
    if terminal_tick < activation_tick:
        raise DirectReachCommandError(f"{context}: terminal tick precedes activation tick")
    terminal = record.get("terminal")
    if terminal not in TERMINALS:
        raise DirectReachCommandError(f"{context}: terminal is invalid")
    hazard_terminal_exact = record.get("hazard_terminal_exact")
    if not isinstance(hazard_terminal_exact, bool):
        raise DirectReachCommandError(f"{context}: hazard terminal exact is invalid")
    if hazard_terminal_exact != (terminal == "hazardous_death"):
        raise DirectReachCommandError(
            f"{context}: hazardous-death terminal does not match exact hazard evidence")


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
                _strict_count(record.get("reach_sequence"), "record reach_sequence")
                _strict_count(record.get("native_tick"), "record native_tick")
                if not isinstance(record.get("reached"), bool):
                    raise DirectReachCommandError(f"events line {event_index} {identity}: record reached is invalid")
                if record.get("link_status") not in LINK_STATUSES:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: record link status is invalid")
                if record.get("caller_origin") not in CALLER_ORIGINS:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: caller origin is invalid")
                if record.get("reject_reason") not in REJECT_REASONS:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: reject reason is invalid")
                if record["reached"] != (record["reject_reason"] == "reached"):
                    raise DirectReachCommandError(
                        f"events line {event_index} {identity}: result and reject reason disagree")
                if record["link_status"] == "same_life_exact" and not record["reached"]:
                    raise DirectReachCommandError(f"events line {event_index} {identity}: false reach was linked")
                if record["link_status"] == "same_life_exact":
                    _validate_same_life_terminal(record,
                        f"events line {event_index} {identity}: same-life record")
                elif any(record.get(field) is not None for field in TERMINAL_FIELDS):
                    raise DirectReachCommandError(
                        f"events line {event_index} {identity}: unlinked record has terminal evidence")
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
        same_life_records = [record for record in emitted
                             if record["link_status"] == "same_life_exact"]
        matches = len(same_life_records)
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
        if sum(final[counter] for counter in TERMINAL_COUNTERS.values()) != matches:
            raise DirectReachCommandError(f"{identity}: terminal partition does not reconcile")
        for terminal, counter in TERMINAL_COUNTERS.items():
            observed = sum(record["terminal"] == terminal for record in same_life_records)
            if final[counter] != observed:
                raise DirectReachCommandError(
                    f"{identity}: {terminal} terminal counter does not reconcile")
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
