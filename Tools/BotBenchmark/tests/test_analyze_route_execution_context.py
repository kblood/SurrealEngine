from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


CATALOG_SPEC = importlib.util.spec_from_file_location(
    "catalog_fixture", "Tools/BotBenchmark/tests/test_validate_map_catalog.py")
CATALOG_FIXTURE = importlib.util.module_from_spec(CATALOG_SPEC)
assert CATALOG_SPEC.loader is not None
CATALOG_SPEC.loader.exec_module(CATALOG_FIXTURE)
CAPABILITY_SPEC = importlib.util.spec_from_file_location(
    "capability_fixture", "Tools/BotBenchmark/tests/test_validate_realized_bot_capabilities.py")
CAPABILITY_FIXTURE = importlib.util.module_from_spec(CAPABILITY_SPEC)
assert CAPABILITY_SPEC.loader is not None
CAPABILITY_SPEC.loader.exec_module(CAPABILITY_FIXTURE)
SPEC = importlib.util.spec_from_file_location(
    "route_context", "Tools/BotBenchmark/Analyze-RouteExecutionContext.py")
ANALYZE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ANALYZE)


def catalog() -> dict:
    value = copy.deepcopy(CATALOG_FIXTURE.catalog())
    value["counts"].update(actors_exact=2, navigation_points_exact=2, reachspecs_exact=1)
    value["actors"].append({"actor_index": 1, "present": True,
                            "name": "PathNode1", "class": "Engine.PathNode"})
    second = copy.deepcopy(value["navigation_points"][0])
    second.update(actor_index=1, name="PathNode1", position={"x": 100.0, "y": 0.0, "z": 0.0})
    value["navigation_points"].append(second)
    value["navigation_points"][0]["paths"] = [0]
    value["navigation_points"][1]["upstream_paths"] = [0]
    value["reachspecs"] = [{
        "index": 0, "start_actor_index": 0, "end_actor_index": 1,
        "distance": 100, "collision_radius": 40, "collision_height": 40,
        "reach_flags": 1, "reach_flag_names": ["walk"], "unknown_reach_flags": 0,
        "pruned": False,
    }]
    return value


def write_run(root: Path, *, broken_edge: bool = False) -> None:
    manifest, summary, witness = CAPABILITY_FIXTURE.run_documents()
    manifest.update(url="DM-Test?Game=Botpack.DeathMatchPlus", config_id="cfg-test")
    CAPABILITY_FIXTURE.write_run(root, manifest, summary, witness)
    second = "PathNode0" if broken_edge else "PathNode1"
    route = {
        "schema": ANALYZE.ROUTE_SCHEMA, "seq": "0", "benchmark_config_id": "cfg-test", "tick": "1",
        "participants": [{"roster_index": 0, "identity": "bot-0", "available": True,
            "move_target": {"name": "PathNode0", "class": "Engine.PathNode"},
            "route_cache": [{"name": "PathNode0", "class": "Engine.PathNode"},
                            {"name": second, "class": "Engine.PathNode"}]}],
    }
    tick = {"type": "tick", "tick": 1, "bots": [{"identity": "bot-0", "latent_action": "MoveToward",
                                                         "deaths_exact": "1", "in_hazard_zone": False}]}
    (root / "route-execution.jsonl").write_text(json.dumps(route) + "\n", encoding="utf-8")
    (root / "events.jsonl").write_text(json.dumps(tick) + "\n", encoding="utf-8")


class RouteExecutionContextTests(unittest.TestCase):
    def test_resolves_active_first_hop_and_binds_death_tick(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            report = ANALYZE.analyze(catalog_path, run)
            self.assertEqual(report["counts"]["first_hop_resolved_exact"], 1)
            self.assertEqual(report["death_contexts"][0]["observed_route_first_hop"]["index"], 0)
            self.assertFalse(report["interpretation"]["selection_safe"])

    def test_rejects_missing_directed_reachspec(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run, broken_edge=True)
            report = ANALYZE.analyze(catalog_path, run)
            self.assertEqual(report["counts"]["first_hop_no_directed_reachspec_exact"], 1)

    def test_rejects_config_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            route_path = run / "route-execution.jsonl"
            value = json.loads(route_path.read_text(encoding="utf-8")); value["benchmark_config_id"] = "wrong"
            route_path.write_text(json.dumps(value) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ANALYZE.ContextError, "config_id"):
                ANALYZE.analyze(catalog_path, run)


if __name__ == "__main__":
    unittest.main()
