from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

CATALOG_SPEC = importlib.util.spec_from_file_location("catalog_fixture", "Tools/BotBenchmark/tests/test_validate_map_catalog.py")
CATALOG_FIXTURE = importlib.util.module_from_spec(CATALOG_SPEC); assert CATALOG_SPEC.loader
CATALOG_SPEC.loader.exec_module(CATALOG_FIXTURE)
CAP_SPEC = importlib.util.spec_from_file_location("cap_fixture", "Tools/BotBenchmark/tests/test_validate_realized_bot_capabilities.py")
CAP_FIXTURE = importlib.util.module_from_spec(CAP_SPEC); assert CAP_SPEC.loader
CAP_SPEC.loader.exec_module(CAP_FIXTURE)
SPEC = importlib.util.spec_from_file_location("path_commits", "Tools/BotBenchmark/Analyze-NativePathCommits.py")
ANALYZE = importlib.util.module_from_spec(SPEC); assert SPEC.loader
SPEC.loader.exec_module(ANALYZE)

def catalog() -> dict:
    value = copy.deepcopy(CATALOG_FIXTURE.catalog())
    value["counts"].update(actors_exact=2, navigation_points_exact=2, reachspecs_exact=1)
    value["actors"].append({"actor_index": 1, "present": True, "name": "PathNode1", "class": "Engine.PathNode"})
    point = copy.deepcopy(value["navigation_points"][0])
    point.update(actor_index=1, name="PathNode1", position={"x": 1.0, "y": 0.0, "z": 0.0})
    value["navigation_points"].append(point)
    value["navigation_points"][0]["paths"] = [0]; value["navigation_points"][1]["upstream_paths"] = [0]
    value["reachspecs"] = [{"index": 0, "start_actor_index": 0, "end_actor_index": 1,
        "distance": 1, "collision_radius": 40, "collision_height": 40, "reach_flags": 1,
        "reach_flag_names": ["walk"], "unknown_reach_flags": 0, "pruned": False}]
    return value

def write_run(root: Path, bad_index: bool = False) -> None:
    manifest, summary, witness = CAP_FIXTURE.run_documents()
    manifest.update(url="DM-Test?Game=Botpack.DeathMatchPlus", config_id="cfg")
    CAP_FIXTURE.write_run(root, manifest, summary, witness)
    edge = {"reachspec_index": 4 if bad_index else 0, "start_node": "PathNode0", "end_node": "PathNode1",
        "distance": 1, "collision_radius": 40, "collision_height": 40, "reach_flags_raw": 1,
        "unknown_reach_flags": 0, "pruned": False}
    commit = {"sequence": "1", "origin": "find_path_toward", "phase": "pre_special_cache_commit",
        "raw_endpoint_cost": 1, "adjusted_endpoint_cost": 1, "failed_navigation_penalty_applications": 0,
        "cache_clear": False, "truncated_by_route_cache": False,
        "nodes": [{"name": "PathNode0", "class": "PathNode"}, {"name": "PathNode1", "class": "PathNode"}], "edges": [edge]}
    row = {"schema": ANALYZE.ROUTE_SCHEMA, "seq": "0", "benchmark_config_id": "cfg", "tick": "1",
        "participants": [{"roster_index": 0, "identity": "bot-0", "available": True,
            "native_path_commit_overflows_exact": "0", "native_path_commits": [commit]}]}
    (root / "route-execution.jsonl").write_text(json.dumps(row) + "\n", encoding="utf-8")

class NativePathCommitTests(unittest.TestCase):
    def test_accepts_exact_catalog_edge(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            report = ANALYZE.analyze(root / "catalog.json", run)
            self.assertEqual(report["counts"]["committed_edges_exact"], 1)
    def test_rejects_unknown_edge_index(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run, True)
            with self.assertRaisesRegex(ANALYZE.PathCommitError, "reachspec index"):
                ANALYZE.analyze(root / "catalog.json", run)

    def test_rejects_missing_terminal_life_provenance_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            path = run / "route-execution.jsonl"; row = json.loads(path.read_text(encoding="utf-8"))
            participant = row["participants"][0]; participant["available"] = False
            del participant["native_path_commits"]
            path.write_text(json.dumps(row) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ANALYZE.PathCommitError, "native_path_commits"):
                ANALYZE.analyze(root / "catalog.json", run)

if __name__ == "__main__": unittest.main()
