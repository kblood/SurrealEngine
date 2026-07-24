from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[1] / "Evaluate-BotQualityGate.py"
SPEC = importlib.util.spec_from_file_location("evaluate_bot_quality_gate", TOOL_PATH)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


def run(path: str, variant: str, map_name: str, metrics: dict, *, complete: bool = True) -> dict:
    return {
        "path": path,
        "variant": variant,
        "config": {"map": map_name},
        "result": {"status": "complete" if complete else "failed", "exit_code": 0 if complete else 1},
        "metrics": metrics,
        "validation": {"status": "passed" if complete else "failed"},
    }


def aggregate(variant: str, metrics: dict[str, list[float]]) -> dict:
    return {
        "variant": variant,
        "runs": max((len(values) for values in metrics.values()), default=0),
        "metrics": {
            name: {
                "count": len(values), "mean": sum(values) / len(values),
                "median": sorted(values)[len(values) // 2],
                "minimum": min(values), "maximum": max(values),
            }
            for name, values in metrics.items()
        },
    }


def report(*runs: dict) -> dict:
    variants: dict[str, dict[str, list[float]]] = {}
    for item in runs:
        target = variants.setdefault(item["variant"], {})
        for name, value in item["metrics"].items():
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                target.setdefault(name, []).append(float(value))
    return {
        "schema": GATE.REPORT_SCHEMA,
        "runs": list(runs),
        "variant_aggregates": [aggregate(name, metrics) for name, metrics in variants.items()],
        "metric_availability": {
            "available": sorted({name for item in runs for name in item["metrics"]}),
            "unavailable_until_telemetry_is_extended": {
                "avoidable_suicides_exact": "Attributed causal telemetry is required."
            },
        },
    }


def config(**overrides) -> dict:
    document = {
        "schema": GATE.CONFIG_SCHEMA,
        "required_metrics": ["completion", "kills_exact"],
        "required_runs": [{"variant": "candidate", "min": 1}],
        "aggregate_gates": [
            {"variant": "candidate", "metric": "kills_exact", "statistic": "mean", "min": 1},
        ],
        "per_run_gates": [
            {"variant": "candidate", "metric": "completion", "equals": True},
            {"variant": "candidate", "metric": "deaths_exact", "max": 2},
        ],
    }
    document.update(overrides)
    return document


class QualityGateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.good = run("candidate-deck", "candidate", "DM-Deck16][", {
            "completion": True, "kills_exact": 2, "deaths_exact": 1,
            "suicides_exact": 1,
        })

    def test_pass_has_auditable_evidence(self) -> None:
        result = GATE.evaluate(report(self.good), config())
        self.assertEqual(result["status"], "passed")
        self.assertEqual(result["violations"], [])
        self.assertGreater(result["summary"]["checks"], 0)

    def test_threshold_regression_fails(self) -> None:
        bad = run("candidate-deck", "candidate", "DM-Deck16][", {
            "completion": True, "kills_exact": 0, "deaths_exact": 3,
        })
        result = GATE.evaluate(report(bad), config())
        self.assertEqual(result["status"], "failed")
        reasons = " ".join(item["reason"] for item in result["violations"])
        self.assertIn("at least", reasons)
        self.assertIn("at most", reasons)

    def test_required_equality_fails_on_unequal_metric(self) -> None:
        unequal = run("candidate-deck", "candidate", "DM-Deck16][", {
            "completion": False, "kills_exact": 2, "deaths_exact": 1,
        })
        result = GATE.evaluate(report(unequal), config())
        self.assertEqual(result["status"], "failed")
        self.assertIn("expected equality", " ".join(
            item["reason"] for item in result["violations"]))

    def test_missing_required_metric_fails_without_aliasing(self) -> None:
        missing = run("candidate-deck", "candidate", "DM-Deck16][", {
            "completion": True, "deaths_exact": 0,
        })
        result = GATE.evaluate(report(missing), config())
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any(item["kind"] == "required_metric" for item in result["violations"]))

    def test_null_metric_is_unavailable_not_zero(self) -> None:
        legacy = run("candidate-deck", "candidate", "DM-Deck16][", {
            "completion": True, "kills_exact": 1, "deaths_exact": 0,
            "avoidable_suicides_exact": None,
        })
        gate_config = config(required_metrics=["completion", "kills_exact", "avoidable_suicides_exact"])
        quality_report = report(legacy)
        quality_report["metric_availability"]["available"].remove("avoidable_suicides_exact")
        result = GATE.evaluate(quality_report, gate_config)
        self.assertEqual(result["status"], "failed")
        self.assertIn("unavailable", " ".join(item["reason"] for item in result["violations"]))

    def test_incomplete_or_invalid_run_fails_even_when_metrics_pass(self) -> None:
        incomplete = run("candidate-deck", "candidate", "DM-Deck16][", {
            "completion": True, "kills_exact": 2, "deaths_exact": 0,
        }, complete=False)
        result = GATE.evaluate(report(incomplete), config())
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any(item["kind"] == "run_validity" for item in result["violations"]))

    def test_map_scoped_per_run_gate_ignores_other_maps(self) -> None:
        morbias = run("candidate-morbias", "candidate", "DM-Morbias][", {
            "completion": True, "kills_exact": 2, "deaths_exact": 99,
        })
        gate_config = config(
            required_runs=[{"variant": "candidate", "map": "DM-Deck16][", "min": 1}],
            per_run_gates=[{
                "id": "deck-deaths", "variant": "candidate", "map": "DM-Deck16][",
                "metric": "deaths_exact", "max": 2,
            }],
        )
        result = GATE.evaluate(report(self.good, morbias), gate_config)
        self.assertEqual(result["status"], "passed")
        checked_paths = [check["evidence"].get("path") for check in result["checks"]
                         if check["kind"] == "per_run_gate"]
        self.assertEqual(checked_paths, ["candidate-deck"])

    def test_missing_map_scoped_run_fails(self) -> None:
        gate_config = config(required_runs=[{
            "variant": "candidate", "map": "DM-Morbias][", "min": 1,
        }])
        result = GATE.evaluate(report(self.good), gate_config)
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any(item["kind"] == "required_runs" for item in result["violations"]))

    def test_map_scoped_attributed_death_safety_fails_closed(self) -> None:
        attributed = {
            "completion": True, "kills_exact": 2, "deaths_exact": 1,
            "direct_self_kills": 0, "direct_enemy_kills": 1,
            "unassisted_environmental_deaths": 0,
            "recent_enemy_contributed_environmental_deaths_proxy": 0,
            "ambiguous_deaths": 0,
            "recent_enemy_momentum_contributed_environmental_deaths_proxy": 0,
        }
        deck = run("candidate-deck", "candidate", "DM-Deck16][", attributed)
        attribution_metrics = [
            "direct_self_kills",
            "direct_enemy_kills",
            "unassisted_environmental_deaths",
            "recent_enemy_contributed_environmental_deaths_proxy",
            "ambiguous_deaths",
            "recent_enemy_momentum_contributed_environmental_deaths_proxy",
        ]
        safety_metrics = [
            "direct_self_kills",
            "unassisted_environmental_deaths",
            "recent_enemy_contributed_environmental_deaths_proxy",
            "ambiguous_deaths",
        ]
        gate_config = config(
            required_metrics=["completion", "deaths_exact", *attribution_metrics],
            required_runs=[{"variant": "candidate", "map": "DM-Deck16][", "min": 1}],
            aggregate_gates=[],
            per_run_gates=[{
                "id": f"deck-{metric}", "variant": "candidate", "map": "DM-Deck16][",
                "metric": metric, "max": 0,
            } for metric in safety_metrics],
        )
        self.assertEqual(GATE.evaluate(report(deck), gate_config)["status"], "passed")

        unsafe_metrics = {**attributed, "unassisted_environmental_deaths": 1}
        unsafe = run("candidate-deck-unsafe", "candidate", "DM-Deck16][", unsafe_metrics)
        unsafe_result = GATE.evaluate(report(unsafe), gate_config)
        self.assertEqual(unsafe_result["status"], "failed")
        self.assertTrue(any(
            item["evidence"].get("metric") == "unassisted_environmental_deaths"
            for item in unsafe_result["violations"]))

        missing_metrics = {**attributed}
        missing_metrics.pop("ambiguous_deaths")
        missing = run("candidate-deck-missing", "candidate", "DM-Deck16][", missing_metrics)
        missing_result = GATE.evaluate(report(missing), gate_config)
        self.assertEqual(missing_result["status"], "failed")
        self.assertTrue(any(
            item["evidence"].get("metric") == "ambiguous_deaths"
            for item in missing_result["violations"]))

    def test_falling_seam_shadow_acceptance_metrics_are_gateable_and_fail_closed(self) -> None:
        shadow_metrics = {
            "falling_seam_detections_exact": 2,
            "horizontal_corner_candidate_probes_exact": 1,
            "horizontal_corner_authorized_escapes_exact": 1,
            "horizontal_corner_target_progress_rejects_exact": 0,
            "horizontal_corner_unknown_or_unsafe_support_exact": 0,
        }
        safe_fixture = run("candidate-safe-corner", "candidate", "DM-SafeCorner", {
            "completion": True, "kills_exact": 0, "deaths_exact": 0, **shadow_metrics,
        })
        required = list(shadow_metrics)
        gate_config = config(
            required_metrics=["completion", *required],
            required_runs=[{"variant": "candidate", "map": "DM-SafeCorner", "min": 1}],
            aggregate_gates=[],
            per_run_gates=[
                {"id": "candidate-probed", "variant": "candidate", "map": "DM-SafeCorner",
                 "metric": "horizontal_corner_candidate_probes_exact", "min": 1},
                {"id": "known-safe-authorization", "variant": "candidate",
                 "map": "DM-SafeCorner",
                 "metric": "horizontal_corner_authorized_escapes_exact", "min": 1},
            ],
        )
        self.assertEqual(GATE.evaluate(report(safe_fixture), gate_config)["status"], "passed")

        missing_metrics = {**safe_fixture["metrics"]}
        missing_metrics.pop("horizontal_corner_unknown_or_unsafe_support_exact")
        missing = run(
            "candidate-safe-corner-missing", "candidate", "DM-SafeCorner", missing_metrics)
        result = GATE.evaluate(report(missing), gate_config)
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any(
            item["kind"] == "required_metric"
            and item["evidence"].get("metric") ==
            "horizontal_corner_unknown_or_unsafe_support_exact"
            for item in result["violations"]))

    def test_cli_returns_nonzero_and_writes_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            report_path = root / "report.json"
            config_path = root / "gates.json"
            output_path = root / "result.json"
            report_path.write_text(json.dumps(report(self.good)), encoding="utf-8")
            config_path.write_text(json.dumps(config(per_run_gates=[{
                "variant": "candidate", "metric": "deaths_exact", "max": 0,
            }])), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(TOOL_PATH), str(report_path), str(config_path),
                 "--output", str(output_path)], capture_output=True, text=True, check=False)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(json.loads(output_path.read_text(encoding="utf-8"))["status"], "failed")
            self.assertIn("violations", completed.stderr)


if __name__ == "__main__":
    unittest.main()
