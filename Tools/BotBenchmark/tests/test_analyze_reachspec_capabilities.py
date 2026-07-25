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
    "analyze_reachspec_capabilities", "Tools/BotBenchmark/Analyze-ReachspecCapabilities.py")
ANALYZE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ANALYZE)


def catalog() -> dict:
    value = CATALOG_FIXTURE.catalog()
    value["map"] = "DM-Test"
    value["map_package"]["name"] = "DM-Test"
    value["counts"]["reachspecs_exact"] = 1
    value["reachspecs"] = [{
        "index": 0, "start_actor_index": 0, "end_actor_index": 0,
        "distance": 10, "collision_radius": 20, "collision_height": 30,
        "reach_flags": 2, "reach_flag_names": ["fly"], "unknown_reach_flags": 0,
        "pruned": False,
    }]
    value["navigation_points"][0]["paths"] = [0]
    return value


def write_run(root: Path) -> None:
    manifest, summary, witness = CAPABILITY_FIXTURE.run_documents()
    manifest["url"] = "DM-Test?Game=Botpack.DeathMatchPlus"
    CAPABILITY_FIXTURE.write_run(root, manifest, summary, witness)


class ReachspecCapabilityAuditTests(unittest.TestCase):
    def test_reports_absent_fly_flag_without_claiming_a_route_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(catalog()), encoding="utf-8")
            run = root / "run"
            run.mkdir()
            write_run(run)
            report = ANALYZE.analyze(catalog_path, run)
            self.assertEqual(report["catalog"]["reach_flag_incidence_exact"], {"fly": 1})
            self.assertEqual(
                report["participants"][0]["absent_capability_flag_incidence_exact"], {"fly": 1})
            self.assertFalse(report["interpretation"]["selection_safe"])

    def test_rejects_catalog_for_a_different_map(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            value = catalog()
            value["map"] = "OtherMap"
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(value), encoding="utf-8")
            run = root / "run"
            run.mkdir()
            write_run(run)
            with self.assertRaisesRegex(ANALYZE.AuditError, "does not match"):
                ANALYZE.analyze(catalog_path, run)

    def test_player_only_is_reported_as_opaque_not_a_capability(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            value = catalog()
            value["reachspecs"][0].update(
                reach_flags=64, reach_flag_names=["player_only"], unknown_reach_flags=0)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(value), encoding="utf-8")
            run = root / "run"
            run.mkdir()
            write_run(run)
            report = ANALYZE.analyze(catalog_path, run)
            self.assertEqual(report["catalog"]["player_only_reachspecs_exact"], 1)
            self.assertEqual(report["participants"][0]["absent_capability_flag_incidence_exact"], {})


if __name__ == "__main__":
    unittest.main()
