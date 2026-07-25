from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "direct_reach_commands", "Tools/BotBenchmark/Analyze-DirectReachCommands.py")
ANALYZE = importlib.util.module_from_spec(SPEC); assert SPEC.loader
SPEC.loader.exec_module(ANALYZE)


COUNTERS = {
    "direct_reach_command_observations_exact": "1",
    "direct_reach_command_successes_exact": "1",
    "direct_reach_command_failures_exact": "0",
    "direct_reach_command_same_life_exact_exact": "1",
    "direct_reach_command_unlinked_exact": "0",
    "direct_reach_command_overflows_exact": "0",
    "direct_reach_command_hazardous_deaths_exact": "0",
    "direct_reach_command_nonhazard_deaths_exact": "0",
    "direct_reach_command_cleared_exact": "1",
    "direct_reach_command_life_boundary_censored_exact": "0",
    "direct_reach_command_run_end_censored_exact": "0",
}


def record(status: str = "same_life_exact", reached: bool = True, sequence: str = "1",
           terminal: str = "cleared", hazard_terminal_exact: bool = False,
           activation_tick: str = "5", terminal_tick: str = "6") -> dict:
    result = {
        "sequence": sequence, "life_id": "1", "target_actor_index": 7,
        "target_name": "PAmmo1", "target_class": "Botpack.Ammo", "reached": reached,
        "check_navpoint": False, "resolved_wall_slide": False,
        "walking_simulation_iterations": 1, "latent_action": "MoveToward",
        "route_head_present": False, "link_status": status,
    }
    if status == "same_life_exact":
        result.update(activation_tick=activation_tick, terminal_tick=terminal_tick,
                      terminal=terminal, hazard_terminal_exact=hazard_terminal_exact)
    else:
        result.update(activation_tick=None, terminal_tick=None, terminal=None,
                      hazard_terminal_exact=None)
    return result


def write_run(root: Path, counters: dict | None = None, records: list[dict] | None = None,
              enabled: bool = True) -> None:
    (root / "manifest.json").write_text(json.dumps({
        "config_id": "cfg", "direct_reach_command_observer_enabled": enabled}), encoding="utf-8")
    (root / "summary.json").write_text(json.dumps({"status": "complete", "exit_code": 0,
        "actual_roster": [{"identity": "pri:1"}]}), encoding="utf-8")
    bot = {"identity": "pri:1", **(COUNTERS if counters is None else counters),
           "direct_reach_command_records": [record()] if records is None else records}
    event = {"direct_reach_command_observer": {"requested": True, "status": "active"},
             "bots": [bot]}
    (root / "events.jsonl").write_text(json.dumps(event) + "\n", encoding="utf-8")


class DirectReachCommandTests(unittest.TestCase):
    def test_accepts_reconciled_same_life_record(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root)
            report = ANALYZE.analyze(root)
            self.assertTrue(report["qualified"])
            self.assertEqual(report["coverage"]["same_life_exact"], 1)

    def test_rejects_disabled_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, enabled=False)
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "does not explicitly enable"):
                ANALYZE.analyze(root)

    def test_rejects_counter_partition_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); counters = copy.deepcopy(COUNTERS)
            counters["direct_reach_command_unlinked_exact"] = "1"
            write_run(root, counters=counters)
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "link partition"):
                ANALYZE.analyze(root)

    def test_rejects_noncontiguous_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, records=[record(sequence="2")])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "not contiguous"):
                ANALYZE.analyze(root)

    def test_rejects_overflow(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); counters = copy.deepcopy(COUNTERS)
            counters["direct_reach_command_overflows_exact"] = "1"
            write_run(root, counters=counters)
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "overflow"):
                ANALYZE.analyze(root)

    def test_zero_link_coverage_is_unqualified(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); counters = copy.deepcopy(COUNTERS)
            counters["direct_reach_command_same_life_exact_exact"] = "0"
            counters["direct_reach_command_unlinked_exact"] = "1"
            counters["direct_reach_command_cleared_exact"] = "0"
            write_run(root, counters=counters, records=[record("unavailable_target_replaced")])
            report = ANALYZE.analyze(root)
            self.assertFalse(report["qualified"])
            self.assertEqual(report["coverage"]["verdict"], "unqualified_no_same_life_exact")

    def test_accepts_each_terminal_with_matching_exact_hazard_evidence(self) -> None:
        for terminal in ANALYZE.TERMINALS:
            with self.subTest(terminal=terminal), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                counters = copy.deepcopy(COUNTERS)
                counters[ANALYZE.TERMINAL_COUNTERS[terminal]] = "1"
                if terminal != "cleared":
                    counters["direct_reach_command_cleared_exact"] = "0"
                write_run(root, records=[record(
                    terminal=terminal,
                    hazard_terminal_exact=terminal == "hazardous_death")], counters=counters)
                self.assertTrue(ANALYZE.analyze(root)["qualified"])

    def test_rejects_same_life_record_without_terminal_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); item = record(); del item["terminal"]
            write_run(root, records=[item])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "terminal is invalid"):
                ANALYZE.analyze(root)

    def test_rejects_hazard_terminal_evidence_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_run(root, records=[record(terminal="hazardous_death")])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "does not match"):
                ANALYZE.analyze(root)
            write_run(root, records=[record(terminal="cleared", hazard_terminal_exact=True)])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "does not match"):
                ANALYZE.analyze(root)

    def test_rejects_terminal_before_activation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_run(root, records=[record(activation_tick="7", terminal_tick="6")])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "precedes activation"):
                ANALYZE.analyze(root)

    def test_rejects_terminal_counter_partition_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); counters = copy.deepcopy(COUNTERS)
            counters["direct_reach_command_cleared_exact"] = "0"
            counters["direct_reach_command_hazardous_deaths_exact"] = "1"
            write_run(root, counters=counters)
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "hazardous_death terminal counter"):
                ANALYZE.analyze(root)

    def test_rejects_terminal_evidence_on_unlinked_record(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); counters = copy.deepcopy(COUNTERS)
            counters["direct_reach_command_same_life_exact_exact"] = "0"
            counters["direct_reach_command_unlinked_exact"] = "1"
            counters["direct_reach_command_cleared_exact"] = "0"
            item = record("unavailable_target_replaced")
            item["terminal"] = "cleared"
            write_run(root, counters=counters, records=[item])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "unlinked record has terminal"):
                ANALYZE.analyze(root)

    def test_rejects_malformed_terminal_tick_or_hazard_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_run(root, records=[record(activation_tick="not-a-tick")])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "activation_tick"):
                ANALYZE.analyze(root)
            item = record(); item["hazard_terminal_exact"] = "false"
            write_run(root, records=[item])
            with self.assertRaisesRegex(ANALYZE.DirectReachCommandError, "hazard terminal exact"):
                ANALYZE.analyze(root)


if __name__ == "__main__":
    unittest.main()
