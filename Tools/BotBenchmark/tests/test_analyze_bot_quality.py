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
            for name in QUALITY.OPTIONAL_EXACT_COUNTERS:
                if name in sample:
                    bot[name] = str(sample[name])
            for name in QUALITY.OPTIONAL_CUMULATIVE_NUMBERS:
                if name in sample:
                    bot[name] = sample[name]
            for name in QUALITY.OPTIONAL_DIAGNOSTIC_FIELDS:
                if name in sample:
                    bot[name] = sample[name]
    path.write_text(
        "".join(json.dumps(event, separators=(",", ":")) + "\n" for event in events),
        encoding="utf-8")


def declare_death_attribution(run: Path) -> None:
    path = run / "manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["death_attribution_recent_window_seconds"] = 2.0
    manifest["suicides_exact_semantics"] = "legacy_scoreboard_self_or_nonplayer_killer"
    path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")


class BotQualityAnalysisTests(unittest.TestCase):
    def test_falling_seam_shadow_counters_are_complete_monotonic_and_reported(self) -> None:
        common = {
            "score": 0, "pri_deaths": 0, "movement_intent": True,
            "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
            "suicides_exact": 0, "environmental_deaths_exact": 0,
            "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0,
        }
        samples = [
            {**common, "falling_seam_detections_exact": 0,
             "horizontal_corner_candidate_probes_exact": 0,
             "horizontal_corner_authorized_escapes_exact": 0,
             "horizontal_corner_target_progress_rejects_exact": 0,
             "horizontal_corner_unknown_or_unsafe_support_exact": 0},
            {**common, "falling_seam_detections_exact": 2,
             "horizontal_corner_candidate_probes_exact": 4,
             "horizontal_corner_authorized_escapes_exact": 1,
             "horizontal_corner_target_progress_rejects_exact": 2,
             "horizontal_corner_unknown_or_unsafe_support_exact": 1},
            {**common, "falling_seam_detections_exact": 3,
             "horizontal_corner_candidate_probes_exact": 6,
             "horizontal_corner_authorized_escapes_exact": 2,
             "horizontal_corner_target_progress_rejects_exact": 2,
             "horizontal_corner_unknown_or_unsafe_support_exact": 2},
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run = write_v2_run(root, "valid")
            upgrade_telemetry_v2(run, counters=samples)
            report = QUALITY.analyze([run])

            self.assertTrue(
                report["metric_availability"]["falling_seam_shadow_metrics_present"])
            per_bot = report["runs"][0]["bots"][0]
            self.assertEqual(
                {name: per_bot[name] for name in QUALITY.FALLING_SEAM_SHADOW_COUNTERS},
                {
                    "falling_seam_detections_exact": 3,
                    "horizontal_corner_candidate_probes_exact": 6,
                    "horizontal_corner_authorized_escapes_exact": 2,
                    "horizontal_corner_target_progress_rejects_exact": 2,
                    "horizontal_corner_unknown_or_unsafe_support_exact": 2,
                })
            metrics = report["runs"][0]["metrics"]
            self.assertEqual(metrics["falling_seam_detections_exact"], 6)
            self.assertEqual(metrics["horizontal_corner_candidate_probes_exact"], 12)
            self.assertEqual(metrics["horizontal_corner_authorized_escapes_exact"], 4)
            aggregate = report["variant_aggregates"][0]["metrics"]
            self.assertEqual(aggregate["falling_seam_detections_exact"]["mean"], 6.0)
            self.assertEqual(
                aggregate["horizontal_corner_authorized_escapes_exact"]["mean"], 4.0)

            incomplete = write_v2_run(root, "incomplete")
            incomplete_samples = [{**sample} for sample in samples]
            for sample in incomplete_samples:
                sample.pop("horizontal_corner_unknown_or_unsafe_support_exact")
            upgrade_telemetry_v2(incomplete, counters=incomplete_samples)
            with self.assertRaisesRegex(QUALITY.QualityError, "falling seam shadow.*complete group"):
                QUALITY.analyze([incomplete])

            regressed = write_v2_run(root, "regressed")
            regressed_samples = [{**sample} for sample in samples]
            regressed_samples[2]["horizontal_corner_authorized_escapes_exact"] = 0
            regressed_samples[2]["horizontal_corner_target_progress_rejects_exact"] = 4
            upgrade_telemetry_v2(regressed, counters=regressed_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "horizontal_corner_authorized_escapes_exact regressed"):
                QUALITY.analyze([regressed])

            bad_subset = write_v2_run(root, "bad-subset")
            bad_subset_samples = [{**sample} for sample in samples]
            bad_subset_samples[1]["horizontal_corner_target_progress_rejects_exact"] = 1
            upgrade_telemetry_v2(bad_subset, counters=bad_subset_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "classifications do not partition candidate probes"):
                QUALITY.analyze([bad_subset])

            candidates_without_detection = write_v2_run(root, "candidates-without-detection")
            candidates_without_detection_samples = [{**sample} for sample in samples]
            candidates_without_detection_samples[1]["falling_seam_detections_exact"] = 0
            upgrade_telemetry_v2(
                candidates_without_detection, counters=candidates_without_detection_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "candidates exceed three per falling seam detection"):
                QUALITY.analyze([candidates_without_detection])

            too_many_authorized = write_v2_run(root, "too-many-authorized")
            too_many_authorized_samples = [{**sample} for sample in samples]
            too_many_authorized_samples[1]["horizontal_corner_authorized_escapes_exact"] = 3
            too_many_authorized_samples[1]["horizontal_corner_target_progress_rejects_exact"] = 0
            upgrade_telemetry_v2(
                too_many_authorized, counters=too_many_authorized_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "authorized escapes exceed falling seam detections"):
                QUALITY.analyze([too_many_authorized])

    def test_older_telemetry_remains_valid_without_falling_seam_shadow_group(self) -> None:
        common = {
            "score": 0, "pri_deaths": 0, "movement_intent": False,
            "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
            "suicides_exact": 0, "environmental_deaths_exact": 0,
            "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0,
        }
        with tempfile.TemporaryDirectory() as temporary:
            run = write_v2_run(Path(temporary), "older-v2")
            upgrade_telemetry_v2(run, counters=[common, common, common])
            report = QUALITY.analyze([run])
            self.assertFalse(
                report["metric_availability"]["falling_seam_shadow_metrics_present"])
            self.assertTrue(all(
                report["runs"][0]["metrics"][name] is None
                for name in QUALITY.FALLING_SEAM_SHADOW_COUNTERS))

    def test_walking_step_preflight_shadow_is_complete_partitioned_and_debounced(self) -> None:
        common = {
            "score": 0, "pri_deaths": 0, "movement_intent": True,
            "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
            "suicides_exact": 0, "environmental_deaths_exact": 0,
            "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0,
        }

        def sample(observations: int, authorizations: int, episodes: int) -> dict:
            reasons = {name: 0 for name in QUALITY.WALKING_STEP_PREFLIGHT_REASON_COUNTERS}
            reasons["walking_step_preflight_reason_supported_step_endpoint_exact"] = \
                observations - authorizations
            reasons["walking_step_preflight_reason_harmful_pain_fall_exact"] = authorizations
            return {
                **common,
                "walking_step_preflight_observations_exact": observations,
                "walking_step_preflight_unsupported_endpoints_exact": authorizations + 1
                    if observations else 0,
                "walking_step_preflight_no_decisions_exact": observations - authorizations,
                "walking_step_preflight_provisional_authorizations_exact": authorizations,
                "walking_step_preflight_post_mayfall_confirmed_authorizations_exact": authorizations,
                "walking_step_preflight_authorizable_episodes_exact": episodes,
                "walking_step_preflight_diagnostic_overflows_exact": 0,
                **reasons,
            }

        samples = [sample(0, 0, 0), sample(10, 2, 1), sample(20, 5, 2)]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run = write_v2_run(root, "walking-preflight")
            upgrade_telemetry_v2(run, counters=samples)
            report = QUALITY.analyze([run])
            self.assertTrue(report["metric_availability"]
                            ["walking_step_preflight_shadow_metrics_present"])
            self.assertEqual(report["runs"][0]["bots"][0]
                             ["walking_step_preflight_authorizable_episodes_exact"], 2)

            incomplete = write_v2_run(root, "walking-preflight-incomplete")
            incomplete_samples = [{**item} for item in samples]
            for item in incomplete_samples:
                item.pop("walking_step_preflight_reason_unknown_gravity_exact")
            upgrade_telemetry_v2(incomplete, counters=incomplete_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "walking step preflight shadow.*complete group"):
                QUALITY.analyze([incomplete])

            bad_partition = write_v2_run(root, "walking-preflight-bad-partition")
            bad_samples = [{**item} for item in samples]
            bad_samples[2]["walking_step_preflight_reason_supported_step_endpoint_exact"] -= 1
            upgrade_telemetry_v2(bad_partition, counters=bad_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "reasons do not partition observations"):
                QUALITY.analyze([bad_partition])

            excess_episode = write_v2_run(root, "walking-preflight-excess-episode")
            excess_samples = [{**item} for item in samples]
            excess_samples[2]["walking_step_preflight_authorizable_episodes_exact"] = 6
            upgrade_telemetry_v2(excess_episode, counters=excess_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "authorizable episodes exceed authorizations"):
                QUALITY.analyze([excess_episode])

    def test_walking_step_preflight_diagnostics_are_strict_correlated_and_bounded(self) -> None:
        vector = {"x": 0.0, "y": 0.0, "z": 0.0}

        def probe(collision: str = "unknown", fraction: float = 1.0) -> dict:
            return {
                "collision": collision, "fraction": fraction,
                "delta": {**vector}, "normal": {**vector},
            }

        provisional = {
            "source_pawn_actor": "Bot1", "sequence": "0", "life_generation": "1",
            "invocation_token": "5", "walking_iteration": 0,
            "phase": "precommit_provisional", "transition_outcome": "",
            "reason": "walking_step_preflight_reason_harmful_pain_fall_exact",
            "origin": {"x": 10.0, "y": 20.0, "z": 30.0},
            "predicted_unsupported_endpoint": {"x": 15.0, "y": 20.0, "z": 30.0},
            "actual_unsupported_endpoint": {**vector}, "semantic_target": "BulletBox4",
            "semantic_destination": {"x": 100.0, "y": 200.0, "z": 30.0},
            "start_support": probe("static_bsp", 0.0), "step_up": probe("clear"),
            "forward": probe("clear"), "actual_step_down": probe("clear"),
            "support_probe": probe("clear"),
            "fall_forecast": {
                "attempted": True, "origin": {"x": 15.0, "y": 20.0, "z": 30.0},
                "velocity": {"x": 100.0, "y": 0.0, "z": 0.0},
                "acceleration": {**vector}, "gravity_known": True,
                "gravity": {"x": 0.0, "y": 0.0, "z": -950.0}, "complete": True,
                "total_drop": 128.0, "continuation_count": 0,
                "landing_collision": "static_bsp",
                "landing_normal": {"x": 0.0, "y": 0.0, "z": 1.0},
                "landing_zone": "pain", "hit_fractions": [0.5],
            },
        }
        confirmation = json.loads(json.dumps(provisional))
        confirmation.update({
            "sequence": "1", "phase": "post_mayfall_confirmation",
            "transition_outcome": "begin_falling",
            "actual_unsupported_endpoint": {"x": 15.0, "y": 20.0, "z": 30.0},
        })
        rejected_confirmation = json.loads(json.dumps(confirmation))
        rejected_confirmation.update({
            "transition_outcome": "post_callback_forecast_rejected",
            "reason": "walking_step_preflight_reason_safe_fall_landing_exact",
        })
        rejected_confirmation["fall_forecast"]["landing_zone"] = "safe"
        common = {
            "score": 0, "pri_deaths": 0, "movement_intent": True,
            "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
            "suicides_exact": 0, "environmental_deaths_exact": 0,
            "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0,
        }

        def sample(observations: int, diagnostics: list[dict]) -> dict:
            reasons = {name: 0 for name in QUALITY.WALKING_STEP_PREFLIGHT_REASON_COUNTERS}
            authorizations = 1 if observations else 0
            reasons["walking_step_preflight_reason_harmful_pain_fall_exact"] = authorizations
            return {
                **common, "walking_step_preflight_observations_exact": observations,
                "walking_step_preflight_unsupported_endpoints_exact": authorizations,
                "walking_step_preflight_no_decisions_exact": 0,
                "walking_step_preflight_provisional_authorizations_exact": authorizations,
                "walking_step_preflight_post_mayfall_confirmed_authorizations_exact":
                    authorizations,
                "walking_step_preflight_authorizable_episodes_exact": authorizations,
                "walking_step_preflight_diagnostic_overflows_exact": 0,
                "walking_step_preflight_diagnostics": diagnostics, **reasons,
            }

        valid_samples = [sample(0, []), sample(1, [provisional, confirmation]), sample(1, [])]

        def analyze_mutation(root: Path, name: str, mutate) -> None:
            samples = json.loads(json.dumps(valid_samples))
            mutate(samples)
            run = write_v2_run(root, name)
            upgrade_telemetry_v2(run, counters=samples)
            QUALITY.analyze([run])

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            analyze_mutation(root, "valid-diagnostics", lambda samples: None)

            def use_rejected_confirmation(samples: list[dict]) -> None:
                samples[1]["walking_step_preflight_diagnostics"][1] = \
                    json.loads(json.dumps(rejected_confirmation))
                for sample_item in samples[1:]:
                    sample_item[
                        "walking_step_preflight_post_mayfall_confirmed_authorizations_exact"] = 0
                    sample_item["walking_step_preflight_authorizable_episodes_exact"] = 0

            analyze_mutation(root, "valid-rejected-confirmation", use_rejected_confirmation)

            malformed_cases = (
                ("not-array", lambda samples: samples[1].update(
                    walking_step_preflight_diagnostics={}), "must be an array"),
                ("unexpected-field", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(extra=1),
                    "unexpected fields"),
                ("nonfinite-normal", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0]["support_probe"]["normal"]
                    .update(z="nan"), "must be a finite number"),
                ("bad-fraction", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0]["fall_forecast"]
                    ["hit_fractions"].__setitem__(0, 1.5), "must be at most 1.0"),
                ("bad-phase", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(phase="precommit"),
                    "phase is not recognized"),
                ("bad-transition", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][1]
                    .update(transition_outcome="fell"), "transition_outcome is not recognized"),
                ("bad-reason", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(reason="harmful"),
                    "reason is not recognized"),
                ("non-authorizing-provisional", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(
                        reason="walking_step_preflight_reason_safe_fall_landing_exact"),
                    "does not follow an authorization"),
                ("accepted-with-rejected-reason", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][1].update(
                        reason="walking_step_preflight_reason_safe_fall_landing_exact"),
                    "must preserve the provisional authorization reason"),
                ("rejected-with-authorizing-reason", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][1].update(
                        transition_outcome="post_callback_forecast_rejected"),
                    "must identify the rejected post-callback forecast"),
                ("bad-collision", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0]["forward"]
                    .update(collision="world"), "collision is not recognized"),
                ("negative-sequence", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(sequence="-1"),
                    "must be at least 0"),
                ("negative-life", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(life_generation="-1"),
                    "must be at least 0"),
                ("negative-invocation", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(invocation_token="-1"),
                    "must be at least 0"),
                ("negative-iteration", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][0].update(walking_iteration=-1),
                    "must be at least 0"),
                ("sequence-regression", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][1].update(sequence="0"),
                    "sequence is not strictly increasing"),
                ("uncorrelated", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][1]["semantic_destination"]
                    .update(x=101.0), "do not correlate"),
                ("orphan-post", lambda samples: samples[1].update(
                    walking_step_preflight_diagnostics=[confirmation]),
                    "no correlatable provisional"),
                ("unattempted-results", lambda samples: samples[1]
                    ["walking_step_preflight_diagnostics"][1]["fall_forecast"]
                    .update(attempted=False), "complete forecast must have been attempted"),
            )
            for name, mutate, message in malformed_cases:
                with self.subTest(name=name):
                    with self.assertRaisesRegex(QUALITY.QualityError, message):
                        analyze_mutation(root, name, mutate)

            with self.assertRaisesRegex(QUALITY.QualityError, "exceed observation evidence"):
                def add_excess(samples: list[dict]) -> None:
                    extra = json.loads(json.dumps(provisional))
                    extra.update({
                        "sequence": "2", "invocation_token": "6",
                        "reason": "walking_step_preflight_reason_supported_step_endpoint_exact",
                    })
                    samples[1]["walking_step_preflight_diagnostics"].append(extra)
                analyze_mutation(root, "excess-evidence", add_excess)

            incomplete = write_v2_run(root, "diagnostics-without-counters")
            upgrade_telemetry_v2(incomplete, counters=[
                {**common, "walking_step_preflight_diagnostics": []} for _ in range(3)])
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "diagnostics require the complete counter group"):
                QUALITY.analyze([incomplete])

    def test_detailed_falling_seam_episode_and_candidate_outcomes_fail_closed(self) -> None:
        common = {
            "score": 0, "pri_deaths": 0, "movement_intent": True,
            "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
            "suicides_exact": 0, "environmental_deaths_exact": 0,
            "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0,
        }
        samples = [
            {**common,
             "falling_seam_detections_exact": 0,
             "horizontal_corner_candidate_probes_exact": 0,
             "horizontal_corner_authorized_escapes_exact": 0,
             "horizontal_corner_target_progress_rejects_exact": 0,
             "horizontal_corner_unknown_or_unsafe_support_exact": 0,
             **{name: 0 for name in QUALITY.FALLING_SEAM_DETAILED_COUNTERS}},
            {**common,
             "falling_seam_detections_exact": 2,
             "horizontal_corner_candidate_probes_exact": 2,
             "horizontal_corner_authorized_escapes_exact": 0,
             "horizontal_corner_target_progress_rejects_exact": 1,
             "horizontal_corner_unknown_or_unsafe_support_exact": 1,
             "falling_seam_episodes_exact": 1,
             "falling_seam_invalid_geometry_rejects_exact": 1,
             "falling_seam_authorizable_episodes_exact": 0,
             "horizontal_corner_authorized_candidates_exact": 0,
             "horizontal_corner_blocked_sweep_candidates_exact": 1,
             "horizontal_corner_no_static_walkable_support_candidates_exact": 0,
             "horizontal_corner_pain_support_candidates_exact": 0,
             "horizontal_corner_no_active_movement_intent_or_target_candidates_exact": 1,
             "horizontal_corner_true_target_regression_candidates_exact": 0,
             "horizontal_corner_unknown_evidence_candidates_exact": 0},
            {**common,
             "falling_seam_detections_exact": 4,
             "horizontal_corner_candidate_probes_exact": 5,
             "horizontal_corner_authorized_escapes_exact": 1,
             "horizontal_corner_target_progress_rejects_exact": 2,
             "horizontal_corner_unknown_or_unsafe_support_exact": 2,
             "falling_seam_episodes_exact": 2,
             "falling_seam_invalid_geometry_rejects_exact": 1,
             "falling_seam_authorizable_episodes_exact": 1,
             "horizontal_corner_authorized_candidates_exact": 1,
             "horizontal_corner_blocked_sweep_candidates_exact": 1,
             "horizontal_corner_no_static_walkable_support_candidates_exact": 1,
             "horizontal_corner_pain_support_candidates_exact": 0,
             "horizontal_corner_no_active_movement_intent_or_target_candidates_exact": 1,
             "horizontal_corner_true_target_regression_candidates_exact": 1,
             "horizontal_corner_unknown_evidence_candidates_exact": 0},
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run = write_v2_run(root, "detailed")
            upgrade_telemetry_v2(run, counters=samples)
            report = QUALITY.analyze([run])
            self.assertTrue(
                report["metric_availability"]["falling_seam_detailed_metrics_present"])
            self.assertEqual(
                report["runs"][0]["bots"][0]["falling_seam_episodes_exact"], 2)

            incomplete = write_v2_run(root, "detailed-incomplete")
            incomplete_samples = [{**sample} for sample in samples]
            for sample in incomplete_samples:
                sample.pop("horizontal_corner_unknown_evidence_candidates_exact")
            upgrade_telemetry_v2(incomplete, counters=incomplete_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "detailed v2.*complete group"):
                QUALITY.analyze([incomplete])

            bad_partition = write_v2_run(root, "detailed-bad-partition")
            bad_samples = [{**sample} for sample in samples]
            bad_samples[2]["horizontal_corner_true_target_regression_candidates_exact"] = 0
            upgrade_telemetry_v2(bad_partition, counters=bad_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "detailed horizontal corner outcomes do not partition"):
                QUALITY.analyze([bad_partition])

            missing_episode = write_v2_run(root, "detailed-missing-episode")
            missing_episode_samples = [{**sample} for sample in samples]
            missing_episode_samples[1]["falling_seam_episodes_exact"] = 0
            upgrade_telemetry_v2(missing_episode, counters=missing_episode_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "detections require at least one episode"):
                QUALITY.analyze([missing_episode])

            missing_authorizable_episode = write_v2_run(
                root, "detailed-missing-authorizable-episode")
            missing_authorizable_samples = [{**sample} for sample in samples]
            missing_authorizable_samples[2]["falling_seam_authorizable_episodes_exact"] = 0
            upgrade_telemetry_v2(
                missing_authorizable_episode, counters=missing_authorizable_samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "require an authorizable episode"):
                QUALITY.analyze([missing_authorizable_episode])

    def test_causal_death_attribution_is_partitioned_monotonic_and_reported(self) -> None:
        common = {
            "score": 0, "pri_deaths": 0, "movement_intent": True,
            "in_hazard_zone": False, "kills_exact": 0, "suicides_exact": 0,
            "environmental_deaths_exact": 0, "hazard_exposed_deaths_proxy": 0,
            "hit_wall_events_exact": 0,
        }
        samples = [
            {**common, "deaths_exact": 0, "direct_self_kills": 0,
             "direct_enemy_kills": 0, "unassisted_environmental_deaths": 0,
             "recent_enemy_contributed_environmental_deaths_proxy": 0,
             "ambiguous_deaths": 0,
             "recent_enemy_momentum_contributed_environmental_deaths_proxy": 0},
            {**common, "deaths_exact": 1, "direct_self_kills": 0,
             "direct_enemy_kills": 1, "unassisted_environmental_deaths": 0,
             "recent_enemy_contributed_environmental_deaths_proxy": 0,
             "ambiguous_deaths": 0,
             "recent_enemy_momentum_contributed_environmental_deaths_proxy": 0},
            {**common, "deaths_exact": 2, "direct_self_kills": 0,
             "direct_enemy_kills": 1, "unassisted_environmental_deaths": 0,
             "recent_enemy_contributed_environmental_deaths_proxy": 1,
             "ambiguous_deaths": 0,
             "recent_enemy_momentum_contributed_environmental_deaths_proxy": 1},
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run = write_v2_run(root, "valid")
            declare_death_attribution(run)
            upgrade_telemetry_v2(run, counters=samples)
            report = QUALITY.analyze([run])
            self.assertTrue(report["metric_availability"]["death_attribution_metrics_present"])
            self.assertEqual(
                report["runs"][0]["config"]["death_attribution_recent_window_seconds"], 2.0)
            self.assertEqual(
                report["runs"][0]["config"]["suicides_exact_semantics"],
                "legacy_scoreboard_self_or_nonplayer_killer")
            metrics = report["runs"][0]["metrics"]
            self.assertEqual({name: metrics[name] for name in QUALITY.DEATH_ATTRIBUTION_COUNTERS}, {
                "direct_self_kills": 0,
                "direct_enemy_kills": 2,
                "unassisted_environmental_deaths": 0,
                "recent_enemy_contributed_environmental_deaths_proxy": 2,
                "ambiguous_deaths": 0,
                "recent_enemy_momentum_contributed_environmental_deaths_proxy": 2,
            })

            incomplete = write_v2_run(root, "incomplete")
            declare_death_attribution(incomplete)
            incomplete_samples = [{**sample} for sample in samples]
            for sample in incomplete_samples:
                sample.pop("ambiguous_deaths")
            upgrade_telemetry_v2(incomplete, counters=incomplete_samples)
            with self.assertRaisesRegex(QUALITY.QualityError, "complete group"):
                QUALITY.analyze([incomplete])

            mismatched = write_v2_run(root, "mismatched")
            declare_death_attribution(mismatched)
            mismatch_samples = [{**sample} for sample in samples]
            mismatch_samples[-1]["ambiguous_deaths"] = 1
            upgrade_telemetry_v2(mismatched, counters=mismatch_samples)
            with self.assertRaisesRegex(QUALITY.QualityError, "do not equal deaths_exact"):
                QUALITY.analyze([mismatched])

            bad_momentum = write_v2_run(root, "bad-momentum")
            declare_death_attribution(bad_momentum)
            bad_momentum_samples = [{**sample} for sample in samples]
            bad_momentum_samples[1][
                "recent_enemy_momentum_contributed_environmental_deaths_proxy"] = 1
            upgrade_telemetry_v2(bad_momentum, counters=bad_momentum_samples)
            with self.assertRaisesRegex(QUALITY.QualityError, "momentum-contributed deaths exceed"):
                QUALITY.analyze([bad_momentum])

            regressed = write_v2_run(root, "regressed")
            declare_death_attribution(regressed)
            regressed_samples = [{**sample} for sample in samples]
            regressed_samples[-1]["direct_enemy_kills"] = 0
            regressed_samples[-1]["recent_enemy_contributed_environmental_deaths_proxy"] = 2
            regressed_samples[-1][
                "recent_enemy_momentum_contributed_environmental_deaths_proxy"] = 1
            upgrade_telemetry_v2(regressed, counters=regressed_samples)
            with self.assertRaisesRegex(QUALITY.QualityError, "direct_enemy_kills regressed"):
                QUALITY.analyze([regressed])

            undeclared = write_v2_run(root, "undeclared")
            upgrade_telemetry_v2(undeclared, counters=samples)
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "requires manifest.death_attribution_recent_window_seconds"):
                QUALITY.analyze([undeclared])

    def test_optional_diagnostics_and_recovery_counters_are_validated_and_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = write_run(Path(temporary), "run", [0.0, 0.0, 2.0, 3.0])
            common = {
                "score": 0, "pri_deaths": 0, "movement_intent": True,
                "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
                "suicides_exact": 0, "environmental_deaths_exact": 0,
                "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0,
                "physics_mode": "Walking", "latent_action": "MoveToward",
                "acceleration": {"x": 100.0, "y": -25.0, "z": 0.0},
                "destination": {"x": 512.0, "y": 256.0, "z": -32.0},
                "move_timer": 0.75, "move_target_identity": "actor:PathNode3",
                "move_target_name": "PathNode3",
            }
            optional_samples = [
                {"pain_ledge_vetoes_exact": 0, "pain_ledge_repeat_vetoes_exact": 0,
                 "pain_ledge_recovery_attempts_exact": 0, "pain_ledge_recovery_escapes_exact": 0,
                 "wall_adjust_calls_exact": 0, "wall_adjust_repeats_exact": 0,
                 "wall_adjust_recovery_attempts_exact": 0,
                 "wall_adjust_recovery_successes_exact": 0,
                 "wall_adjust_forced_replans_exact": 0,
                 "move_stall_detections_exact": 0, "move_stall_episode_resets_exact": 0,
                 "move_stall_forced_replans_exact": 0,
                 "move_stall_navigation_forced_replans_exact": 0,
                 "move_stall_targetless_move_to_timeouts_exact": 0,
                 "move_stall_eligible_seconds": 0.0,
                 "failed_navigation_avoidance_activations_exact": 0,
                 "failed_navigation_safeguard_suppressions_exact": 0,
                 "failed_navigation_route_penalty_applications_exact": 0},
                {"pain_ledge_vetoes_exact": 2, "pain_ledge_repeat_vetoes_exact": 1,
                 "pain_ledge_recovery_attempts_exact": 1, "pain_ledge_recovery_escapes_exact": 0,
                 "wall_adjust_calls_exact": 3, "wall_adjust_repeats_exact": 2,
                 "wall_adjust_recovery_attempts_exact": 1,
                 "wall_adjust_recovery_successes_exact": 0,
                 "wall_adjust_forced_replans_exact": 0,
                 "move_stall_detections_exact": 0, "move_stall_episode_resets_exact": 0,
                 "move_stall_forced_replans_exact": 0,
                 "move_stall_navigation_forced_replans_exact": 0,
                 "move_stall_targetless_move_to_timeouts_exact": 0,
                 "move_stall_eligible_seconds": 1.0,
                 "failed_navigation_avoidance_activations_exact": 0,
                 "failed_navigation_safeguard_suppressions_exact": 1,
                 "failed_navigation_route_penalty_applications_exact": 0},
                {"pain_ledge_vetoes_exact": 3, "pain_ledge_repeat_vetoes_exact": 1,
                 "pain_ledge_recovery_attempts_exact": 2, "pain_ledge_recovery_escapes_exact": 1,
                 "wall_adjust_calls_exact": 7, "wall_adjust_repeats_exact": 5,
                 "wall_adjust_recovery_attempts_exact": 2,
                 "wall_adjust_recovery_successes_exact": 1,
                 "wall_adjust_forced_replans_exact": 1,
                 "move_stall_detections_exact": 1, "move_stall_episode_resets_exact": 1,
                 "move_stall_forced_replans_exact": 0,
                 "move_stall_navigation_forced_replans_exact": 0,
                 "move_stall_targetless_move_to_timeouts_exact": 0,
                 "move_stall_eligible_seconds": 2.5,
                 "failed_navigation_avoidance_activations_exact": 1,
                 "failed_navigation_safeguard_suppressions_exact": 1,
                 "failed_navigation_route_penalty_applications_exact": 3},
                {"pain_ledge_vetoes_exact": 4, "pain_ledge_repeat_vetoes_exact": 1,
                 "pain_ledge_recovery_attempts_exact": 3, "pain_ledge_recovery_escapes_exact": 2,
                 "wall_adjust_calls_exact": 9, "wall_adjust_repeats_exact": 6,
                 "wall_adjust_recovery_attempts_exact": 3,
                 "wall_adjust_recovery_successes_exact": 2,
                 "wall_adjust_forced_replans_exact": 1,
                 "move_stall_detections_exact": 2, "move_stall_episode_resets_exact": 1,
                 "move_stall_forced_replans_exact": 1,
                 "move_stall_navigation_forced_replans_exact": 1,
                 "move_stall_targetless_move_to_timeouts_exact": 0,
                 "move_stall_eligible_seconds": 4.25,
                 "failed_navigation_avoidance_activations_exact": 1,
                 "failed_navigation_safeguard_suppressions_exact": 2,
                 "failed_navigation_route_penalty_applications_exact": 5},
            ]
            upgrade_telemetry_v2(
                run, counters=[{**common, **sample} for sample in optional_samples])
            analyzed = QUALITY.analyze([run])
            metrics = analyzed["runs"][0]["metrics"]
            self.assertEqual(metrics["pain_ledge_vetoes_exact"], 4)
            self.assertEqual(metrics["pain_ledge_recovery_escapes_exact"], 2)
            self.assertEqual(metrics["wall_adjust_calls_exact"], 9)
            self.assertEqual(metrics["wall_adjust_recovery_attempts_exact"], 3)
            self.assertEqual(metrics["wall_adjust_recovery_successes_exact"], 2)
            self.assertEqual(metrics["wall_adjust_forced_replans_exact"], 1)
            self.assertEqual(metrics["move_stall_detections_exact"], 2)
            self.assertEqual(metrics["move_stall_episode_resets_exact"], 1)
            self.assertEqual(metrics["move_stall_forced_replans_exact"], 1)
            self.assertEqual(metrics["move_stall_navigation_forced_replans_exact"], 1)
            self.assertEqual(metrics["move_stall_targetless_move_to_timeouts_exact"], 0)
            self.assertEqual(metrics["move_stall_eligible_seconds"], 4.25)
            self.assertEqual(metrics["failed_navigation_avoidance_activations_exact"], 1)
            self.assertEqual(metrics["failed_navigation_safeguard_suppressions_exact"], 2)
            self.assertEqual(metrics["failed_navigation_route_penalty_applications_exact"], 5)
            validation = analyzed["runs"][0]["validation"]
            self.assertIn("physics_mode", validation["optional_telemetry_fields"])
            self.assertIn("wall_adjust_recovery_successes_exact",
                          validation["optional_telemetry_fields"])
            self.assertIn("move_stall_eligible_seconds",
                          validation["optional_telemetry_fields"])
            self.assertIn("move_stall_navigation_forced_replans_exact",
                          validation["optional_telemetry_fields"])
            self.assertIn("failed_navigation_avoidance_activations_exact",
                          validation["optional_telemetry_fields"])
            self.assertIs(
                analyzed["metric_availability"]["optional_counter_metrics_present"]
                ["wall_adjust_recovery_successes_exact"], True)
            self.assertIs(
                analyzed["metric_availability"]["optional_counter_metrics_present"]
                ["move_stall_eligible_seconds"], True)
            self.assertIs(
                analyzed["metric_availability"]["optional_counter_metrics_present"]
                ["failed_navigation_route_penalty_applications_exact"], True)
            aggregate = analyzed["variant_aggregates"][0]["metrics"]
            self.assertEqual(aggregate["wall_adjust_recovery_successes_exact"]["mean"], 2.0)
            self.assertEqual(aggregate["move_stall_detections_exact"]["mean"], 2.0)
            self.assertEqual(
                aggregate["move_stall_navigation_forced_replans_exact"]["mean"], 1.0)
            self.assertEqual(aggregate["move_stall_eligible_seconds"]["mean"], 4.25)
            self.assertEqual(
                aggregate["failed_navigation_route_penalty_applications_exact"]["mean"], 5.0)

    def test_optional_counter_regression_and_malformed_diagnostics_are_rejected(self) -> None:
        base = {
            "score": 0, "pri_deaths": 0, "movement_intent": False,
            "in_hazard_zone": False, "kills_exact": 0, "deaths_exact": 0,
            "suicides_exact": 0, "environmental_deaths_exact": 0,
            "hazard_exposed_deaths_proxy": 0, "hit_wall_events_exact": 0,
            "pain_ledge_vetoes_exact": 0, "pain_ledge_repeat_vetoes_exact": 0,
            "pain_ledge_recovery_attempts_exact": 0, "pain_ledge_recovery_escapes_exact": 0,
            "wall_adjust_calls_exact": 2, "wall_adjust_repeats_exact": 1,
            "wall_adjust_recovery_attempts_exact": 1,
            "wall_adjust_recovery_successes_exact": 0,
            "wall_adjust_forced_replans_exact": 0,
            "physics_mode": "Walking", "latent_action": "MoveTo",
            "acceleration": {"x": 0.0, "y": 0.0, "z": 0.0},
            "destination": {"x": 1.0, "y": 2.0, "z": 3.0}, "move_timer": 1.0,
            "move_target_identity": "", "move_target_name": "",
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            regressing = write_run(root, "regressing", [0.0, 1.0])
            upgrade_telemetry_v2(
                regressing, counters=[base, {**base, "wall_adjust_calls_exact": 1}])
            with self.assertRaisesRegex(QUALITY.QualityError, "wall_adjust_calls_exact regressed"):
                QUALITY.analyze_run(regressing)

            malformed = write_run(root, "malformed", [0.0, 1.0])
            upgrade_telemetry_v2(malformed, counters=[base, base])
            path = malformed / "events.jsonl"
            events = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            events[1]["bots"][0]["move_timer"] = "nan"
            path.write_text(
                "".join(json.dumps(event) + "\n" for event in events), encoding="utf-8")
            with self.assertRaisesRegex(QUALITY.QualityError, "move_timer must be a finite number"):
                QUALITY.analyze_run(malformed)

            stall_base = {
                **base, "move_stall_detections_exact": 1,
                "move_stall_episode_resets_exact": 1,
                "move_stall_eligible_seconds": 2.0,
            }
            stall_regressing = write_run(root, "stall-regressing", [0.0, 1.0])
            upgrade_telemetry_v2(stall_regressing, counters=[
                stall_base, {**stall_base, "move_stall_detections_exact": 0}])
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "move_stall_detections_exact regressed"):
                QUALITY.analyze_run(stall_regressing)

            stall_seconds_regressing = write_run(
                root, "stall-seconds-regressing", [0.0, 1.0])
            upgrade_telemetry_v2(stall_seconds_regressing, counters=[
                stall_base, {**stall_base, "move_stall_eligible_seconds": 1.5}])
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "move_stall_eligible_seconds regressed"):
                QUALITY.analyze_run(stall_seconds_regressing)

            stall_nonfinite = write_run(root, "stall-nonfinite", [0.0, 1.0])
            upgrade_telemetry_v2(stall_nonfinite, counters=[stall_base, stall_base])
            path = stall_nonfinite / "events.jsonl"
            events = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            events[1]["bots"][0]["move_stall_eligible_seconds"] = "nan"
            path.write_text(
                "".join(json.dumps(event) + "\n" for event in events), encoding="utf-8")
            with self.assertRaisesRegex(
                    QUALITY.QualityError,
                    "move_stall_eligible_seconds must be a finite number"):
                QUALITY.analyze_run(stall_nonfinite)

            stall_incomplete = write_run(root, "stall-incomplete", [0.0, 1.0])
            upgrade_telemetry_v2(stall_incomplete, counters=[
                {**base, "move_stall_detections_exact": 0},
                {**base, "move_stall_detections_exact": 0},
            ])
            with self.assertRaisesRegex(QUALITY.QualityError, "move stall counters"):
                QUALITY.analyze_run(stall_incomplete)

            failed_navigation_base = {
                **base, "failed_navigation_avoidance_activations_exact": 1,
                "failed_navigation_safeguard_suppressions_exact": 1,
                "failed_navigation_route_penalty_applications_exact": 2,
            }
            failed_navigation_regressing = write_run(
                root, "failed-navigation-regressing", [0.0, 1.0])
            upgrade_telemetry_v2(failed_navigation_regressing, counters=[
                failed_navigation_base,
                {**failed_navigation_base,
                 "failed_navigation_route_penalty_applications_exact": 1},
            ])
            with self.assertRaisesRegex(
                    QUALITY.QualityError,
                    "failed_navigation_route_penalty_applications_exact regressed"):
                QUALITY.analyze_run(failed_navigation_regressing)

            failed_navigation_incomplete = write_run(
                root, "failed-navigation-incomplete", [0.0, 1.0])
            upgrade_telemetry_v2(failed_navigation_incomplete, counters=[
                {**base, "failed_navigation_avoidance_activations_exact": 0},
                {**base, "failed_navigation_avoidance_activations_exact": 0},
            ])
            with self.assertRaisesRegex(QUALITY.QualityError, "failed navigation counters"):
                QUALITY.analyze_run(failed_navigation_incomplete)

            current_stall = write_run(root, "current-stall", [0.0, 1.0])
            upgrade_telemetry_v2(current_stall, counters=[
                {**base, "move_stall_detections_exact": 0,
                 "move_stall_forced_replans_exact": 0,
                 "move_stall_eligible_seconds": 0.0},
                {**base, "move_stall_detections_exact": 1,
                 "move_stall_forced_replans_exact": 1,
                 "move_stall_eligible_seconds": 2.0},
            ])
            current_metrics = QUALITY.analyze_run(current_stall)["metrics"]
            self.assertEqual(current_metrics["move_stall_detections_exact"], 1)
            self.assertEqual(current_metrics["move_stall_forced_replans_exact"], 1)
            self.assertIsNone(current_metrics["move_stall_episode_resets_exact"])

            excessive_replans = write_run(root, "excessive-replans", [0.0, 1.0])
            upgrade_telemetry_v2(excessive_replans, counters=[
                {**base, "move_stall_detections_exact": 0,
                 "move_stall_forced_replans_exact": 0,
                 "move_stall_eligible_seconds": 0.0},
                {**base, "move_stall_detections_exact": 0,
                 "move_stall_forced_replans_exact": 1,
                 "move_stall_eligible_seconds": 2.0},
            ])
            with self.assertRaisesRegex(
                    QUALITY.QualityError, "move stall forced replans exceed detections"):
                QUALITY.analyze_run(excessive_replans)

            misattributed_replans = write_run(
                root, "misattributed-replans", [0.0, 1.0])
            attributed_stall = {
                **base, "move_stall_detections_exact": 1,
                "move_stall_episode_resets_exact": 0,
                "move_stall_forced_replans_exact": 1,
                "move_stall_navigation_forced_replans_exact": 0,
                "move_stall_targetless_move_to_timeouts_exact": 0,
                "move_stall_eligible_seconds": 2.0,
            }
            upgrade_telemetry_v2(
                misattributed_replans, counters=[attributed_stall, attributed_stall])
            with self.assertRaisesRegex(
                    QUALITY.QualityError,
                    "attributed move stall recoveries do not equal forced replans"):
                QUALITY.analyze_run(misattributed_replans)

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
