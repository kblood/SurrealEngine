#!/usr/bin/env python3
"""Evaluate explicit release gates against a bot quality analyzer report."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


REPORT_SCHEMA = "surreal-bot-quality-analysis-v1"
CONFIG_SCHEMA = "surreal-bot-quality-gates-v1"
RESULT_SCHEMA = "surreal-bot-quality-gate-result-v1"
TOOL_VERSION = 3
AGGREGATE_STATISTICS = {"count", "mean", "median", "minimum", "maximum"}
CONFIG_KEYS = {
    "schema", "validity_only", "required_runs", "required_metrics",
    "aggregate_gates", "per_run_gates", "paired_gates",
}
REQUIRED_RUN_KEYS = {"id", "variant", "map", "min"}
AGGREGATE_GATE_KEYS = {"id", "variant", "metric", "statistic", "min", "max", "equals"}
PER_RUN_GATE_KEYS = {"id", "variant", "map", "metric", "min", "max", "equals"}
PAIRED_GATE_KEYS = {
    "id", "baseline_variant", "candidate_variant", "map", "metric", "comparison",
    "direction", "min_pairs", "exact_equality", "maximum_regression", "minimum_improvement",
}
PAIRED_COMPARISONS = {"delta", "ratio"}
PAIRED_DIRECTIONS = {"higher", "lower"}
PAIRED_THRESHOLDS = {"exact_equality", "maximum_regression", "minimum_improvement"}


class GateInputError(ValueError):
    pass


def _load_json(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GateInputError(f"cannot read {path}: {error}") from error
    if not isinstance(document, dict):
        raise GateInputError(f"{path} must contain a JSON object")
    return document


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _reject_unknown_keys(document: dict[str, Any], accepted: set[str], context: str) -> None:
    unknown = sorted(set(document) - accepted)
    if unknown:
        raise GateInputError(f"{context} contains unknown keys: {', '.join(unknown)}")


def _entry_id(document: dict[str, Any], context: str) -> str:
    value = document.get("id")
    if not isinstance(value, str) or not value.strip():
        raise GateInputError(f"{context} must define a non-empty string id")
    return value


def _selector(document: dict[str, Any], *, allow_map: bool) -> dict[str, str]:
    result: dict[str, str] = {}
    for key in ("variant", "map"):
        if key not in document:
            continue
        if key == "map" and not allow_map:
            raise GateInputError("aggregate selectors cannot be map-scoped")
        value = document[key]
        if not isinstance(value, str) or not value:
            raise GateInputError(f"selector {key} must be a non-empty string")
        result[key] = value
    return result


def _run_map(run: dict[str, Any]) -> Any:
    config = run.get("config")
    if isinstance(config, dict) and config.get("map") is not None:
        return config.get("map")
    result = run.get("result")
    return result.get("map") if isinstance(result, dict) else None


def _matches_run(run: dict[str, Any], selector: dict[str, str]) -> bool:
    return ("variant" not in selector or run.get("variant") == selector["variant"]) and (
        "map" not in selector or _run_map(run) == selector["map"])


def _matches_aggregate(aggregate: dict[str, Any], selector: dict[str, str]) -> bool:
    return "variant" not in selector or aggregate.get("variant") == selector["variant"]


def _thresholds(gate: dict[str, Any]) -> dict[str, Any]:
    thresholds = {key: gate[key] for key in ("min", "max", "equals") if key in gate}
    if not thresholds:
        raise GateInputError("each gate must define min, max, or equals")
    for key in ("min", "max"):
        if key in thresholds and not _is_number(thresholds[key]):
            raise GateInputError(f"gate {key} must be a finite number")
    if ("equals" in thresholds and isinstance(thresholds["equals"], (int, float))
            and not isinstance(thresholds["equals"], bool) and not _is_number(thresholds["equals"])):
        raise GateInputError("gate equals must be finite when numeric")
    if "min" in thresholds and "max" in thresholds and thresholds["min"] > thresholds["max"]:
        raise GateInputError("gate min cannot exceed max")
    if "equals" in thresholds and ("min" in thresholds or "max" in thresholds):
        expected = thresholds["equals"]
        if not _is_number(expected):
            raise GateInputError("gate equals must be a finite number when combined with min or max")
        if "min" in thresholds and expected < thresholds["min"]:
            raise GateInputError("gate equals is below min")
        if "max" in thresholds and expected > thresholds["max"]:
            raise GateInputError("gate equals is above max")
    return thresholds


def _paired_threshold(gate: dict[str, Any]) -> tuple[str, float | bool]:
    present = [key for key in PAIRED_THRESHOLDS if key in gate]
    if len(present) != 1:
        raise GateInputError(
            "each paired gate must define exactly one of exact_equality, "
            "maximum_regression, or minimum_improvement")
    name = present[0]
    value = gate[name]
    if name == "exact_equality":
        if value is not True:
            raise GateInputError("paired gate exact_equality must be true")
    elif not _is_number(value) or value < 0:
        raise GateInputError(f"paired gate {name} must be a finite non-negative number")
    return name, value


def _paired_string(gate: dict[str, Any], key: str) -> str:
    value = gate.get(key)
    if not isinstance(value, str) or not value:
        raise GateInputError(f"paired gates require a non-empty {key}")
    return value


def _validate_config(config: dict[str, Any]) -> None:
    _reject_unknown_keys(config, CONFIG_KEYS, "gate config")

    validity_only = config.get("validity_only", False)
    if not isinstance(validity_only, bool):
        raise GateInputError("validity_only must be a boolean")

    required_runs = config.get("required_runs", [])
    required_metrics = config.get("required_metrics", [])
    aggregate_gates = config.get("aggregate_gates", [])
    per_run_gates = config.get("per_run_gates", [])
    paired_gates = config.get("paired_gates", [])
    for name, value in (
        ("required_runs", required_runs),
        ("required_metrics", required_metrics),
        ("aggregate_gates", aggregate_gates),
        ("per_run_gates", per_run_gates),
        ("paired_gates", paired_gates),
    ):
        if not isinstance(value, list):
            raise GateInputError(f"{name} must be an array")

    substantive_count = sum(map(len, (
        required_runs, required_metrics, aggregate_gates, per_run_gates, paired_gates,
    )))
    if validity_only:
        if substantive_count:
            raise GateInputError("validity_only cannot be combined with substantive gates")
    elif not substantive_count:
        raise GateInputError("gate config must define at least one substantive gate or validity_only true")

    if any(not isinstance(item, str) or not item for item in required_metrics):
        raise GateInputError("required_metrics must be an array of non-empty metric names")

    ids: set[str] = set()

    def register_id(value: str) -> None:
        if value in ids:
            raise GateInputError(f"duplicate gate id {value!r}")
        ids.add(value)

    for index, requirement in enumerate(required_runs):
        if not isinstance(requirement, dict):
            raise GateInputError("required run entries must be objects")
        context = f"required_runs[{index}]"
        _reject_unknown_keys(requirement, REQUIRED_RUN_KEYS, context)
        register_id(_entry_id(requirement, context))
        _selector(requirement, allow_map=True)
        minimum = requirement.get("min")
        if not isinstance(minimum, int) or isinstance(minimum, bool) or minimum <= 0:
            raise GateInputError("required run min must be a positive integer")

    for index, gate in enumerate(aggregate_gates):
        if not isinstance(gate, dict):
            raise GateInputError("aggregate gates must be objects")
        context = f"aggregate_gates[{index}]"
        _reject_unknown_keys(gate, AGGREGATE_GATE_KEYS, context)
        register_id(_entry_id(gate, context))
        _selector(gate, allow_map=False)
        metric = gate.get("metric")
        statistic = gate.get("statistic", "mean")
        if (not isinstance(metric, str) or not metric or not isinstance(statistic, str)
                or statistic not in AGGREGATE_STATISTICS):
            raise GateInputError("aggregate gates require a metric and valid statistic")
        _thresholds(gate)

    for index, gate in enumerate(per_run_gates):
        if not isinstance(gate, dict):
            raise GateInputError("per-run gates must be objects")
        context = f"per_run_gates[{index}]"
        _reject_unknown_keys(gate, PER_RUN_GATE_KEYS, context)
        register_id(_entry_id(gate, context))
        _selector(gate, allow_map=True)
        metric = gate.get("metric")
        if not isinstance(metric, str) or not metric:
            raise GateInputError("per-run gates require a metric")
        _thresholds(gate)

    for index, gate in enumerate(paired_gates):
        if not isinstance(gate, dict):
            raise GateInputError("paired gates must be objects")
        context = f"paired_gates[{index}]"
        _reject_unknown_keys(gate, PAIRED_GATE_KEYS, context)
        register_id(_entry_id(gate, context))
        baseline_variant = _paired_string(gate, "baseline_variant")
        candidate_variant = _paired_string(gate, "candidate_variant")
        if baseline_variant == candidate_variant:
            raise GateInputError("paired gate baseline_variant and candidate_variant must differ")
        _paired_string(gate, "map")
        _paired_string(gate, "metric")
        if gate.get("comparison") not in PAIRED_COMPARISONS:
            raise GateInputError("paired gate comparison must be delta or ratio")
        if gate.get("direction") not in PAIRED_DIRECTIONS:
            raise GateInputError("paired gate direction must be higher or lower")
        minimum = gate.get("min_pairs", 1)
        if not isinstance(minimum, int) or isinstance(minimum, bool) or minimum <= 0:
            raise GateInputError("paired gate min_pairs must be a positive integer")
        _paired_threshold(gate)


def _equal(actual: Any, expected: Any) -> bool:
    if isinstance(actual, bool) or isinstance(expected, bool):
        return type(actual) is type(expected) and actual == expected
    if _is_number(actual) and _is_number(expected):
        return actual == expected
    return type(actual) is type(expected) and actual == expected


def _threshold_failures(value: Any, thresholds: dict[str, Any]) -> list[str]:
    if value is None:
        return ["metric is null/unavailable"]
    failures: list[str] = []
    if "equals" in thresholds and not _equal(value, thresholds["equals"]):
        failures.append(f"expected equality with {thresholds['equals']!r}, observed {value!r}")
    for key, operator in (("min", "at least"), ("max", "at most")):
        if key not in thresholds:
            continue
        if not _is_number(value):
            failures.append(f"{key} requires a finite numeric metric, observed {value!r}")
        elif key == "min" and value < thresholds[key]:
            failures.append(f"expected {operator} {thresholds[key]!r}, observed {value!r}")
        elif key == "max" and value > thresholds[key]:
            failures.append(f"expected {operator} {thresholds[key]!r}, observed {value!r}")
    return failures


def _numeric_metric(run: dict[str, Any], metric: str) -> float | None:
    metrics = run.get("metrics")
    if not isinstance(metrics, dict) or metric not in metrics:
        return None
    value = metrics[metric]
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    return float(value) if _is_number(value) else None


def _paired_change(
        baseline: float, candidate: float, comparison: str, direction: str,
) -> tuple[float | None, float | None, str | None]:
    delta = candidate - baseline
    if comparison == "delta":
        compared = delta
        improvement = delta if direction == "higher" else -delta
        return compared, improvement, None
    if baseline <= 0:
        return None, None, "ratio comparison requires a strictly positive baseline value"
    ratio = candidate / baseline
    improvement = ratio - 1.0 if direction == "higher" else 1.0 - ratio
    return ratio, improvement, None


def _paired_threshold_failures(
        baseline: float, candidate: float, improvement: float,
        threshold: tuple[str, float | bool],
) -> list[str]:
    name, limit = threshold
    if name == "exact_equality":
        return [] if candidate == baseline else [
            f"expected exact candidate/baseline equality, observed {candidate!r} versus {baseline!r}"]
    assert isinstance(limit, (int, float)) and not isinstance(limit, bool)
    if (name == "maximum_regression" and improvement < -limit
            and not math.isclose(improvement, -limit, rel_tol=1e-12, abs_tol=1e-12)):
        return [f"regression {-improvement!r} exceeds maximum {limit!r}"]
    if (name == "minimum_improvement" and improvement < limit
            and not math.isclose(improvement, limit, rel_tol=1e-12, abs_tol=1e-12)):
        return [f"improvement {improvement!r} is below minimum {limit!r}"]
    return []


def evaluate(report: dict[str, Any], config: dict[str, Any]) -> dict[str, Any]:
    if report.get("schema") != REPORT_SCHEMA:
        raise GateInputError(f"quality report schema must be {REPORT_SCHEMA!r}")
    if config.get("schema") != CONFIG_SCHEMA:
        raise GateInputError(f"gate config schema must be {CONFIG_SCHEMA!r}")
    _validate_config(config)

    runs = report.get("runs")
    aggregates = report.get("variant_aggregates")
    if not isinstance(runs, list) or not isinstance(aggregates, list):
        raise GateInputError("quality report runs and variant_aggregates must be arrays")
    availability = report.get("metric_availability")
    available = availability.get("available") if isinstance(availability, dict) else None
    if not isinstance(available, list) or any(not isinstance(item, str) for item in available):
        raise GateInputError("quality report metric_availability.available must be an array of names")
    available_metrics = set(available)
    checks: list[dict[str, Any]] = []
    violations: list[dict[str, Any]] = []

    def add_check(kind: str, check_id: str, passed: bool, evidence: dict[str, Any], reason: str = "") -> None:
        check = {"kind": kind, "id": check_id, "passed": passed, "evidence": evidence}
        if reason:
            check["reason"] = reason
        checks.append(check)
        if not passed:
            violations.append({"kind": kind, "id": check_id, "reason": reason, "evidence": evidence})

    add_check("required_runs", "report-has-runs", bool(runs), {"observed_runs": len(runs)},
              "quality report contains no runs" if not runs else "")

    for index, run in enumerate(runs):
        if not isinstance(run, dict):
            add_check("run_validity", f"run-{index}", False, {"index": index}, "run is not an object")
            continue
        result = run.get("result")
        validation = run.get("validation")
        valid = (isinstance(result, dict) and result.get("status") == "complete"
                 and result.get("exit_code") == 0 and isinstance(validation, dict)
                 and validation.get("status") == "passed")
        evidence = {
            "path": run.get("path"), "variant": run.get("variant"), "map": _run_map(run),
            "status": result.get("status") if isinstance(result, dict) else None,
            "exit_code": result.get("exit_code") if isinstance(result, dict) else None,
            "validation_status": validation.get("status") if isinstance(validation, dict) else None,
        }
        add_check("run_validity", f"run-{index}", valid, evidence,
                  "run is incomplete, unsuccessful, or structurally invalid" if not valid else "")

    required_runs = config.get("required_runs", [])
    if not isinstance(required_runs, list):
        raise GateInputError("required_runs must be an array")
    for index, requirement in enumerate(required_runs):
        if not isinstance(requirement, dict):
            raise GateInputError("required run entries must be objects")
        selector = _selector(requirement, allow_map=True)
        minimum = requirement.get("min")
        if not isinstance(minimum, int) or isinstance(minimum, bool) or minimum <= 0:
            raise GateInputError("required run min must be a positive integer")
        matched = [run for run in runs if isinstance(run, dict) and _matches_run(run, selector)]
        check_id = requirement["id"]
        passed = len(matched) >= minimum
        add_check("required_runs", check_id, passed,
                  {"selector": selector, "required_minimum": minimum, "observed_runs": len(matched)},
                  "required run count was not met" if not passed else "")

    required_metrics = config.get("required_metrics", [])
    if not isinstance(required_metrics, list) or any(not isinstance(item, str) or not item for item in required_metrics):
        raise GateInputError("required_metrics must be an array of non-empty metric names")
    for metric in required_metrics:
        for index, run in enumerate(runs):
            metrics = run.get("metrics") if isinstance(run, dict) else None
            declared = metric in available_metrics
            present = declared and isinstance(metrics, dict) and metric in metrics and metrics[metric] is not None
            add_check("required_metric", f"{metric}:run-{index}", present,
                      {"metric": metric, "path": run.get("path") if isinstance(run, dict) else None,
                       "declared_available": declared,
                       "value": metrics.get(metric) if isinstance(metrics, dict) else None},
                      f"required metric {metric!r} is missing or unavailable" if not present else "")

    aggregate_gates = config.get("aggregate_gates", [])
    if not isinstance(aggregate_gates, list):
        raise GateInputError("aggregate_gates must be an array")
    for index, gate in enumerate(aggregate_gates):
        if not isinstance(gate, dict):
            raise GateInputError("aggregate gates must be objects")
        selector = _selector(gate, allow_map=False)
        metric = gate.get("metric")
        statistic = gate.get("statistic", "mean")
        if (not isinstance(metric, str) or not metric or not isinstance(statistic, str)
                or statistic not in AGGREGATE_STATISTICS):
            raise GateInputError("aggregate gates require a metric and valid statistic")
        thresholds = _thresholds(gate)
        matched = [item for item in aggregates if isinstance(item, dict) and _matches_aggregate(item, selector)]
        gate_id = gate["id"]
        if metric not in available_metrics:
            add_check("aggregate_gate", gate_id, False,
                      {"selector": selector, "metric": metric, "declared_available": False},
                      f"metric {metric!r} is not declared available by the analyzer")
            continue
        if not matched:
            add_check("aggregate_gate", gate_id, False, {"selector": selector, "metric": metric},
                      "aggregate selector matched no variants")
            continue
        for aggregate in matched:
            summary = aggregate.get("metrics", {}).get(metric) if isinstance(aggregate.get("metrics"), dict) else None
            value = summary.get(statistic) if isinstance(summary, dict) else None
            failures = _threshold_failures(value, thresholds)
            evidence = {"selector": selector, "variant": aggregate.get("variant"), "metric": metric,
                        "statistic": statistic, "value": value, "thresholds": thresholds}
            add_check("aggregate_gate", f"{gate_id}:{aggregate.get('variant')}", not failures, evidence,
                      "; ".join(failures))

    per_run_gates = config.get("per_run_gates", [])
    if not isinstance(per_run_gates, list):
        raise GateInputError("per_run_gates must be an array")
    for index, gate in enumerate(per_run_gates):
        if not isinstance(gate, dict):
            raise GateInputError("per-run gates must be objects")
        selector = _selector(gate, allow_map=True)
        metric = gate.get("metric")
        if not isinstance(metric, str) or not metric:
            raise GateInputError("per-run gates require a metric")
        thresholds = _thresholds(gate)
        matched = [(run_index, run) for run_index, run in enumerate(runs)
                   if isinstance(run, dict) and _matches_run(run, selector)]
        gate_id = gate["id"]
        if metric not in available_metrics:
            add_check("per_run_gate", gate_id, False,
                      {"selector": selector, "metric": metric, "declared_available": False},
                      f"metric {metric!r} is not declared available by the analyzer")
            continue
        if not matched:
            add_check("per_run_gate", gate_id, False, {"selector": selector, "metric": metric},
                      "per-run selector matched no runs")
            continue
        for run_index, run in matched:
            metrics = run.get("metrics")
            value = metrics.get(metric) if isinstance(metrics, dict) else None
            failures = ([f"metric {metric!r} is missing"] if not isinstance(metrics, dict) or metric not in metrics
                        else _threshold_failures(value, thresholds))
            evidence = {"selector": selector, "path": run.get("path"), "variant": run.get("variant"),
                        "map": _run_map(run), "metric": metric, "value": value, "thresholds": thresholds}
            add_check("per_run_gate", f"{gate_id}:run-{run_index}", not failures, evidence,
                      "; ".join(failures))

    paired_gates = config.get("paired_gates", [])
    if not isinstance(paired_gates, list):
        raise GateInputError("paired_gates must be an array")
    paired_comparisons = report.get("paired_comparisons")
    if paired_gates and not isinstance(paired_comparisons, list):
        raise GateInputError("quality report paired_comparisons must be an array")
    for gate in paired_gates:
        baseline_variant = gate["baseline_variant"]
        candidate_variant = gate["candidate_variant"]
        map_name = gate["map"]
        metric = gate["metric"]
        comparison_mode = gate["comparison"]
        direction = gate["direction"]
        minimum_pairs = gate.get("min_pairs", 1)
        threshold = _paired_threshold(gate)
        gate_id = gate["id"]
        selector = {
            "baseline_variant": baseline_variant,
            "candidate_variant": candidate_variant,
            "map": map_name,
        }
        issues: list[str] = []

        if metric not in available_metrics:
            issues.append(f"metric {metric!r} is not declared available by the analyzer")

        selected_runs = [
            run for run in runs if isinstance(run, dict)
            and run.get("variant") in {baseline_variant, candidate_variant}
            and _run_map(run) == map_name
        ]
        if not selected_runs:
            issues.append("paired selector matched no baseline or candidate runs")

        roles_by_pair: dict[str, dict[str, dict[str, Any]]] = {}
        for run in selected_runs:
            metadata = run.get("metadata")
            path = run.get("path")
            expected_role = "baseline" if run.get("variant") == baseline_variant else "candidate"
            if not isinstance(metadata, dict):
                issues.append(f"run {path!r} has no quality pair metadata")
                continue
            pair_id = metadata.get("pair_id")
            role = metadata.get("comparison_role")
            if metadata.get("variant") != run.get("variant"):
                issues.append(f"run {path!r} metadata variant does not match the analyzed variant")
                continue
            if not isinstance(pair_id, str) or not pair_id:
                issues.append(f"run {path!r} has no non-empty pair_id")
                continue
            if role != expected_role:
                issues.append(
                    f"run {path!r} comparison_role {role!r} does not match {expected_role!r}")
                continue
            pair_roles = roles_by_pair.setdefault(pair_id, {})
            if role in pair_roles:
                issues.append(f"pair {pair_id!r} has duplicate {role} runs on {map_name!r}")
                continue
            pair_roles[role] = run

        complete_pairs: dict[str, dict[str, dict[str, Any]]] = {}
        for pair_id, pair_roles in sorted(roles_by_pair.items()):
            missing_roles = sorted({"baseline", "candidate"} - set(pair_roles))
            if missing_roles:
                issues.append(
                    f"pair {pair_id!r} is missing {', '.join(missing_roles)} evidence on {map_name!r}")
            else:
                complete_pairs[pair_id] = pair_roles

        comparison_matches = [
            item for item in paired_comparisons or [] if isinstance(item, dict)
            and item.get("baseline_variant") == baseline_variant
            and item.get("candidate_variant") == candidate_variant
        ]
        comparison_object = comparison_matches[0] if len(comparison_matches) == 1 else None
        if not comparison_matches:
            issues.append("analyzer report has no paired comparison for the selected variants")
        elif len(comparison_matches) > 1:
            issues.append("analyzer report has duplicate paired comparisons for the selected variants")

        reported_pairs: dict[str, dict[str, Any]] = {}
        if comparison_object is not None:
            pair_items = comparison_object.get("pairs")
            if not isinstance(pair_items, list):
                issues.append("paired comparison pairs is not an array")
            else:
                for pair in pair_items:
                    if not isinstance(pair, dict):
                        issues.append("paired comparison contains a non-object pair")
                        continue
                    pair_id = pair.get("pair_id")
                    if not isinstance(pair_id, str) or not pair_id:
                        issues.append("paired comparison contains a pair without a non-empty pair_id")
                    elif pair_id in reported_pairs:
                        issues.append(f"paired comparison contains duplicate pair {pair_id!r}")
                    else:
                        reported_pairs[pair_id] = pair
                reported_count = comparison_object.get("pair_count")
                if (not isinstance(reported_count, int) or isinstance(reported_count, bool)
                        or reported_count != len(pair_items)):
                    issues.append("paired comparison pair_count does not match its pair array")
            metric_summaries = comparison_object.get("metrics")
            metric_summary = (metric_summaries.get(metric)
                              if isinstance(metric_summaries, dict) else None)
            if not isinstance(metric_summary, dict):
                issues.append(f"paired comparison has no summary for metric {metric!r}")
            else:
                reported_direction = metric_summary.get("preferred_direction")
                if reported_direction not in (None, direction):
                    issues.append(
                        f"configured direction {direction!r} conflicts with analyzer direction "
                        f"{reported_direction!r}")

        observed_pairs = len(complete_pairs)
        if observed_pairs < minimum_pairs:
            issues.append(
                f"observed {observed_pairs} complete pairs, fewer than required {minimum_pairs}")
        for pair_id in complete_pairs:
            if pair_id not in reported_pairs:
                issues.append(f"complete pair {pair_id!r} is absent from paired_comparisons")

        coverage_evidence = {
            "selector": selector,
            "metric": metric,
            "comparison": comparison_mode,
            "direction": direction,
            "threshold": {threshold[0]: threshold[1]},
            "required_pairs": minimum_pairs,
            "observed_complete_pairs": observed_pairs,
            "selected_runs": len(selected_runs),
            "pair_ids": sorted(complete_pairs),
        }
        add_check("paired_gate", f"{gate_id}:coverage", not issues, coverage_evidence,
                  "; ".join(issues))

        for pair_id, pair_roles in sorted(complete_pairs.items()):
            baseline_run = pair_roles["baseline"]
            candidate_run = pair_roles["candidate"]
            reported_pair = reported_pairs.get(pair_id)
            failures: list[str] = []
            baseline_value = _numeric_metric(baseline_run, metric)
            candidate_value = _numeric_metric(candidate_run, metric)
            reported_delta: Any = None
            if baseline_value is None:
                failures.append("baseline metric is missing, null, or non-numeric")
            if candidate_value is None:
                failures.append("candidate metric is missing, null, or non-numeric")
            if reported_pair is None:
                failures.append("pair is absent from paired_comparisons")
            else:
                if reported_pair.get("baseline_path") != baseline_run.get("path"):
                    failures.append("paired comparison baseline_path does not match quality metadata")
                if reported_pair.get("candidate_path") != candidate_run.get("path"):
                    failures.append("paired comparison candidate_path does not match quality metadata")
                deltas = reported_pair.get("candidate_minus_baseline")
                reported_delta = deltas.get(metric) if isinstance(deltas, dict) else None
                if not _is_number(reported_delta):
                    failures.append("paired comparison delta is missing, null, or non-numeric")
                elif (baseline_value is not None and candidate_value is not None
                      and reported_delta != candidate_value - baseline_value):
                    failures.append("paired comparison delta disagrees with the paired run metrics")

            compared_value = improvement = None
            if baseline_value is not None and candidate_value is not None:
                compared_value, improvement, comparison_error = _paired_change(
                    baseline_value, candidate_value, comparison_mode, direction)
                if comparison_error:
                    failures.append(comparison_error)
                elif improvement is not None:
                    failures.extend(_paired_threshold_failures(
                        baseline_value, candidate_value, improvement, threshold))
            evidence = {
                "selector": selector,
                "pair_id": pair_id,
                "baseline_path": baseline_run.get("path"),
                "candidate_path": candidate_run.get("path"),
                "metric": metric,
                "baseline_value": baseline_value,
                "candidate_value": candidate_value,
                "reported_candidate_minus_baseline": reported_delta,
                "comparison": comparison_mode,
                "compared_value": compared_value,
                "direction": direction,
                "directed_improvement": improvement,
                "threshold": {threshold[0]: threshold[1]},
            }
            add_check("paired_gate", f"{gate_id}:{pair_id}", not failures, evidence,
                      "; ".join(failures))

    return {
        "schema": RESULT_SCHEMA,
        "tool": {"name": "Evaluate-BotQualityGate.py", "version": TOOL_VERSION},
        "status": "passed" if not violations else "failed",
        "summary": {"runs": len(runs), "checks": len(checks), "violations": len(violations)},
        "checks": checks,
        "violations": violations,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="Analyze-BotQuality.py JSON report")
    parser.add_argument("config", type=Path, help=f"{CONFIG_SCHEMA} JSON configuration")
    parser.add_argument("--output", type=Path, help="optional gate-result JSON path")
    args = parser.parse_args(argv)
    try:
        result = evaluate(_load_json(args.report), _load_json(args.config))
    except GateInputError as error:
        print(f"bot quality gate input error: {error}", file=sys.stderr)
        return 2
    serialized = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        try:
            args.output.write_text(serialized, encoding="utf-8")
        except OSError as error:
            print(f"cannot write {args.output}: {error}", file=sys.stderr)
            return 2
    else:
        sys.stdout.write(serialized)
    print(f"bot quality gate {result['status']}: {result['summary']['checks']} checks, "
          f"{result['summary']['violations']} violations", file=sys.stderr)
    for violation in result["violations"]:
        print(f"  {violation['id']}: {violation['reason']}", file=sys.stderr)
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
