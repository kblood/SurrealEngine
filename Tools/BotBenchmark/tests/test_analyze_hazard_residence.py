from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[1] / "Analyze-HazardResidence.py"
SPEC = importlib.util.spec_from_file_location("hazard_residence", TOOL_PATH)
assert SPEC and SPEC.loader
RESIDENCE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RESIDENCE
SPEC.loader.exec_module(RESIDENCE)


def event(tick: int, seconds: float, *, hazard: bool, deaths: int = 0,
          candidate: int = 0, superseded: int = 0, commands: int = 0,
          target: str = "Path1", latent: str = "MoveToward", candidate_name: str = "",
          status: str = "running") -> dict:
    return {
        "schema": "surreal-bot-benchmark-telemetry-v2", "tick": str(tick),
        "simulated_seconds": seconds, "type": "run_result" if status == "complete" else "tick",
        "status": status, "failure_reason": "", "bots": [{
            "identity": "pri:1", "actor": "Bot1", "in_hazard_zone": hazard,
            "position": {"x": float(tick), "y": 0.0, "z": 0.0}, "health": 100 - deaths * 100,
            "deaths_exact": str(deaths), "physics_mode": "Swimming", "state": "Roaming",
            "latent_action": latent, "move_target_name": target,
            "hazard_residence_candidates_observed_exact": str(candidate),
            "hazard_residence_candidate_other_commands_exact": str(superseded),
            "hazard_residence_command_changes_exact": str(commands),
            "hazard_swim_egress_direct_nav_best_candidate_name": candidate_name,
        }],
    }


class HazardResidenceTests(unittest.TestCase):
    def test_candidate_supersession_and_death_are_attributed(self) -> None:
        report = RESIDENCE.analyze_events([
            event(0, 0.0, hazard=False),
            event(1, 0.1, hazard=True),
            event(2, 0.2, hazard=True, candidate=1, commands=1, candidate_name="Path144"),
            event(3, 0.3, hazard=True, candidate=1, superseded=1, commands=2,
                  target="JumpBoots", latent="MoveTo"),
            event(4, 0.4, hazard=True, deaths=1, candidate=1, superseded=1, commands=2,
                  target="JumpBoots", latent="MoveTo"),
            event(5, 0.5, hazard=False, deaths=1, candidate=1, superseded=1, commands=2,
                  target="JumpBoots", latent="MoveTo", status="complete"),
        ])
        self.assertEqual(report["totals"], {
            "episodes": "1", "deaths": "1", "cleared": "0", "run_end_censored": "0",
            "candidate_observed": "1", "candidate_superseded": "1", "no_candidate": "0"})
        episode = report["episodes"][0]
        self.assertTrue(episode["candidate_superseded"])
        self.assertEqual(episode["candidate_names"], ["Path144"])
        self.assertEqual(episode["target_changes"], "1")

    def test_grace_period_reentry_and_no_candidate_are_distinct(self) -> None:
        report = RESIDENCE.analyze_events([
            event(0, 0.0, hazard=True), event(1, 0.1, hazard=False),
            event(2, 0.2, hazard=True), event(3, 0.3, hazard=False),
            event(4, 0.6, hazard=False, status="complete"),
        ])
        episode = report["episodes"][0]
        self.assertEqual(episode["terminal"], "cleared")
        self.assertEqual(episode["reentries"], "1")
        self.assertFalse(episode["candidate_observed"])

    def test_incomplete_run_fails_closed(self) -> None:
        with self.assertRaisesRegex(RESIDENCE.ResidenceError, "successful complete"):
            RESIDENCE.analyze_events([event(0, 0.0, hazard=True)])
