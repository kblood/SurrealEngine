#!/usr/bin/env python3
"""Contract tests for independent controlled HitWall trace validation."""

from __future__ import annotations

import copy
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


ASSERTIONS = (
    "movetoward_preserves_location", "movetoward_latent_started",
    "movetoward_target_assigned", "hitwall_setup_is_roaming", "hitwall_setup_is_walking",
    "fixture_wall_callback_observed", "fixture_wall_callback_ids_and_blockers_match",
    "fixture_wall_callback_is_non_mover", "fixture_wall_callback_event_enabled",
    "fixture_wall_normal_opposes_lane", "fixture_bot_alive_after_callback",
    "fixture_bot_remains_source_side", "fixture_bot_remains_walking",
    "stock_hitwall_sets_from_wall", "stock_hitwall_sets_one_unit_side_destination",
    "stock_hitwall_preserves_endpoint_focus", "stock_hitwall_selects_roaming_adjust_label",
    "stock_hitwall_clears_movetoward_latent", "stock_hitwall_keeps_move_timer_nonnegative",
    "fixture_actors_destroyed",
)


def make_events() -> list[dict]:
    events: list[dict] = []

    def emit(tick: int, event_type: str, fields: dict[str, str]) -> None:
        events.append({"schema": 1, "seq": len(events), "tick": tick, "type": event_type, "fields": fields})

    fixture_id = "controlled-hitwall-blockall-v1"
    emit(0, "run_config", {"fixture_id": fixture_id})
    emit(0, "benchmark_spectator", {"class_is_spectator": "true", "pri_is_spectator": "true"})
    emit(0, "bot_observed", {})
    emit(0, "simulation_start", {})
    emit(0, "fixture_wait", {
        "fixture_id": fixture_id, "condition": "PHYS_Walking", "timeout_ticks": "60",
    })
    emit(5, "fixture_setup", {
        "fixture_id": fixture_id, "bot": "Botpack.TFemale1Bot:TFemale1Bot0",
        "target": "Engine.AmbientSound:AmbientSound0", "blocker_count": "9",
        "blockers": ">".join(f"BlockAll{index}" for index in range(9)),
        "source_x": "0", "source_y": "0", "source_z": "0",
        "target_x": "0", "target_y": "256", "target_z": "0",
        "direction_x": "0", "direction_y": "1", "blocker_radius": "36",
        "blocker_height": "96", "blocker_distance": "128", "blocker_spacing": "64",
        "blocker_offset_min": "-256", "blocker_offset_max": "256",
        "movement_timeout_ticks": "120",
    })
    before = {
        "hit_id": "1", "actor": "TFemale1Bot0", "blocker": "BlockAll4",
        "blocker_class": "Engine.BlockAll", "blocker_is_mover": "false", "event_enabled": "true",
        "normal_x": "0", "normal_y": "-1", "normal_z": "0", "physics_before": "1",
        "state_before": "Roaming", "latent_before": "6", "statement_index_before": "34",
        "location_x_before": "0", "location_y_before": "74", "location_z_before": "0",
        "destination_x_before": "0", "destination_y_before": "256", "destination_z_before": "0",
        "focus_x_before": "0", "focus_y_before": "256", "focus_z_before": "0",
        "move_timer_before": "1.5", "b_from_wall_before": "false",
    }
    after = {
        "hit_id": "1", "actor": "TFemale1Bot0", "blocker": "BlockAll4",
        "blocker_class": "Engine.BlockAll", "event_enabled": "true", "physics_after": "1",
        "state_after": "Roaming", "state_changed": "false", "latent_after": "0",
        "statement_index_after": "87", "adjust_from_wall_label_index_after": "87",
        "location_x_after": "0", "location_y_after": "74", "location_z_after": "0",
        "destination_x_after": "1", "destination_y_after": "74", "destination_z_after": "0",
        "focus_x_after": "0", "focus_y_after": "256", "focus_z_after": "0",
        "move_timer_after": "1.5", "b_from_wall_after": "true",
        "health_after": "100", "b_delete_me_after": "false",
    }
    emit(19, "walking_hit_wall", before)
    emit(19, "walking_hit_wall_result", after)
    for name in ASSERTIONS:
        emit(20, "fixture_assert", {
            "fixture_id": fixture_id, "name": name, "expected": "true", "actual": "true", "passed": "true",
        })
    emit(20, "fixture_complete", {
        "fixture_id": fixture_id, "status": "passed", "assertions_total": "20",
        "assertions_passed": "20", "assertions_failed": "0", "walking_hit_wall_pairs": "1",
    })
    emit(20, "run_end", {"status": "passed"})
    return events


def validate_events(events: list[dict]) -> dict:
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "events.jsonl"
        path.write_text("".join(json.dumps(event) + "\n" for event in events), encoding="utf-8")
        return MODULE.validate(path, None)


class HitWallTraceValidationTests(unittest.TestCase):
    def test_valid_raw_protocol_passes(self) -> None:
        report = validate_events(make_events())
        self.assertTrue(report["passed"], report["errors"])

    def test_contradictory_raw_result_fails_despite_passing_assertions(self) -> None:
        events = copy.deepcopy(make_events())
        result = next(event for event in events if event["type"] == "walking_hit_wall_result")
        result["fields"]["b_from_wall_after"] = "false"
        report = validate_events(events)
        self.assertFalse(report["passed"])
        self.assertTrue(any("bFromWall transition" in error for error in report["errors"]))

    def test_same_tick_setup_does_not_count_as_normal_tick_collision(self) -> None:
        events = copy.deepcopy(make_events())
        for event in events:
            if event["type"] in ("walking_hit_wall", "walking_hit_wall_result"):
                event["tick"] = 5
        report = validate_events(events)
        self.assertFalse(report["passed"])
        self.assertTrue(any("normal tick after setup" in error for error in report["errors"]))


if __name__ == "__main__":
    unittest.main()
