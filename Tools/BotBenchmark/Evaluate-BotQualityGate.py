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
TOOL_VERSION = 2
AGGREGATE_STATISTICS = {"count", "mean", "median", "minimum", "maximum"}
CONFIG_KEYS = {
    "schema", "validity_only", "required_runs", "required_metrics",
    "aggregate_gates", "per_run_gates",
}
REQUIRED_RUN_KEYS = {"id", "variant", "map", "min"}
AGGREGATE_GATE_KEYS = {"id", "variant", "metric", "statistic", "min", "max", "equals"}
PER_RUN_GATE_KEYS = {"id", "variant", "map", "metric", "min", "max", "equals"}


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


def _validate_config(config: dict[str, Any]) -> None:
    _reject_unknown_keys(config, CONFIG_KEYS, "gate config")

    validity_only = config.get("validity_only", False)
    if not isinstance(validity_only, bool):
        raise GateInputError("validity_only must be a boolean")

    required_runs = config.get("required_runs", [])
    required_metrics = config.get("required_metrics", [])
    aggregate_gates = config.get("aggregate_gates", [])
    per_run_gates = config.get("per_run_gates", [])
    for name, value in (
        ("required_runs", required_runs),
        ("required_metrics", required_metrics),
        ("aggregate_gates", aggregate_gates),
        ("per_run_gates", per_run_gates),
    ):
        if not isinstance(value, list):
            raise GateInputError(f"{name} must be an array")

    substantive_count = sum(map(len, (
        required_runs, required_metrics, aggregate_gates, per_run_gates,
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
