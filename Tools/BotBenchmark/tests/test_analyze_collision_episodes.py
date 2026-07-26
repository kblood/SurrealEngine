from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).resolve().parents[1] / "Analyze-CollisionEpisodes.py"
SPEC = importlib.util.spec_from_file_location("collision_episodes", TOOL_PATH)
assert SPEC and SPEC.loader
COLLISIONS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = COLLISIONS
SPEC.loader.exec_module(COLLISIONS)


def event(tick: int, *, hitwall: int = 0, attempts: int = 0, escapes: int = 0,
          deaths: int = 0, physics: str = "Walking", latent: str = "Continue",
          target: str = "", x: float = 0.0, status: str = "running") -> dict:
    return {
        "schema": "surreal-bot-benchmark-telemetry-v2", "tick": str(tick),
        "simulated_seconds": tick / 60.0,
        "type": "run_result" if status == "complete" else "tick",
        "status": status, "failure_reason": "", "bots": [{
            "identity": "pri:1", "actor": "Bot1", "hit_wall_events_exact": str(hitwall),
            "pain_ledge_recovery_attempts_exact": str(attempts),
            "pain_ledge_recovery_escapes_exact": str(escapes), "deaths_exact": str(deaths),
            "physics_mode": physics, "latent_action": latent, "move_target_identity": target,
            "position": {"x": x, "y": 0.0, "z": 0.0},
        }],
    }


class CollisionEpisodeTests(unittest.TestCase):
    def test_partition_separates_falling_recovery_and_stationary_wall_loop(self) -> None:
        report = COLLISIONS.analyze_events([
            event(0),
            event(1, hitwall=1, physics="Falling", x=0.0),
            event(2, hitwall=2, physics="Walking", x=0.0),
            event(3, hitwall=3, attempts=1, x=0.0),
            event(4, hitwall=4, attempts=1, x=1.0),
            event(5, hitwall=5, attempts=1, x=2.0),
            event(6, hitwall=6, attempts=1, escapes=1, x=3.0, status="complete"),
        ])
        self.assertEqual(report["totals"]["hit_wall_callbacks_exact"], "6")
        self.assertEqual(report["totals"]["falling_or_landing_callbacks_exact"], "2")
        self.assertEqual(report["totals"]["recovery_transition_callbacks_exact"], "1")
        self.assertEqual(report["totals"]["ordinary_walking_callbacks_exact"], "3")
        self.assertEqual(report["totals"]["recovery_lifecycle_temporal_window_callbacks_exact"], "4")
        self.assertEqual(report["totals"]["wall_loop_candidates_exact"], "0")

    def test_stationary_targetless_continue_run_is_wall_loop_candidate(self) -> None:
        report = COLLISIONS.analyze_events([
            event(0), event(1, hitwall=1), event(2, hitwall=2),
            event(3, hitwall=3, status="complete"),
        ])
        self.assertEqual(report["totals"]["wall_loop_candidates_exact"], "1")
        self.assertTrue(report["episodes"][0]["wall_loop_candidate"])

    def test_escape_without_attempt_and_incomplete_run_fail_closed(self) -> None:
        with self.assertRaisesRegex(COLLISIONS.CollisionError, "no active recovery"):
            COLLISIONS.analyze_events([event(0), event(1, escapes=1, status="complete")])
        with self.assertRaisesRegex(COLLISIONS.CollisionError, "successful complete"):
            COLLISIONS.analyze_events([event(0), event(1, hitwall=1)])
