from __future__ import annotations

import importlib.util
import json
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
CAMPAIGNS = TOOLS / "QualificationCampaigns"
EVALUATOR_PATH = TOOLS / "Evaluate-BotQualityGate.py"
SPEC = importlib.util.spec_from_file_location("qualification_campaign_gate", EVALUATOR_PATH)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


class QualificationCampaignTests(unittest.TestCase):
    def test_campaign_references_every_checked_in_gate_file(self) -> None:
        campaign = json.loads(
            (CAMPAIGNS / "UT436-Unreal226b-qualification-campaign-v1.json").read_text(
                encoding="utf-8"))
        self.assertEqual(campaign["schema"], "surreal-bot-qualification-campaign-v1")
        self.assertEqual(campaign["runner_contract"]["provenance_mode"], "release")
        self.assertEqual(campaign["runner_contract"]["bot_count"], 16)
        self.assertEqual(campaign["runner_contract"]["start_layouts"], 3)
        self.assertEqual(campaign["runner_contract"]["repetitions_per_layout"], 2)

        matrices = campaign["matrices"]
        self.assertEqual(
            {entry["id"] for entry in matrices},
            {"ut436-tuning", "ut436-heldout", "unreal226b-tuning", "unreal226b-heldout"},
        )
        referenced = {entry["quality_gates"] for entry in matrices}
        checked_in = {path.name for path in CAMPAIGNS.glob("*-quality-gates-v1.json")}
        self.assertEqual(referenced, checked_in)
        self.assertEqual(
            {entry["id"] for entry in campaign["unrepresented_release_requirements"]},
            {
                "role-swapped-participant-policy-evidence",
                "avoidable-causal-suicide-rate",
                "sixteen-bot-ai-frame-budget",
            },
        )
        self.assertTrue(all(
            entry["status"] == "fail-closed"
            for entry in campaign["unrepresented_release_requirements"]
        ))

    def test_every_checked_in_gate_file_is_a_valid_fail_closed_configuration(self) -> None:
        for path in sorted(CAMPAIGNS.glob("*-quality-gates-v1.json")):
            with self.subTest(path=path.name):
                config = json.loads(path.read_text(encoding="utf-8"))
                self.assertEqual(config["schema"], GATE.CONFIG_SCHEMA)
                GATE._validate_config(config)
                required = set(config["required_metrics"])
                self.assertTrue({
                    "avoidable_suicide_rate",
                    "recoverable_movement_episode_clear_within_2s_fraction",
                    "recoverable_movement_episode_clear_or_replanned_within_5s_fraction",
                    "role_swapped_participant_policy_coverage",
                    "ai_frame_p95_ms",
                }.issubset(required))


if __name__ == "__main__":
    unittest.main()
