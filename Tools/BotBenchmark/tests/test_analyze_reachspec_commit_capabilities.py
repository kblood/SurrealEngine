from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


NATIVE_SPEC = importlib.util.spec_from_file_location(
    "native_fixture", "Tools/BotBenchmark/tests/test_analyze_native_path_commits.py")
NATIVE_FIXTURE = importlib.util.module_from_spec(NATIVE_SPEC); assert NATIVE_SPEC.loader
NATIVE_SPEC.loader.exec_module(NATIVE_FIXTURE)
SPEC = importlib.util.spec_from_file_location(
    "reachspec_capabilities", "Tools/BotBenchmark/Analyze-ReachspecCommitCapabilities.py")
ANALYZE = importlib.util.module_from_spec(SPEC); assert SPEC.loader
SPEC.loader.exec_module(ANALYZE)


def capability_snapshot() -> dict:
    return {"native_tick": "1", "life_id": "1", "capability_flags": 1,
            "capabilities": {"walk": True, "fly": False, "swim": False, "jump": False,
                             "open_doors": False, "special": False, "is_player": False}}


def eligibility() -> dict:
    return {"eligible": True, "required_flags": 1, "capability_flags": 1,
            "missing_flags": 0, "invalid_flags": 0, "disposition": "eligible"}


def write_run(root: Path) -> None:
    NATIVE_FIXTURE.write_run(root)
    manifest_path = root / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["reachspec_capability_observer_enabled"] = True
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    route_path = root / "route-execution.jsonl"
    route = json.loads(route_path.read_text(encoding="utf-8"))
    commit = route["participants"][0]["native_path_commits"][0]
    commit["reachspec_capability"] = capability_snapshot()
    commit["edges"][0]["reachspec_eligibility"] = eligibility()
    route_path.write_text(json.dumps(route) + "\n", encoding="utf-8")


class ReachSpecCommitCapabilityTests(unittest.TestCase):
    def test_accepts_exact_live_snapshot_and_model_result(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(NATIVE_FIXTURE.catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            report = ANALYZE.analyze(root / "catalog.json", run)
            self.assertEqual(report["counts"]["capability_snapshots_exact"], 1)
            self.assertEqual(report["counts"]["eligible_edges_exact"], 1)
            self.assertTrue(report["qualified"])

    def test_rejects_missing_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(NATIVE_FIXTURE.catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            path = run / "route-execution.jsonl"; route = json.loads(path.read_text(encoding="utf-8"))
            del route["participants"][0]["native_path_commits"][0]["reachspec_capability"]
            path.write_text(json.dumps(route) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ANALYZE.ReachSpecCommitCapabilityError, "missing live capability"):
                ANALYZE.analyze(root / "catalog.json", run)

    def test_rejects_model_result_that_does_not_match_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(NATIVE_FIXTURE.catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            path = run / "route-execution.jsonl"; route = json.loads(path.read_text(encoding="utf-8"))
            route["participants"][0]["native_path_commits"][0]["edges"][0]["reachspec_eligibility"] = copy.deepcopy(eligibility())
            route["participants"][0]["native_path_commits"][0]["edges"][0]["reachspec_eligibility"]["eligible"] = False
            path.write_text(json.dumps(route) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ANALYZE.ReachSpecCommitCapabilityError, "does not match"):
                ANALYZE.analyze(root / "catalog.json", run)

    def test_accepts_missing_capability_result(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(NATIVE_FIXTURE.catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            path = run / "route-execution.jsonl"; route = json.loads(path.read_text(encoding="utf-8"))
            commit = route["participants"][0]["native_path_commits"][0]
            snapshot = capability_snapshot(); snapshot["capabilities"]["walk"] = False; snapshot["capability_flags"] = 0
            commit["reachspec_capability"] = snapshot
            commit["edges"][0]["reachspec_eligibility"] = {
                "eligible": False, "required_flags": 1, "capability_flags": 0,
                "missing_flags": 1, "invalid_flags": 0, "disposition": "missing_capabilities"}
            path.write_text(json.dumps(route) + "\n", encoding="utf-8")
            report = ANALYZE.analyze(root / "catalog.json", run)
            self.assertEqual(report["counts"]["missing_capabilities_edges_exact"], 1)

    def test_rejects_disabled_observer(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / "catalog.json").write_text(json.dumps(NATIVE_FIXTURE.catalog()), encoding="utf-8")
            run = root / "run"; run.mkdir(); write_run(run)
            path = run / "manifest.json"; manifest = json.loads(path.read_text(encoding="utf-8"))
            manifest["reachspec_capability_observer_enabled"] = False
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ANALYZE.ReachSpecCommitCapabilityError, "explicitly enabled"):
                ANALYZE.analyze(root / "catalog.json", run)


if __name__ == "__main__":
    unittest.main()
