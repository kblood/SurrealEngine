from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "movement_command_provenance", "Tools/BotBenchmark/Analyze-MovementCommandProvenance.py")
ANALYZE = importlib.util.module_from_spec(SPEC); assert SPEC.loader
SPEC.loader.exec_module(ANALYZE)


def command() -> dict:
    return {
        "sequence": "1", "command_token": "1", "life_id": "3", "native_tick": "41",
        "source_actor_index": 7, "caller_invocation_token": "12",
        "caller_class": "Botpack.Bot", "caller_function": "PickDestination", "kind": "move_toward",
        "target_known": True, "target_actor_index": 8, "target_address": "0000000000000001",
        "target_name": "PathNode12", "target_class": "Engine.PathNode",
        "route_head_known": True, "route_head_actor_index": 8, "route_head_name": "PathNode12",
        "route_head_class": "Engine.PathNode", "last_native_path_commit_known": True,
        "last_native_path_commit_sequence": "4", "last_native_path_commit_first_reachspec_index": 13,
        "integrity_valid": True,
    }


def pain_timer_death(exact: bool = True) -> dict:
    return {
        "environmental_source": "pain_timer", "hazard_residence_terminal_exact": True,
        "hazard_residence_command_ownership_life_id": "3",
        "hazard_residence_movement_command_token": "1",
        "hazard_residence_movement_command_provenance_exact": exact,
        "hazard_residence_movement_command_caller_class": "Botpack.Bot",
        "hazard_residence_movement_command_caller_function": "PickDestination",
    }


def write_run(root: Path, *, records: list[dict] | None = None,
              deaths: list[dict] | None = None, overflow: str = "0") -> None:
    (root / "manifest.json").write_text(json.dumps({
        "movement_command_provenance_observer_enabled": True,
        "native_path_commit_observer_enabled": True}), encoding="utf-8")
    (root / "summary.json").write_text(json.dumps({"status": "complete", "exit_code": 0}),
                                          encoding="utf-8")
    bot = {
        "identity": "pri:7", "movement_command_provenance_observations_exact": "1",
        "movement_command_provenance_overflows_exact": overflow,
        "movement_command_provenance_records": [command()] if records is None else records,
        "hazard_death_partition_records": [] if deaths is None else deaths,
    }
    event = {"movement_command_provenance_observer": {"requested": True, "status": "active"},
             "bots": [bot]}
    (root / "events.jsonl").write_text(json.dumps(event) + "\n", encoding="utf-8")


class MovementCommandProvenanceTests(unittest.TestCase):
    def test_accepts_reconciled_command_and_pain_timer_lineage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, deaths=[pain_timer_death()])
            report = ANALYZE.analyze(root)
            self.assertEqual(report["pain_timer_hazard_deaths_exact"], "1")

    def test_rejects_overflow(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, overflow="1")
            with self.assertRaisesRegex(ANALYZE.ProvenanceError, "overflow"):
                ANALYZE.analyze(root)

    def test_rejects_inexact_pain_timer_lineage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, deaths=[pain_timer_death(False)])
            with self.assertRaisesRegex(ANALYZE.ProvenanceError, "lacks exact"):
                ANALYZE.analyze(root)

    def test_rejects_unmatched_pain_timer_command_token(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); death = pain_timer_death(); death[
                "hazard_residence_movement_command_token"] = "2"
            write_run(root, deaths=[death])
            with self.assertRaisesRegex(ANALYZE.ProvenanceError, "no matching"):
                ANALYZE.analyze(root)

    def test_rejects_missing_caller_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); item = command(); item["caller_function"] = ""
            write_run(root, records=[item])
            with self.assertRaisesRegex(ANALYZE.ProvenanceError, "caller provenance"):
                ANALYZE.analyze(root)


if __name__ == "__main__":
    unittest.main()
