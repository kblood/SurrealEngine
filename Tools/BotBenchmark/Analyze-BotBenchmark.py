#!/usr/bin/env python3
"""Aggregate and compare deterministic Surreal bot benchmark matrices."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any


METRICS: dict[str, str] = {
    "first_nonstarter_weapon_tick_min": "lower",
    "maximum_weapon_count_max": "higher",
    "maximum_useful_ammo_max": "higher",
    "health_gained_total": "higher",
    "damage_dealt_exact_total": "higher",
    "damage_taken_exact_total": "lower",
    "self_damage_exact_total": "lower",
    "fatal_damage_kills_exact_total": "higher",
    "fatal_damage_deaths_exact_total": "lower",
    "hitscan_accuracy": "higher",
    "projectile_finalized_accuracy": "higher",
    "damage_taken_snapshot_proxy_total": "lower",
    "final_score_total": "higher",
    "pri_deaths_total": "lower",
    "no_progress_seconds_proxy_total": "lower",
    "stuck_events_proxy_total": "lower",
    "candidate_score_margin": "higher",
    "candidate_death_advantage": "higher",
    "candidate_damage_dealt_exact": "higher",
    "opponent_damage_dealt_exact": "lower",
    "wall_seconds": "lower",
}


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * quantile
    low = math.floor(position)
    high = math.ceil(position)
    if low == high:
        return ordered[low]
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def numeric(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def describe(values: list[float]) -> dict[str, float | int | None]:
    return {
        "count": len(values),
        "mean": statistics.fmean(values) if values else None,
        "median": statistics.median(values) if values else None,
        "p10": percentile(values, 0.10),
        "p90": percentile(values, 0.90),
        "minimum": min(values) if values else None,
        "maximum": max(values) if values else None,
    }


def parse_input(spec: str) -> tuple[str, Path]:
    if "=" in spec:
        label, raw_path = spec.split("=", 1)
    else:
        raw_path = spec
        label = Path(raw_path).resolve().parent.name
    if not label.strip():
        raise ValueError(f"Input label is empty: {spec}")
    path = Path(raw_path).resolve()
    if path.is_dir():
        path = path / "matrix-results.json"
    if not path.is_file():
        raise FileNotFoundError(path)
    return label.strip(), path


def load_matrix(label: str, path: Path) -> list[dict[str, Any]]:
    root = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(root, dict) or not isinstance(root.get("runs"), list):
        raise ValueError(f"{path} is not a matrix-results document")
    rows: list[dict[str, Any]] = []
    for source in root["runs"]:
        if not source.get("valid", False):
            continue
        row = dict(source)
        row["source"] = label
        row["source_path"] = str(path)
        rows.append(row)
    return rows


def summarize(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str, int], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        groups[(str(row["source"]), str(row["map"]), int(row["skill"]))].append(row)

    result: list[dict[str, Any]] = []
    for (source, map_name, skill), group in sorted(groups.items()):
        item: dict[str, Any] = {
            "source": source,
            "map": map_name,
            "skill": skill,
            "runs": len(group),
            "unique_seeds": len({str(row["seed"]) for row in group}),
        }
        for metric in METRICS:
            values = [value for row in group if (value := numeric(row.get(metric))) is not None]
            item[metric] = describe(values)
        result.append(item)
    return result


def monotonic_checks(groups: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_series: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for group in groups:
        by_series[(group["source"], group["map"])].append(group)

    checks: list[dict[str, Any]] = []
    for (source, map_name), series in sorted(by_series.items()):
        ordered = sorted(series, key=lambda item: item["skill"])
        for metric, direction in METRICS.items():
            comparisons = []
            for left, right in zip(ordered, ordered[1:]):
                left_value = left[metric]["median"]
                right_value = right[metric]["median"]
                if left_value is None or right_value is None:
                    passed = None
                elif direction == "higher":
                    passed = right_value >= left_value
                else:
                    passed = right_value <= left_value
                comparisons.append({
                    "from_skill": left["skill"],
                    "to_skill": right["skill"],
                    "from_median": left_value,
                    "to_median": right_value,
                    "passed": passed,
                })
            available = [entry["passed"] for entry in comparisons if entry["passed"] is not None]
            checks.append({
                "source": source,
                "map": map_name,
                "metric": metric,
                "preferred_direction": direction,
                "all_adjacent_medians_monotonic": all(available) if available else None,
                "comparisons": comparisons,
            })
    return checks


def paired_comparisons(rows: list[dict[str, Any]], labels: list[str]) -> list[dict[str, Any]]:
    if len(labels) != 2:
        return []
    indexed: dict[str, dict[tuple[str, int, str, int], dict[str, Any]]] = defaultdict(dict)
    for row in rows:
        key = (str(row["map"]), int(row["skill"]), str(row["seed"]), int(row["repetition"]))
        indexed[str(row["source"])][key] = row

    left_label, right_label = labels
    keys = sorted(set(indexed[left_label]) & set(indexed[right_label]))
    result = []
    for metric, direction in METRICS.items():
        deltas: list[float] = []
        wins = ties = losses = 0
        for key in keys:
            left = numeric(indexed[left_label][key].get(metric))
            right = numeric(indexed[right_label][key].get(metric))
            if left is None or right is None:
                continue
            delta = right - left
            deltas.append(delta)
            preferred = delta if direction == "higher" else -delta
            if preferred > 0:
                wins += 1
            elif preferred < 0:
                losses += 1
            else:
                ties += 1
        result.append({
            "baseline": left_label,
            "candidate": right_label,
            "metric": metric,
            "preferred_direction": direction,
            "pairs": len(deltas),
            "candidate_wins": wins,
            "ties": ties,
            "candidate_losses": losses,
            "raw_delta_candidate_minus_baseline": describe(deltas),
        })
    return result


def write_group_csv(path: Path, groups: list[dict[str, Any]]) -> None:
    fields = ["source", "map", "skill", "runs", "unique_seeds"]
    for metric in METRICS:
        fields.extend(f"{metric}_{stat}" for stat in ("count", "mean", "median", "p10", "p90", "minimum", "maximum"))
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for group in groups:
            row = {field: group.get(field) for field in fields[:5]}
            for metric in METRICS:
                for stat, value in group[metric].items():
                    row[f"{metric}_{stat}"] = value
            writer.writerow(row)


def main() -> int:
    parser = argparse.ArgumentParser(description="Aggregate one matrix or compare two paired matrices.")
    parser.add_argument("inputs", nargs="+", help="[LABEL=]matrix-results.json or its containing directory")
    parser.add_argument("--output", required=True, help="New output directory (must not already exist)")
    args = parser.parse_args()

    inputs = [parse_input(spec) for spec in args.inputs]
    labels = [label for label, _ in inputs]
    if len(set(labels)) != len(labels):
        raise ValueError("Input labels must be unique")
    if len(inputs) > 2:
        raise ValueError("At most two matrices can be compared in one invocation")

    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    rows = [row for label, path in inputs for row in load_matrix(label, path)]
    groups = summarize(rows)
    report = {
        "schema": 1,
        "inputs": [{"label": label, "path": str(path)} for label, path in inputs],
        "valid_runs": len(rows),
        "metric_directions": METRICS,
        "groups": groups,
        "adjacent_skill_median_checks": monotonic_checks(groups),
        "paired_comparisons": paired_comparisons(rows, labels),
        "interpretation_warning": (
            "Damage, fatal damage, hitscan, projectile, and firing-intent fields are exact at recorded script/native boundaries; "
            "fields ending _proxy are fixed-tick deltas. Equal-skill self-play is not a skill-separation test; use mixed-skill "
            "or fixed-opponent cases before claiming monotonic combat skill."
        ),
    }
    (output / "analysis.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_group_csv(output / "groups.csv", groups)
    print(output / "analysis.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
