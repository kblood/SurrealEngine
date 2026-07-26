from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[1] / "Analyze-HazardResidenceCommandTransitionLedger.py"
SPEC = importlib.util.spec_from_file_location("hazard_residence_command_transition_ledger", TOOL_PATH)
assert SPEC and SPEC.loader
LEDGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LEDGER)


def command(token: int = 1, sequence: int = 1, tick: int = 41) -> dict:
    return {
        "sequence": str(sequence), "command_token": str(token), "life_id": "3", "native_tick": str(tick),
        "source_actor_index": 7, "caller_invocation_token": "12",
        "caller_class": "Botpack.Bot", "caller_function": "PickDestination", "kind": "move_toward",
        "target_known": True, "target_actor_index": 8, "target_address": "0000000000000001",
        "target_name": "PathNode12", "target_class": "Engine.PathNode",
        "route_head_known": True, "route_head_actor_index": 8, "route_head_name": "PathNode12",
        "route_head_class": "Engine.PathNode", "last_native_path_commit_known": True,
        "last_native_path_commit_sequence": "4", "last_native_path_commit_first_reachspec_index": 13,
        "integrity_valid": True,
    }


def ledger(commands: list[dict] | None = None, terminal: str = "death") -> dict:
    commands = [command()] if commands is None else commands
    entries = []
    for index, item in enumerate(commands, 1):
        entries.append({"sequence": str(index), "role": "entry" if index == 1 else "replacement",
                        "prior_command_token": "0" if index == 1 else commands[index - 2]["command_token"],
                        "command": item})
    return {
        "sequence": "1", "episode_id": "1", "life_id": "3", "terminal": terminal,
        "entry_command_token": commands[0]["command_token"], "terminal_command_token": commands[-1]["command_token"],
        "entry_integrity_valid": True, "terminal_integrity_valid": True, "entries": entries,
    }


def write_run(root: Path, *, commands: list[dict] | None = None, records: list[dict] | None = None,
              ledger_overflow: str = "0", summary_ledger_flag: bool = True) -> None:
    commands = [command()] if commands is None else commands
    records = [ledger(commands)] if records is None else records
    flags = {"native_path_commit_observer_enabled": True,
             "movement_command_provenance_observer_enabled": True,
             "hazard_residence_command_transition_ledger_observer_enabled": True}
    (root / "manifest.json").write_text(json.dumps(flags), encoding="utf-8")
    summary_flags = dict(flags); summary_flags[
        "hazard_residence_command_transition_ledger_observer_enabled"] = summary_ledger_flag
    (root / "summary.json").write_text(json.dumps({"status": "complete", "exit_code": 0,
                                                      "config": summary_flags}), encoding="utf-8")
    bot = {
        "identity": "pri:7", "movement_command_provenance_observations_exact": str(len(commands)),
        "movement_command_provenance_overflows_exact": "0", "movement_command_provenance_records": commands,
        "hazard_residence_command_transition_ledger_episodes_exact": str(len(records)),
        "hazard_residence_command_transition_ledger_overflows_exact": ledger_overflow,
        "hazard_residence_command_transition_ledger_records": records,
    }
    active = {"requested": True, "status": "active"}
    event = {"movement_command_provenance_observer": active,
             "hazard_residence_command_transition_ledger_observer": active, "bots": [bot]}
    (root / "events.jsonl").write_text(json.dumps(event) + "\n", encoding="utf-8")


class HazardResidenceCommandTransitionLedgerTests(unittest.TestCase):
    def test_accepts_complete_ordered_terminal_ledger(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root)
            report = LEDGER.analyze(root)
            self.assertEqual(report["episodes_exact"], "1")
            self.assertEqual(report["terminal_dispositions_exact"]["death"], "1")

    def test_accepts_ordered_same_life_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); commands = [command(), command(token=2, sequence=2, tick=43)]
            write_run(root, commands=commands)
            self.assertEqual(LEDGER.analyze(root)["episodes_exact"], "1")

    def test_rejects_missing_summary_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, summary_ledger_flag=False)
            with self.assertRaisesRegex(LEDGER.LedgerError, "summary.config"):
                LEDGER.analyze(root)

    def test_rejects_overflow(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, ledger_overflow="1")
            with self.assertRaisesRegex(LEDGER.LedgerError, "overflow"):
                LEDGER.analyze(root)

    def test_rejects_unknown_terminal(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, records=[ledger(terminal="unknown")])
            with self.assertRaisesRegex(LEDGER.LedgerError, "terminal disposition"):
                LEDGER.analyze(root)

    def test_rejects_cross_life_command(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); item = command(); item["life_id"] = "4"
            write_run(root, commands=[item], records=[ledger([item])])
            with self.assertRaisesRegex(LEDGER.LedgerError, "life boundary"):
                LEDGER.analyze(root)

    def test_rejects_unreconciled_ledger_command(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = command(); ledger_item = command(); ledger_item["caller_function"] = "Other"
            write_run(root, commands=[source], records=[ledger([ledger_item])])
            with self.assertRaisesRegex(LEDGER.LedgerError, "exactly reconcile"):
                LEDGER.analyze(root)

    def test_rejects_broken_terminal_join(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); record = ledger(); record["terminal_command_token"] = "2"
            write_run(root, records=[record])
            with self.assertRaisesRegex(LEDGER.LedgerError, "terminal command token"):
                LEDGER.analyze(root)


if __name__ == "__main__":
    unittest.main()
