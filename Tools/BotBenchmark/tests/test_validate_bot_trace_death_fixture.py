#!/usr/bin/env python3
"""Controlled death-outcome fixture replay and mutation tests."""

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).resolve().parents[1] / "Validate-BotTrace.py"
SPEC = importlib.util.spec_from_file_location("validate_bot_trace_death_fixture", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

FIXTURE_ID = "controlled-death-outcomes-v1"
PROFILES = (
    ("pri:1", "Loque", 7, "Botpack.ChallengeBotInfo|name=Loque"),
    ("pri:2", "Tamerlane", 6, "Botpack.ChallengeBotInfo|name=Tamerlane"),
)
ASSERTIONS = (
    "fixture_two_profile_roster_bound", "fixture_game_is_deathmatchplus",
    "environment_damage_one_fatal_call", "environment_damage_one_adjudicated_death",
    "environment_damage_linked", "environment_damage_nested_deduplicated",
    "respawn_1_live_same_identity", "direct_opponent_no_damage_call",
    "direct_opponent_one_adjudicated_death", "direct_opponent_nested_deduplicated",
    "respawn_2_live_same_identity", "direct_environment_no_damage_call",
    "direct_environment_one_adjudicated_death", "direct_environment_nested_deduplicated",
    "respawn_3_live_same_identity", "death_fixture_active_maps_empty",
    "death_fixture_bots_restored",
)


def empty_metric(index: int) -> dict:
    identity, name, skill, profile_id = PROFILES[index]
    result = {
        "identity": identity, "roster_index": index, "player_name": name,
        "profile_id": profile_id, "requested_external_skill": skill,
        "combat_by_weapon": [],
    }
    for field in (
        "damage_events_dealt_exact", "damage_events_taken_exact", "damage_dealt_exact",
        "damage_taken_exact", "self_damage_exact", "environmental_damage_taken_exact",
        "external_damage_taken_exact", "external_damage_dealt_exact", "fatal_damage_kills_exact",
        "fatal_damage_deaths_exact", "self_fatal_damage_deaths_exact",
        "environmental_fatal_damage_deaths_exact", "external_fatal_damage_deaths_exact",
        "adjudicated_deaths_exact", "adjudicated_opponent_kills_exact",
        "adjudicated_self_deaths_exact", "adjudicated_environmental_deaths_exact",
        "adjudicated_external_deaths_exact", "adjudicated_external_kills_exact",
        "adjudicated_direct_deaths_exact",
    ):
        result[field] = 0
    return result


def valid_documents() -> tuple[list[dict], dict]:
    events: list[dict] = []

    def emit(tick: int, event_type: str, fields: dict[str, str]) -> None:
        events.append({"schema": 1, "seq": len(events), "tick": tick,
                       "type": event_type, "fields": fields})

    emit(0, "run_config", {
        "scenario": "controlled-death-outcomes", "ticks": "720", "fixed_delta": "0.016667",
        "bots": "2", "fixture_id": FIXTURE_ID, "requested_bot_names": "Loque,Tamerlane",
        "requested_skills": "7,6", "seed": "104729",
        "url": "DM-Morbias][?Game=Botpack.DeathMatchPlus",
    })
    emit(0, "benchmark_spectator", {"class_is_spectator": "true", "pri_is_spectator": "true"})
    for index, (identity, name, skill, profile_id) in enumerate(PROFILES):
        emit(0, "bot_skill_configured", {
            "identity": identity, "roster_index": str(index), "requested_profile_name": name,
            "actual_profile_name": name, "bot_config_class": "Botpack.ChallengeBotInfo",
            "profile_id": profile_id, "requested_external_skill": str(skill), "mapping_valid": "true",
        })
    emit(0, "fixture_setup", {
        "fixture_id": FIXTURE_ID, "game_class": "Botpack.DeathMatchPlus",
        "expected_actions": "3", "respawn_timeout_ticks": "180",
        "bot0": "Bot0", "bot0_identity": "pri:1", "bot0_roster_index": "0",
        "bot0_profile_id": PROFILES[0][3], "bot0_initial_health": "100",
        "bot0_initial_score": "0", "bot0_initial_deaths": "0",
        "bot1": "Bot1", "bot1_identity": "pri:2", "bot1_roster_index": "1",
        "bot1_profile_id": PROFILES[1][3], "bot1_initial_health": "100",
        "bot1_initial_score": "0", "bot1_initial_deaths": "0",
    })
    emit(0, "bot_observed", {})
    emit(0, "simulation_start", {})
    for name in ASSERTIONS[:2]:
        emit(0, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": name,
                                    "expected": "true", "actual": "true", "passed": "true"})

    def action(step: int, tick: int, mechanism: str, classification: str, mediated: bool,
               victim: int, killer: int, damage_type: str, deaths_before: int,
               victim_score_before: int, victim_score_after: int,
               killer_score_before: int, killer_score_after: int) -> None:
        damage_before = 0 if step == 1 else 1
        death_before = step - 1
        victim_identity = PROFILES[victim][0]
        killer_identity = "None" if killer < 0 else PROFILES[killer][0]
        emit(tick, "fixture_death_action", {
            "fixture_id": FIXTURE_ID, "step": str(step), "mechanism": mechanism,
            "expected_classification": classification,
            "expected_damage_mediated": str(mediated).lower(), "victim": victim_identity,
            "victim_roster_index": str(victim), "killer": killer_identity,
            "killer_roster_index": str(killer), "damage_id_before": str(damage_before),
            "death_id_before": str(death_before),
        })
        damage_id = 1 if mediated else 0
        emit(tick, "death_adjudicated", {
            "death_id": str(step), "damage_id": str(damage_id), "game_class": "Botpack.DeathMatchPlus",
            "game_killed_dispatch_count": "2", "game_killed_max_depth": "2",
            "victim": victim_identity, "killer": killer_identity,
            "victim_roster_index": str(victim), "killer_roster_index": str(killer),
            "classification": classification, "damage_mediated": str(mediated).lower(),
            "damage_type": damage_type, "victim_deaths_before": str(deaths_before),
            "victim_deaths_after": str(deaths_before + 1),
            "victim_score_before": str(victim_score_before), "victim_score_after": str(victim_score_after),
            "killer_score_before": str(killer_score_before), "killer_score_after": str(killer_score_after),
        })
        if mediated:
            emit(tick, "damage", {
                "damage_id": "1", "victim": victim_identity, "instigator": "None",
                "victim_roster_index": str(victim), "instigator_roster_index": "-1",
                "health_before": "100", "health_after": "-10000", "raw_health_delta": "10100",
                "effective_health_damage": "100", "fatal": "true", "self_damage": "false",
                "damage_origin": "environment_null", "damage_type": "Burned",
            })
        emit(tick, "fixture_death_action_result", {
            "fixture_id": FIXTURE_ID, "step": str(step), "damage_id_after": str(damage_before + int(mediated)),
            "death_id_after": str(step), "damage_id_delta": str(int(mediated)), "death_id_delta": "1",
            "outcome_death_id": str(step), "outcome_damage_id": str(damage_id),
            "outcome_classification": classification, "outcome_damage_mediated": str(mediated).lower(),
            "game_killed_dispatch_count": "2", "game_killed_max_depth": "2",
            "victim_health": "-1", "victim_state": "Dying",
            "active_damage_entries": "0", "active_killed_entries": "0",
        })

    action(1, 1, "TakeDamage", "environment_null", True, 0, -1, "Burned", 0, 0, -1, 0, 0)
    for name in ASSERTIONS[2:6]:
        emit(1, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": name,
                                    "expected": "true", "actual": "true", "passed": "true"})
    emit(100, "fixture_respawn", {
        "fixture_id": FIXTURE_ID, "step": "1", "wait_ticks": "99", "identity": "pri:1",
        "roster_index": "0", "profile_id": PROFILES[0][3], "health": "100", "state": "Roaming",
        "hidden": "false", "collides_actors": "true", "blocks_actors": "true",
        "blocks_players": "true", "weapon_present": "true", "identity_preserved": "true",
    })
    emit(100, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": ASSERTIONS[6],
                                  "expected": "true", "actual": "true", "passed": "true"})

    action(2, 101, "gibbedBy", "participant_opponent", False, 1, 0, "Gibbed", 0, 0, 0, -1, 0)
    for name in ASSERTIONS[7:10]:
        emit(101, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": name,
                                      "expected": "true", "actual": "true", "passed": "true"})
    emit(218, "fixture_respawn", {
        "fixture_id": FIXTURE_ID, "step": "2", "wait_ticks": "117", "identity": "pri:2",
        "roster_index": "1", "profile_id": PROFILES[1][3], "health": "100", "state": "Roaming",
        "hidden": "false", "collides_actors": "true", "blocks_actors": "true",
        "blocks_players": "true", "weapon_present": "true", "identity_preserved": "true",
    })
    emit(218, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": ASSERTIONS[10],
                                  "expected": "true", "actual": "true", "passed": "true"})

    action(3, 219, "FellOutOfWorld", "environment_null", False, 1, -1, "Fell", 1, 0, -1, 0, 0)
    for name in ASSERTIONS[11:14]:
        emit(219, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": name,
                                      "expected": "true", "actual": "true", "passed": "true"})
    emit(299, "fixture_respawn", {
        "fixture_id": FIXTURE_ID, "step": "3", "wait_ticks": "80", "identity": "pri:2",
        "roster_index": "1", "profile_id": PROFILES[1][3], "health": "100", "state": "Roaming",
        "hidden": "false", "collides_actors": "true", "blocks_actors": "true",
        "blocks_players": "true", "weapon_present": "true", "identity_preserved": "true",
    })
    emit(299, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": ASSERTIONS[14],
                                  "expected": "true", "actual": "true", "passed": "true"})
    emit(299, "fixture_cleanup", {
        "fixture_id": FIXTURE_ID, "bot0_identity": "pri:1", "bot0_health": "100",
        "bot0_state": "Roaming", "bot0_hidden": "false", "bot0_collides": "true",
        "bot0_weapon_present": "true", "bot1_identity": "pri:2", "bot1_health": "100",
        "bot1_state": "Roaming", "bot1_hidden": "false", "bot1_collides": "true",
        "bot1_weapon_present": "true", "active_damage_entries": "0", "active_killed_entries": "0",
        "damage_events": "1", "death_events": "3",
    })
    for name in ASSERTIONS[15:]:
        emit(299, "fixture_assert", {"fixture_id": FIXTURE_ID, "name": name,
                                      "expected": "true", "actual": "true", "passed": "true"})
    emit(300, "fixture_complete", {
        "fixture_id": FIXTURE_ID, "status": "passed", "assertions_total": "17",
        "assertions_passed": "17", "assertions_failed": "0", "damage_events": "1",
        "death_events": "3", "game_killed_dispatches": "6", "max_game_killed_depth": "2",
    })
    emit(300, "run_end", {
        "status": "passed", "reason": "", "ticks": "300", "digest": "0123456789abcdef",
    })

    slot0 = empty_metric(0)
    slot0.update({
        "damage_events_taken_exact": 1, "damage_taken_exact": 100,
        "environmental_damage_taken_exact": 100, "fatal_damage_deaths_exact": 1,
        "environmental_fatal_damage_deaths_exact": 1, "adjudicated_deaths_exact": 1,
        "adjudicated_opponent_kills_exact": 1, "adjudicated_environmental_deaths_exact": 1,
    })
    slot1 = empty_metric(1)
    slot1.update({
        "adjudicated_deaths_exact": 2, "adjudicated_environmental_deaths_exact": 1,
        "adjudicated_direct_deaths_exact": 2,
    })
    summary = {
        "status": "passed", "reason": "", "scenario": "controlled-death-outcomes", "ticks": 300,
        "fixed_delta": 1.0 / 60.0, "seed": 104729, "digest_fnv1a64": "0123456789abcdef",
        "requested_bots": 2, "maximum_observed_bots": 2, "maximum_observed_non_bot_participants": 0,
        "non_bot_enemy_target_observations": 0, "viewport_actor_is_spectator": True,
        "viewport_pri_is_spectator": True, "requested_bot_names": ["Loque", "Tamerlane"],
        "fixture_id": FIXTURE_ID,
        "fixture": {"id": FIXTURE_ID, "status": "passed", "assertions_total": 17,
                    "assertions_passed": 17, "assertions_failed": 0},
        "bots": [
            {"roster_index": 0, "health": 100, "state": "Roaming", "inventory_count": 3,
             "profile_id": PROFILES[0][3], "pri_deaths": 1, "pri_score": 0},
            {"roster_index": 1, "health": 100, "state": "Roaming", "inventory_count": 3,
             "profile_id": PROFILES[1][3], "pri_deaths": 2, "pri_score": -1},
        ],
        "bot_metrics": [slot0, slot1],
    }
    return events, summary


def resequence(events: list[dict]) -> None:
    last_tick = 0
    for seq, item in enumerate(events):
        item["seq"] = seq
        item["tick"] = max(last_tick, int(item["tick"]))
        last_tick = item["tick"]


class DeathFixtureValidationTests(unittest.TestCase):
    def validate(self, events: list[dict], summary: dict) -> dict:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        events_path = root / "events.jsonl"
        summary_path = root / "summary.json"
        events_path.write_text("".join(json.dumps(item) + "\n" for item in events), encoding="utf-8")
        summary_path.write_text(json.dumps(summary), encoding="utf-8")
        return MODULE.validate(events_path, summary_path)

    def test_valid_fixture_passes(self) -> None:
        events, summary = valid_documents()
        report = self.validate(events, summary)
        self.assertTrue(report["passed"], report["errors"])

    def test_each_outcome_requires_two_nested_dispatches(self) -> None:
        for ordinal in (0, 1, 2):
            with self.subTest(ordinal=ordinal):
                events, summary = valid_documents()
                deaths = [item for item in events if item["type"] == "death_adjudicated"]
                deaths[ordinal]["fields"]["game_killed_dispatch_count"] = "1"
                report = self.validate(events, summary)
                self.assertFalse(report["passed"])
                self.assertTrue(any("nested dispatch/dedup proof failed" in error for error in report["errors"]))

    def test_reordered_respawn_fails_lifecycle(self) -> None:
        events, summary = valid_documents()
        respawn = next(item for item in events if item["type"] == "fixture_respawn"
                       and item["fields"]["step"] == "2")
        events.remove(respawn)
        action3 = next(index for index, item in enumerate(events)
                       if item["type"] == "fixture_death_action" and item["fields"]["step"] == "3")
        events.insert(action3 + 1, respawn)
        resequence(events)
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("lifecycle" in error for error in report["errors"]))

    def test_direct_outcome_with_damage_link_fails(self) -> None:
        events, summary = valid_documents()
        death = [item for item in events if item["type"] == "death_adjudicated"][1]
        death["fields"]["damage_id"] = "1"
        death["fields"]["damage_mediated"] = "true"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("step 2 outcome mechanism mismatch" in error for error in report["errors"]))

    def test_cleanup_and_summary_corruption_fail(self) -> None:
        events, summary = valid_documents()
        cleanup = next(item for item in events if item["type"] == "fixture_cleanup")
        cleanup["fields"]["active_killed_entries"] = "1"
        summary["bot_metrics"][1]["adjudicated_direct_deaths_exact"] = 1
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("cleanup retained active hook state" in error for error in report["errors"]))
        self.assertTrue(any("adjudicated_direct_deaths_exact" in error for error in report["errors"]))

    def test_wrong_scenario_fails(self) -> None:
        events, summary = valid_documents()
        events[0]["fields"]["scenario"] = "matrix-death-fixture"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("run_config scenario mismatch" in error for error in report["errors"]))

    def test_wrong_map_or_game_fails(self) -> None:
        events, summary = valid_documents()
        events[0]["fields"]["url"] = "DM-Other?Game=Other.Game"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("URL/game mismatch" in error for error in report["errors"]))

    def test_wrong_tick_ceiling_fails(self) -> None:
        events, summary = valid_documents()
        events[0]["fields"]["ticks"] = "300"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("tick ceiling mismatch" in error for error in report["errors"]))

    def test_wrong_fixed_delta_fails(self) -> None:
        events, summary = valid_documents()
        events[0]["fields"]["fixed_delta"] = "0.5"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("run_config fixed delta mismatch" in error for error in report["errors"]))

    def test_wrong_bot_count_fails(self) -> None:
        events, summary = valid_documents()
        events[0]["fields"]["bots"] = "99"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("bot count mismatch" in error for error in report["errors"]))

    def test_wrong_seed_fails(self) -> None:
        events, summary = valid_documents()
        events[0]["fields"]["seed"] = "999"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("run_config seed mismatch" in error for error in report["errors"]))

    def test_wrong_profiles_or_skills_fail(self) -> None:
        for field, value, expected_error in (
            ("requested_bot_names", "Tamerlane,Loque", "profile names"),
            ("requested_skills", "6,7", "skills are not exactly"),
        ):
            with self.subTest(field=field):
                events, summary = valid_documents()
                events[0]["fields"][field] = value
                report = self.validate(events, summary)
                self.assertFalse(report["passed"])
                self.assertTrue(any(expected_error in error for error in report["errors"]))

    def test_failed_or_unbound_run_end_fails(self) -> None:
        events, summary = valid_documents()
        run_end = next(item for item in events if item["type"] == "run_end")
        run_end["fields"].update({"status": "failed", "ticks": "999", "digest": "bad"})
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("run_end/summary ticks" in error for error in report["errors"]))
        self.assertTrue(any("clean pass" in error for error in report["errors"]))
        self.assertTrue(any("digest does not match" in error for error in report["errors"]))

    def test_run_end_must_be_unique_and_final(self) -> None:
        events, summary = valid_documents()
        duplicate = copy.deepcopy(next(item for item in events if item["type"] == "run_end"))
        events.insert(-1, duplicate)
        resequence(events)
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("exactly one run_config and run_end" in error for error in report["errors"]))

    def test_respawn_wait_ticks_are_derived_from_trace(self) -> None:
        events, summary = valid_documents()
        for item in events:
            if item["type"] == "fixture_respawn":
                item["fields"]["wait_ticks"] = "1"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("wait_ticks does not match trace ticks" in error for error in report["errors"]))

    def test_action_counter_baselines_are_replayed(self) -> None:
        events, summary = valid_documents()
        for item in events:
            if item["type"] == "fixture_death_action":
                item["fields"]["damage_id_before"] = "999"
                item["fields"]["death_id_before"] = "999"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("action counter baseline mismatch" in error for error in report["errors"]))

    def test_result_counter_endpoints_are_replayed(self) -> None:
        events, summary = valid_documents()
        result = next(item for item in events if item["type"] == "fixture_death_action_result")
        result["fields"]["damage_id_after"] = "999"
        result["fields"]["death_id_after"] = "999"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("result counter endpoint mismatch" in error for error in report["errors"]))

    def test_result_outcome_ids_are_bound_to_raw_outcome(self) -> None:
        events, summary = valid_documents()
        result = next(item for item in events if item["type"] == "fixture_death_action_result")
        result["fields"]["outcome_damage_id"] = "999"
        result["fields"]["outcome_death_id"] = "999"
        report = self.validate(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("result/outcome mismatch" in error for error in report["errors"]))


if __name__ == "__main__":
    unittest.main()
