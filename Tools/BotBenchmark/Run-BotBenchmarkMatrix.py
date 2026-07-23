#!/usr/bin/env python3
"""Run a deterministic matrix against separate unified-engine bot variants."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import math
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


MATRIX_SCHEMA = "surreal-bot-benchmark-matrix-v1"
RESULT_SCHEMA = "surreal-bot-benchmark-matrix-results-v1"
METADATA_SCHEMA = "surreal-bot-quality-run-metadata-v1"
INVOCATION_SCHEMA = "surreal-bot-benchmark-invocation-v1"
TOOL_VERSION = 2
MAX_CONCURRENCY = 64
MAX_BOT_COUNT = 16


class MatrixError(ValueError):
    pass


@dataclass(frozen=True)
class Variant:
    id: str
    executable: Path
    comparison_role: str | None


@dataclass(frozen=True)
class MatrixConfig:
    source: Path
    game_family: str
    game_root: Path
    variants: tuple[Variant, ...]
    map_urls: tuple[str, ...]
    seeds: tuple[int, ...]
    max_ticks: int
    fixed_delta: float
    difficulty: int
    bot_count: int
    per_bot_skills: tuple[int, ...] | None
    requested_names: tuple[str, ...] | None
    concurrency: int
    timeout_seconds: float
    repetitions: int


@dataclass(frozen=True)
class MatrixCase:
    ordinal: int
    run_id: str
    pair_id: str | None
    variant: Variant
    map_url: str
    seed: int
    repetition: int


@dataclass(frozen=True)
class LaunchResult:
    exit_code: int | None
    timed_out: bool
    wall_seconds: float
    error: str | None = None


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise MatrixError(f"{context} must be a JSON object")
    return value


def _string(fields: dict[str, Any], name: str, context: str) -> str:
    value = fields.get(name)
    if not isinstance(value, str) or not value:
        raise MatrixError(f"{context}.{name} must be a non-empty string")
    return value


def _integer(value: Any, context: str, minimum: int, maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise MatrixError(f"{context} must be an integer")
    if value < minimum or (maximum is not None and value > maximum):
        end = f" and {maximum}" if maximum is not None else ""
        raise MatrixError(f"{context} must be between {minimum}{end}")
    return value


def _number(value: Any, context: str, minimum: float, maximum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise MatrixError(f"{context} must be a finite number")
    result = float(value)
    if not math.isfinite(result) or result < minimum or (maximum is not None and result > maximum):
        end = f" and {maximum}" if maximum is not None else ""
        raise MatrixError(f"{context} must be finite and between {minimum}{end}")
    return result


def _ascii_lower(value: str) -> str:
    return "".join(chr(ord(character) + 32) if "A" <= character <= "Z" else character
                   for character in value)


def _resolve_existing(path_text: str, base: Path, context: str, *, directory: bool) -> Path:
    path = Path(path_text)
    if not path.is_absolute():
        path = base / path
    path = path.resolve()
    valid = path.is_dir() if directory else path.is_file()
    if not valid:
        kind = "directory" if directory else "file"
        raise MatrixError(f"{context} is not an existing {kind}: {path}")
    return path


def load_matrix(path: Path) -> MatrixConfig:
    source = path.resolve()
    try:
        raw = _object(json.loads(source.read_text(encoding="utf-8-sig")), "matrix manifest")
    except FileNotFoundError as exc:
        raise MatrixError(f"matrix manifest does not exist: {source}") from exc
    except json.JSONDecodeError as exc:
        raise MatrixError(f"invalid matrix manifest JSON: {exc}") from exc
    if raw.get("schema") != MATRIX_SCHEMA:
        raise MatrixError(f"unsupported matrix schema {raw.get('schema')!r}")

    game = _object(raw.get("game"), "matrix.game")
    game_family = _string(game, "family", "matrix.game")
    game_root = _resolve_existing(_string(game, "root", "matrix.game"), source.parent, "matrix.game.root",
                                  directory=True)

    variants_raw = raw.get("variants")
    if not isinstance(variants_raw, list) or not variants_raw:
        raise MatrixError("matrix.variants must be a non-empty array")
    variants: list[Variant] = []
    for index, item in enumerate(variants_raw):
        fields = _object(item, f"matrix.variants[{index}]")
        variant_id = _string(fields, "id", f"matrix.variants[{index}]")
        executable = _resolve_existing(
            _string(fields, "executable", f"matrix.variants[{index}]"), source.parent,
            f"matrix.variants[{index}].executable", directory=False)
        role = fields.get("comparison_role")
        if role is not None and role not in ("baseline", "candidate"):
            raise MatrixError(f"matrix.variants[{index}].comparison_role must be baseline or candidate")
        variants.append(Variant(variant_id, executable, role))
    ids = [variant.id for variant in variants]
    if len(ids) != len(set(ids)):
        raise MatrixError("matrix variant IDs must be unique")
    roles = [variant.comparison_role for variant in variants if variant.comparison_role]
    if roles and sorted(roles) != ["baseline", "candidate"]:
        raise MatrixError("paired comparison requires exactly one baseline and one candidate variant")

    map_urls = raw.get("map_urls")
    if (not isinstance(map_urls, list) or not map_urls
            or any(not isinstance(value, str) or not value for value in map_urls)):
        raise MatrixError("matrix.map_urls must be a non-empty array of non-empty strings")
    if len(map_urls) != len(set(map_urls)):
        raise MatrixError("matrix.map_urls contains duplicates; use repetitions instead")
    seeds = raw.get("seeds")
    if not isinstance(seeds, list) or not seeds:
        raise MatrixError("matrix.seeds must be a non-empty array")
    parsed_seeds = tuple(_integer(value, f"matrix.seeds[{index}]", 0) for index, value in enumerate(seeds))
    if len(parsed_seeds) != len(set(parsed_seeds)):
        raise MatrixError("matrix.seeds contains duplicates; use repetitions instead")

    max_ticks = _integer(raw.get("max_ticks"), "matrix.max_ticks", 1, 10_000_000)
    fixed_delta = _number(raw.get("fixed_delta"), "matrix.fixed_delta", 0.0, 1.0)
    if fixed_delta == 0:
        raise MatrixError("matrix.fixed_delta must be positive")
    difficulty = _integer(raw.get("difficulty"), "matrix.difficulty", 0, 7)
    bot_count = _integer(raw.get("bot_count", 1), "matrix.bot_count", 1, MAX_BOT_COUNT)

    per_bot_skills_raw = raw.get("per_bot_skills")
    per_bot_skills = None
    if per_bot_skills_raw is not None:
        if not isinstance(per_bot_skills_raw, list):
            raise MatrixError("matrix.per_bot_skills must be an array")
        if len(per_bot_skills_raw) != bot_count:
            raise MatrixError("matrix.per_bot_skills count must match matrix.bot_count")
        per_bot_skills = tuple(
            _integer(value, f"matrix.per_bot_skills[{index}]", 0, 7)
            for index, value in enumerate(per_bot_skills_raw))

    requested_names_raw = raw.get("requested_names")
    requested_names = None
    if requested_names_raw is not None:
        if not isinstance(requested_names_raw, list):
            raise MatrixError("matrix.requested_names must be an array")
        if len(requested_names_raw) != bot_count:
            raise MatrixError("matrix.requested_names count must match matrix.bot_count")
        if any(not isinstance(value, str) or not value or value != value.strip() or "," in value
               for value in requested_names_raw):
            raise MatrixError("matrix.requested_names entries must be non-empty trimmed strings without commas")
        normalized_names = [_ascii_lower(value) for value in requested_names_raw]
        if len(normalized_names) != len(set(normalized_names)):
            raise MatrixError("matrix.requested_names entries must be unique case-insensitively")
        requested_names = tuple(requested_names_raw)

    concurrency = _integer(raw.get("concurrency", 1), "matrix.concurrency", 1, MAX_CONCURRENCY)
    timeout_seconds = _number(raw.get("timeout_seconds", 120), "matrix.timeout_seconds", 0.0)
    if timeout_seconds == 0:
        raise MatrixError("matrix.timeout_seconds must be positive")
    repetitions = _integer(raw.get("repetitions", 1), "matrix.repetitions", 1, 10_000)
    return MatrixConfig(
        source=source,
        game_family=game_family,
        game_root=game_root,
        variants=tuple(variants),
        map_urls=tuple(map_urls),
        seeds=parsed_seeds,
        max_ticks=max_ticks,
        fixed_delta=fixed_delta,
        difficulty=difficulty,
        bot_count=bot_count,
        per_bot_skills=per_bot_skills,
        requested_names=requested_names,
        concurrency=concurrency,
        timeout_seconds=timeout_seconds,
        repetitions=repetitions,
    )


def _slug(value: str, limit: int = 32) -> str:
    result = "".join(character.lower() if character.isalnum() else "-" for character in value)
    result = "-".join(filter(None, result.split("-")))[:limit].rstrip("-")
    return result or "case"


def _digest(fields: list[Any], length: int = 12) -> str:
    canonical = json.dumps(fields, ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:length]


def expand_cases(config: MatrixConfig) -> list[MatrixCase]:
    cases: list[MatrixCase] = []
    paired = any(variant.comparison_role for variant in config.variants)
    ordinal = 0
    for map_index, map_url in enumerate(config.map_urls):
        for seed in config.seeds:
            for repetition in range(config.repetitions):
                shared = [config.game_family, map_index, map_url, seed, repetition,
                          config.max_ticks, config.fixed_delta, config.difficulty,
                          config.bot_count, config.per_bot_skills, config.requested_names]
                pair_id = f"case-{_digest(shared, 16)}" if paired else None
                for variant in config.variants:
                    identity = [*shared, variant.id, str(variant.executable)]
                    run_id = (
                        f"{ordinal:06d}-{_slug(variant.id)}-{_slug(map_url)}-"
                        f"s{seed}-r{repetition}-{_digest(identity)}"
                    )
                    cases.append(MatrixCase(ordinal, run_id, pair_id if variant.comparison_role else None,
                                            variant, map_url, seed, repetition))
                    ordinal += 1
    run_ids = [case.run_id for case in cases]
    if len(run_ids) != len(set(run_ids)):
        raise MatrixError("case expansion produced colliding run IDs")
    return cases


def command_for(config: MatrixConfig, case: MatrixCase, run_directory: Path) -> list[str]:
    command = [
        str(case.variant.executable),
        "--autoplay",
        "--headless-driver=bot-benchmark",
        f"--botbench-url={case.map_url}",
        f"--botbench-output={run_directory}",
        f"--botbench-seed={case.seed}",
        f"--botbench-ticks={config.max_ticks}",
        f"--botbench-fixed-delta={format(config.fixed_delta, '.17g')}",
        f"--botbench-difficulty={config.difficulty}",
        f"--botbench-bots={config.bot_count}",
    ]
    if config.per_bot_skills is not None:
        command.append("--botbench-skills=" + ",".join(str(value) for value in config.per_bot_skills))
    if config.requested_names is not None:
        command.append("--botbench-names=" + ",".join(config.requested_names))
    command.append(str(config.game_root))
    return command


def _launch(command: list[str], timeout: float, stdout_path: Path, stderr_path: Path) -> LaunchResult:
    started = time.monotonic()
    try:
        with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open("w", encoding="utf-8") as stderr:
            process = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=timeout, check=False, shell=False)
        return LaunchResult(process.returncode, False, time.monotonic() - started)
    except subprocess.TimeoutExpired:
        return LaunchResult(None, True, time.monotonic() - started, f"timed out after {timeout:g} seconds")
    except OSError as exc:
        return LaunchResult(None, False, time.monotonic() - started, f"process launch failed: {exc}")


def _load_analyzer() -> Any:
    path = Path(__file__).with_name("Analyze-BotQuality.py")
    spec = importlib.util.spec_from_file_location("surreal_bot_quality", path)
    if spec is None or spec.loader is None:
        raise MatrixError(f"could not load analyzer: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def _run_case(
    config: MatrixConfig,
    case: MatrixCase,
    runs_directory: Path,
    launcher: Callable[[list[str], float, Path, Path], LaunchResult],
    validator: Callable[[Path], Any],
) -> dict[str, Any]:
    run_directory = runs_directory / case.run_id
    run_directory.mkdir(parents=False, exist_ok=False)
    command = command_for(config, case, run_directory)
    metadata: dict[str, Any] = {"schema": METADATA_SCHEMA, "variant": case.variant.id}
    if case.variant.comparison_role:
        metadata.update({"pair_id": case.pair_id, "comparison_role": case.variant.comparison_role})
    _write_json(run_directory / "quality-metadata.json", metadata)
    _write_json(run_directory / "invocation.json", {
        "schema": INVOCATION_SCHEMA,
        "run_id": case.run_id,
        "variant": case.variant.id,
        "variant_executable": str(case.variant.executable),
        "game_family": config.game_family,
        "game_root": str(config.game_root),
        "map_url": case.map_url,
        "seed": str(case.seed),
        "repetition": case.repetition,
        "bot_count": config.bot_count,
        "per_bot_skills": config.per_bot_skills,
        "requested_names": config.requested_names,
        "command": command,
    })
    launch = launcher(command, config.timeout_seconds, run_directory / "stdout.txt", run_directory / "stderr.txt")
    required = {name: (run_directory / name).is_file() and (run_directory / name).stat().st_size > 0
                for name in ("manifest.json", "events.jsonl", "summary.json")}
    errors: list[str] = []
    if launch.error:
        errors.append(launch.error)
    if launch.timed_out:
        errors.append("benchmark process timed out")
    if launch.exit_code != 0:
        errors.append(f"benchmark exit code was {launch.exit_code}")
    for name, present in required.items():
        if not present:
            errors.append(f"missing or empty {name}")
    if not errors:
        try:
            validator(run_directory)
        except Exception as exc:
            errors.append(f"structural validation failed: {exc}")
    return {
        "ordinal": case.ordinal,
        "run_id": case.run_id,
        "run_directory": str(run_directory),
        "variant": case.variant.id,
        "comparison_role": case.variant.comparison_role,
        "pair_id": case.pair_id,
        "map_url": case.map_url,
        "seed": str(case.seed),
        "repetition": case.repetition,
        "bot_count": config.bot_count,
        "per_bot_skills": config.per_bot_skills,
        "requested_names": config.requested_names,
        "exit_code": launch.exit_code,
        "timed_out": launch.timed_out,
        "wall_seconds": launch.wall_seconds,
        "required_outputs": required,
        "status": "passed" if not errors else "failed",
        "errors": errors,
    }


def dry_run_plan(config: MatrixConfig, output: Path) -> dict[str, Any]:
    cases = expand_cases(config)
    runs_directory = output.resolve() / "runs"
    return {
        "schema": RESULT_SCHEMA,
        "dry_run": True,
        "manifest": str(config.source),
        "output": str(output.resolve()),
        "case_count": len(cases),
        "cases": [{
            "ordinal": case.ordinal,
            "run_id": case.run_id,
            "variant": case.variant.id,
            "comparison_role": case.variant.comparison_role,
            "pair_id": case.pair_id,
            "map_url": case.map_url,
            "seed": str(case.seed),
            "repetition": case.repetition,
            "bot_count": config.bot_count,
            "per_bot_skills": config.per_bot_skills,
            "requested_names": config.requested_names,
            "command": command_for(config, case, runs_directory / case.run_id),
        } for case in cases],
    }


def run_matrix(
    config: MatrixConfig,
    output: Path,
    *,
    analyze: bool = False,
    launcher: Callable[[list[str], float, Path, Path], LaunchResult] = _launch,
    validator: Callable[[Path], Any] | None = None,
    aggregate_analyzer: Callable[[list[Path]], dict[str, Any]] | None = None,
) -> dict[str, Any]:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runs_directory = output / "runs"
    runs_directory.mkdir()
    analyzer = None
    if validator is None or (analyze and aggregate_analyzer is None):
        analyzer = _load_analyzer()
    validator = validator or analyzer.analyze_run
    aggregate_analyzer = aggregate_analyzer or (analyzer.analyze if analyzer else None)
    cases = expand_cases(config)
    rows: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=config.concurrency) as executor:
        futures = [executor.submit(_run_case, config, case, runs_directory, launcher, validator) for case in cases]
        for future in concurrent.futures.as_completed(futures):
            rows.append(future.result())
    rows.sort(key=lambda row: row["ordinal"])
    passed = all(row["status"] == "passed" for row in rows)
    report: dict[str, Any] = {
        "schema": RESULT_SCHEMA,
        "tool_version": TOOL_VERSION,
        "manifest": str(config.source),
        "game_family": config.game_family,
        "status": "passed" if passed else "failed",
        "case_count": len(rows),
        "passed": sum(row["status"] == "passed" for row in rows),
        "failed": sum(row["status"] == "failed" for row in rows),
        "quality_analysis": None,
        "runs": rows,
    }
    if analyze and passed:
        assert aggregate_analyzer is not None
        try:
            quality = aggregate_analyzer([Path(row["run_directory"]) for row in rows])
            quality_path = output / "quality-analysis.json"
            _write_json(quality_path, quality)
            report["quality_analysis"] = str(quality_path)
        except Exception as exc:
            report["status"] = "failed"
            report["analysis_error"] = str(exc)
    _write_json(output / "matrix-results.json", report)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, help=f"{MATRIX_SCHEMA} JSON manifest")
    parser.add_argument("--output", required=True, help="New matrix output directory")
    parser.add_argument("--dry-run", action="store_true", help="Print expanded commands without creating output")
    parser.add_argument("--analyze", action="store_true", help="Write quality-analysis.json after all runs validate")
    args = parser.parse_args(argv)
    try:
        config = load_matrix(Path(args.manifest))
        output = Path(args.output)
        if args.dry_run:
            print(json.dumps(dry_run_plan(config, output), indent=2, allow_nan=False))
            return 0
        report = run_matrix(config, output, analyze=args.analyze)
        print(output.resolve() / "matrix-results.json")
        return 0 if report["status"] == "passed" else 1
    except (MatrixError, OSError) as exc:
        parser.error(str(exc))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
