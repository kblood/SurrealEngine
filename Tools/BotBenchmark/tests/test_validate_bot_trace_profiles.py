#!/usr/bin/env python3
"""Focused stable-profile contract tests for Validate-BotTrace.py."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).resolve().parents[1] / "Validate-BotTrace.py"
SPEC = importlib.util.spec_from_file_location("validate_bot_trace", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def event(seq: int, event_type: str, fields: dict[str, str] | None = None) -> dict:
    return {"schema": 1, "seq": seq, "tick": 0, "type": event_type, "fields": fields or {}}


def write_trace(root: Path, *, explicit: bool = True, bad_profile: bool = False) -> None:
    names = "Loque,Tamerlane" if explicit else ""
    events = [event(0, "run_config", {
        "fixture_id": "", "requested_bot_names": names, "requested_skills": "7,6",
    }), event(1, "benchmark_spectator", {
        "class_is_spectator": "true", "pri_is_spectator": "true",
    })]
    if explicit:
        for index, (name, skill) in enumerate((("Loque", 7), ("Tamerlane", 6))):
            actual = "Kragoth" if bad_profile and index == 0 else name
            events.append(event(len(events), "bot_skill_configured", {
                "roster_index": str(index), "requested_profile_name": name,
                "actual_profile_name": actual, "bot_config_class": "Botpack.ChallengeBotInfo",
                "profile_id": f"Botpack.ChallengeBotInfo|name={actual}",
                "requested_external_skill": str(skill), "mapping_valid": "true",
            }))
    events.extend((event(len(events), "bot_observed"), event(len(events) + 1, "simulation_start"),
                   event(len(events) + 2, "run_end")))
    (root / "events.jsonl").write_text(
        "".join(json.dumps(item) + "\n" for item in events), encoding="utf-8"
    )
    metrics = []
    if explicit:
        for index, (name, skill) in enumerate((("Loque", 7), ("Tamerlane", 6))):
            actual = "Kragoth" if bad_profile and index == 0 else name
            metrics.append({
                "identity": f"pri:{index + 1}", "roster_index": index, "player_name": actual,
                "profile_id": f"Botpack.ChallengeBotInfo|name={actual}",
                "requested_external_skill": skill, "combat_by_weapon": [],
            })
    summary = {
        "status": "passed", "reason": "", "maximum_observed_bots": 2, "requested_bots": 2,
        "maximum_observed_non_bot_participants": 0, "non_bot_enemy_target_observations": 0,
        "viewport_actor_is_spectator": True, "viewport_pri_is_spectator": True,
        "fixture_id": "", "fixture": {"status": "inactive"},
        "requested_bot_names": names.split(",") if names else [], "bot_metrics": metrics,
    }
    (root / "summary.json").write_text(json.dumps(summary), encoding="utf-8")


class ProfileTraceTests(unittest.TestCase):
    def test_explicit_profiles_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_trace(root)
            self.assertTrue(MODULE.validate(root / "events.jsonl", root / "summary.json")["passed"])

    def test_explicit_profile_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_trace(root, bad_profile=True)
            report = MODULE.validate(root / "events.jsonl", root / "summary.json")
            self.assertFalse(report["passed"])
            self.assertTrue(any("actual profile name" in error for error in report["errors"]))

    def test_legacy_trace_remains_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_trace(root, explicit=False)
            self.assertTrue(MODULE.validate(root / "events.jsonl", root / "summary.json")["passed"])


if __name__ == "__main__":
    unittest.main()
