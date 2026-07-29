from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "direct_inventory_retry_chains", "Tools/BotBenchmark/Analyze-DirectInventoryRetryChains.py")
ANALYZE = importlib.util.module_from_spec(SPEC); assert SPEC.loader
SPEC.loader.exec_module(ANALYZE)


def direct(sequence: int, tick: int) -> dict:
    return {
        "sequence": str(sequence), "life_id": "4", "reach_sequence": str(sequence),
        "native_tick": str(tick), "target_actor_index": 101, "target_name": "BulletBox4",
        "target_class": "Botpack.Ammo", "target_is_inventory": True,
        "marker_known": True, "marker_live": True, "marker_actor_index": 53,
        "marker_name": "InventorySpot53", "marker_class": "Botpack.InventorySpot",
        "reached": True, "check_navpoint": False, "caller_origin": "script_actor_reachable",
        "reject_reason": "reached", "resolved_wall_slide": False,
        "walking_simulation_iterations": 1, "latent_action": "MoveToward",
        "route_head_present": False, "link_status": "same_life_exact",
        "activation_tick": str(tick + 1), "terminal_tick": str(tick + 2),
        "terminal": "cleared", "hazard_terminal_exact": False,
    }


def stall(tick: int = 12) -> dict:
    return {
        "sequence": "1", "life_id": "4", "episode_id": "8", "native_tick": str(tick),
        "latent_mode": "move_toward", "decision": "none", "no_progress_seconds": 2.0,
        "no_progress_displacement": 3.5, "progress_radius": 4.0,
        "move_target_known": True, "move_target_live": True, "move_target_actor_index": 101,
        "move_target_name": "BulletBox4", "move_target_class": "Botpack.Ammo",
        "move_target_is_inventory": True, "marker_known": True, "marker_live": True,
        "marker_actor_index": 53, "marker_name": "InventorySpot53",
        "marker_class": "Botpack.InventorySpot", "move_timer": 1.0,
    }


def event(records: list[dict], decisions: list[dict]) -> dict:
    return {"type": "tick", "direct_reach_command_observer": {"requested": True, "status": "active"},
            "bots": [{"identity": "pri:1", "direct_reach_command_overflows_exact": "0",
                      "move_stall_recovery_decision_record_overflows_exact": "0",
                      "direct_reach_command_records": records,
                      "move_stall_recovery_decisions": decisions}]}


def route(tick: int, commits: list[dict]) -> dict:
    return {"schema": "surreal-bot-route-execution-observation-v1", "benchmark_config_id": "cfg",
            "tick": str(tick), "participants": [{"identity": "pri:1",
            "native_path_commit_overflows_exact": "0", "native_path_commits": commits}]}


def commit(marker: str = "InventorySpot53") -> dict:
    return {"origin": "find_path_toward", "phase": "pre_special_cache_commit",
            "cache_clear": False, "truncated_by_route_cache": False,
            "nodes": [{"name": "PathNode20", "class": "Engine.PathNode"},
                      {"name": marker, "class": "Botpack.InventorySpot"}],
            "edges": [{"pruned": False}]}


def write_run(root: Path, events: list[dict], routes: list[dict]) -> None:
    (root / "manifest.json").write_text(json.dumps({"config_id": "cfg", "url": "DM-Deck16][",
        "direct_reach_command_observer_enabled": True,
        "native_path_commit_observer_enabled": True}), encoding="utf-8")
    (root / "summary.json").write_text(json.dumps({"status": "complete", "exit_code": 0,
        "actual_roster": [{"identity": "pri:1"}]}), encoding="utf-8")
    (root / "events.jsonl").write_text("\n".join(json.dumps(value) for value in events) + "\n",
                                            encoding="utf-8")
    (root / "route-execution.jsonl").write_text("\n".join(json.dumps(value) for value in routes) + "\n",
                                                     encoding="utf-8")


class DirectInventoryRetryChainTests(unittest.TestCase):
    def valid(self) -> tuple[list[dict], list[dict]]:
        return ([event([direct(1, 10)], []), event([], [stall()]), event([direct(2, 13)], [])],
                [route(10, []), route(12, []), route(13, [commit()])])

    def test_accepts_exact_chain(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            events, routes = self.valid(); write_run(Path(temp), events, routes)
            report = ANALYZE.analyze(Path(temp))
            self.assertTrue(report["qualified"])
            self.assertEqual(report["counts"]["qualifying_chains_exact"], "1")

    def test_missing_marker_commit_is_unqualified(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            events, routes = self.valid(); routes[-1] = route(13, [])
            write_run(Path(temp), events, routes)
            report = ANALYZE.analyze(Path(temp))
            self.assertFalse(report["qualified"])
            self.assertEqual(report["coverage"]["verdict"], "unqualified_no_exact_retry_chain")

    def test_rejects_non_immediate_reissue(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            events, routes = self.valid(); events[-1] = event([direct(2, 14)], [])
            routes[-1] = route(14, [commit()]); write_run(Path(temp), events, routes)
            self.assertFalse(ANALYZE.analyze(Path(temp))["qualified"])

    def test_rejects_overflow(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            events, routes = self.valid(); bad = copy.deepcopy(events)
            bad[1]["bots"][0]["move_stall_recovery_decision_record_overflows_exact"] = "1"
            write_run(Path(temp), bad, routes)
            with self.assertRaisesRegex(ANALYZE.RetryChainError, "overflow"):
                ANALYZE.analyze(Path(temp))

    def test_ignores_non_inventory_stall_with_blank_marker_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            events, routes = self.valid(); non_inventory = copy.deepcopy(stall())
            non_inventory.update({"sequence": "2", "move_target_is_inventory": False,
                                  "marker_known": False, "marker_live": False,
                                  "marker_actor_index": -1, "marker_name": "", "marker_class": ""})
            events[1] = event([], [stall(), non_inventory]); write_run(Path(temp), events, routes)
            self.assertTrue(ANALYZE.analyze(Path(temp))["qualified"])

    def test_rejects_non_inventory_identity_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            events, routes = self.valid(); bad = copy.deepcopy(events)
            del bad[0]["bots"][0]["direct_reach_command_records"][0]["marker_actor_index"]
            write_run(Path(temp), bad, routes)
            with self.assertRaisesRegex(ANALYZE.RetryChainError, "marker actor index"):
                ANALYZE.analyze(Path(temp))


if __name__ == "__main__":
    unittest.main()
