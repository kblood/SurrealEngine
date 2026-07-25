from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "validate_realized_bot_capabilities", "Tools/BotBenchmark/Validate-RealizedBotCapabilities.py")
VALIDATE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATE)


def run_documents() -> tuple[dict, dict, dict]:
    manifest = {"schema": "surreal-bot-benchmark-manifest-v2", "driver": "bot-benchmark", "bot_count": 1}
    summary = {
        "schema": "surreal-bot-benchmark-summary-v2", "status": "complete", "exit_code": 0,
        "actual_roster": [{"roster_index": 0, "identity": "bot-0", "actor": "Bot0",
                           "player_name": "Bot0", "class": "Botpack.TMale1"}],
    }
    witness = {
        "schema": VALIDATE.SCHEMA, "sample": VALIDATE.SAMPLE,
        "participants": [{"identity": "bot-0", "actor": "Bot0", "class": "Botpack.TMale1",
            "movement": {"ground_speed": 400, "water_speed": 200, "air_speed": 400,
                         "jump_z": 357.5, "max_step_height": 25, "accel_rate": 2048},
            "capabilities": {"walk": True, "jump": True, "swim": True, "fly": False,
                             "open_doors": True, "special": True}}],
    }
    return manifest, summary, witness


def write_run(root: Path, manifest: dict, summary: dict, witness: dict) -> None:
    for name, value in (("manifest.json", manifest), ("summary.json", summary),
                        (VALIDATE.ARTIFACT_NAME, witness)):
        (root / name).write_text(json.dumps(value) + "\n", encoding="utf-8")


class RealizedCapabilityValidatorTests(unittest.TestCase):
    def test_accepts_matching_complete_v2_run(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_run(root, *run_documents())
            result = VALIDATE.validate_run(root)
            self.assertEqual(result["participants"], 1)
            self.assertEqual(len(result["participants_sha256"]), 64)

    def test_rejects_mismatched_actor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, summary, witness = run_documents()
            witness["participants"][0]["actor"] = "Other"
            write_run(root, manifest, summary, witness)
            with self.assertRaisesRegex(VALIDATE.CapabilityError, "actor differs"):
                VALIDATE.validate_run(root)

    def test_rejects_unknown_capability_field(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, summary, witness = run_documents()
            witness["participants"][0]["capabilities"]["invented"] = True
            write_run(root, manifest, summary, witness)
            with self.assertRaisesRegex(VALIDATE.CapabilityError, "fields differ"):
                VALIDATE.validate_run(root)

    def test_rejects_non_boolean_capability(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, summary, witness = run_documents()
            witness["participants"][0]["capabilities"]["fly"] = 0
            write_run(root, manifest, summary, witness)
            with self.assertRaisesRegex(VALIDATE.CapabilityError, "expected a boolean"):
                VALIDATE.validate_run(root)

    def test_rejects_incomplete_run(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, summary, witness = run_documents()
            summary["status"] = "failed"
            write_run(root, manifest, summary, witness)
            with self.assertRaisesRegex(VALIDATE.CapabilityError, "complete successful"):
                VALIDATE.validate_run(root)


if __name__ == "__main__":
    unittest.main()
