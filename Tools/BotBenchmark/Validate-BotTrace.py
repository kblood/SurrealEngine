#!/usr/bin/env python3
"""Check structural and AI-contract invariants in one bot benchmark trace."""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def as_int(fields: dict[str, str], name: str) -> int:
    return int(fields[name])


def as_float(fields: dict[str, str], name: str) -> float:
    value = float(fields[name])
    if not math.isfinite(value):
        raise ValueError(f"{name} is not finite")
    return value


def validate(events_path: Path, summary_path: Path | None) -> dict[str, Any]:
    errors: list[str] = []
    warnings: list[str] = []
    counts: Counter[str] = Counter()
    expected_seq = 0
    previous_tick = 0
    exact: dict[str, Counter[str]] = defaultdict(Counter)
    combat: dict[tuple[str, str], Counter[str]] = defaultdict(Counter)
    projectile_launches: dict[str, tuple[str, str]] = {}
    projectile_results: set[str] = set()
    fixture_id = ""
    fixture_assertions = Counter()
    fixture_assertion_names: set[str] = set()
    fixture_setup: dict[str, str] | None = None
    fixture_complete: dict[str, str] | None = None

    def fixture_bool(fields: dict[str, str], name: str, line_number: int) -> bool:
        value = fields.get(name)
        if value not in ("true", "false"):
            raise ValueError(f"fixture {name} is not a literal boolean at line {line_number}")
        return value == "true"

    with events_path.open("r", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, 1):
            try:
                event = json.loads(line)
                event_type = str(event["type"])
                seq = int(event["seq"])
                tick = int(event["tick"])
                fields = event.get("fields", {})
            except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
                errors.append(f"line {line_number}: invalid event: {exc}")
                continue

            counts[event_type] += 1
            if seq != expected_seq:
                errors.append(f"line {line_number}: seq {seq}, expected {expected_seq}")
            expected_seq = seq + 1
            if tick < previous_tick:
                errors.append(f"line {line_number}: tick regressed from {previous_tick} to {tick}")
            previous_tick = tick

            try:
                if event_type == "run_config":
                    fixture_id = str(fields.get("fixture_id", ""))

                elif event_type == "fixture_assert":
                    if not fixture_id:
                        errors.append(f"line {line_number}: fixture assertion without configured fixture")
                    if fields.get("fixture_id") != fixture_id:
                        errors.append(f"line {line_number}: fixture assertion id mismatch")
                    if fixture_setup is None:
                        errors.append(f"line {line_number}: fixture assertion occurred before setup")
                    if fixture_complete is not None:
                        errors.append(f"line {line_number}: fixture assertion occurred after completion")
                    assertion_name = fields.get("name", "")
                    if not assertion_name:
                        errors.append(f"line {line_number}: fixture assertion has no name")
                    elif assertion_name in fixture_assertion_names:
                        errors.append(f"line {line_number}: duplicate fixture assertion name {assertion_name!r}")
                    fixture_assertion_names.add(assertion_name)
                    passed = fixture_bool(fields, "passed", line_number)
                    expected = fixture_bool(fields, "expected", line_number)
                    actual = fixture_bool(fields, "actual", line_number)
                    if passed != (expected == actual):
                        errors.append(f"line {line_number}: fixture assertion result mismatch")
                    fixture_assertions["total"] += 1
                    fixture_assertions["passed" if passed else "failed"] += 1

                elif event_type == "fixture_complete":
                    if not fixture_id:
                        errors.append(f"line {line_number}: fixture completion without configured fixture")
                    if fields.get("fixture_id") != fixture_id:
                        errors.append(f"line {line_number}: fixture completion id mismatch")
                    if fixture_setup is None:
                        errors.append(f"line {line_number}: fixture completion occurred before setup")
                    if fixture_complete is not None:
                        errors.append(f"line {line_number}: duplicate fixture completion")
                    fixture_complete = fields

                elif event_type in ("fixture_setup", "fixture_error"):
                    if not fixture_id:
                        errors.append(f"line {line_number}: {event_type} without configured fixture")
                    if fields.get("fixture_id") != fixture_id:
                        errors.append(f"line {line_number}: {event_type} id mismatch")
                    if event_type == "fixture_setup":
                        if fixture_assertions["total"] != 0 or fixture_complete is not None:
                            errors.append(f"line {line_number}: fixture setup occurred after fixture results")
                        if fixture_setup is not None:
                            errors.append(f"line {line_number}: duplicate fixture setup")
                        fixture_setup = fields

                elif event_type == "route_search":
                    nodes = as_int(fields, "route_nodes")
                    travel = as_int(fields, "travel_distance")
                    weighted = as_int(fields, "weighted_cost")
                    route = [part for part in fields.get("route", "").split(">") if part]
                    if fields.get("status") == "success":
                        if nodes <= 0 or len(route) != nodes:
                            errors.append(f"line {line_number}: successful route node count mismatch")
                        if travel < 0 or weighted < travel:
                            errors.append(f"line {line_number}: invalid route costs {travel}/{weighted}")
                    elif nodes != 0 or route:
                        errors.append(f"line {line_number}: failed route contains nodes")

                elif event_type == "route_cache":
                    nodes = as_int(fields, "route_nodes")
                    route = [part for part in fields.get("route", "").split(">") if part]
                    if nodes != len(route):
                        errors.append(f"line {line_number}: route-cache node count mismatch")
                    expected_next = route[0] if route else "None"
                    if fields.get("next") != expected_next:
                        errors.append(f"line {line_number}: route-cache next is not first route node")

                elif event_type == "inventory_path_candidate":
                    eligible = fields.get("eligible") == "true"
                    path_found = fields.get("path_found") == "true"
                    state = fields.get("inventory_state", "").lower()
                    reason = fields.get("eligibility_reason")
                    travel = as_int(fields, "travel_distance")
                    weighted = as_int(fields, "weighted_cost")
                    eta = as_float(fields, "physical_eta_seconds")
                    score = as_float(fields, "score")
                    if path_found:
                        if travel < 0 or weighted < travel or eta < 0.0 or score < 0.0:
                            errors.append(f"line {line_number}: invalid inventory path fields")
                    elif travel != 0 or weighted != 0 or eta != -1.0 or score != 0.0:
                        errors.append(f"line {line_number}: no-path inventory has nonempty path fields")
                    if state == "sleeping":
                        remaining = as_float(fields, "respawn_remaining_seconds")
                        requested = fields.get("predict_respawns_requested") == "true"
                        if reason == "prediction_disabled":
                            if requested or eligible or path_found:
                                errors.append(f"line {line_number}: disabled prediction contract violated")
                        elif reason == "no_path":
                            if not requested or eligible or path_found:
                                errors.append(f"line {line_number}: sleeping no-path contract violated")
                        elif reason == "respawns_after_eta":
                            if not requested or eligible or not path_found or remaining <= eta + 1.0e-5:
                                errors.append(f"line {line_number}: late-respawn ETA contract violated")
                        elif reason == "respawns_by_eta":
                            if not requested or not eligible or not path_found or remaining > eta + 1.0e-5:
                                errors.append(f"line {line_number}: eligible-respawn ETA contract violated")
                        else:
                            errors.append(f"line {line_number}: unknown sleeping-item reason {reason!r}")

                elif event_type == "benchmark_spectator":
                    if fields.get("class_is_spectator") != "true" or fields.get("pri_is_spectator") != "true":
                        errors.append(f"line {line_number}: viewport spectator validation failed")

                elif event_type == "damage":
                    before = as_int(fields, "health_before")
                    after = as_int(fields, "health_after")
                    raw = as_int(fields, "raw_health_delta")
                    effective = as_int(fields, "effective_health_damage")
                    expected_raw = max(0, before - after)
                    expected_effective = max(0, before - max(after, 0)) if before > 0 else 0
                    fatal = fields.get("fatal") == "true"
                    self_damage = fields.get("self_damage") == "true"
                    victim = fields.get("victim", "")
                    instigator = fields.get("instigator", "")
                    if raw != expected_raw or effective != expected_effective:
                        errors.append(f"line {line_number}: damage health delta mismatch")
                    if fatal != (before > 0 and after <= 0):
                        errors.append(f"line {line_number}: fatal damage flag mismatch")
                    if self_damage != (instigator == victim):
                        errors.append(f"line {line_number}: self-damage flag mismatch")
                    if before > 0 and victim.startswith("pri:"):
                        exact[victim]["events_taken"] += 1
                        exact[victim]["damage_taken"] += effective
                        if fatal:
                            exact[victim]["deaths"] += 1
                    if before > 0 and instigator.startswith("pri:"):
                        if self_damage:
                            exact[instigator]["self_damage"] += effective
                        else:
                            exact[instigator]["events_dealt"] += 1
                            exact[instigator]["damage_dealt"] += effective
                            if fatal:
                                exact[instigator]["kills"] += 1

                elif event_type == "hitscan_shot":
                    shooter = fields.get("shooter", "")
                    weapon_mode = fields.get("weapon_mode", "")
                    key = (shooter, weapon_mode)
                    combat[key]["hitscan_shots"] += 1
                    hit = fields.get("hit_opponent") == "true"
                    damage_events = as_int(fields, "opponent_damage_events")
                    if hit != (damage_events > 0):
                        errors.append(f"line {line_number}: hitscan hit/event mismatch")
                    if hit:
                        combat[key]["hitscan_hits"] += 1

                elif event_type == "projectile_launch":
                    projectile = fields.get("projectile", "")
                    if not projectile or projectile in projectile_launches:
                        errors.append(f"line {line_number}: duplicate/empty projectile launch identity")
                    projectile_launches[projectile] = (fields.get("shooter", ""), fields.get("weapon_mode", ""))
                    combat[projectile_launches[projectile]]["projectile_launches"] += 1

                elif event_type == "projectile_result":
                    projectile = fields.get("projectile", "")
                    if projectile not in projectile_launches:
                        errors.append(f"line {line_number}: projectile result has no launch")
                    elif projectile in projectile_results:
                        errors.append(f"line {line_number}: duplicate projectile result")
                    else:
                        projectile_results.add(projectile)
                        key = projectile_launches[projectile]
                        field_key = "projectile_hits" if fields.get("hit_opponent") == "true" else "projectile_misses"
                        combat[key][field_key] += 1
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(f"line {line_number}: {event_type} field error: {exc}")

    required = ("run_config", "simulation_start", "benchmark_spectator", "bot_observed", "run_end")
    for event_type in required:
        if counts[event_type] == 0:
            errors.append(f"required event missing: {event_type}")
    if counts["see_player"] == 0:
        warnings.append("trace contains no SeePlayer probe; it does not exercise visual acquisition")
    if counts["enemy_not_visible"] == 0:
        warnings.append("trace contains no EnemyNotVisible probe; it does not exercise visual loss")
    if fixture_id:
        if counts["fixture_setup"] != 1:
            errors.append(f"configured fixture has {counts['fixture_setup']} setup events, expected 1")
        if fixture_id == "controlled-reachability-blockall-v1" and fixture_setup is not None:
            required_geometry = (
                "source_x", "source_y", "source_z", "target_x", "target_y", "target_z",
                "direction_x", "direction_y", "blocker_radius", "blocker_height",
                "blocker_spacing", "blocker_offset_min", "blocker_offset_max",
            )
            try:
                for field in required_geometry:
                    as_float(fixture_setup, field)
                if as_int(fixture_setup, "blocker_count") != 9:
                    errors.append("reachability fixture setup did not contain nine blockers")
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(f"reachability fixture setup geometry error: {exc}")
        if fixture_complete is None:
            errors.append("configured fixture has no completion event")
        else:
            if fixture_complete.get("fixture_id") != fixture_id:
                errors.append("fixture completion id mismatch")
            for field, observed in (
                ("assertions_total", fixture_assertions["total"]),
                ("assertions_passed", fixture_assertions["passed"]),
                ("assertions_failed", fixture_assertions["failed"]),
            ):
                try:
                    if int(fixture_complete.get(field, -1)) != observed:
                        errors.append(f"fixture completion {field} does not match assertion events")
                except (TypeError, ValueError):
                    errors.append(f"fixture completion has invalid {field}")
            if fixture_complete.get("status") != "passed" or fixture_assertions["failed"] != 0:
                errors.append("fixture completion did not pass all assertions")

    if summary_path is not None:
        try:
            summary = json.loads(summary_path.read_text(encoding="utf-8-sig"))
            if summary.get("status") != "passed":
                errors.append(f"summary status is {summary.get('status')!r}: {summary.get('reason', '')}")
            if int(summary.get("maximum_observed_bots", -1)) != int(summary.get("requested_bots", -2)):
                errors.append("summary observed bot roster does not match requested roster")
            if int(summary.get("maximum_observed_non_bot_participants", -1)) != 0:
                errors.append("summary observed a non-bot participant")
            if int(summary.get("non_bot_enemy_target_observations", -1)) != 0:
                errors.append("summary observed a bot targeting a non-bot pawn")
            if not summary.get("viewport_actor_is_spectator") or not summary.get("viewport_pri_is_spectator"):
                errors.append("summary viewport spectator proof failed")
            summary_fixture = summary.get("fixture", {})
            if fixture_id:
                if summary.get("fixture_id") != fixture_id or summary_fixture.get("id") != fixture_id:
                    errors.append("summary fixture id does not match trace")
                if summary_fixture.get("status") != "passed":
                    errors.append("summary fixture status did not pass")
                for field, observed in (
                    ("assertions_total", fixture_assertions["total"]),
                    ("assertions_passed", fixture_assertions["passed"]),
                    ("assertions_failed", fixture_assertions["failed"]),
                ):
                    if int(summary_fixture.get(field, -1)) != observed:
                        errors.append(f"summary fixture {field} does not match trace")
            else:
                if summary.get("fixture_id") not in (None, ""):
                    errors.append("ordinary summary unexpectedly names a fixture")
                if summary_fixture.get("status") != "inactive":
                    errors.append("ordinary summary fixture status is not inactive")
            for metric in summary.get("bot_metrics", []):
                identity = str(metric.get("identity", ""))
                expected_fields = {
                    "damage_events_dealt_exact": exact[identity]["events_dealt"],
                    "damage_events_taken_exact": exact[identity]["events_taken"],
                    "damage_dealt_exact": exact[identity]["damage_dealt"],
                    "damage_taken_exact": exact[identity]["damage_taken"],
                    "self_damage_exact": exact[identity]["self_damage"],
                    "fatal_damage_kills_exact": exact[identity]["kills"],
                    "fatal_damage_deaths_exact": exact[identity]["deaths"],
                }
                for name, expected in expected_fields.items():
                    if name in metric and int(metric[name]) != expected:
                        errors.append(f"summary {identity} {name}={metric[name]}, trace={expected}")
                for weapon in metric.get("combat_by_weapon", []):
                    weapon_mode = str(weapon.get("weapon_mode", ""))
                    observed = combat[(identity, weapon_mode)]
                    weapon_fields = {
                        "hitscan_shots": observed["hitscan_shots"],
                        "hitscan_hits": observed["hitscan_hits"],
                        "projectile_launches": observed["projectile_launches"],
                        "projectile_hits_finalized": observed["projectile_hits"],
                        "projectile_misses_finalized": observed["projectile_misses"],
                    }
                    for name, expected in weapon_fields.items():
                        if int(weapon.get(name, -1)) != expected:
                            errors.append(f"summary {identity}/{weapon_mode} {name}={weapon.get(name)}, trace={expected}")
        except (OSError, TypeError, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"invalid summary: {exc}")

    return {
        "schema": 1,
        "events": sum(counts.values()),
        "last_tick": previous_tick,
        "event_counts": dict(sorted(counts.items())),
        "passed": not errors,
        "errors": errors,
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", help="Benchmark run directory or events.jsonl")
    parser.add_argument("--output", help="Optional report JSON path")
    args = parser.parse_args()
    path = Path(args.run).resolve()
    run_directory = path if path.is_dir() else path.parent
    events_path = run_directory / "events.jsonl" if path.is_dir() else path
    candidate_summary = run_directory / "summary.json"
    summary_path = candidate_summary if candidate_summary.is_file() else None
    if not events_path.is_file():
        raise FileNotFoundError(events_path)
    report = validate(events_path, summary_path)
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = Path(args.output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
