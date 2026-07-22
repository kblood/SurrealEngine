#!/usr/bin/env python3
"""Run the fail-closed desktop/IWER profiler across the acceptance map set."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from profile_webxr import parse_extra_query
from webxr_performance import ProfileValidationError
from webxr_performance_matrix import (
	MATRIX_SCHEMA,
	MATRIX_VERSION,
	MapSpec,
	build_matrix,
	map_slug,
	matrix_summary,
	read_json,
	sha256_file,
	validate_map_specs,
)


HERE = Path(__file__).resolve().parent
DEFAULT_REPRESENTATIVE_MAPS = [
	"DM-Deck16][",
	"CTF-Face",
	"DOM-Sesmar",
	"AS-Overlord",
	"DM-Morpheus",
]
DEFAULT_STRESS_MAPS = ["CTF-Darji16"]


def parse_args(argv: list[str]) -> argparse.Namespace:
	parser = argparse.ArgumentParser(description=(
		"Run a comparable desktop Chrome/IWER profile matrix. This never qualifies Quest performance."))
	parser.add_argument("--map", action="append", default=[], help="Representative map; repeatable")
	parser.add_argument("--stress-map", action="append", default=[], help="Stress map; repeatable")
	parser.add_argument("--build", default="build-emscripten")
	parser.add_argument("--output", type=Path, default=None, help="New aggregate JSON path")
	parser.add_argument("--duration", type=float, default=30.0)
	parser.add_argument("--warmup", type=float, default=10.0)
	parser.add_argument("--sample-interval", type=float, default=0.25)
	parser.add_argument("--base-url", default="http://localhost:8091/")
	parser.add_argument("--query", action="append", default=[], metavar="KEY=VALUE")
	parser.add_argument("--browser-channel", default="chrome")
	parser.add_argument("--headed", action="store_true")
	parser.add_argument("--boot-timeout", type=float, default=180.0)
	args = parser.parse_args(argv)
	if not args.map and not args.stress_map:
		args.map = list(DEFAULT_REPRESENTATIVE_MAPS)
		args.stress_map = list(DEFAULT_STRESS_MAPS)
	specs = [MapSpec(name, "representative") for name in args.map]
	specs.extend(MapSpec(name, "stress") for name in args.stress_map)
	try:
		validate_map_specs(specs, require_acceptance_set=True)
		parse_extra_query(args.query)
	except ProfileValidationError as error:
		parser.error(str(error))
	args.specs = specs
	for name in ("duration", "warmup", "sample_interval", "boot_timeout"):
		value = getattr(args, name)
		if value != value or value in (float("inf"), float("-inf")):
			parser.error(f"--{name.replace('_', '-')} must be finite")
	if args.duration <= 0 or args.warmup < 0 or args.sample_interval <= 0 or \
			args.sample_interval > args.duration / 2 or args.boot_timeout <= 0:
		parser.error("duration/interval/timeout values are outside their valid range")
	return args


def child_command(args: argparse.Namespace, spec: MapSpec, output: Path) -> list[str]:
	command = [
		sys.executable, "-B", "-u", str(HERE / "profile_webxr.py"),
		"--map", spec.name,
		"--build", args.build,
		"--output", str(output),
		"--duration", str(args.duration),
		"--warmup", str(args.warmup),
		"--sample-interval", str(args.sample_interval),
		"--base-url", args.base_url,
		"--browser-channel", args.browser_channel,
		"--boot-timeout", str(args.boot_timeout),
	]
	if args.headed:
		command.append("--headed")
	for query in args.query:
		command.extend(("--query", query))
	return command


def atomic_write(path: Path, text: str) -> None:
	temporary = path.with_name(path.name + ".tmp")
	try:
		temporary.write_text(text, encoding="utf-8")
		os.replace(temporary, path)
	finally:
		if temporary.exists():
			temporary.unlink()


def main(argv: list[str] | None = None) -> int:
	args = parse_args(argv if argv is not None else sys.argv[1:])
	stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
	output = (args.output or HERE / "profiles" / f"webxr-desktop-iwer-matrix-{stamp}.json").resolve()
	summary_path = output.with_suffix(".txt")
	reports_directory = output.with_suffix("").with_name(output.stem + "-reports")
	if output.exists() or summary_path.exists() or reports_directory.exists():
		print("FAIL: matrix output/report targets must not already exist", file=sys.stderr)
		return 1
	output.parent.mkdir(parents=True, exist_ok=True)
	reports_directory.mkdir()
	profiles = []
	failures = []
	for index, spec in enumerate(args.specs, start=1):
		child_output = reports_directory / f"{index:02d}-{spec.role}-{map_slug(spec.name)}.json"
		print(f"[{index}/{len(args.specs)}] {spec.role}: {spec.name}")
		completed = subprocess.run(child_command(args, spec, child_output), cwd=HERE.parent,
			check=False)
		if not child_output.is_file():
			failures.append(f"{spec.name}: profiler exited {completed.returncode} without a report")
			continue
		report = read_json(child_output)
		if completed.returncode != 0 or report.get("result") != "pass":
			failures.append(f"{spec.name}: {report.get('error') or 'child profile failed'}")
			continue
		profiles.append((report, child_output.relative_to(output.parent).as_posix(),
			sha256_file(child_output)))
	try:
		if failures:
			raise ProfileValidationError("; ".join(failures))
		matrix = build_matrix(datetime.now(timezone.utc).isoformat(), args.specs, profiles,
			require_acceptance_set=True)
		atomic_write(output, json.dumps(matrix, indent=2, sort_keys=True) + "\n")
		atomic_write(summary_path, matrix_summary(matrix))
		print(matrix_summary(matrix), end="")
		print(f"JSON: {output}")
		print(f"Summary: {summary_path}")
		return 0
	except Exception as error:
		failure = {
			"schema": MATRIX_SCHEMA,
			"version": MATRIX_VERSION,
			"createdAt": datetime.now(timezone.utc).isoformat(),
			"result": "fail",
			"environment": {
				"kind": "desktop-chrome-iwer-multi-map", "headset": False,
				"questQualified": False,
				"label": "DESKTOP CHROME + IWER MULTI-MAP; NOT A QUEST PERFORMANCE RESULT",
			},
			"error": str(error),
			"completedProfiles": len(profiles),
			"requestedMaps": [{"map": spec.name, "role": spec.role} for spec in args.specs],
		}
		atomic_write(output, json.dumps(failure, indent=2, sort_keys=True) + "\n")
		atomic_write(summary_path,
			"SurrealEngine WebXR multi-map performance matrix v1\n"
			"DESKTOP CHROME + IWER MULTI-MAP; NOT A QUEST PERFORMANCE RESULT\n"
			f"Result: FAIL\nError: {error}\n")
		print(f"FAIL: {error}", file=sys.stderr)
		print(f"Failure report: {output}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
