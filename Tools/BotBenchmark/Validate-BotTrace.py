#!/usr/bin/env python3
"""Check structural and AI-contract invariants in one bot benchmark trace."""

from __future__ import annotations

import argparse
import hashlib
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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate(events_path: Path, summary_path: Path | None, *, qualification: bool = False) -> dict[str, Any]:
    events_path = events_path.resolve()
    canonical_summary_path = (summary_path if summary_path is not None else events_path.parent / "summary.json").resolve()
    summary_present = canonical_summary_path.is_file()
    events_sha256 = sha256_file(events_path)
    summary_sha256 = sha256_file(canonical_summary_path) if summary_present else None
    summary_path = canonical_summary_path if summary_present else None
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
    fixture_setup_tick: int | None = None
    fixture_complete_tick: int | None = None
    fixture_waits: list[tuple[int, int, dict[str, str]]] = []
    pending_wall_hits: dict[int, tuple[str, str, int, int, dict[str, str]]] = {}
    matched_wall_hits: list[tuple[int, int, dict[str, str], int, int, dict[str, str]]] = []
    legacy_wall_hits: list[tuple[str, str]] = []
    last_wall_hit_id = 0
    wall_hit_pairs = 0
    warned_legacy_wall_hits = False
    requested_bot_names: list[str] = []
    requested_skills: list[int] = []
    profile_configurations: list[tuple[int, dict[str, str]]] = []
    run_config_fields: dict[str, str] = {}
    run_end_fields: dict[str, str] = {}
    last_damage_id = 0
    last_death_id = 0
    damage_records: dict[int, dict[str, Any]] = {}
    death_records: list[tuple[int, dict[str, Any]]] = []
    summary_validated = False
    run_identity: dict[str, Any] = {
        "map": None,
        "seed": None,
        "requested_skills": [],
        "requested_bot_names": [],
        "digest_fnv1a64": None,
        "scenario": None,
        "requested_bots": None,
        "ticks": None,
        "fixed_delta": None,
    }

    def fixture_bool(fields: dict[str, str], name: str, line_number: int) -> bool:
        value = fields.get(name)
        if value not in ("true", "false"):
            raise ValueError(f"fixture {name} is not a literal boolean at line {line_number}")
        return value == "true"

    def literal_bool(fields: dict[str, str], name: str, line_number: int, context: str) -> bool:
        value = fields.get(name)
        if value not in ("true", "false"):
            raise ValueError(f"{context} {name} is not a literal boolean at line {line_number}")
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
                    run_config_fields = fields
                    fixture_id = str(fields.get("fixture_id", ""))
                    names_text = str(fields.get("requested_bot_names", ""))
                    requested_bot_names = names_text.split(",") if names_text else []
                    skills_text = str(fields.get("requested_skills", ""))
                    requested_skills = [int(value) for value in skills_text.split(",")] if skills_text else []

                elif event_type == "bot_skill_configured":
                    profile_configurations.append((line_number, fields))

                elif event_type == "run_end":
                    run_end_fields = fields

                elif event_type == "fixture_wait":
                    if not fixture_id or fields.get("fixture_id") != fixture_id:
                        errors.append(f"line {line_number}: fixture wait id mismatch")
                    fixture_waits.append((tick, seq, fields))

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
                    fixture_complete_tick = tick

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
                        fixture_setup_tick = tick

                elif event_type == "walking_hit_wall":
                    if "hit_id" not in fields:
                        legacy_wall_hits.append((fields.get("actor", ""), fields.get("blocker", "")))
                        if fixture_id == "controlled-hitwall-blockall-v1":
                            errors.append(f"line {line_number}: controlled HitWall fixture event has no hit_id")
                        elif not warned_legacy_wall_hits:
                            warnings.append("legacy walking HitWall events have no hit_id or extended diagnostics")
                            warned_legacy_wall_hits = True
                    else:
                        hit_id = as_int(fields, "hit_id")
                        if hit_id <= last_wall_hit_id:
                            errors.append(f"line {line_number}: walking HitWall id {hit_id} is not monotonic")
                        last_wall_hit_id = hit_id
                        if hit_id in pending_wall_hits:
                            errors.append(f"line {line_number}: duplicate pending walking HitWall id {hit_id}")
                        pending_wall_hits[hit_id] = (
                            fields.get("actor", ""), fields.get("blocker", ""), tick, seq, fields
                        )
                        required_before = (
                            "blocker_class", "blocker_is_mover", "event_enabled", "latent_before",
                            "statement_index_before", "destination_x_before", "destination_y_before",
                            "destination_z_before", "focus_x_before", "focus_y_before", "focus_z_before",
                            "location_x_before", "location_y_before", "location_z_before",
                            "move_timer_before", "b_from_wall_before",
                        )
                        for field in required_before:
                            if field not in fields:
                                errors.append(f"line {line_number}: walking HitWall missing {field}")

                elif event_type == "walking_hit_wall_result":
                    if "hit_id" not in fields:
                        result_key = (fields.get("actor", ""), fields.get("blocker", ""))
                        if fixture_id == "controlled-hitwall-blockall-v1":
                            errors.append(f"line {line_number}: controlled HitWall fixture result has no hit_id")
                        if not legacy_wall_hits:
                            errors.append(f"line {line_number}: legacy walking HitWall result has no pending begin")
                        else:
                            pending = legacy_wall_hits.pop(0)
                            if pending != result_key:
                                errors.append(f"line {line_number}: legacy walking HitWall actor/blocker mismatch")
                    else:
                        hit_id = as_int(fields, "hit_id")
                        pending = pending_wall_hits.pop(hit_id, None)
                        if pending is None:
                            errors.append(f"line {line_number}: walking HitWall result {hit_id} has no pending begin")
                        elif pending[:2] != (fields.get("actor", ""), fields.get("blocker", "")):
                            errors.append(f"line {line_number}: walking HitWall result {hit_id} actor/blocker mismatch")
                        else:
                            matched_wall_hits.append((pending[2], pending[3], pending[4], tick, seq, fields))
                        required_after = (
                            "blocker_class", "event_enabled", "latent_after", "statement_index_after",
                            "adjust_from_wall_label_index_after",
                            "destination_x_after", "destination_y_after", "destination_z_after",
                            "location_x_after", "location_y_after", "location_z_after",
                            "focus_x_after", "focus_y_after", "focus_z_after", "move_timer_after",
                            "b_from_wall_after", "health_after", "b_delete_me_after",
                        )
                        for field in required_after:
                            if field not in fields:
                                errors.append(f"line {line_number}: walking HitWall result missing {field}")
                    wall_hit_pairs += 1

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
                    fatal = literal_bool(fields, "fatal", line_number, "damage")
                    self_damage = literal_bool(fields, "self_damage", line_number, "damage")
                    victim = fields.get("victim", "")
                    instigator = fields.get("instigator", "")
                    origin = fields.get("damage_origin")
                    participant_identities = {entry[1].get("identity", "") for entry in profile_configurations}
                    expected_self_damage = victim in participant_identities and instigator == victim
                    expected_origin = "self" if expected_self_damage else (
                        "opponent" if victim in participant_identities and instigator in participant_identities else (
                            "environment_null" if victim in participant_identities and instigator == "None"
                            else "external_nonparticipant"
                        )
                    )
                    if raw != expected_raw or effective != expected_effective:
                        errors.append(f"line {line_number}: damage health delta mismatch")
                    if fatal != (before > 0 and after <= 0):
                        errors.append(f"line {line_number}: fatal damage flag mismatch")
                    if self_damage != expected_self_damage:
                        errors.append(f"line {line_number}: self-damage flag mismatch")
                    if qualification and origin is None:
                        errors.append(f"line {line_number}: qualification damage event lacks damage_origin")
                    elif origin is not None and origin != expected_origin:
                        errors.append(f"line {line_number}: damage origin mismatch")
                    if qualification:
                        damage_id = as_int(fields, "damage_id")
                        if damage_id != last_damage_id + 1:
                            errors.append(f"line {line_number}: damage_id is not monotonic")
                        last_damage_id = damage_id
                        roster_by_identity = {
                            entry[1].get("identity", ""): int(entry[1].get("roster_index", -1))
                            for entry in profile_configurations
                        }
                        if as_int(fields, "victim_roster_index") != roster_by_identity.get(victim, -1) \
                                or as_int(fields, "instigator_roster_index") != roster_by_identity.get(instigator, -1):
                            errors.append(f"line {line_number}: damage roster index mismatch")
                        if damage_id in damage_records:
                            errors.append(f"line {line_number}: duplicate damage_id {damage_id}")
                        damage_records[damage_id] = {
                            "line": line_number,
                            "victim": victim,
                            "instigator": instigator,
                            "fatal": fatal,
                            "origin": expected_origin,
                        }
                    if before > 0 and victim in participant_identities:
                        exact[victim]["events_taken"] += 1
                        exact[victim]["damage_taken"] += effective
                        if fatal:
                            exact[victim]["deaths"] += 1
                            if self_damage:
                                exact[victim]["self_fatal_deaths"] += 1
                            elif expected_origin == "environment_null":
                                exact[victim]["environmental_fatal_deaths"] += 1
                        if expected_origin == "environment_null":
                            exact[victim]["environmental_damage"] += effective
                        elif expected_origin == "external_nonparticipant":
                            exact[victim]["external_damage_taken"] += effective
                            if fatal:
                                exact[victim]["external_fatal_damage_deaths"] += 1
                    if before > 0 and instigator in participant_identities:
                        if self_damage:
                            exact[instigator]["self_damage"] += effective
                        elif expected_origin == "opponent":
                            exact[instigator]["events_dealt"] += 1
                            exact[instigator]["damage_dealt"] += effective
                            if fatal:
                                exact[instigator]["kills"] += 1
                        elif instigator in participant_identities:
                            exact[instigator]["external_damage_dealt"] += effective

                elif event_type == "death_adjudicated":
                    victim = fields.get("victim", "")
                    killer = fields.get("killer", "")
                    classification = fields.get("classification", "")
                    death_id = as_int(fields, "death_id")
                    damage_id = as_int(fields, "damage_id")
                    damage_mediated = literal_bool(
                        fields, "damage_mediated", line_number, "adjudicated death"
                    )
                    if death_id != last_death_id + 1:
                        errors.append(f"line {line_number}: death_id is not monotonic")
                    last_death_id = death_id
                    victim_roster = as_int(fields, "victim_roster_index")
                    killer_roster = as_int(fields, "killer_roster_index")
                    roster_by_identity = {
                        entry[1].get("identity", ""): int(entry[1].get("roster_index", -1))
                        for entry in profile_configurations
                    }
                    expected_victim_roster = roster_by_identity.get(victim, -1)
                    expected_killer_roster = roster_by_identity.get(killer, -1)
                    if victim_roster != expected_victim_roster or killer_roster != expected_killer_roster:
                        errors.append(f"line {line_number}: adjudicated death roster index mismatch")
                    participant_victim = expected_victim_roster >= 0
                    participant_killer = expected_killer_roster >= 0
                    if not participant_victim and not participant_killer:
                        errors.append(f"line {line_number}: adjudicated death has no configured participant")
                    expected_classification = (
                        "self" if participant_victim and killer == victim else
                        "participant_opponent" if participant_victim and participant_killer else
                        "environment_null" if participant_victim and killer == "None" else
                        "external_nonparticipant"
                    )
                    if classification != expected_classification:
                        errors.append(
                            f"line {line_number}: adjudicated death classification mismatch; "
                            f"expected {expected_classification!r}"
                        )
                    if damage_mediated != (damage_id > 0):
                        errors.append(f"line {line_number}: adjudicated death damage link/boolean mismatch")

                    victim_deaths_before = as_float(fields, "victim_deaths_before")
                    victim_deaths_after = as_float(fields, "victim_deaths_after")
                    victim_score_before = as_float(fields, "victim_score_before")
                    victim_score_after = as_float(fields, "victim_score_after")
                    killer_score_before = as_float(fields, "killer_score_before")
                    killer_score_after = as_float(fields, "killer_score_after")
                    if participant_victim and not math.isclose(
                            victim_deaths_after, victim_deaths_before + 1.0, abs_tol=1e-4):
                        errors.append(f"line {line_number}: participant victim death counter did not increase by one")
                    expected_victim_score_delta = -1.0 if expected_classification in {
                        "self", "environment_null"
                    } else 0.0
                    if participant_victim and not math.isclose(
                            victim_score_after - victim_score_before, expected_victim_score_delta, abs_tol=1e-4):
                        errors.append(f"line {line_number}: participant victim score transition mismatch")
                    if expected_classification == "self":
                        if not math.isclose(killer_score_before, victim_score_before, abs_tol=1e-4) \
                                or not math.isclose(killer_score_after, victim_score_after, abs_tol=1e-4):
                            errors.append(f"line {line_number}: self-death killer/victim score snapshots mismatch")
                    elif participant_killer and not math.isclose(
                            killer_score_after, killer_score_before + 1.0, abs_tol=1e-4):
                        errors.append(f"line {line_number}: participant killer score did not increase by one")

                    if participant_victim:
                        exact[victim]["adjudicated_deaths"] += 1
                    if expected_classification == "self":
                        exact[victim]["adjudicated_self_deaths"] += 1
                    elif expected_classification == "participant_opponent":
                        exact[killer]["adjudicated_opponent_kills"] += 1
                    elif expected_classification == "environment_null":
                        exact[victim]["adjudicated_environmental_deaths"] += 1
                    elif participant_victim:
                        exact[victim]["adjudicated_external_deaths"] += 1
                    elif participant_killer:
                        exact[killer]["adjudicated_external_kills"] += 1
                    if not damage_mediated and participant_victim:
                        exact[victim]["adjudicated_direct_deaths"] += 1
                    death_records.append((line_number, {
                        "damage_id": damage_id,
                        "damage_mediated": damage_mediated,
                        "victim": victim,
                        "killer": killer,
                        "classification": expected_classification,
                    }))

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

    if qualification:
        participant_identities = {entry[1].get("identity", "") for entry in profile_configurations}
        death_links: Counter[int] = Counter()
        for line_number, death in death_records:
            if not death["damage_mediated"]:
                continue
            damage_id = int(death["damage_id"])
            death_links[damage_id] += 1
            damage = damage_records.get(damage_id)
            if damage is None:
                errors.append(f"line {line_number}: mediated death references missing damage_id {damage_id}")
                continue
            if not damage["fatal"]:
                errors.append(f"line {line_number}: mediated death references nonfatal damage_id {damage_id}")
            if damage["victim"] != death["victim"]:
                errors.append(f"line {line_number}: mediated death victim does not match damage_id {damage_id}")
            expected_death_classification = {
                "self": "self",
                "opponent": "participant_opponent",
                "environment_null": "environment_null",
                "external_nonparticipant": "external_nonparticipant",
            }.get(str(damage["origin"]))
            if death["classification"] != expected_death_classification:
                errors.append(f"line {line_number}: mediated death classification does not match damage_id {damage_id}")
            expected_killer = "None" if damage["origin"] == "environment_null" else damage["instigator"]
            if death["killer"] != expected_killer:
                errors.append(f"line {line_number}: mediated death killer does not match damage_id {damage_id}")
        for damage_id, damage in damage_records.items():
            involves_participant = damage["victim"] in participant_identities \
                or damage["instigator"] in participant_identities
            expected_links = 1 if damage["fatal"] and involves_participant else 0
            if death_links[damage_id] != expected_links:
                errors.append(
                    f"damage_id {damage_id}: fatal/mediated death links={death_links[damage_id]}, "
                    f"expected {expected_links}"
                )

    required = ("run_config", "simulation_start", "benchmark_spectator", "bot_observed", "run_end")
    for event_type in required:
        if counts[event_type] == 0:
            errors.append(f"required event missing: {event_type}")
    if counts["see_player"] == 0:
        warnings.append("trace contains no SeePlayer probe; it does not exercise visual acquisition")
    if counts["enemy_not_visible"] == 0:
        warnings.append("trace contains no EnemyNotVisible probe; it does not exercise visual loss")
    if pending_wall_hits:
        errors.append(f"trace ended with unpaired walking HitWall ids: {sorted(pending_wall_hits)}")
    if legacy_wall_hits:
        errors.append(f"trace ended with {len(legacy_wall_hits)} unpaired legacy walking HitWall events")
    if counts["walking_hit_wall"] != counts["walking_hit_wall_result"] or counts["walking_hit_wall"] != wall_hit_pairs:
        errors.append("walking HitWall before/result counts do not reconcile")
    if requested_bot_names:
        if len(profile_configurations) != len(requested_bot_names):
            errors.append(
                f"explicit profile trace has {len(profile_configurations)} configuration events, "
                f"expected {len(requested_bot_names)}"
            )
        seen_roster_indexes: set[int] = set()
        seen_profile_ids: set[str] = set()
        seen_identities: set[str] = set()
        for line_number, fields in profile_configurations:
            try:
                roster_index = as_int(fields, "roster_index")
                if roster_index < 0 or roster_index >= len(requested_bot_names):
                    errors.append(f"line {line_number}: explicit profile roster index is out of range")
                    continue
                if roster_index in seen_roster_indexes:
                    errors.append(f"line {line_number}: duplicate explicit profile roster index {roster_index}")
                seen_roster_indexes.add(roster_index)
                identity = fields.get("identity", "")
                if not identity or identity in seen_identities:
                    errors.append(f"line {line_number}: explicit profile identity is empty or duplicated")
                seen_identities.add(identity)
                requested_name = fields.get("requested_profile_name", "")
                actual_name = fields.get("actual_profile_name", "")
                profile_id = fields.get("profile_id", "")
                bot_config_class = fields.get("bot_config_class", "")
                if requested_name.casefold() != requested_bot_names[roster_index].casefold():
                    errors.append(f"line {line_number}: requested profile name does not match run configuration")
                if not actual_name or actual_name.casefold() != requested_name.casefold():
                    errors.append(f"line {line_number}: actual profile name does not match requested name")
                if not bot_config_class or profile_id != f"{bot_config_class}|name={actual_name}":
                    errors.append(f"line {line_number}: profile ID is not derived from class and canonical name")
                if not profile_id or profile_id in seen_profile_ids:
                    errors.append(f"line {line_number}: profile ID is empty or duplicated")
                seen_profile_ids.add(profile_id)
                if fields.get("mapping_valid") != "true":
                    errors.append(f"line {line_number}: explicit profile skill mapping was not valid")
                if roster_index >= len(requested_skills) or as_int(fields, "requested_external_skill") != requested_skills[roster_index]:
                    errors.append(f"line {line_number}: explicit profile requested skill mismatch")
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(f"line {line_number}: explicit profile configuration error: {exc}")
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
        if fixture_id == "controlled-hitwall-blockall-v1" and fixture_setup is not None:
            required_geometry = (
                "source_x", "source_y", "source_z", "target_x", "target_y", "target_z",
                "direction_x", "direction_y", "blocker_radius", "blocker_height",
                "blocker_distance", "blocker_spacing", "blocker_offset_min", "blocker_offset_max",
            )
            try:
                for field in required_geometry:
                    as_float(fixture_setup, field)
                if as_int(fixture_setup, "blocker_count") != 9:
                    errors.append("HitWall fixture setup did not contain nine blockers")
                if as_int(fixture_setup, "movement_timeout_ticks") != 120:
                    errors.append("HitWall fixture setup movement timeout was not 120 ticks")
                if wall_hit_pairs < 1:
                    errors.append("HitWall fixture did not contain a paired walking HitWall callback")
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(f"HitWall fixture setup geometry error: {exc}")

            # Independently reconstruct the fixture lifecycle, geometry, and
            # stock-script transition from raw setup/before/result events. The
            # fixture_assert records remain useful diagnostics, but are not the
            # sole authority for this controlled contract.
            if len(fixture_waits) != 1:
                errors.append(f"HitWall fixture has {len(fixture_waits)} wait events, expected 1")
            try:
                wait_tick, _wait_seq, wait_fields = fixture_waits[0]
                if wait_fields.get("condition") != "PHYS_Walking" or as_int(wait_fields, "timeout_ticks") != 60:
                    errors.append("HitWall fixture wait contract is not PHYS_Walking/60 ticks")
                if fixture_setup_tick is None or wait_tick >= fixture_setup_tick:
                    errors.append("HitWall fixture setup did not occur after its walking wait")
            except (IndexError, KeyError, TypeError, ValueError) as exc:
                errors.append(f"HitWall fixture wait contract error: {exc}")

            try:
                source = tuple(as_float(fixture_setup, f"source_{axis}") for axis in "xyz")
                target = tuple(as_float(fixture_setup, f"target_{axis}") for axis in "xyz")
                direction = (as_float(fixture_setup, "direction_x"), as_float(fixture_setup, "direction_y"))
                direction_length = math.hypot(*direction)
                delta = tuple(target[index] - source[index] for index in range(3))
                along = delta[0] * direction[0] + delta[1] * direction[1]
                across = delta[0] * -direction[1] + delta[1] * direction[0]
                if abs(direction_length - 1.0) > 1.0e-4:
                    errors.append("HitWall fixture lane direction is not unit length")
                if abs(along - 256.0) > 1.0e-3 or abs(across) > 1.0e-3 or abs(delta[2]) > 1.0e-3:
                    errors.append("HitWall fixture target is not 256 UU along its horizontal lane")
                expected_geometry = {
                    "blocker_radius": 36.0, "blocker_height": 96.0, "blocker_distance": 128.0,
                    "blocker_spacing": 64.0, "blocker_offset_min": -256.0, "blocker_offset_max": 256.0,
                }
                for field, expected in expected_geometry.items():
                    if abs(as_float(fixture_setup, field) - expected) > 1.0e-4:
                        errors.append(f"HitWall fixture {field} did not equal {expected}")
                blocker_names = [name for name in fixture_setup.get("blockers", "").split(">") if name]
                if len(blocker_names) != 9 or len(set(blocker_names)) != 9:
                    errors.append("HitWall fixture setup blocker identities are not nine unique actors")
                setup_actor = fixture_setup.get("bot", "").rsplit(":", 1)[-1]

                if not matched_wall_hits:
                    errors.append("HitWall fixture has no structurally matched callback pair")
                for before_tick, before_seq, before, after_tick, after_seq, after in matched_wall_hits:
                    if fixture_setup_tick is None or before_tick <= fixture_setup_tick:
                        errors.append("HitWall callback did not occur on a normal tick after setup")
                    if after_tick != before_tick or after_seq != before_seq + 1:
                        errors.append("HitWall before/result pair was not synchronous and adjacent")
                    if before.get("actor") != setup_actor or after.get("actor") != setup_actor:
                        errors.append("HitWall callback actor did not match the configured fixture bot")
                    if before.get("blocker") not in blocker_names:
                        errors.append("HitWall callback blocker was not one of the fixture wall actors")
                    if before.get("blocker_class") != "Engine.BlockAll" or after.get("blocker_class") != "Engine.BlockAll":
                        errors.append("HitWall callback blocker class was not Engine.BlockAll")
                    if fixture_bool(before, "blocker_is_mover", 0) or not fixture_bool(before, "event_enabled", 0):
                        errors.append("HitWall callback mover/event-enabled contract failed")
                    if not fixture_bool(after, "event_enabled", 0):
                        errors.append("HitWall result did not retain event_enabled=true")

                    normal = tuple(as_float(before, f"normal_{axis}") for axis in "xyz")
                    normal_xy = math.hypot(normal[0], normal[1])
                    normal_dot = 1.0 if normal_xy == 0.0 else (
                        normal[0] * direction[0] + normal[1] * direction[1]
                    ) / normal_xy
                    if normal_dot >= -0.9 or abs(normal[2]) > 1.0e-4:
                        errors.append("HitWall callback normal did not oppose the horizontal lane")
                    if as_int(before, "physics_before") != 1 or as_int(after, "physics_after") != 1:
                        errors.append("HitWall callback did not retain PHYS_Walking")
                    if before.get("state_before") != "Roaming" or after.get("state_after") != "Roaming":
                        errors.append("HitWall callback did not remain in Roaming")
                    if as_int(before, "latent_before") != 6 or as_int(after, "latent_after") != 0:
                        errors.append("HitWall callback latent transition was not MoveToward to Continue")
                    if fixture_bool(before, "b_from_wall_before", 0) or not fixture_bool(after, "b_from_wall_after", 0):
                        errors.append("HitWall callback bFromWall transition was not false to true")
                    label_index = as_int(after, "adjust_from_wall_label_index_after")
                    if label_index < 0 or as_int(after, "statement_index_after") != label_index:
                        errors.append("HitWall result was not positioned at Roaming.AdjustFromWall")
                    if as_float(after, "move_timer_after") < 0.0:
                        errors.append("HitWall result move timer was negative")
                    if as_int(after, "health_after") <= 0 or fixture_bool(after, "b_delete_me_after", 0):
                        errors.append("HitWall fixture bot was not alive after the callback")

                    location_before = tuple(as_float(before, f"location_{axis}_before") for axis in "xyz")
                    location_after = tuple(as_float(after, f"location_{axis}_after") for axis in "xyz")
                    destination_before = tuple(as_float(before, f"destination_{axis}_before") for axis in "xyz")
                    destination_after = tuple(as_float(after, f"destination_{axis}_after") for axis in "xyz")
                    focus_before = tuple(as_float(before, f"focus_{axis}_before") for axis in "xyz")
                    focus_after = tuple(as_float(after, f"focus_{axis}_after") for axis in "xyz")
                    if math.dist(location_before, location_after) > 1.0e-4:
                        errors.append("HitWall script callback changed pawn location")
                    if (math.dist(destination_before, target) > 1.0e-3
                            or math.dist(focus_before, target) > 1.0e-3
                            or math.dist(focus_after, target) > 1.0e-3):
                        errors.append("HitWall callback did not preserve its endpoint destination/focus contract")
                    side_distance = math.dist(destination_after, location_after)
                    if not 0.75 <= side_distance <= 1.25:
                        errors.append("HitWall callback side-adjust destination was not approximately one UU")
                    source_projection = ((location_after[0] - source[0]) * direction[0]
                                         + (location_after[1] - source[1]) * direction[1])
                    if source_projection > 130.0:
                        errors.append("HitWall callback pawn crossed the fixture wall center plane")

                if fixture_complete_tick is None or (matched_wall_hits and
                        fixture_complete_tick <= max(pair[3] for pair in matched_wall_hits)):
                    errors.append("HitWall fixture completion did not occur after callback observation")
                if (fixture_setup_tick is not None and matched_wall_hits
                        and max(pair[0] for pair in matched_wall_hits) - fixture_setup_tick > 120):
                    errors.append("HitWall callback exceeded the movement timeout")
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(f"HitWall fixture raw protocol error: {exc}")
            expected_assertions = {
                "movetoward_preserves_location", "movetoward_latent_started",
                "movetoward_target_assigned", "hitwall_setup_is_roaming",
                "hitwall_setup_is_walking", "fixture_wall_callback_observed",
                "fixture_wall_callback_ids_and_blockers_match",
                "fixture_wall_callback_is_non_mover", "fixture_wall_callback_event_enabled",
                "fixture_wall_normal_opposes_lane", "fixture_bot_alive_after_callback",
                "fixture_bot_remains_source_side", "fixture_bot_remains_walking",
                "stock_hitwall_sets_from_wall", "stock_hitwall_sets_one_unit_side_destination",
                "stock_hitwall_preserves_endpoint_focus",
                "stock_hitwall_selects_roaming_adjust_label",
                "stock_hitwall_clears_movetoward_latent",
                "stock_hitwall_keeps_move_timer_nonnegative", "fixture_actors_destroyed",
            }
            if fixture_assertion_names != expected_assertions:
                missing = sorted(expected_assertions - fixture_assertion_names)
                extra = sorted(fixture_assertion_names - expected_assertions)
                errors.append(f"HitWall fixture assertion protocol mismatch; missing={missing}, extra={extra}")
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
            if fixture_id == "controlled-hitwall-blockall-v1":
                try:
                    if int(fixture_complete.get("walking_hit_wall_pairs", -1)) != wall_hit_pairs:
                        errors.append("HitWall fixture completion pair count does not match trace")
                except (TypeError, ValueError):
                    errors.append("HitWall fixture completion has invalid walking_hit_wall_pairs")

    if summary_path is not None:
        summary_error_start = len(errors)
        try:
            summary = json.loads(summary_path.read_text(encoding="utf-8-sig"))
            run_identity = {
                "map": run_config_fields.get("url", "").split("?", 1)[0],
                "seed": run_config_fields.get("seed"),
                "requested_skills": list(requested_skills),
                "requested_bot_names": list(requested_bot_names),
                "digest_fnv1a64": summary.get("digest_fnv1a64"),
                "scenario": run_config_fields.get("scenario"),
                "requested_bots": int(run_config_fields["bots"]) if "bots" in run_config_fields else None,
                "ticks": int(run_config_fields["ticks"]) if "ticks" in run_config_fields else None,
                "fixed_delta": float(run_config_fields["fixed_delta"])
                if "fixed_delta" in run_config_fields else None,
            }
            if qualification:
                if counts["run_config"] != 1 or counts["run_end"] != 1:
                    errors.append("qualification requires exactly one run_config and run_end event")
                if run_config_fields.get("scenario") != "skill-qualification":
                    errors.append("qualification run_config scenario mismatch")
                if int(run_config_fields.get("ticks", -1)) != 5400:
                    errors.append("qualification run_config tick count mismatch")
                if not math.isclose(float(run_config_fields.get("fixed_delta", "nan")), 1.0 / 60.0, abs_tol=5e-7):
                    errors.append("qualification run_config fixed delta mismatch")
                if int(run_config_fields.get("bots", -1)) != 2 or run_config_fields.get("fixture_id") != "":
                    errors.append("qualification run_config bot/fixture contract mismatch")
                if requested_bot_names != ["Loque", "Tamerlane"]:
                    errors.append("qualification run_config profile names mismatch")
                if len(requested_skills) != 2 or abs(requested_skills[0] - requested_skills[1]) != 1:
                    errors.append("qualification run_config skills are not one adjacent pair")
                url = run_config_fields.get("url", "")
                if not url.endswith("?Game=Botpack.DeathMatchPlus") or url.split("?", 1)[0] not in {
                    "DM-Morbias][", "DM-Deck16][", "DM-Phobos"
                }:
                    errors.append("qualification run_config URL/game class mismatch")
                if summary.get("scenario") != "skill-qualification" or int(summary.get("ticks", -1)) != 5400:
                    errors.append("qualification summary scenario/ticks mismatch")
                if not math.isclose(float(summary.get("fixed_delta", float("nan"))), 1.0 / 60.0, abs_tol=5e-7):
                    errors.append("qualification summary fixed delta mismatch")
                if run_end_fields.get("status") != "passed" or int(run_end_fields.get("ticks", -1)) != 5400:
                    errors.append("qualification run_end status/ticks mismatch")
                run_digest = run_end_fields.get("digest", "")
                if len(run_digest) != 16 or any(character not in "0123456789abcdef" for character in run_digest) \
                        or run_digest != summary.get("digest_fnv1a64"):
                    errors.append("qualification run_end digest does not match summary")
                if str(summary.get("seed")) != run_config_fields.get("seed"):
                    errors.append("qualification summary seed does not match run_config")
                if int(summary.get("requested_bots", -1)) != int(run_config_fields.get("bots", -2)):
                    errors.append("qualification summary bot count does not match run_config")
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
            if requested_bot_names:
                summary_names = [str(name) for name in summary.get("requested_bot_names", [])]
                if summary_names != requested_bot_names:
                    errors.append("summary requested bot names do not match trace")
                summary_metrics = summary.get("bot_metrics", [])
                for roster_index, requested_name in enumerate(requested_bot_names):
                    matching = [metric for metric in summary_metrics if int(metric.get("roster_index", -1)) == roster_index]
                    if len(matching) != 1:
                        errors.append(f"summary has {len(matching)} metrics for roster index {roster_index}, expected 1")
                        continue
                    metric = matching[0]
                    if str(metric.get("player_name", "")).casefold() != requested_name.casefold():
                        errors.append(f"summary roster index {roster_index} player name mismatch")
                    profile_id = str(metric.get("profile_id", ""))
                    event_profiles = [fields.get("profile_id", "") for _, fields in profile_configurations
                                      if int(fields.get("roster_index", -1)) == roster_index]
                    if len(event_profiles) != 1 or profile_id != event_profiles[0]:
                        errors.append(f"summary roster index {roster_index} profile ID mismatch")
                    if roster_index >= len(requested_skills) or int(metric.get("requested_external_skill", -1)) != requested_skills[roster_index]:
                        errors.append(f"summary roster index {roster_index} requested skill mismatch")
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
                    "environmental_damage_taken_exact": exact[identity]["environmental_damage"],
                    "external_damage_taken_exact": exact[identity]["external_damage_taken"],
                    "external_damage_dealt_exact": exact[identity]["external_damage_dealt"],
                    "fatal_damage_kills_exact": exact[identity]["kills"],
                    "fatal_damage_deaths_exact": exact[identity]["deaths"],
                    "self_fatal_damage_deaths_exact": exact[identity]["self_fatal_deaths"],
                    "environmental_fatal_damage_deaths_exact": exact[identity]["environmental_fatal_deaths"],
                    "external_fatal_damage_deaths_exact": exact[identity]["external_fatal_damage_deaths"],
                    "adjudicated_deaths_exact": exact[identity]["adjudicated_deaths"],
                    "adjudicated_opponent_kills_exact": exact[identity]["adjudicated_opponent_kills"],
                    "adjudicated_self_deaths_exact": exact[identity]["adjudicated_self_deaths"],
                    "adjudicated_environmental_deaths_exact": exact[identity]["adjudicated_environmental_deaths"],
                    "adjudicated_external_deaths_exact": exact[identity]["adjudicated_external_deaths"],
                    "adjudicated_external_kills_exact": exact[identity]["adjudicated_external_kills"],
                    "adjudicated_direct_deaths_exact": exact[identity]["adjudicated_direct_deaths"],
                }
                for name, expected in expected_fields.items():
                    if qualification and name not in metric:
                        errors.append(f"summary {identity} lacks required qualification field {name}")
                    elif name in metric and int(metric[name]) != expected:
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
            summary_validated = len(errors) == summary_error_start
        except (OSError, TypeError, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"invalid summary: {exc}")
    elif qualification:
        errors.append("qualification requires summary.json")

    return {
        "schema": 1,
        "events": sum(counts.values()),
        "last_tick": previous_tick,
        "event_counts": dict(sorted(counts.items())),
        "passed": not errors,
        "validation_mode": "qualification" if qualification else "standard",
        "protocol_id": "surreal-bot-skill-qualification-v1" if qualification else None,
        "events_path": str(events_path),
        "events_sha256": events_sha256,
        "summary_path": str(canonical_summary_path),
        "summary_sha256": summary_sha256,
        "summary_present": summary_present,
        "summary_validated": summary_validated,
        "run_identity": run_identity,
        "errors": errors,
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", help="Benchmark run directory or events.jsonl")
    parser.add_argument("--output", help="Optional report JSON path")
    parser.add_argument("--qualification", action="store_true", help="Require qualification telemetry fields")
    args = parser.parse_args()
    path = Path(args.run).resolve()
    run_directory = path if path.is_dir() else path.parent
    events_path = run_directory / "events.jsonl" if path.is_dir() else path
    candidate_summary = run_directory / "summary.json"
    summary_path = candidate_summary if candidate_summary.is_file() else None
    if not events_path.is_file():
        raise FileNotFoundError(events_path)
    report = validate(events_path, summary_path, qualification=args.qualification)
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = Path(args.output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
