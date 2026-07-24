#!/usr/bin/env python3

import copy
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from new_physical_run import attachment_record, build_run, frozen_candidate, sha256_file
from validate_physical_run import Validation


STAMP = "2026-07-24T12:00:00Z"


class PhysicalRunValidationTests(unittest.TestCase):
    def make_pass(self, root: Path) -> tuple[Path, dict]:
        args = SimpleNamespace(
            row="Q1", game="ut99", tester="lab-a", attachment=[],
            candidate_data=frozen_candidate(),
        )
        record = build_run(args, root)
        record["generatedAtUtc"] = STAMP
        record["test"].update({
            "startedAtUtc": STAMP,
            "completedAtUtc": "2026-07-24T12:20:00Z",
            "clientVersion": "Meta Quest Browser 99.0",
            "questOs": "99.0",
            "questBrowser": "99.0",
            "refreshRateHz": 90,
        })
        record["result"] = {"status": "pass", "category": "qualified", "summary": "All required checks passed."}
        record["resourcePolicy"] = {
            "maxHeapGrowthBytes": 0, "maxTextureGrowth": 0, "maxFrameDeltaUs": 100000,
        }
        for index, cycle in enumerate(record["cycles"], 1):
            minute = index
            cycle["result"] = "pass"
            cycle["entry"].update({
                "requestedAtUtc": f"2026-07-24T12:{minute:02d}:00Z",
                "activatedAtUtc": f"2026-07-24T12:{minute:02d}:01Z",
                "firstFrameAtUtc": f"2026-07-24T12:{minute:02d}:02Z",
                "firstFrameSeen": True, "viewCount": 2, "positiveViewports": True,
            })
            cycle["exit"].update({
                "requestedAtUtc": f"2026-07-24T12:{minute:02d}:03Z",
                "flatRestoredAtUtc": f"2026-07-24T12:{minute:02d}:04Z",
                "flatRestored": True,
            })
            cycle["metrics"].update({
                "entryLatencyMs": 1000, "firstFrameLatencyMs": 2000, "exitLatencyMs": 1000,
                "xrFrames": 90, "skippedFrames": 0, "maxFrameDeltaUs": 20000,
                "frameTimeMs": {"samples": 90, "p50": 11.0, "p95": 13.0, "p99": 15.0},
            })
            cycle["resources"] = {
                "heapBytesBefore": 1000000, "heapBytesAfter": 1000000,
                "texturesBefore": 20, "texturesAfter": 20,
                "webglGenerationBefore": 1, "webglGenerationAfter": 1,
            }
            cycle["continuity"] = {
                "engineStable": True, "levelStable": True, "playerStable": True,
                "rendererStable": True, "audioStable": True,
                "tickBefore": index * 100, "tickAtFirstFrame": index * 100 + 1,
                "tickAfter": index * 100 + 2,
            }
            cycle["observations"] = {
                "sameMap": True, "samePosition": True, "sameHealth": True,
                "sameWeapon": True, "sameAudio": True, "duplicateSimulation": False,
                "largeTimeStep": False, "blackOrStaleEye": False,
                "stuckInputOrHaptics": False, "pointerLockRecovered": True,
            }
        for scenario in record["failureScenarios"]:
            scenario.update({"result": "pass", "flatRecovered": True, "laterEntrySucceeded": True})

        attachments = []
        for index, (role, name, contents) in enumerate((
            ("before-diagnostics", "before.json", b"{}\n"),
            ("running-diagnostics", "running.txt", b"privacy: no-game-data,no-paths,no-logs\n"),
            ("after-diagnostics", "after.json", b"{}\n"),
            ("screenshot", "headset.png", b"synthetic-png-for-validator-test"),
        ), 1):
            (root / name).write_bytes(contents)
            attachments.append(attachment_record(f"{role}={name}", root, index))
        record["attachments"] = attachments
        path = root / "run.json"
        path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        return path, record

    def test_complete_game_case_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            path, _ = self.make_pass(Path(directory))
            self.assertEqual([], Validation(path, frozen_candidate()).validate(json.loads(path.read_text())))

    def test_candidate_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path, record = self.make_pass(Path(directory))
            expected = copy.deepcopy(frozen_candidate())
            expected["browser"]["sourceCommit"] = "0" * 40
            errors = Validation(path, expected).validate(record)
            self.assertTrue(any("explicitly expected candidate" in error for error in errors))

    def test_nine_cycles_and_not_run_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path, record = self.make_pass(Path(directory))
            record["cycles"].pop()
            record["failureScenarios"][0]["result"] = "not-run"
            errors = Validation(path, frozen_candidate()).validate(record)
            self.assertTrue(any("exactly ten" in error for error in errors))
            self.assertTrue(any("not-run" in error or "pass or fail" in error for error in errors))

    def test_private_path_in_text_attachment_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, record = self.make_pass(root)
            before = root / "before.json"
            before.write_text('{"private":"C:/Users/tester/Game/System"}\n', encoding="utf-8")
            item = record["attachments"][0]
            item["bytes"] = before.stat().st_size
            item["sha256"] = sha256_file(before)
            errors = Validation(path, frozen_candidate()).validate(record)
            self.assertTrue(any("private absolute path" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
