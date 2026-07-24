from __future__ import annotations

import json
import math
import unittest
from pathlib import Path


FIXTURE = Path(__file__).parent / "fixtures" / "falling_hazard_labels_v1.json"


def _expanded_tick_count(ranges: list[str]) -> int:
    count = 0
    for value in ranges:
        bounds = value.split("-", 1)
        start = int(bounds[0])
        end = int(bounds[-1])
        if end < start:
            raise ValueError(f"descending tick range: {value}")
        count += end - start + 1
    return count


class FallingHazardLabelsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.document = json.loads(FIXTURE.read_text(encoding="utf-8"))
        cls.runs = {run["id"]: run for run in cls.document["runs"]}
        cls.labels = {label["id"]: label for label in cls.document["labels"]}

    def test_schema_and_selectors_are_well_formed(self) -> None:
        self.assertEqual(self.document["schema"], "surreal-falling-hazard-labels-v1")
        self.assertEqual(len(self.runs), len(self.document["runs"]))
        self.assertEqual(len(self.labels), len(self.document["labels"]))

        invocation_keys: set[tuple[str, str, str, str]] = set()
        transition_keys: set[tuple[str, str, str, int]] = set()
        for label in self.document["labels"]:
            selector = label["selector"]
            run = self.runs[selector["run_id"]]
            self.assertEqual(run["map"], "DM-Deck16][")
            self.assertTrue(run["repeat_equivalent"])
            self.assertRegex(run["config_id"], r"^fnv1a64:[0-9a-f]{16}$")
            self.assertRegex(run["events_raw_sha256"], r"^[0-9A-F]{64}$")
            self.assertLess(selector["origin_tick"], selector["start_tick"])
            self.assertLessEqual(selector["start_tick"], selector["outcome_tick"])
            if selector["correlation_end_tick"] is not None:
                self.assertGreaterEqual(
                    selector["correlation_end_tick"], selector["outcome_tick"])

            if selector["kind"] == "legacy_actor_life_invocation":
                self.assertIsInstance(selector["invocation_token"], str)
                key = (run["config_id"], selector["actor"],
                       selector["life_generation"], selector["invocation_token"])
                self.assertNotIn(key, invocation_keys)
                invocation_keys.add(key)
            else:
                self.assertEqual(selector["kind"], "actor_life_transition")
                self.assertIsNone(selector["invocation_token"])
                key = (run["config_id"], selector["actor"],
                       selector["life_generation"], selector["start_tick"])
                self.assertNotIn(key, transition_keys)
                transition_keys.add(key)

    def test_observations_are_internally_consistent(self) -> None:
        for label in self.document["labels"]:
            observed = label["observed"]
            origin = observed["origin_position"]
            outcome = observed["outcome_position"]
            dx = outcome["x"] - origin["x"]
            dy = outcome["y"] - origin["y"]
            dz = origin["z"] - outcome["z"]
            displacement = observed["displacement"]
            self.assertAlmostEqual(
                displacement["realised_vertical_drop_uu"], dz, places=5)
            self.assertAlmostEqual(
                displacement["realised_horizontal_displacement_uu"],
                math.hypot(dx, dy), places=5)
            self.assertAlmostEqual(
                displacement["realised_euclidean_displacement_uu"],
                math.sqrt(dx * dx + dy * dy + dz * dz), places=5)

            wall = observed["hit_wall_counter"]
            self.assertEqual(
                wall["after"] - wall["before"],
                _expanded_tick_count(wall["tick_ranges"]),
                label["id"])

    def test_frozen_safe_negative_contract(self) -> None:
        safe = [label for label in self.document["labels"]
                if label["expected"]["outcome_class"] == "safe_negative"]
        self.assertEqual(len(safe), 4)
        self.assertEqual(
            {label["provenance"]["set_rank_by_realised_drop"] for label in safe},
            {1, 2, 3, 4})
        drops = [label["observed"]["displacement"]["realised_vertical_drop_uu"]
                 for label in sorted(
                     safe, key=lambda item: item["provenance"]["set_rank_by_realised_drop"])]
        self.assertEqual(drops, sorted(drops, reverse=True))
        for label in safe:
            observed = label["observed"]
            self.assertEqual(label["selector"]["run_id"], "ut-deck271828-candidate3")
            self.assertEqual(observed["primary_outcome"], "landed")
            self.assertEqual(observed["correlation_terminal"], "landed")
            self.assertEqual(observed["terminal_physics"], "Walking")
            self.assertEqual(observed["terminal_state"], "Roaming")
            self.assertEqual(observed["health_before"], observed["health_after"])
            self.assertEqual(observed["pain_entry_ticks"], [])
            self.assertFalse(observed["in_hazard_zone_after"])
            self.assertTrue(all(delta == 0 for delta in observed["counter_deltas"].values()))

        late_wall = self.labels["deck271828-safe-malakai-life1-token732"]
        self.assertAlmostEqual(
            late_wall["observed"]["legacy_forecast"]["total_drop_uu"],
            568.069824)
        self.assertAlmostEqual(
            late_wall["observed"]["displacement"]["realised_vertical_drop_uu"],
            568.007202)
        self.assertEqual(late_wall["observed"]["callback_barriers"][0]["tick"], 1433)

    def test_resolved_positive_identities(self) -> None:
        first = self.labels["deck104729-positive-necroth-life1-token591"]
        second = self.labels["deck104729-positive-necroth-life1-token694"]
        self.assertEqual(first["selector"]["actor"], second["selector"]["actor"])
        self.assertEqual(first["selector"]["life_generation"], "1")
        self.assertEqual(second["selector"]["life_generation"], "1")
        self.assertNotEqual(first["selector"]["invocation_token"],
                            second["selector"]["invocation_token"])
        self.assertEqual(first["observed"]["counter_deltas"]["deaths"], 0)
        self.assertEqual(second["observed"]["counter_deltas"], {
            "deaths": 1,
            "suicides": 1,
            "environmental_deaths": 1,
            "unassisted_environmental_deaths": 1,
        })

        external = self.labels["deck314159-positive-tamerlane-external-595"]
        walking = self.labels["deck314159-positive-tamerlane-life1-token730"]
        false_negative = self.labels["deck314159-false-negative-cilia-life2-token610"]
        self.assertEqual(external["selector"]["kind"], "actor_life_transition")
        self.assertIsNone(external["selector"]["invocation_token"])
        self.assertEqual(external["expected"]["transition_source"],
                         "external_impulse_commit")
        self.assertGreater(external["observed"]["start_velocity"]["z"], 0.0)
        self.assertEqual(external["observed"]["combat_damage_at_start"], 25)
        self.assertEqual(walking["selector"]["invocation_token"], "730")
        self.assertEqual(walking["expected"]["transition_source"],
                         "walking_support_loss")
        self.assertEqual(walking["observed"]["primary_outcome"], "died")
        self.assertEqual(false_negative["selector"]["invocation_token"], "610")
        self.assertEqual(false_negative["expected"]["evaluation_role"],
                         "false_negative_control")
        self.assertEqual(false_negative["observed"]["legacy_forecast"]["classification"],
                         "safe")
        self.assertEqual(false_negative["observed"]["primary_outcome"], "pain_entered")


if __name__ == "__main__":
    unittest.main()
