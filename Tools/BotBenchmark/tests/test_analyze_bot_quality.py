from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[1] / "Analyze-BotQuality.py"
SPEC = importlib.util.spec_from_file_location("analyze_bot_quality", TOOL_PATH)
assert SPEC and SPEC.loader
QUALITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(QUALITY)


def write_run(
    root: Path,
    name: str,
    positions: list[float],
    health: list[int] | None = None,
    *,
    seed: int = 104729,
    metadata: dict | None = None,
) -> Path:
    run = root / name
    run.mkdir()
    health = health or [100] * len(positions)
    max_ticks = len(positions) - 1
    fixed_delta = 1.0
    url = "DM-Test?Game=Botpack.DeathMatchPlus"
    config_id = QUALITY._config_id(url, seed, max_ticks, fixed_delta, 7)
    manifest = {
        "schema": QUALITY.MANIFEST_SCHEMA,
        "driver": "bot-benchmark",
        "config_id": config_id,
        "url": url,
        "output_directory": str(run),
        "seed": str(seed),
        "max_ticks": str(max_ticks),
        "fixed_delta": fixed_delta,
        "difficulty": 7,
        "telemetry_event_cap": str(max_ticks + 2),
    }
    (run / "manifest.json").write_text(json.dumps(manifest) + "\n", encoding="utf-8")

    def bot(index: int) -> dict:
        return {
            "identity": "pri:1",
            "actor": "Bot1",
            "player_name": "Loque",
            "class": "Botpack.Bot",
            "position": {"x": positions[index], "y": 0.0, "z": 0.0},
            "velocity": {"x": 0.0, "y": 0.0, "z": 0.0},
            "health": health[index],
            "state": "Roaming",
        }

    events = []
    for tick in range(max_ticks + 1):
        events.append({
            "schema": QUALITY.TELEMETRY_SCHEMA,
            "seq": str(tick),
            "config_id": config_id,
            "tick": str(tick),
            "simulated_seconds": float(tick),
            "type": "run_start" if tick == 0 else "tick",
            "map": "DM-Test",
            "status": "running",
            "failure_reason": "",
            "bots": [bot(tick)],
        })
    events.append({
        **events[-1],
        "seq": str(max_ticks + 1),
        "type": "run_result",
        "status": "complete",
    })
    (run / "events.jsonl").write_text(
        "".join(json.dumps(event, separators=(",", ":")) + "\n" for event in events), encoding="utf-8")
    summary = {
        "schema": QUALITY.SUMMARY_SCHEMA,
        "status": "complete",
        "exit_code": 0,
        "ticks": str(max_ticks),
        "simulated_seconds": float(max_ticks),
        "game": "Unreal Tournament",
        "version": "436",
        "map": "DM-Test",
        "bot_class": "Botpack.Bot",
        "bot_name": "Loque",
        "failure_reason": "",
        "config": {
            "url": url,
            "output_directory": str(run),
            "seed": str(seed),
            "max_ticks": str(max_ticks),
            "fixed_delta": fixed_delta,
            "difficulty": 7,
        },
    }
    (run / "summary.json").write_text(json.dumps(summary) + "\n", encoding="utf-8")
    if metadata:
        document = {"schema": QUALITY.METADATA_SCHEMA, **metadata}
        (run / "quality-metadata.json").write_text(json.dumps(document) + "\n", encoding="utf-8")
    return run


def write_v2_run(root: Path, name: str, *, bot_count: int = 2) -> Path:
    run = write_run(root, name, [0.0, 1.0, 2.0])
    manifest_path = run / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    names = ["Alpha", "Bravo"][:bot_count]
    skills = [7, 6][:bot_count]
    requested_roster = [{
        "roster_index": index,
        "requested_name": names[index],
        "external_skill": skills[index],
        "identity_fragment": (
            f"participant-v1:index={index};external_skill={skills[index]};"
            f"requested_name_hex={names[index].encode('utf-8').hex()}"),
    } for index in range(bot_count)]
    manifest["schema"] = QUALITY.MANIFEST_SCHEMA_V2
    manifest["bot_count"] = bot_count
    manifest["requested_roster"] = requested_roster
    manifest["config_id"] = QUALITY._config_id(
        manifest["url"], int(manifest["seed"]), int(manifest["max_ticks"]), manifest["fixed_delta"],
        manifest["difficulty"], bot_count, requested_roster)
    manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")

    events_path = run / "events.jsonl"
    events = [json.loads(line) for line in events_path.read_text(encoding="utf-8").splitlines()]
    for event in events:
        source = event["bots"][0]
        event["config_id"] = manifest["config_id"]
        event["bots"] = [{
            **source,
            "identity": f"pri:{index + 1}",
            "actor": f"Bot{index + 1}",
            "player_name": names[index],
            "position": {**source["position"], "y": float(index)},
        } for index in range(bot_count)]
    events_path.write_text(
        "".join(json.dumps(event, separators=(",", ":")) + "\n" for event in events), encoding="utf-8")

    summary_path = run / "summary.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    summary["schema"] = QUALITY.SUMMARY_SCHEMA_V2
    summary.pop("bot_class")
    summary.pop("bot_name")
    summary["requested_roster"] = requested_roster
    summary["actual_roster"] = [{
        "roster_index": index,
        "identity": f"pri:{index + 1}",
        "actor": f"Bot{index + 1}",
        "player_name": names[index],
        "class": "Botpack.Bot",
    } for index in range(bot_count)]
    summary["config"]["bot_count"] = bot_count
    summary_path.write_text(json.dumps(summary) + "\n", encoding="utf-8")
    return run


def upgrade_telemetry_v2(run: Path, *, counters: list[dict]) -> None:
    path = run / "events.jsonl"
    events = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    tick_events = [event for event in events if event["type"] != "run_result"]
    if len(counters) != len(tick_events):
        raise AssertionError("one v2 counter sample is required for run_start and every tick")
    for index, event in enumerate(events):
        event["schema"] = QUALITY.TELEMETRY_SCHEMA_V2
        sample = counters[min(index, len(counters) - 1)]
        for bot in event["bots"]:
            bot.update({
                "score": sample["score"],
                "pri_deaths": sample["pri_deaths"],
                "movement_intent": sample["movement_intent"],
                "in_hazard_zone": sample["in_hazard_zone"],
                "kills_exact": str(sample["kills_exact"]),
                "deaths_exact": str(sample["deaths_exact"]),
                "suicides_exact": str(sample["suicides_exact"]),
                "environmental_deaths_exact": str(sample["environmental_deaths_exact"]),
                "hazard_exposed_deaths_proxy": str(sample["hazard_exposed_deaths_proxy"]),
                "hit_wall_events_exact": str(sample["hit_wall_events_exact"]),
            })
    path.write_text(
        "".join(json.dumps(event, separators=(",", ":")) + "\n" for event in events),
        encoding="utf-8")


class BotQualityAnalysisTests(unittest.TestCase):
    def test_v2_exact_outcomes_and_intent_proxies_are_computed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = write_run(Path(temporary), "run", [0.0, 0.0, 0.0, 1.0])
            counters = [
                {"score": 0, "pri_deaths": 0, "movement_intent": True,
                 "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
                 "suicides_exact": 0, "environmental_deaths_exact": 0,
                 "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0},
                {"score": 1, "pri_deaths": 0, "movement_intent": True,
                 "in_hazard_zone": True, "kills_exact": 1, "deaths_exact": 0,
                 "suicides_exact": 0, "environmental_deaths_exact": 0,
                 "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 2},
                {"score": 0, "pri_deaths": 2, "movement_intent": True,
                 "in_hazard_zone": True, "kills_exact": 1, "deaths_exact": 2,
                 "suicides_exact": 1, "environmental_deaths_exact": 1,
                 "hazard_exposed_deaths_proxy": 1, "hit_wall_events_exact": 4},
                {"score": 0, "pri_deaths": 3, "movement_intent": False,
                 "in_hazard_zone": False, "kills_exact": 2, "deaths_exact": 3,
                 "suicides_exact": 2, "environmental_deaths_exact": 1,
                 "hazard_exposed_deaths_proxy": 1, "hit_wall_events_exact": 5},
            ]
            upgrade_telemetry_v2(run, counters=counters)
            metrics = QUALITY.analyze_run(run)["metrics"]
            self.assertEqual(metrics["kills_exact"], 2)
            self.assertEqual(metrics["deaths_exact"], 3)
            self.assertEqual(metrics["suicides_exact"], 2)
            self.assertEqual(metrics["environmental_deaths_exact"], 1)
            self.assertEqual(metrics["suicide_to_kill_ratio"], 1.0)
            self.assertEqual(metrics["hit_wall_events_exact"], 5)
            self.assertEqual(metrics["movement_intent_no_progress_seconds_proxy"], 2.0)
            self.assertEqual(metrics["movement_intent_stuck_events_proxy"], 1)
            self.assertEqual(metrics["hazard_exposure_seconds"], 2.0)
            self.assertEqual(metrics["hazard_entries"], 1)
            self.assertEqual(metrics["hazard_exposed_deaths_proxy"], 1)
            self.assertEqual(metrics["hazard_exposed_death_fraction_proxy"], 1.0 / 3.0)

    def test_supported_metrics_are_computed_from_samples(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = write_run(Path(temporary), "run", [0.0, 3.0, 3.0, 3.0], [100, 90, 90, 90])
            report = QUALITY.analyze([run])
            metrics = report["runs"][0]["metrics"]
            self.assertEqual(metrics["distance_traveled"], 3.0)
            self.assertEqual(metrics["active_movement_seconds"], 1.0)
            self.assertEqual(metrics["active_movement_fraction"], 1.0 / 3.0)
            self.assertEqual(metrics["no_progress_seconds_proxy"], 2.0)
            self.assertEqual(metrics["longest_no_progress_seconds_proxy"], 2.0)
            self.assertEqual(metrics["stuck_events_proxy"], 1)
            self.assertEqual(metrics["health_loss_observed"], 10.0)
            self.assertEqual(metrics["minimum_health_observed"], 90)
            self.assertIs(metrics["survived_to_final_sample"], True)
            self.assertIs(metrics["completion"], True)
            self.assertIsNone(report["metric_availability"]["composite_quality_score"])
            self.assertIn("accuracy", report["metric_availability"]["unavailable_until_telemetry_is_extended"])

    def test_malformed_sequence_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = write_run(Path(temporary), "run", [0.0, 1.0, 2.0])
            path = run / "events.jsonl"
            events = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            events[1]["seq"] = "9"
            path.write_text("".join(json.dumps(event) + "\n" for event in events), encoding="utf-8")
            with self.assertRaisesRegex(QUALITY.QualityError, "sequence"):
                QUALITY.analyze([run])

    def test_v2_regressing_exact_counter_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = write_run(Path(temporary), "run", [0.0, 1.0])
            base = {"score": 0, "pri_deaths": 0, "movement_intent": False,
                    "in_hazard_zone": False, "kills_exact": 1, "deaths_exact": 0,
                    "suicides_exact": 0, "environmental_deaths_exact": 0,
                    "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0}
            upgrade_telemetry_v2(run, counters=[base, {**base, "kills_exact": 0}])
            with self.assertRaisesRegex(QUALITY.QualityError, "kills_exact regressed"):
                QUALITY.analyze_run(run)

    def test_explicit_comparable_pair_is_aggregated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = write_run(root, "baseline", [0.0, 0.0, 0.0, 0.0], metadata={
                "variant": "stock", "pair_id": "case-1", "comparison_role": "baseline",
            })
            candidate = write_run(root, "candidate", [0.0, 2.0, 4.0, 6.0], metadata={
                "variant": "new-ai", "pair_id": "case-1", "comparison_role": "candidate",
            })
            comparison = QUALITY.analyze([baseline, candidate])["paired_comparisons"][0]
            self.assertEqual(comparison["pair_count"], 1)
            distance = comparison["metrics"]["distance_traveled"]
            self.assertIsNone(distance["preferred_direction"])
            self.assertEqual(distance["candidate_minus_baseline"]["mean"], 6.0)
            stuck = comparison["metrics"]["stuck_events_proxy"]
            self.assertEqual(stuck["candidate_wins"], 1)
            self.assertEqual(stuck["candidate_losses"], 0)

    def test_incomplete_or_incomparable_pairs_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = write_run(root, "baseline", [0.0, 0.0, 0.0], metadata={
                "variant": "stock", "pair_id": "case-1", "comparison_role": "baseline",
            })
            with self.assertRaisesRegex(QUALITY.QualityError, "exactly one baseline"):
                QUALITY.analyze([baseline])
            candidate = write_run(root, "candidate", [0.0, 1.0, 2.0], seed=271828, metadata={
                "variant": "new-ai", "pair_id": "case-1", "comparison_role": "candidate",
            })
            with self.assertRaisesRegex(QUALITY.QualityError, "incomparable"):
                QUALITY.analyze([baseline, candidate])

    def test_v2_rosters_are_reconciled(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = write_v2_run(Path(temporary), "v2")
            analyzed = QUALITY.analyze_run(run)
            self.assertEqual(analyzed["config"]["initial_bot_count"], 2)
            self.assertEqual(
                [entry["requested_name"] for entry in analyzed["config"]["requested_roster"]],
                ["Alpha", "Bravo"])
            self.assertEqual(len(analyzed["result"]["actual_roster"]), 2)

    def test_v2_adversarial_rosters_are_rejected(self) -> None:
        mutations = {
            "indexes": ("manifest.json", lambda document: document["requested_roster"][1].update(
                roster_index=0), "indexes"),
            "requested mismatch": ("summary.json", lambda document: document["requested_roster"][1].update(
                external_skill=5,
                identity_fragment="participant-v1:index=1;external_skill=5;requested_name_hex=427261766f"),
                "requested_roster differs"),
            "actual count": ("summary.json", lambda document: document["actual_roster"].pop(),
                             "count must equal bot_count"),
            "duplicate actor": ("summary.json", lambda document: document["actual_roster"][1].update(
                actor="Bot1"), "actor values must be unique"),
            "duplicate identity": ("summary.json", lambda document: document["actual_roster"][1].update(
                identity="pri:1"), "identity values must be unique"),
            "duplicate player name": ("summary.json", lambda document: document["actual_roster"][1].update(
                player_name="alpha"), "player names must be unique"),
            "noncanonical fragment": ("manifest.json", lambda document: document["requested_roster"][1].update(
                identity_fragment="not-canonical"), "identity_fragment is not canonical"),
            "unknown event identity": ("events.jsonl", None, "absent from actual_roster"),
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index, (label, (filename, mutate, message)) in enumerate(mutations.items()):
                with self.subTest(label=label):
                    run = write_v2_run(root, f"v2-{index}")
                    path = run / filename
                    if filename == "events.jsonl":
                        events = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
                        events[0]["bots"][0]["identity"] = "pri:0"
                        path.write_text("".join(json.dumps(event) + "\n" for event in events), encoding="utf-8")
                    else:
                        document = json.loads(path.read_text(encoding="utf-8"))
                        assert mutate is not None
                        mutate(document)
                        path.write_text(json.dumps(document) + "\n", encoding="utf-8")
                    with self.assertRaisesRegex(QUALITY.QualityError, message):
                        QUALITY.analyze_run(run)


if __name__ == "__main__":
    unittest.main()
