from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[1] / "Run-BotBenchmarkMatrix.py"
SPEC = importlib.util.spec_from_file_location("run_bot_benchmark_matrix", TOOL_PATH)
assert SPEC and SPEC.loader
MATRIX = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MATRIX
SPEC.loader.exec_module(MATRIX)


def write_manifest(
    root: Path,
    *,
    maps: list[str] | None = None,
    seeds: list[int] | None = None,
    repetitions: int = 1,
    paired: bool = True,
    bot_count: int | None = None,
    per_bot_skills: list[int] | None = None,
    requested_names: list[str] | None = None,
) -> Path:
    game = root / "game"
    game.mkdir(exist_ok=True)
    variants = [
        {"id": "stock", "executable": sys.executable},
        {"id": "candidate", "executable": sys.executable},
    ]
    if paired:
        variants[0]["comparison_role"] = "baseline"
        variants[1]["comparison_role"] = "candidate"
    manifest = {
        "schema": MATRIX.MATRIX_SCHEMA,
        "game": {"family": "ut99", "root": str(game)},
        "variants": variants,
        "map_urls": maps or ["DM-Morbias][?Game=Botpack.DeathMatchPlus"],
        "seeds": seeds or [104729],
        "max_ticks": 60,
        "fixed_delta": 1.0 / 60.0,
        "difficulty": 7,
        "concurrency": 1,
        "timeout_seconds": 10,
        "repetitions": repetitions,
    }
    if bot_count is not None:
        manifest["bot_count"] = bot_count
    if per_bot_skills is not None:
        manifest["per_bot_skills"] = per_bot_skills
    if requested_names is not None:
        manifest["requested_names"] = requested_names
    path = root / "matrix.json"
    path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
    return path


def output_from_command(command: list[str]) -> Path:
    argument = next(value for value in command if value.startswith("--botbench-output="))
    return Path(argument.split("=", 1)[1])


class MatrixRunnerTests(unittest.TestCase):
    def test_expansion_order_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = MATRIX.load_matrix(write_manifest(
                root, maps=["DM-A?Game=A", "DM-B?Game=B"], seeds=[2, 1], repetitions=2))
            first = MATRIX.expand_cases(config)
            second = MATRIX.expand_cases(config)
            self.assertEqual(first, second)
            self.assertEqual(len(first), 16)
            self.assertEqual(
                [(case.map_url, case.seed, case.repetition, case.variant.id) for case in first[:4]],
                [("DM-A?Game=A", 2, 0, "stock"), ("DM-A?Game=A", 2, 0, "candidate"),
                 ("DM-A?Game=A", 2, 1, "stock"), ("DM-A?Game=A", 2, 1, "candidate")],
            )

    def test_cross_game_url_is_preserved_as_one_argument(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            url = "DmAriza?Game=UnrealShare.DeathMatchGame?Mutator=Example.Mod&Flag=One Two"
            config = MATRIX.load_matrix(write_manifest(root, maps=[url]))
            case = MATRIX.expand_cases(config)[0]
            command = MATRIX.command_for(config, case, root / "output")
            self.assertIn(f"--botbench-url={url}", command)
            self.assertEqual(sum(value.startswith("--botbench-url=") for value in command), 1)
            self.assertEqual(command[-1], str((root / "game").resolve()))

    def test_roster_arguments_and_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            default_config = MATRIX.load_matrix(write_manifest(root))
            default_command = MATRIX.command_for(
                default_config, MATRIX.expand_cases(default_config)[0], root / "default")
            self.assertIn("--botbench-bots=1", default_command)
            self.assertFalse(any(value.startswith("--botbench-skills=") for value in default_command))
            self.assertFalse(any(value.startswith("--botbench-names=") for value in default_command))

            roster_config = MATRIX.load_matrix(write_manifest(
                root, bot_count=2, per_bot_skills=[7, 5], requested_names=["Alpha", "Bravo"]))
            roster_command = MATRIX.command_for(
                roster_config, MATRIX.expand_cases(roster_config)[0], root / "roster")
            self.assertIn("--botbench-bots=2", roster_command)
            self.assertIn("--botbench-skills=7,5", roster_command)
            self.assertIn("--botbench-names=Alpha,Bravo", roster_command)

    def test_roster_is_part_of_case_and_pair_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first_config = MATRIX.load_matrix(write_manifest(
                root, bot_count=2, per_bot_skills=[7, 5], requested_names=["Alpha", "Bravo"]))
            first = MATRIX.expand_cases(first_config)[0]
            second_config = MATRIX.load_matrix(write_manifest(
                root, bot_count=2, per_bot_skills=[7, 4], requested_names=["Alpha", "Bravo"]))
            second = MATRIX.expand_cases(second_config)[0]
            self.assertNotEqual(first.run_id, second.run_id)
            self.assertNotEqual(first.pair_id, second.pair_id)

    def test_invalid_roster_configuration_is_rejected(self) -> None:
        invalid = [
            {"bot_count": 0},
            {"bot_count": 17},
            {"bot_count": 2, "per_bot_skills": [7]},
            {"bot_count": 2, "per_bot_skills": [7, 8]},
            {"bot_count": 2, "requested_names": ["Alpha"]},
            {"bot_count": 2, "requested_names": ["Alpha", "alpha"]},
            {"bot_count": 2, "requested_names": ["Alpha", "Bad,Name"]},
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index, roster in enumerate(invalid):
                with self.subTest(roster=roster):
                    path = write_manifest(root)
                    manifest = json.loads(path.read_text(encoding="utf-8"))
                    manifest.update(roster)
                    path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
                    with self.assertRaises(MATRIX.MatrixError):
                        MATRIX.load_matrix(path)

    def test_slug_collisions_still_produce_unique_run_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            maps = ["DM-A?Game=One.Mode", "DM-A?Game=Two.Mode"]
            cases = MATRIX.expand_cases(MATRIX.load_matrix(write_manifest(root, maps=maps)))
            run_ids = [case.run_id for case in cases]
            self.assertEqual(len(run_ids), len(set(run_ids)))

    def test_metadata_pairs_and_analysis_are_written_after_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = MATRIX.load_matrix(write_manifest(root))
            validation_calls: list[Path] = []
            analysis_calls: list[list[Path]] = []

            def launcher(command, timeout, stdout, stderr):
                run = output_from_command(command)
                for name in ("manifest.json", "events.jsonl", "summary.json"):
                    (run / name).write_text("{}\n", encoding="utf-8")
                return MATRIX.LaunchResult(0, False, 0.01)

            def validator(path):
                validation_calls.append(path)

            def analyzer(paths):
                self.assertEqual(len(validation_calls), 2)
                analysis_calls.append(paths)
                return {"schema": "synthetic-quality-report"}

            output = root / "results"
            report = MATRIX.run_matrix(
                config, output, analyze=True, launcher=launcher,
                validator=validator, aggregate_analyzer=analyzer)
            self.assertEqual(report["status"], "passed")
            self.assertEqual(len(validation_calls), 2)
            self.assertEqual(len(analysis_calls), 1)
            metadata = [json.loads((Path(row["run_directory"]) / "quality-metadata.json").read_text())
                        for row in report["runs"]]
            self.assertEqual({item["comparison_role"] for item in metadata}, {"baseline", "candidate"})
            self.assertEqual(len({item["pair_id"] for item in metadata}), 1)
            self.assertTrue((output / "quality-analysis.json").is_file())

    def test_dry_run_creates_no_output_and_launches_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = write_manifest(root)
            output = root / "not-created"
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured):
                result = MATRIX.main(["--manifest", str(manifest), "--output", str(output), "--dry-run"])
            self.assertEqual(result, 0)
            self.assertFalse(output.exists())
            plan = json.loads(captured.getvalue())
            self.assertTrue(plan["dry_run"])
            self.assertEqual(plan["case_count"], 2)

    def test_process_failure_blocks_analysis_and_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = MATRIX.load_matrix(write_manifest(root, paired=False))
            analysis_called = False

            def launcher(command, timeout, stdout, stderr):
                return MATRIX.LaunchResult(9, False, 0.02, "synthetic process failure")

            def analyzer(paths):
                nonlocal analysis_called
                analysis_called = True
                return {}

            report = MATRIX.run_matrix(
                config, root / "failed", analyze=True, launcher=launcher,
                validator=lambda path: None, aggregate_analyzer=analyzer)
            self.assertEqual(report["status"], "failed")
            self.assertEqual(report["failed"], 2)
            self.assertFalse(analysis_called)
            for row in report["runs"]:
                self.assertIn("benchmark exit code was 9", row["errors"])
                self.assertIn("missing or empty events.jsonl", row["errors"])
            persisted = json.loads((root / "failed" / "matrix-results.json").read_text())
            self.assertEqual(persisted["status"], "failed")

    def test_timeout_is_a_hard_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = MATRIX.load_matrix(write_manifest(root, paired=False))

            def launcher(command, timeout, stdout, stderr):
                return MATRIX.LaunchResult(None, True, timeout, "synthetic timeout")

            report = MATRIX.run_matrix(
                config, root / "timed-out", launcher=launcher, validator=lambda path: None)
            self.assertEqual(report["status"], "failed")
            self.assertTrue(all(row["timed_out"] for row in report["runs"]))
            self.assertTrue(all("benchmark process timed out" in row["errors"] for row in report["runs"]))

    def test_structural_failure_blocks_aggregate_analysis(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = MATRIX.load_matrix(write_manifest(root, paired=False))
            analysis_called = False

            def launcher(command, timeout, stdout, stderr):
                run = output_from_command(command)
                for name in ("manifest.json", "events.jsonl", "summary.json"):
                    (run / name).write_text("{}\n", encoding="utf-8")
                return MATRIX.LaunchResult(0, False, 0.01)

            def analyzer(paths):
                nonlocal analysis_called
                analysis_called = True
                return {}

            def validator(path):
                raise ValueError("synthetic malformed trace")

            report = MATRIX.run_matrix(
                config, root / "invalid", analyze=True, launcher=launcher,
                validator=validator, aggregate_analyzer=analyzer)
            self.assertEqual(report["status"], "failed")
            self.assertFalse(analysis_called)
            self.assertTrue(all("structural validation failed" in row["errors"][0]
                                for row in report["runs"]))


if __name__ == "__main__":
    unittest.main()
