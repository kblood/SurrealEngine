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


def paired_report(*pairs: tuple[str, str, dict, dict]) -> dict:
    runs = []
    pair_entries = []
    metric_names: set[str] = set()
    for pair_id, map_name, baseline_metrics, candidate_metrics in pairs:
        baseline = run(f"{pair_id}-baseline", "baseline", map_name, baseline_metrics)
        candidate = run(f"{pair_id}-candidate", "candidate", map_name, candidate_metrics)
        baseline["metadata"] = {
            "variant": "baseline", "pair_id": pair_id, "comparison_role": "baseline",
        }
        candidate["metadata"] = {
            "variant": "candidate", "pair_id": pair_id, "comparison_role": "candidate",
        }
        runs.extend((baseline, candidate))
        pair_metric_names = set(baseline_metrics) | set(candidate_metrics)
        metric_names.update(pair_metric_names)
        pair_entries.append({
            "pair_id": pair_id,
            "baseline_path": baseline["path"],
            "candidate_path": candidate["path"],
            "candidate_minus_baseline": {
                metric: (float(candidate_metrics[metric]) - float(baseline_metrics[metric])
                         if metric in baseline_metrics and metric in candidate_metrics
                         and baseline_metrics[metric] is not None and candidate_metrics[metric] is not None
                         else None)
                for metric in pair_metric_names
            },
        })
    quality_report = report(*runs)
    quality_report["paired_comparisons"] = [{
        "baseline_variant": "baseline",
        "candidate_variant": "candidate",
        "pair_count": len(pair_entries),
        "metrics": {
            metric: {"preferred_direction": None}
            for metric in metric_names
        },
        "pairs": pair_entries,
    }]
    return quality_report


def paired_gate(**overrides) -> dict:
    gate = {
        "id": "deck-death-non-regression",
        "baseline_variant": "baseline",
        "candidate_variant": "candidate",
        "map": "DM-Deck16][",
        "metric": "deaths_exact",
        "comparison": "delta",
        "direction": "lower",
        "maximum_regression": 0,
    }
    gate.update(overrides)
    return {"schema": GATE.CONFIG_SCHEMA, "paired_gates": [gate]}


def config(**overrides) -> dict:
    document = {
        "schema": GATE.CONFIG_SCHEMA,
        "required_metrics": ["completion", "kills_exact"],
        "required_runs": [{"id": "candidate-runs", "variant": "candidate", "min": 1}],
        "aggregate_gates": [
            {"id": "candidate-kills", "variant": "candidate", "metric": "kills_exact",
             "statistic": "mean", "min": 1},
        ],
        "per_run_gates": [
            {"id": "candidate-completion", "variant": "candidate",
             "metric": "completion", "equals": True},
            {"id": "candidate-deaths", "variant": "candidate",
             "metric": "deaths_exact", "max": 2},
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

    def test_explicit_validity_only_config_passes(self) -> None:
        gate_config = {
            "schema": GATE.CONFIG_SCHEMA,
            "validity_only": True,
        }
        result = GATE.evaluate(report(self.good), gate_config)
        self.assertEqual(result["status"], "passed")
        self.assertTrue(all(
            item["kind"] in {"required_runs", "run_validity"}
            for item in result["checks"]))

        invalid = run("failed", "candidate", "DM-Deck16][", {}, complete=False)
        invalid_result = GATE.evaluate(report(invalid), gate_config)
        self.assertEqual(invalid_result["status"], "failed")
        self.assertTrue(any(
            item["kind"] == "run_validity" for item in invalid_result["violations"]))

    def test_schema_only_config_is_rejected(self) -> None:
        with self.assertRaisesRegex(GATE.GateInputError, "substantive gate"):
            GATE.evaluate(report(self.good), {"schema": GATE.CONFIG_SCHEMA})

    def test_non_boolean_validity_only_is_rejected(self) -> None:
        with self.assertRaisesRegex(GATE.GateInputError, "must be a boolean"):
            GATE.evaluate(report(self.good), {
                "schema": GATE.CONFIG_SCHEMA,
                "validity_only": 1,
            })

    def test_validity_only_cannot_be_mixed_with_substantive_gates(self) -> None:
        with self.assertRaisesRegex(GATE.GateInputError, "cannot be combined"):
            GATE.evaluate(report(self.good), {
                "schema": GATE.CONFIG_SCHEMA,
                "validity_only": True,
                "required_metrics": ["completion"],
            })

    def test_unknown_top_level_and_nested_keys_are_rejected(self) -> None:
        cases = [
            config(typo_per_run_gates=[]),
            config(required_runs=[{"variant": "candidate", "min": 1, "count": 1}]),
            config(aggregate_gates=[{
                "variant": "candidate", "metric": "kills_exact", "min": 1,
                "map": "DM-Deck16][",
            }]),
            config(per_run_gates=[{
                "variant": "candidate", "metric": "deaths_exact", "max": 2,
                "statistic": "mean",
            }]),
        ]
        for gate_config in cases:
            with self.subTest(gate_config=gate_config):
                with self.assertRaisesRegex(GATE.GateInputError, "unknown keys"):
                    GATE.evaluate(report(self.good), gate_config)

    def test_duplicate_and_empty_gate_ids_are_rejected(self) -> None:
        duplicate = config(
            required_runs=[{"id": "deck", "variant": "candidate", "min": 1}],
            aggregate_gates=[{
                "id": "deck", "variant": "candidate", "metric": "kills_exact", "min": 1,
            }],
        )
        with self.assertRaisesRegex(GATE.GateInputError, "duplicate gate id"):
            GATE.evaluate(report(self.good), duplicate)

        for empty_id in ("", "   "):
            with self.subTest(empty_id=empty_id):
                gate_config = config(per_run_gates=[{
                    "id": empty_id, "variant": "candidate",
                    "metric": "deaths_exact", "max": 2,
                }])
                with self.assertRaisesRegex(GATE.GateInputError, "non-empty string"):
                    GATE.evaluate(report(self.good), gate_config)

        without_id = config(required_runs=[{"variant": "candidate", "min": 1}])
        with self.assertRaisesRegex(GATE.GateInputError, "must define a non-empty string id"):
            GATE.evaluate(report(self.good), without_id)

    def test_inconsistent_thresholds_are_rejected(self) -> None:
        gates = [
            {"id": "bad-min-max", "metric": "deaths_exact", "min": 3, "max": 2},
            {"id": "bad-equals-min", "metric": "deaths_exact", "equals": 1, "min": 2},
            {"id": "bad-equals-max", "metric": "deaths_exact", "equals": 3, "max": 2},
            {"id": "bad-bool-range", "metric": "completion", "equals": True, "min": 0},
            {"id": "bad-infinite", "metric": "deaths_exact", "equals": float("inf")},
        ]
        for gate in gates:
            with self.subTest(gate=gate):
                with self.assertRaises(GATE.GateInputError):
                    GATE.evaluate(report(self.good), config(per_run_gates=[gate]))

    def test_required_metrics_alone_is_a_substantive_config(self) -> None:
        result = GATE.evaluate(report(self.good), {
            "schema": GATE.CONFIG_SCHEMA,
            "required_metrics": ["completion"],
        })
        self.assertEqual(result["status"], "passed")

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
            required_runs=[{"id": "candidate-deck-runs", "variant": "candidate",
                            "map": "DM-Deck16][", "min": 1}],
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
            "id": "candidate-morbias-runs", "variant": "candidate",
            "map": "DM-Morbias][", "min": 1,
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
            required_runs=[{"id": "candidate-deck-runs", "variant": "candidate",
                            "map": "DM-Deck16][", "min": 1}],
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
            required_runs=[{"id": "candidate-safe-corner-runs", "variant": "candidate",
                            "map": "DM-SafeCorner", "min": 1}],
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

    def test_falling_parity_and_vertical_column_qualification_metrics_are_gateable(self) -> None:
        qualification_metrics = {
            "falling_parity_realized_episode_completion_fraction": 0.95,
            "falling_parity_realized_comparable_step_fraction": 0.8,
            "falling_parity_realized_mismatch_fraction": 0.0,
            "falling_parity_realized_unknown_step_fraction": 0.0,
            "vertical_pain_column_precision": 0.9,
            "vertical_pain_column_recall": 0.8,
            "vertical_pain_column_false_positive_rate": 0.1,
            "vertical_pain_column_labeled_episode_fraction": 0.75,
        }
        candidate = run("candidate-parity-column", "candidate", "DM-Deck16][", {
            "completion": True, **qualification_metrics,
        })
        gate_config = config(
            required_metrics=["completion", *qualification_metrics],
            required_runs=[{
                "id": "candidate-deck", "variant": "candidate",
                "map": "DM-Deck16][", "min": 1,
            }],
            aggregate_gates=[],
            per_run_gates=[
                {"id": "parity-complete", "variant": "candidate", "map": "DM-Deck16][",
                 "metric": "falling_parity_realized_episode_completion_fraction",
                 "min": 0.95},
                {"id": "parity-comparable", "variant": "candidate", "map": "DM-Deck16][",
                 "metric": "falling_parity_realized_comparable_step_fraction", "min": 0.8},
                {"id": "parity-no-mismatch", "variant": "candidate", "map": "DM-Deck16][",
                 "metric": "falling_parity_realized_mismatch_fraction", "max": 0.0},
                {"id": "parity-no-unknown", "variant": "candidate", "map": "DM-Deck16][",
                 "metric": "falling_parity_realized_unknown_step_fraction", "max": 0.0},
                {"id": "column-precision", "variant": "candidate", "map": "DM-Deck16][",
                 "metric": "vertical_pain_column_precision", "min": 0.9},
                {"id": "column-recall", "variant": "candidate", "map": "DM-Deck16][",
                 "metric": "vertical_pain_column_recall", "min": 0.8},
                {"id": "column-false-positive-rate", "variant": "candidate",
                 "map": "DM-Deck16][",
                 "metric": "vertical_pain_column_false_positive_rate", "max": 0.1},
                {"id": "column-labeled-support", "variant": "candidate",
                 "map": "DM-Deck16][",
                 "metric": "vertical_pain_column_labeled_episode_fraction", "min": 0.75},
            ],
        )
        self.assertEqual(GATE.evaluate(report(candidate), gate_config)["status"], "passed")

        degraded = run("candidate-parity-column-degraded", "candidate", "DM-Deck16][", {
            "completion": True, **qualification_metrics,
            "vertical_pain_column_precision": 0.5,
        })
        degraded_result = GATE.evaluate(report(degraded), gate_config)
        self.assertEqual(degraded_result["status"], "failed")
        self.assertTrue(any(
            item["evidence"].get("metric") == "vertical_pain_column_precision"
            for item in degraded_result["violations"]))

        missing_metrics = {**qualification_metrics}
        missing_metrics.pop("falling_parity_realized_mismatch_fraction")
        missing = run("candidate-parity-column-missing", "candidate", "DM-Deck16][", {
            "completion": True, **missing_metrics,
        })
        missing_result = GATE.evaluate(report(missing), gate_config)
        self.assertEqual(missing_result["status"], "failed")
        self.assertTrue(any(
            item["kind"] == "required_metric"
            and item["evidence"].get("metric")
            == "falling_parity_realized_mismatch_fraction"
            for item in missing_result["violations"]))

    def test_paired_gate_supports_equality_delta_and_ratio_thresholds(self) -> None:
        equal_report = paired_report((
            "deck-1", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2},
        ))
        equality = paired_gate(maximum_regression=None)
        equality["paired_gates"][0].pop("maximum_regression")
        equality["paired_gates"][0]["exact_equality"] = True
        self.assertEqual(GATE.evaluate(equal_report, equality)["status"], "passed")

        improved_report = paired_report((
            "deck-2", "DM-Deck16][", {"deaths_exact": 10}, {"deaths_exact": 8},
        ))
        ratio = paired_gate(comparison="ratio", minimum_improvement=0.2)
        ratio["paired_gates"][0].pop("maximum_regression")
        result = GATE.evaluate(improved_report, ratio)
        self.assertEqual(result["status"], "passed")
        pair_check = next(item for item in result["checks"] if item["id"].endswith(":deck-2"))
        self.assertAlmostEqual(pair_check["evidence"]["directed_improvement"], 0.2)

    def test_paired_gate_rejects_regression(self) -> None:
        quality_report = paired_report((
            "deck-regression", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 3},
        ))
        result = GATE.evaluate(quality_report, paired_gate())
        self.assertEqual(result["status"], "failed")
        violation = next(
            item for item in result["violations"] if item["id"].endswith(":deck-regression"))
        self.assertIn("regression", violation["reason"])

    def test_paired_gate_enforces_minimum_pair_coverage(self) -> None:
        quality_report = paired_report((
            "deck-only-pair", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2},
        ))
        result = GATE.evaluate(quality_report, paired_gate(min_pairs=2))
        self.assertEqual(result["status"], "failed")
        coverage = next(item for item in result["violations"] if item["id"].endswith(":coverage"))
        self.assertIn("fewer than required 2", coverage["reason"])

    def test_paired_ratio_rejects_zero_baseline(self) -> None:
        quality_report = paired_report((
            "deck-zero", "DM-Deck16][", {"deaths_exact": 0}, {"deaths_exact": 0},
        ))
        gate_config = paired_gate(comparison="ratio")
        result = GATE.evaluate(quality_report, gate_config)
        self.assertEqual(result["status"], "failed")
        violation = next(item for item in result["violations"] if item["id"].endswith(":deck-zero"))
        self.assertIn("strictly positive baseline", violation["reason"])

    def test_paired_gate_rejects_tampered_paths_and_deltas(self) -> None:
        wrong_path = paired_report((
            "deck-path", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2},
        ))
        wrong_path["paired_comparisons"][0]["pairs"][0]["baseline_path"] = "not-the-baseline"
        path_result = GATE.evaluate(wrong_path, paired_gate())
        self.assertEqual(path_result["status"], "failed")
        self.assertIn("baseline_path does not match", " ".join(
            item["reason"] for item in path_result["violations"]))

        wrong_delta = paired_report((
            "deck-delta", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2},
        ))
        wrong_delta["paired_comparisons"][0]["pairs"][0][
            "candidate_minus_baseline"]["deaths_exact"] = 1
        delta_result = GATE.evaluate(wrong_delta, paired_gate())
        self.assertEqual(delta_result["status"], "failed")
        self.assertIn("delta disagrees", " ".join(
            item["reason"] for item in delta_result["violations"]))

    def test_paired_gate_applies_explicit_higher_and_lower_directions(self) -> None:
        higher = paired_report((
            "deck-higher", "DM-Deck16][", {"kills_exact": 2}, {"kills_exact": 3},
        ))
        higher_gate = paired_gate(metric="kills_exact", direction="higher", minimum_improvement=1)
        higher_gate["paired_gates"][0].pop("maximum_regression")
        self.assertEqual(GATE.evaluate(higher, higher_gate)["status"], "passed")

        lower = paired_report((
            "deck-lower", "DM-Deck16][", {"deaths_exact": 3}, {"deaths_exact": 2},
        ))
        lower_gate = paired_gate(minimum_improvement=1)
        lower_gate["paired_gates"][0].pop("maximum_regression")
        self.assertEqual(GATE.evaluate(lower, lower_gate)["status"], "passed")

        higher_regression = paired_report((
            "deck-higher-regression", "DM-Deck16][", {"kills_exact": 2}, {"kills_exact": 1},
        ))
        non_regression = paired_gate(metric="kills_exact", direction="higher")
        self.assertEqual(GATE.evaluate(higher_regression, non_regression)["status"], "failed")

    def test_paired_gate_rejects_invalid_or_multiple_thresholds(self) -> None:
        quality_report = paired_report((
            "deck-1", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2},
        ))
        invalid_gates = [
            paired_gate(maximum_regression=-1),
            paired_gate(maximum_regression=True),
        ]
        false_equality = paired_gate(exact_equality=False)
        false_equality["paired_gates"][0].pop("maximum_regression")
        invalid_gates.append(false_equality)
        minimum_negative = paired_gate(minimum_improvement=-1)
        minimum_negative["paired_gates"][0].pop("maximum_regression")
        invalid_gates.append(minimum_negative)
        minimum_boolean = paired_gate(minimum_improvement=False)
        minimum_boolean["paired_gates"][0].pop("maximum_regression")
        invalid_gates.append(minimum_boolean)
        multiple = paired_gate(minimum_improvement=0)
        invalid_gates.append(multiple)
        for gate_config in invalid_gates:
            with self.subTest(gate_config=gate_config):
                with self.assertRaises(GATE.GateInputError):
                    GATE.evaluate(quality_report, gate_config)

    def test_paired_gate_fails_on_missing_or_unpaired_evidence(self) -> None:
        quality_report = paired_report((
            "deck-missing", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2},
        ))
        quality_report["runs"][1]["metadata"] = None
        quality_report["paired_comparisons"][0]["pairs"] = []
        quality_report["paired_comparisons"][0]["pair_count"] = 0
        result = GATE.evaluate(quality_report, paired_gate())
        self.assertEqual(result["status"], "failed")
        coverage = next(item for item in result["violations"] if item["id"].endswith(":coverage"))
        self.assertIn("no quality pair metadata", coverage["reason"])
        self.assertIn("missing candidate evidence", coverage["reason"])

        null_report = paired_report((
            "deck-null", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": None},
        ))
        null_result = GATE.evaluate(null_report, paired_gate())
        self.assertEqual(null_result["status"], "failed")
        null_violation = next(
            item for item in null_result["violations"] if item["id"].endswith(":deck-null"))
        self.assertIn("null", null_violation["reason"])

    def test_paired_gate_rejects_duplicate_ids_and_unknown_keys(self) -> None:
        quality_report = paired_report((
            "deck-1", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2},
        ))
        duplicate = paired_gate()
        duplicate["required_runs"] = [{"id": "deck-death-non-regression", "min": 1}]
        with self.assertRaisesRegex(GATE.GateInputError, "duplicate gate id"):
            GATE.evaluate(quality_report, duplicate)

        unknown = paired_gate()
        unknown["paired_gates"][0]["preferred_direction"] = "lower"
        with self.assertRaisesRegex(GATE.GateInputError, "unknown keys"):
            GATE.evaluate(quality_report, unknown)

    def test_paired_gate_is_map_scoped_with_mixed_pairs(self) -> None:
        quality_report = paired_report(
            ("deck-pass", "DM-Deck16][", {"deaths_exact": 2}, {"deaths_exact": 2}),
            ("morbias-regression", "DM-Morbias][", {"deaths_exact": 0}, {"deaths_exact": 9}),
        )
        result = GATE.evaluate(quality_report, paired_gate())
        self.assertEqual(result["status"], "passed")
        checked_pairs = {
            item["evidence"].get("pair_id") for item in result["checks"]
            if item["kind"] == "paired_gate" and item["evidence"].get("pair_id")
        }
        self.assertEqual(checked_pairs, {"deck-pass"})

    def test_cli_returns_nonzero_and_writes_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            report_path = root / "report.json"
            config_path = root / "gates.json"
            output_path = root / "result.json"
            report_path.write_text(json.dumps(report(self.good)), encoding="utf-8")
            config_path.write_text(json.dumps(config(per_run_gates=[{
                "id": "candidate-zero-deaths", "variant": "candidate",
                "metric": "deaths_exact", "max": 0,
            }])), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(TOOL_PATH), str(report_path), str(config_path),
                 "--output", str(output_path)], capture_output=True, text=True, check=False)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(json.loads(output_path.read_text(encoding="utf-8"))["status"], "failed")
            self.assertIn("violations", completed.stderr)


if __name__ == "__main__":
    unittest.main()
