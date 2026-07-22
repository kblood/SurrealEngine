#!/usr/bin/env python3
"""Focused qualification death/damage protocol tests for Validate-BotTrace.py."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).resolve().parents[1] / "Validate-BotTrace.py"
SPEC = importlib.util.spec_from_file_location("validate_bot_trace_qualification", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def event(seq: int, event_type: str, fields: dict[str, str], tick: int = 0) -> dict:
    return {"schema": 1, "seq": seq, "tick": tick, "type": event_type, "fields": fields}


def metric(identity: str, roster: int, name: str, skill: int) -> dict:
    result = {
        "identity": identity,
        "roster_index": roster,
        "player_name": name,
        "profile_id": f"Botpack.ChallengeBotInfo|name={name}",
        "requested_external_skill": skill,
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
    digest = "0123456789abcdef"
    events = [
        event(0, "run_config", {
            "scenario": "skill-qualification", "ticks": "5400", "fixed_delta": "0.016667",
            "bots": "2", "fixture_id": "", "requested_bot_names": "Loque,Tamerlane",
            "requested_skills": "7,6", "seed": "104729",
            "url": "DM-Morbias][?Game=Botpack.DeathMatchPlus",
        }),
        event(1, "benchmark_spectator", {
            "class_is_spectator": "true", "pri_is_spectator": "true",
        }),
    ]
    for index, (identity, name, skill) in enumerate((
        ("pri:1", "Loque", 7), ("pri:2", "Tamerlane", 6),
    )):
        events.append(event(len(events), "bot_skill_configured", {
            "identity": identity, "roster_index": str(index), "requested_profile_name": name,
            "actual_profile_name": name, "bot_config_class": "Botpack.ChallengeBotInfo",
            "profile_id": f"Botpack.ChallengeBotInfo|name={name}",
            "requested_external_skill": str(skill), "mapping_valid": "true",
        }))
    events.extend((
        event(4, "bot_observed", {}),
        event(5, "simulation_start", {}),
        event(6, "death_adjudicated", {
            "death_id": "1", "damage_id": "1", "victim": "pri:1", "killer": "pri:2",
            "victim_roster_index": "0", "killer_roster_index": "1",
            "classification": "participant_opponent", "damage_mediated": "true",
            "damage_type": "shot", "victim_deaths_before": "0.000000",
            "victim_deaths_after": "1.000000", "victim_score_before": "0.000000",
            "victim_score_after": "0.000000", "killer_score_before": "0.000000",
            "killer_score_after": "1.000000",
        }, tick=1),
        event(7, "damage", {
            "damage_id": "1", "victim": "pri:1", "instigator": "pri:2",
            "victim_roster_index": "0", "instigator_roster_index": "1",
            "health_before": "100", "health_after": "0", "raw_health_delta": "100",
            "effective_health_damage": "100", "fatal": "true", "self_damage": "false",
            "damage_origin": "opponent",
        }, tick=1),
        event(8, "run_end", {"status": "passed", "ticks": "5400", "digest": digest}, tick=5400),
    ))
    loque = metric("pri:1", 0, "Loque", 7)
    loque.update({
        "damage_events_taken_exact": 1, "damage_taken_exact": 100,
        "fatal_damage_deaths_exact": 1, "adjudicated_deaths_exact": 1,
    })
    tamerlane = metric("pri:2", 1, "Tamerlane", 6)
    tamerlane.update({
        "damage_events_dealt_exact": 1, "damage_dealt_exact": 100,
        "fatal_damage_kills_exact": 1, "adjudicated_opponent_kills_exact": 1,
    })
    summary = {
        "status": "passed", "reason": "", "scenario": "skill-qualification", "ticks": 5400,
        "fixed_delta": 1.0 / 60.0, "seed": 104729, "digest_fnv1a64": digest,
        "requested_bots": 2, "maximum_observed_bots": 2,
        "maximum_observed_non_bot_participants": 0, "non_bot_enemy_target_observations": 0,
        "viewport_actor_is_spectator": True, "viewport_pri_is_spectator": True,
        "requested_bot_names": ["Loque", "Tamerlane"], "fixture_id": "",
        "fixture": {"status": "inactive"}, "bot_metrics": [loque, tamerlane],
    }
    return events, summary


def three_damage_documents(emission_ids: tuple[int, int, int]) -> tuple[list[dict], dict]:
    """Build a reconciled trace whose middle emitted damage is the fatal linked record."""
    events, summary = valid_documents()
    assert emission_ids[1] == 1

    def nonfatal_damage(seq: int, damage_id: int, before: int, after: int) -> dict:
        return event(seq, "damage", {
            "damage_id": str(damage_id), "victim": "pri:2", "instigator": "pri:1",
            "victim_roster_index": "1", "instigator_roster_index": "0",
            "health_before": str(before), "health_after": str(after),
            "raw_health_delta": str(before - after),
            "effective_health_damage": str(before - after),
            "fatal": "false", "self_damage": "false", "damage_origin": "opponent",
        }, tick=1)

    fatal_damage = copy.deepcopy(events[7])
    fatal_damage["seq"] = 8
    fatal_damage["fields"]["damage_id"] = str(emission_ids[1])
    run_end = copy.deepcopy(events[8])
    run_end["seq"] = 10
    events = events[:7] + [
        nonfatal_damage(7, emission_ids[0], 100, 90),
        fatal_damage,
        nonfatal_damage(9, emission_ids[2], 90, 70),
        run_end,
    ]

    loque, tamerlane = summary["bot_metrics"]
    loque.update({"damage_events_dealt_exact": 2, "damage_dealt_exact": 30})
    tamerlane.update({"damage_events_taken_exact": 2, "damage_taken_exact": 30})
    return events, summary


def write_documents(root: Path, events: list[dict], summary: dict | None) -> tuple[Path, Path | None]:
    events_path = root / "events.jsonl"
    events_path.write_text("".join(json.dumps(item) + "\n" for item in events), encoding="utf-8")
    summary_path = root / "summary.json"
    if summary is not None:
        summary_path.write_text(json.dumps(summary), encoding="utf-8")
        return events_path, summary_path
    return events_path, None


class QualificationTraceTests(unittest.TestCase):
    def validate_documents(self, events: list[dict], summary: dict | None, *, qualification: bool = True):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        events_path, summary_path = write_documents(Path(directory.name), events, summary)
        return MODULE.validate(events_path, summary_path, qualification=qualification), events_path, summary_path

    def test_valid_trace_passes_and_reports_bound_evidence_identity(self) -> None:
        events, summary = valid_documents()
        report, events_path, summary_path = self.validate_documents(events, summary)
        self.assertTrue(report["passed"], report["errors"])
        self.assertEqual(report["events_path"], str(events_path.resolve()))
        self.assertEqual(report["summary_path"], str(summary_path.resolve()))
        self.assertEqual(report["events_sha256"], hashlib.sha256(events_path.read_bytes()).hexdigest())
        self.assertEqual(report["summary_sha256"], hashlib.sha256(summary_path.read_bytes()).hexdigest())
        self.assertTrue(report["summary_present"])
        self.assertTrue(report["summary_validated"])
        self.assertEqual(report["run_identity"], {
            "map": "DM-Morbias][", "seed": "104729", "requested_skills": [7, 6],
            "requested_bot_names": ["Loque", "Tamerlane"],
            "digest_fnv1a64": "0123456789abcdef", "scenario": "skill-qualification",
            "requested_bots": 2, "ticks": 5400, "fixed_delta": 0.016667,
        })

    def test_wrong_death_roster_and_classification_fail(self) -> None:
        events, summary = valid_documents()
        events[6]["fields"]["victim_roster_index"] = "1"
        events[6]["fields"]["classification"] = "self"
        report, _, _ = self.validate_documents(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("death roster index mismatch" in error for error in report["errors"]))
        self.assertTrue(any("death classification mismatch" in error for error in report["errors"]))

    def test_nonliteral_death_boolean_fails(self) -> None:
        events, summary = valid_documents()
        events[6]["fields"]["damage_mediated"] = "yes"
        report, _, _ = self.validate_documents(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("not a literal boolean" in error for error in report["errors"]))

    def test_missing_or_nonfatal_damage_link_fails(self) -> None:
        events, summary = valid_documents()
        events[6]["fields"]["damage_id"] = "2"
        report, _, _ = self.validate_documents(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("references missing damage_id 2" in error for error in report["errors"]))

        events, summary = valid_documents()
        events[7]["fields"].update({
            "health_after": "1", "raw_health_delta": "99", "effective_health_damage": "99",
            "fatal": "false",
        })
        report, _, _ = self.validate_documents(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("references nonfatal damage_id 1" in error for error in report["errors"]))

    def test_nested_damage_completion_order_passes(self) -> None:
        events, summary = three_damage_documents((2, 1, 3))
        report, _, _ = self.validate_documents(events, summary)
        self.assertTrue(report["passed"], report["errors"])

    def test_damage_ids_remain_positive_unique_and_contiguous(self) -> None:
        adversarial = (
            ((2, 1, 2), "duplicate damage_id 2"),
            ((2, 1, 4), "damage_id set is not contiguous from 1"),
            ((0, 1, 2), "damage_id must be positive"),
            ((-1, 1, 2), "damage_id must be positive"),
        )
        for emission_ids, expected_error in adversarial:
            with self.subTest(emission_ids=emission_ids):
                events, summary = three_damage_documents(emission_ids)
                report, _, _ = self.validate_documents(events, summary)
                self.assertFalse(report["passed"])
                self.assertTrue(
                    any(expected_error in error for error in report["errors"]),
                    report["errors"],
                )

    def test_invalid_pri_transition_fails(self) -> None:
        events, summary = valid_documents()
        events[6]["fields"]["victim_deaths_after"] = "2.000000"
        report, _, _ = self.validate_documents(events, summary)
        self.assertFalse(report["passed"])
        self.assertTrue(any("death counter did not increase by one" in error for error in report["errors"]))

    def test_qualification_requires_summary(self) -> None:
        events, _ = valid_documents()
        report, _, _ = self.validate_documents(events, None)
        self.assertFalse(report["passed"])
        self.assertFalse(report["summary_present"])
        self.assertFalse(report["summary_validated"])
        self.assertIsNone(report["summary_sha256"])
        self.assertTrue(any("requires summary.json" in error for error in report["errors"]))

    def test_standard_unnamed_combat_discovers_configured_participants(self) -> None:
        events, summary = valid_documents()
        events[0]["fields"].update({"scenario": "matrix-standard", "requested_bot_names": ""})
        summary["requested_bot_names"] = []
        report, _, _ = self.validate_documents(events, summary, qualification=False)
        self.assertTrue(report["passed"], report["errors"])


if __name__ == "__main__":
    unittest.main()
