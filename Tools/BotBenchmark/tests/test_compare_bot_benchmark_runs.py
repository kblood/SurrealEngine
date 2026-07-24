import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "Compare-BotBenchmarkRuns.py"
SPEC = importlib.util.spec_from_file_location("compare_bot_benchmark_runs", SCRIPT)
COMPARE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(COMPARE)


def _write_json(path: Path, value: object, *, compact: bool = False) -> None:
    if compact:
        text = json.dumps(value, separators=(",", ":")) + "\n"
    else:
        text = json.dumps(value, indent=2) + "\n"
    path.write_text(text, encoding="utf-8")


def write_run(root: Path, *, shadow: bool = True, compact: bool = True) -> Path:
    root.mkdir()
    config_id = "fnv1a64:1234567890abcdef"
    requested = [{"roster_index": 0, "class": "Botpack.TMale2Bot", "skill": 7,
                  "requested_name": "Dante", "identity_fragment": "bot-0"}]
    actual = [{"roster_index": 0, "identity": "pri:1", "actor": "TMale2Bot0",
               "player_name": "Dante", "class": "Botpack.TMale2Bot"}]
    manifest = {
        "schema": "surreal-bot-benchmark-manifest-v2", "driver": "bot-benchmark",
        "config_id": config_id, "url": "DM-Deck16][?Game=Botpack.DeathMatchPlus",
        "output_directory": "stable-output", "seed": 104729, "max_ticks": 2,
        "fixed_delta": 1.0 / 60.0, "difficulty": 7, "bot_count": 1,
        "requested_roster": requested, "telemetry_event_cap": 4,
    }
    bot = {"identity": "pri:1", "actor": "TMale2Bot0", "player_name": "Dante",
           "class": "Botpack.TMale2Bot", "kills_exact": "1"}
    events = [
        {"schema": "surreal-bot-benchmark-telemetry-v2", "seq": "0",
         "config_id": config_id, "tick": "0", "simulated_seconds": 0.0,
         "type": "run_start", "map": "DM-Deck16][", "status": "running",
         "failure_reason": "", "bots": [copy.deepcopy(bot)]},
        {"schema": "surreal-bot-benchmark-telemetry-v2", "seq": "1",
         "config_id": config_id, "tick": "1", "simulated_seconds": 1.0 / 60.0,
         "type": "run_result", "map": "DM-Deck16][", "status": "complete",
         "failure_reason": "", "bots": [copy.deepcopy(bot)]},
    ]
    summary = {
        "schema": "surreal-bot-benchmark-summary-v2", "status": "complete",
        "exit_code": 0, "ticks": 1, "simulated_seconds": 1.0 / 60.0,
        "game": "UnrealTournament", "version": "436", "map": "DM-Deck16][",
        "failure_reason": "", "requested_roster": requested, "actual_roster": actual,
        "config": {"url": manifest["url"], "output_directory": "stable-output",
                   "seed": 104729, "max_ticks": 2, "fixed_delta": 1.0 / 60.0,
                   "difficulty": 7, "bot_count": 1},
    }
    _write_json(root / "manifest.json", manifest, compact=compact)
    (root / "events.jsonl").write_text(
        "".join(json.dumps(event, separators=(",", ":")) + "\n" for event in events),
        encoding="utf-8")
    _write_json(root / "summary.json", summary, compact=compact)
    if shadow:
        shadow_manifest = {
            "schema": "surreal-bot-benchmark-shadow-manifest-v1",
            "benchmark_config_id": config_id, "record_cap": 2,
            "controls_live_bots": False, "policies": [],
            "participants": [{"roster_index": 0, "identity": "pri:1"}],
            "observation_limits": {},
        }
        shadow_events = [
            {"schema": "surreal-bot-benchmark-shadow-event-v1", "seq": index,
             "benchmark_config_id": config_id, "tick": index + 1, "participants": []}
            for index in range(2)
        ]
        for event in shadow_events:
            event["participants"] = [{"roster_index": 0, "identity": "pri:1"}]
        _write_json(root / "shadow-manifest.json", shadow_manifest, compact=compact)
        (root / "shadow-decisions.jsonl").write_text(
            "".join(json.dumps(event, separators=(",", ":")) + "\n"
                    for event in shadow_events), encoding="utf-8")
    return root


def mutate_json(path: Path, mutation) -> None:
    value = json.loads(path.read_text(encoding="utf-8"))
    mutation(value)
    _write_json(path, value, compact=True)


def mutate_jsonl(path: Path, mutation) -> None:
    values = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    mutation(values)
    path.write_text("".join(json.dumps(value, separators=(",", ":")) + "\n"
                            for value in values), encoding="utf-8")


class CompareBotBenchmarkRunsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def pair(self, **kwargs) -> tuple[Path, Path]:
        return write_run(self.root / "left", **kwargs), write_run(self.root / "right", **kwargs)

    def test_equal_semantic_json_passes_and_reports_raw_and_normalized_hashes(self) -> None:
        left = write_run(self.root / "left", compact=True)
        right = write_run(self.root / "right", compact=False)
        report = COMPARE.compare_runs(left, right, [])
        self.assertTrue(report["equivalent"])
        manifest = next(item for item in report["artifacts"] if item["path"] == "manifest.json")
        self.assertNotEqual(manifest["left"]["raw_sha256"], manifest["right"]["raw_sha256"])
        self.assertEqual(manifest["left"]["normalized_sha256"],
                         manifest["right"]["normalized_sha256"])

    def test_explicit_one_sided_integer_counter_can_be_ignored_with_audit(self) -> None:
        left, right = self.pair()
        mutate_jsonl(right / "events.jsonl", lambda events: [
            bot.update({"walking_preflight_candidates_exact": str(index)})
            for index, event in enumerate(events) for bot in event["bots"]])
        report = COMPARE.compare_runs(left, right, ["walking_preflight_candidates_exact"])
        self.assertTrue(report["equivalent"])
        events = next(item for item in report["artifacts"] if item["path"] == "events.jsonl")
        audit = events["right"]["ignored"]["walking_preflight_candidates_exact"]
        self.assertEqual(audit["occurrences"], 2)
        self.assertEqual(audit["first"]["line"], 1)
        self.assertIn("occurrences_sha256", audit)

    def test_unused_nonnumeric_and_protected_ignores_fail_closed(self) -> None:
        left, right = self.pair()
        with self.assertRaisesRegex(COMPARE.ComparisonError, "not present"):
            COMPARE.compare_runs(left, right, ["unknown_counter_exact"])
        with self.assertRaisesRegex(COMPARE.ComparisonError, "protected"):
            COMPARE.compare_runs(left, right, ["seed"])
        mutate_jsonl(right / "events.jsonl", lambda events: events[0]["bots"][0].update(
            {"new_counter_exact": "not-an-integer"}))
        with self.assertRaisesRegex(COMPARE.ComparisonError, "integer-valued"):
            COMPARE.compare_runs(left, right, ["new_counter_exact"])

    def test_config_difference_reports_exact_location(self) -> None:
        left, right = self.pair()
        # Keep the run internally valid so this is an across-run mismatch.
        mutate_json(right / "manifest.json", lambda value: value.update(seed=271828))
        mutate_json(right / "summary.json", lambda value: value["config"].update(seed=271828))
        report = COMPARE.compare_runs(left, right, [])
        self.assertFalse(report["equivalent"])
        self.assertEqual(report["mismatch"]["artifact"], "manifest.json")
        self.assertEqual(report["mismatch"]["location"], "/seed")

    def test_schema_difference_is_rejected_before_comparison(self) -> None:
        left, right = self.pair()
        mutate_json(right / "manifest.json", lambda value: value.update(schema="future-v9"))
        with self.assertRaisesRegex(COMPARE.ComparisonError, "unsupported schema"):
            COMPARE.compare_runs(left, right, [])

    def test_roster_order_and_internal_event_order_fail_closed(self) -> None:
        left, right = self.pair()
        mutate_json(right / "manifest.json", lambda value: value["requested_roster"][0].update(roster_index=1))
        with self.assertRaisesRegex(COMPARE.ComparisonError, "roster order"):
            COMPARE.compare_runs(left, right, [])

        # Recreate and make the line ordering internally invalid.
        self.temporary.cleanup()
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        left, right = self.pair()
        mutate_jsonl(right / "events.jsonl", lambda values: values.reverse())
        with self.assertRaisesRegex(COMPARE.ComparisonError, "seq does not match line order"):
            COMPARE.compare_runs(left, right, [])

    def test_jsonl_line_count_difference_is_auditable(self) -> None:
        left, right = self.pair()
        # Add a valid middle event and repair sequence numbers.
        def add_event(events):
            middle = copy.deepcopy(events[0])
            middle.update(seq="1", tick="1", simulated_seconds=1.0 / 60.0, type="tick")
            events[-1].update(seq="2", tick="2", simulated_seconds=2.0 / 60.0)
            events.insert(1, middle)
        mutate_jsonl(right / "events.jsonl", add_event)
        mutate_json(right / "summary.json",
                    lambda value: value.update(ticks=2, simulated_seconds=2.0 / 60.0))
        report = COMPARE.compare_runs(left, right, [])
        events = next(item for item in report["artifacts"] if item["path"] == "events.jsonl")
        self.assertFalse(events["equivalent"])
        self.assertEqual(events["mismatch"], {"kind": "line_count", "location": "/",
                                               "left": 2, "right": 3})

    def test_shadow_artifacts_are_required_as_a_pair_and_compared(self) -> None:
        left, right = self.pair()
        (right / "shadow-decisions.jsonl").unlink()
        with self.assertRaisesRegex(COMPARE.ComparisonError, "must appear together"):
            COMPARE.compare_runs(left, right, [])

        self.temporary.cleanup()
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        left, right = self.pair()
        mutate_jsonl(right / "shadow-decisions.jsonl",
                     lambda values: values[1].update(diagnostic_counter=1))
        report = COMPARE.compare_runs(left, right, [])
        self.assertFalse(report["equivalent"])
        self.assertEqual(report["mismatch"]["artifact"], "shadow-decisions.jsonl")
        self.assertEqual(report["mismatch"]["line"], 2)

    def test_duplicate_json_fields_and_nonfinite_numbers_fail_closed(self) -> None:
        left, right = self.pair()
        (right / "summary.json").write_text(
            '{"schema":"surreal-bot-benchmark-summary-v2","schema":"duplicate"}\n',
            encoding="utf-8")
        with self.assertRaisesRegex(COMPARE.ComparisonError, "duplicate JSON object field"):
            COMPARE.compare_runs(left, right, [])

        _write_json(right / "summary.json", {"schema": float("nan")})
        with self.assertRaisesRegex(COMPARE.ComparisonError, "non-finite JSON number"):
            COMPARE.compare_runs(left, right, [])

    def test_unrecognized_shadow_artifact_fails_closed(self) -> None:
        left, right = self.pair()
        (right / "shadow-notes.txt").write_text("not JSON\n", encoding="utf-8")
        with self.assertRaisesRegex(COMPARE.ComparisonError, "unsupported shadow artifact extension"):
            COMPARE.compare_runs(left, right, [])

        (right / "shadow-notes.txt").unlink()
        _write_json(right / "shadow-extra.json", {"schema": "unknown"})
        with self.assertRaisesRegex(COMPARE.ComparisonError, "unsupported shadow artifact name"):
            COMPARE.compare_runs(left, right, [])

    def test_output_directory_may_only_be_ignored_explicitly(self) -> None:
        left, right = self.pair()
        mutate_json(right / "manifest.json", lambda value: value.update(output_directory="right"))
        mutate_json(right / "summary.json",
                    lambda value: value["config"].update(output_directory="right"))
        self.assertFalse(COMPARE.compare_runs(left, right, [])["equivalent"])
        report = COMPARE.compare_runs(left, right, ["output_directory"])
        self.assertTrue(report["equivalent"])
        manifest = next(item for item in report["artifacts"] if item["path"] == "manifest.json")
        self.assertEqual(manifest["left"]["ignored"]["output_directory"]["occurrences"], 1)

    def test_same_resolved_run_directory_is_rejected(self) -> None:
        left, _ = self.pair()
        with self.assertRaisesRegex(COMPARE.ComparisonError, "must be distinct"):
            COMPARE.compare_runs(left, left / ".", [])

    def test_telemetry_schema_version_must_match_run_schema(self) -> None:
        left, right = self.pair()
        mutate_jsonl(right / "events.jsonl", lambda values: [
            value.update(schema="surreal-bot-benchmark-telemetry-v1") for value in values])
        with self.assertRaisesRegex(COMPARE.ComparisonError, "version differs from manifest"):
            COMPARE.compare_runs(left, right, [])

    def test_identically_malformed_final_results_cannot_pass(self) -> None:
        left, right = self.pair()
        for run in (left, right):
            mutate_jsonl(run / "events.jsonl",
                         lambda values: values[-1].update(failure_reason="hidden failure"))
        with self.assertRaisesRegex(COMPARE.ComparisonError, "complete run_result has a failure"):
            COMPARE.compare_runs(left, right, [])

    def test_only_canonical_exact_counter_names_can_be_ignored(self) -> None:
        left, right = self.pair()
        for field in ("health", "score", "position", "counter", "bad/name_exact", "x-exact"):
            with self.subTest(field=field):
                with self.assertRaisesRegex(COMPARE.ComparisonError, "safe identifier ending"):
                    COMPARE.compare_runs(left, right, [field])

    def test_counter_ignore_cannot_hide_manifest_configuration(self) -> None:
        left, right = self.pair()
        mutate_json(left / "manifest.json", lambda value: value.update(new_mode_exact=1))
        mutate_json(right / "manifest.json", lambda value: value.update(new_mode_exact=2))
        with self.assertRaisesRegex(COMPARE.ComparisonError, "limited to JSONL streams"):
            COMPARE.compare_runs(left, right, ["new_mode_exact"])

    def test_ignored_counter_may_appear_on_both_sides(self) -> None:
        left, right = self.pair()
        mutate_jsonl(left / "events.jsonl", lambda values: [
            bot.update(custom_observer_exact=str(index + 10))
            for index, event in enumerate(values) for bot in event["bots"]])
        mutate_jsonl(right / "events.jsonl", lambda values: [
            bot.update(custom_observer_exact=str(index + 20))
            for index, event in enumerate(values) for bot in event["bots"]])
        report = COMPARE.compare_runs(left, right, ["custom_observer_exact"])
        self.assertTrue(report["equivalent"])
        events = next(item for item in report["artifacts"] if item["path"] == "events.jsonl")
        self.assertEqual(events["left"]["ignored"]["custom_observer_exact"]["occurrences"], 2)
        self.assertEqual(events["right"]["ignored"]["custom_observer_exact"]["occurrences"], 2)

    def test_one_sided_diagnostic_array_can_be_explicitly_ignored(self) -> None:
        left, right = self.pair()
        mutate_jsonl(right / "events.jsonl", lambda values: [
            bot.update(walking_step_preflight_diagnostics=[
                {"reason": "pain_support", "probe": index, "samples": [1.0, None, True]}
            ])
            for index, event in enumerate(values) for bot in event["bots"]])
        report = COMPARE.compare_runs(
            left, right, [], ["walking_step_preflight_diagnostics"])
        self.assertTrue(report["equivalent"])
        events = next(item for item in report["artifacts"] if item["path"] == "events.jsonl")
        audit = events["right"]["ignored"]["walking_step_preflight_diagnostics"]
        self.assertEqual(audit["occurrences"], 2)
        self.assertEqual(audit["first"]["line"], 1)
        self.assertIsInstance(audit["first"]["value"], list)

    def test_diagnostic_ignore_names_are_strict_and_must_be_used(self) -> None:
        left, right = self.pair()
        for field in ("health", "bad/name_diagnostics", "x-diagnostics", "participants"):
            with self.subTest(field=field):
                with self.assertRaisesRegex(COMPARE.ComparisonError, "protected|safe identifier ending"):
                    COMPARE.compare_runs(left, right, [], [field])
        with self.assertRaisesRegex(COMPARE.ComparisonError, "were not present"):
            COMPARE.compare_runs(left, right, [], ["unused_diagnostics"])

    def test_cli_refuses_to_write_inside_either_run(self) -> None:
        left, right = self.pair()
        output = left / "comparison.json"
        result = COMPARE.main([str(left), str(right), "--output", str(output)])
        self.assertEqual(result, 2)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
