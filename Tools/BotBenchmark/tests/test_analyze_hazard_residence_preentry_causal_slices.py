from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[1] / "Analyze-HazardResidencePreentryCausalSlices.py"
SPEC = importlib.util.spec_from_file_location("hazard_residence_preentry_causal_slices", TOOL_PATH)
assert SPEC and SPEC.loader
SLICES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SLICES)


def command(token: int = 1, sequence: int = 1, tick: int = 41) -> dict:
    return {
        "sequence": str(sequence), "command_token": str(token), "life_id": "3", "native_tick": str(tick),
        "source_actor_index": 7, "caller_invocation_token": "12",
        "caller_class": "Botpack.Bot", "caller_function": "PickDestination", "kind": "move_toward",
        "target_known": True, "target_actor_index": 8,
        "target_name": "PathNode12", "target_class": "Engine.PathNode",
        "route_head_known": True, "route_head_actor_index": 8, "route_head_name": "PathNode12",
        "route_head_class": "Engine.PathNode", "last_native_path_commit_known": True,
        "last_native_path_commit_sequence": "4", "last_native_path_commit_first_reachspec_index": 13,
        "integrity_valid": True,
    }


def ledger(commands: list[dict] | None = None, terminal: str = "death") -> dict:
    commands = [command()] if commands is None else commands
    entries = [{
        "sequence": str(index), "role": "entry" if index == 1 else "replacement",
        "prior_command_token": "0" if index == 1 else commands[index - 2]["command_token"],
        "command": item,
    } for index, item in enumerate(commands, 1)]
    return {
        "sequence": "1", "episode_id": "1", "life_id": "3", "terminal": terminal,
        "entry_command_token": commands[0]["command_token"], "terminal_command_token": commands[-1]["command_token"],
        "entry_integrity_valid": True, "terminal_integrity_valid": True, "entries": entries,
    }


def lineage(record: dict) -> list[dict]:
    return [{
        "sequence": item["sequence"], "role": item["role"],
        "prior_command_token": item["prior_command_token"],
        "command_token": item["command"]["command_token"], "native_tick": item["command"]["native_tick"],
        "caller_class": item["command"]["caller_class"], "caller_function": item["command"]["caller_function"],
        "kind": item["command"]["kind"],
    } for item in record["entries"]]


def slice_record(record: dict | None = None) -> dict:
    record = ledger() if record is None else record
    return {
        "sequence": record["sequence"], "episode_id": record["episode_id"], "life_id": record["life_id"],
        "terminal": record["terminal"], "zone_actor_index": 14, "zone_name": "SlimeZone",
        "zone_class": "Engine.ZoneInfo", "entry_location": {"x": 1.0, "y": 2.0, "z": 3.0},
        "pre_entry_physics": "Falling", "entry_physics": "Swimming", "support_known": False,
        "support_actor_index": -1, "support_class": "", "transition": "entered_hazard_zone",
        "command_lineage": lineage(record),
        "mayfall_boundary": {"observed": True, "native_tick": "39", "physics": "Falling"},
        "hitwall_boundary": {"observed": True, "native_tick": "40", "physics": "Falling"},
        "integrity_valid": True, "entry_integrity_valid": True, "terminal_integrity_valid": True,
    }


def write_run(root: Path, *, terminal: dict | None = None, causal: dict | None = None,
              slice_overflow: str = "0", summary_slice_flag: bool = True) -> None:
    terminal = ledger() if terminal is None else terminal
    causal = slice_record(terminal) if causal is None else causal
    flags = {
        "native_path_commit_observer_enabled": True,
        "movement_command_provenance_observer_enabled": True,
        "hazard_residence_command_transition_ledger_observer_enabled": True,
        "hazard_residence_preentry_causal_slice_observer_enabled": True,
    }
    (root / "manifest.json").write_text(json.dumps(flags), encoding="utf-8")
    summary_flags = dict(flags)
    summary_flags["hazard_residence_preentry_causal_slice_observer_enabled"] = summary_slice_flag
    (root / "summary.json").write_text(json.dumps({"status": "complete", "exit_code": 0,
                                                     "config": summary_flags}), encoding="utf-8")
    bot = {
        "identity": "pri:7", "hazard_residence_command_transition_ledger_episodes_exact": "1",
        "hazard_residence_command_transition_ledger_overflows_exact": "0",
        "hazard_residence_command_transition_ledger_records": [terminal],
        "hazard_residence_preentry_causal_slice_episodes_exact": "1",
        "hazard_residence_preentry_causal_slice_overflows_exact": slice_overflow,
        "hazard_residence_preentry_causal_slice_records": [causal],
    }
    active = {"requested": True, "status": "active"}
    event = {"movement_command_provenance_observer": active,
             "hazard_residence_command_transition_ledger_observer": active,
             "hazard_residence_preentry_causal_slice_observer": active, "bots": [bot]}
    (root / "events.jsonl").write_text(json.dumps(event) + "\n", encoding="utf-8")


class HazardResidencePreentryCausalSliceTests(unittest.TestCase):
    def test_accepts_complete_causal_slice_reconciled_to_terminal_ledger(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root)
            report = SLICES.analyze(root)
            self.assertEqual(report["episodes_exact"], "1")
            self.assertEqual(report["terminal_dispositions_exact"]["death"], "1")

    def test_accepts_replacement_command_lineage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); terminal = ledger([command(), command(token=2, sequence=2, tick=43)])
            write_run(root, terminal=terminal, causal=slice_record(terminal))
            self.assertEqual(SLICES.analyze(root)["episodes_exact"], "1")

    def test_rejects_missing_summary_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, summary_slice_flag=False)
            with self.assertRaisesRegex(SLICES.CausalSliceError, "summary.config"):
                SLICES.analyze(root)

    def test_rejects_overflow(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); write_run(root, slice_overflow="1")
            with self.assertRaisesRegex(SLICES.CausalSliceError, "overflow"):
                SLICES.analyze(root)

    def test_rejects_unknown_zone_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); causal = slice_record(); causal["zone_name"] = "unknown"
            write_run(root, causal=causal)
            with self.assertRaisesRegex(SLICES.CausalSliceError, "zone_name"):
                SLICES.analyze(root)

    def test_rejects_incomplete_boundary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); causal = slice_record(); causal["mayfall_boundary"]["native_tick"] = "0"
            write_run(root, causal=causal)
            with self.assertRaisesRegex(SLICES.CausalSliceError, "mayfall_boundary"):
                SLICES.analyze(root)

    def test_rejects_unreconciled_command_lineage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); causal = slice_record(); causal["command_lineage"][0]["caller_function"] = "Other"
            write_run(root, causal=causal)
            with self.assertRaisesRegex(SLICES.CausalSliceError, "lineage does not exactly"):
                SLICES.analyze(root)

    def test_rejects_terminal_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); causal = slice_record(); causal["terminal"] = "cleared"
            write_run(root, causal=causal)
            with self.assertRaisesRegex(SLICES.CausalSliceError, "terminal does not"):
                SLICES.analyze(root)


if __name__ == "__main__":
    unittest.main()
