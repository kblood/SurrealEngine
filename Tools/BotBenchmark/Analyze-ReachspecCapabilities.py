#!/usr/bin/env python3
"""Audit catalog reachspec flags against an exact live bot-capability witness.

This tool deliberately reports flag incidence only.  UE1 reach-flag combination
semantics and player-only eligibility are not inferred from a static catalog,
so this report must never be used as a path-selection decision on its own.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any


SCHEMA = "surreal-reachspec-realized-capability-audit-v1"
FLAG_TO_CAPABILITY = {
    "walk": "walk", "fly": "fly", "swim": "swim", "jump": "jump",
    "door": "open_doors", "special": "special",
}
OPAQUE_FLAGS = {"player_only"}


class AuditError(ValueError):
    pass


def _load_module(filename: str, module_name: str) -> Any:
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise AuditError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AuditError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise AuditError(f"{path}: root must be an object")
    return value


def _map_from_url(value: Any) -> str:
    if not isinstance(value, str) or not value:
        raise AuditError("benchmark manifest url must be a non-empty string")
    return value.split("?", 1)[0]


def _flag_counts(reachspecs: list[dict[str, Any]]) -> dict[str, int]:
    counts: Counter[str] = Counter()
    for spec in reachspecs:
        for flag in spec["reach_flag_names"]:
            counts[flag] += 1
    return dict(sorted(counts.items()))


def analyze(catalog_path: Path, run: Path) -> dict[str, Any]:
    map_validator = _load_module("Validate-MapCatalog.py", "surreal_map_catalog_validation")
    capability_validator = _load_module(
        "Validate-RealizedBotCapabilities.py", "surreal_realized_capability_validation")
    try:
        catalog = _load_json(catalog_path)
        map_validator.validate_catalog(catalog)
        witness = capability_validator.validate_run(run)
    except (AuditError, map_validator.CatalogError, capability_validator.CapabilityError) as exc:
        raise AuditError(str(exc)) from exc

    manifest = _load_json(run / "manifest.json")
    map_name = _map_from_url(manifest.get("url"))
    if catalog.get("map", "").casefold() != map_name.casefold():
        raise AuditError(
            f"catalog map {catalog.get('map')!r} does not match benchmark map {map_name!r}")
    artifact = _load_json(run / capability_validator.ARTIFACT_NAME)
    participants = artifact["participants"]
    reachspecs = catalog["reachspecs"]

    known_flag_counts = _flag_counts(reachspecs)
    unknown_specs = sum(1 for spec in reachspecs if spec["unknown_reach_flags"] != 0)
    player_only_specs = sum(1 for spec in reachspecs if "player_only" in spec["reach_flag_names"])
    per_participant: list[dict[str, Any]] = []
    for participant in participants:
        capabilities = participant["capabilities"]
        absent_flags: Counter[str] = Counter()
        specs_with_absent_flags = 0
        for spec in reachspecs:
            missing = [
                flag for flag, capability in FLAG_TO_CAPABILITY.items()
                if flag in spec["reach_flag_names"] and not capabilities[capability]
            ]
            if missing:
                specs_with_absent_flags += 1
                absent_flags.update(missing)
        per_participant.append({
            "identity": participant["identity"], "actor": participant["actor"],
            "class": participant["class"], "capabilities": capabilities,
            "reachspecs_with_observed_absent_capability_flags_exact": specs_with_absent_flags,
            "absent_capability_flag_incidence_exact": dict(sorted(absent_flags.items())),
        })
    return {
        "schema": SCHEMA,
        "catalog": {
            "map": catalog["map"], "map_package_sha1": catalog["map_package"]["sha1"],
            "reachspecs_exact": len(reachspecs), "reach_flag_incidence_exact": known_flag_counts,
            "unknown_flag_reachspecs_exact": unknown_specs,
            "player_only_reachspecs_exact": player_only_specs,
        },
        "benchmark": {
            "map": map_name, "config_id": manifest.get("config_id"),
            "realized_capability_witness": witness,
        },
        "participants": per_participant,
        "interpretation": {
            "selection_safe": False,
            "reason": (
                "This is a static flag-incidence audit only. It does not establish whether "
                "reach flags are conjunctive or alternative for a route, whether player_only "
                "permits the observed bot, collision clearance, current anchor, or dynamic traversal state."),
            "opaque_flag_names": sorted(OPAQUE_FLAGS),
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path, help="validated map-catalog v3 JSON")
    parser.add_argument("run", type=Path, help="matching completed benchmark run directory")
    parser.add_argument("--output", type=Path, help="write report to this external file")
    args = parser.parse_args(argv)
    try:
        report = analyze(args.catalog, args.run)
        encoded = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
        if args.output is None:
            print(encoded, end="")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(encoded, encoding="utf-8")
    except AuditError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
