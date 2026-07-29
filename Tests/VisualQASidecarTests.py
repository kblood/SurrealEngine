import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

MODULE_PATH = pathlib.Path(__file__).parents[1] / "tools" / "visual-qa" / "visual_qa.py"
SPEC = importlib.util.spec_from_file_location("surreal_visual_qa", MODULE_PATH)
visual_qa = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(visual_qa)
sys.path.insert(0, str(MODULE_PATH.parent))
import shadow_replay
sys.path.pop(0)


class VisualQASidecarTests(unittest.TestCase):
    def make_fixture(self, root: pathlib.Path):
        image = b"bounded-fake-png-data"
        (root / "capture.png").write_bytes(image)
        manifest = {
            "schema": "surreal-visual-qa-capture-manifest-v1",
            "captures": [{"id": "frame-10", "path": "capture.png",
                          "sha256": hashlib.sha256(image).hexdigest(), "tick": 10,
                          "camera": "viewport-player", "width": 640, "height": 480,
                          "command_id": "walk-to-point-1"}],
        }
        profile = {
            "schema": "surreal-visual-qa-assertion-profile-v1",
            "profile_id": "smoke-v1", "prompt": "Check whether the HUD is readable.",
            "assertions": [{"id": "hud-readable", "description": "HUD text is readable"}],
        }
        manifest_path = root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest, manifest_path, profile

    def shadow_observation(self):
        return {
            "schema": "surreal-automation-observation-v1", "revision": "7", "tick": "10",
            "player_identity": "actor:DeusEx.JCDentonMale:JCDentonMale0#0",
            "player_position": {"x": 0, "y": 0, "z": 0},
            "targets": [
                {"identity": "actor:Engine.Light:Light155#0", "class": "Engine.Light",
                 "owner_identity": "", "state_token": "None", "tag": "Light", "event": "",
                 "location": {"x": 100, "y": 0, "z": 0}, "deleted": False,
                 "reachable": True, "interactable": False, "acquirable": False},
                {"identity": "actor:DeusEx.Switch1:Switch1#0", "class": "DeusEx.Switch1",
                 "owner_identity": "", "state_token": "Active", "tag": "Switch1",
                 "event": "FirstDoor", "location": {"x": 120, "y": 0, "z": 0},
                 "deleted": False, "reachable": True, "interactable": True,
                 "acquirable": False},
            ],
        }

    def grounded_manifest(self, root, observation=None):
        observation = observation or self.shadow_observation()
        observation_raw = json.dumps(
            observation, sort_keys=True, separators=(",", ":")).encode("utf-8")
        (root / "observation.json").write_bytes(observation_raw)
        image = b"grounded-fake-png-data"
        (root / "capture.png").write_bytes(image)
        grounding = {
            "schema": "surreal-visual-qa-grounding-v1",
            "session_id": "fixture-session-1", "source_revision": "c" * 40,
            "binary_sha256": "d" * 64, "config_sha256": "e" * 64,
            "command_id": "interact-1",
            "observation_sha256": hashlib.sha256(observation_raw).hexdigest(),
            "observation_revision": observation["revision"],
            "observation_tick": observation["tick"],
        }
        manifest = {
            "schema": "surreal-visual-qa-capture-manifest-v2",
            "session": {key: grounding[key] for key in (
                "session_id", "source_revision", "binary_sha256", "config_sha256")},
            "captures": [{
                "id": "frame-10", "path": "capture.png",
                "sha256": hashlib.sha256(image).hexdigest(), "tick": 10,
                "camera": "viewport-player", "width": 640, "height": 480,
                "command_id": grounding["command_id"],
                "observation": {"path": "observation.json",
                                "sha256": grounding["observation_sha256"],
                                "revision": grounding["observation_revision"],
                                "tick": grounding["observation_tick"]},
            }],
        }
        manifest_path = root / "manifest-v2.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest, manifest_path, observation, observation_raw, grounding

    def shadow_replay_values(self, proposal=None, accepted=None):
        observation = self.shadow_observation()
        observation_raw = json.dumps(observation, sort_keys=True).encode("utf-8")
        observation_sha256 = hashlib.sha256(observation_raw).hexdigest()
        target = "actor:Engine.Light:Light155#0"
        context = shadow_replay.validate_shadow_context(
            observation, [target], ["walk_to_actor", "wait"], 60)
        if proposal is None:
            proposal = {"schema": "surreal-visual-qa-shadow-proposal-v1",
                        "action": "none", "target_identity": "", "wait_ticks": 0,
                        "confidence": 1.0, "rationale": "No action is justified."}
        if accepted is None:
            accepted = [{"action": "none", "target_identity": "", "wait_ticks": 0}]
        fixture = {
            "schema": "surreal-visual-qa-shadow-replay-fixture-v1",
            "fixture_id": "synthetic-startup-none-v1",
            "capture": {"id": "frame-10", "sha256": "a" * 64},
            "observation_sha256": observation_sha256,
            "policy": {"allowed_actions": ["walk_to_actor", "wait"],
                       "target_identities": [target], "max_wait_ticks": 60},
            "accepted_proposals": accepted,
        }
        report = {
            "schema": "surreal-visual-qa-report-v1", "mode": "shadow",
            "controls_live_player": False,
            "model": {"tag": "synthetic-vision:v1", "digest": "b" * 64},
            "provenance": {"shadow_observation_sha256": observation_sha256},
            "results": [{"capture_id": "frame-10", "image_sha256": "a" * 64,
                         "finding": {"schema": "surreal-visual-qa-finding-v1",
                                     "capture_id": "frame-10", "proposal": proposal},
                         "shadow_candidate": shadow_replay.shadow_candidate(proposal, context)}],
            "shadow_policy": {**context, "dispatch_authorized": False,
                              "controls_live_player": False},
        }
        return observation, observation_raw, fixture, report

    def test_shadow_context_accepts_resource_aware_observation_v2(self):
        observation = self.shadow_observation()
        observation["schema"] = "surreal-automation-observation-v2"
        observation["targets"][0]["inventory_resource"] = None
        context = visual_qa.validate_shadow_context(
            observation, ["actor:Engine.Light:Light155#0"],
            ["walk_to_actor", "wait"], 60)
        self.assertEqual(context["observation_revision"], "7")

    def test_hashes_paths_profile_and_schema_are_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            manifest, manifest_path, profile = self.make_fixture(root)
            captures = visual_qa.validate_manifest(manifest, manifest_path)
            assertions = visual_qa.validate_profile(profile)
            self.assertEqual(captures[0]["_sha256"], manifest["captures"][0]["sha256"])
            schema = visual_qa.finding_schema("frame-10", ["hud-readable"])
            self.assertFalse(schema["additionalProperties"])
            value = {"schema": "surreal-visual-qa-finding-v1", "capture_id": "frame-10",
                     "summary": "Readable", "findings": [{"assertion_id": "hud-readable",
                     "status": "pass", "confidence": 0.8, "explanation": "Visible"}]}
            self.assertEqual(visual_qa.validate_finding(value, "frame-10", [assertions[0]["id"]]), value)

    def test_path_traversal_and_hash_substitution_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            manifest, manifest_path, _ = self.make_fixture(root)
            manifest["captures"][0]["path"] = "../capture.png"
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.validate_manifest(manifest, manifest_path)
            manifest["captures"][0]["path"] = "capture.png"
            manifest["captures"][0]["sha256"] = "0" * 64
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.validate_manifest(manifest, manifest_path)

    def test_capture_manifest_v2_binds_exact_same_session_observation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            manifest, manifest_path, observation, raw, grounding = \
                self.grounded_manifest(root)
            captures = visual_qa.validate_manifest(manifest, manifest_path)
            self.assertEqual(captures[0]["_grounding"], grounding)
            self.assertEqual(visual_qa.validate_shadow_grounding(
                captures, observation, raw), "same_session")

            swapped = self.shadow_observation()
            swapped["revision"] = "8"
            swapped_raw = json.dumps(
                swapped, sort_keys=True, separators=(",", ":")).encode("utf-8")
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.validate_shadow_grounding(captures, swapped, swapped_raw)

            extra = json.loads(json.dumps(manifest))
            extra["session"]["unexpected"] = "field"
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.validate_manifest(extra, manifest_path)

            empty_command = json.loads(json.dumps(manifest))
            empty_command["captures"][0]["command_id"] = ""
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.validate_manifest(empty_command, manifest_path)
            bad_command_shape = json.loads(json.dumps(manifest))
            bad_command_shape["captures"][0]["unexpected"] = False
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.validate_manifest(bad_command_shape, manifest_path)
            wrong_tick = json.loads(json.dumps(manifest))
            wrong_tick["captures"][0]["observation"]["tick"] = "11"
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.validate_manifest(wrong_tick, manifest_path)
            duplicate_path = root / "duplicate-manifest.json"
            duplicate_path.write_text(
                '{"schema":"surreal-visual-qa-capture-manifest-v2",'
                '"schema":"surreal-visual-qa-capture-manifest-v1","captures":[]}',
                encoding="utf-8")
            with self.assertRaises(visual_qa.VisualQAError):
                visual_qa.load_json(duplicate_path)

    def test_legacy_v1_shadow_allows_none_but_rejects_positive_action(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            manifest, manifest_path, profile = self.make_fixture(root)
            profile_path = root / "profile.json"
            profile_path.write_text(json.dumps(profile), encoding="utf-8")
            observation_path = root / "observation.json"
            observation_path.write_text(json.dumps(self.shadow_observation()),
                                        encoding="utf-8")
            args = types.SimpleNamespace(
                seed=104729, timeout=30, ollama_url="http://127.0.0.1:11434",
                run_manifest=str(manifest_path), profile=str(profile_path),
                model="synthetic-vision:v1", shadow_observation=str(observation_path),
                shadow_action=["interact"],
                shadow_target_identity=["actor:DeusEx.Switch1:Switch1#0"],
                shadow_max_wait_ticks=60)
            proposal = {"schema": "surreal-visual-qa-shadow-proposal-v1",
                        "action": "interact",
                        "target_identity": "actor:DeusEx.Switch1:Switch1#0",
                        "wait_ticks": 0, "confidence": 1.0,
                        "rationale": "synthetic"}

            def response_for(value):
                finding = {"schema": "surreal-visual-qa-finding-v1",
                           "capture_id": "frame-10", "summary": "synthetic",
                           "findings": [{"assertion_id": "hud-readable",
                                         "status": "pass", "confidence": 1.0,
                                         "explanation": "synthetic"}],
                           "proposal": value}
                return {"message": {"content": json.dumps(finding)}}

            with mock.patch.object(visual_qa, "resolve_model", return_value=(
                    "synthetic-vision:v1", "b" * 64, {"capabilities": ["vision"]})), \
                    mock.patch.object(visual_qa, "http_json",
                                      side_effect=lambda url, *unused:
                                      {"version": "test"} if url.endswith("/api/version")
                                      else response_for(proposal)):
                with self.assertRaises(visual_qa.VisualQAError):
                    visual_qa.analyze(args)

            none = {**proposal, "action": "none", "target_identity": "",
                    "wait_ticks": 0}
            with mock.patch.object(visual_qa, "resolve_model", return_value=(
                    "synthetic-vision:v1", "b" * 64, {"capabilities": ["vision"]})), \
                    mock.patch.object(visual_qa, "http_json",
                                      side_effect=lambda url, *unused:
                                      {"version": "test"} if url.endswith("/api/version")
                                      else response_for(none)):
                report = visual_qa.analyze(args)
            self.assertEqual(report["provenance"]["shadow_grounding"],
                             "legacy_unbound")
            self.assertIsNone(report["results"][0]["shadow_candidate"])

    def test_declared_v2_shadow_rejects_positive_action(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            _, manifest_path, _, _, grounding = self.grounded_manifest(root)
            profile = {
                "schema": "surreal-visual-qa-assertion-profile-v1",
                "profile_id": "grounded-positive-v1",
                "prompt": "Evaluate the exact bounded interaction.",
                "assertions": [{"id": "target-actionable",
                                "description": "The selected target is actionable"}],
            }
            profile_path = root / "profile.json"
            profile_path.write_text(json.dumps(profile), encoding="utf-8")
            args = types.SimpleNamespace(
                seed=104729, timeout=30, ollama_url="http://127.0.0.1:11434",
                run_manifest=str(manifest_path), profile=str(profile_path),
                model="synthetic-vision:v1",
                shadow_observation=str(root / "observation.json"),
                shadow_action=["interact"],
                shadow_target_identity=["actor:DeusEx.Switch1:Switch1#0"],
                shadow_max_wait_ticks=60)
            proposal = {"schema": "surreal-visual-qa-shadow-proposal-v1",
                        "action": "interact",
                        "target_identity": "actor:DeusEx.Switch1:Switch1#0",
                        "wait_ticks": 0, "confidence": 1.0,
                        "rationale": "synthetic grounded action"}
            finding = {"schema": "surreal-visual-qa-finding-v1",
                       "capture_id": "frame-10", "summary": "synthetic",
                       "findings": [{"assertion_id": "target-actionable",
                                     "status": "pass", "confidence": 1.0,
                                     "explanation": "synthetic"}],
                       "proposal": proposal}
            with mock.patch.object(visual_qa, "resolve_model", return_value=(
                    "synthetic-vision:v1", "b" * 64, {"capabilities": ["vision"]})), \
                    mock.patch.object(visual_qa, "http_json",
                                      side_effect=lambda url, *unused:
                                      {"version": "test"} if url.endswith("/api/version")
                                      else {"message": {"content": json.dumps(finding)}}):
                with self.assertRaises(visual_qa.VisualQAError):
                    visual_qa.analyze(args)

    def test_remote_endpoint_and_model_identity_substitution_are_rejected(self):
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_loopback_url("https://example.com")
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_finding(
                {"schema": "surreal-visual-qa-finding-v1", "capture_id": "other",
                 "summary": "", "findings": []}, "expected", ["a"])

    def test_unknown_or_duplicate_assertion_findings_are_rejected(self):
        base = {"schema": "surreal-visual-qa-finding-v1", "capture_id": "frame-10", "summary": ""}
        unknown = {**base, "findings": [{"assertion_id": "unknown", "status": "pass",
                                         "confidence": 1.0, "explanation": ""}]}
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_finding(unknown, "frame-10", ["known"])
        duplicate = {**base, "findings": [
            {"assertion_id": "known", "status": "pass", "confidence": 1.0, "explanation": ""},
            {"assertion_id": "known", "status": "fail", "confidence": 0.0, "explanation": ""}]}
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_finding(duplicate, "frame-10", ["known"])

    def test_missing_assertion_and_nonfinite_confidence_are_rejected(self):
        empty = {"schema": "surreal-visual-qa-finding-v1", "capture_id": "frame-10",
                 "summary": "", "findings": []}
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_finding(empty, "frame-10", ["known"])
        nonfinite = {**empty, "findings": [{"assertion_id": "known", "status": "uncertain",
                                            "confidence": float("nan"), "explanation": ""}]}
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_finding(nonfinite, "frame-10", ["known"])

    def test_shadow_proposal_is_allowlisted_and_non_dispatchable(self):
        target = "actor:Engine.Light:Light155#0"
        context = visual_qa.validate_shadow_context(
            self.shadow_observation(), [target], ["walk_to_actor", "wait"], 90)
        schema = visual_qa.finding_schema("frame-10", ["known"], context)
        self.assertIn("proposal", schema["required"])
        proposal = {"schema": "surreal-visual-qa-shadow-proposal-v1",
                    "action": "walk_to_actor", "target_identity": target,
                    "wait_ticks": 0, "confidence": 0.75,
                    "rationale": "The allowed waypoint appears to be ahead."}
        finding = {"schema": "surreal-visual-qa-finding-v1", "capture_id": "frame-10",
                   "summary": "A route could be attempted.",
                   "findings": [{"assertion_id": "known", "status": "pass",
                                 "confidence": 0.8, "explanation": "Visible"}],
                   "proposal": proposal}
        self.assertEqual(visual_qa.validate_finding(
            finding, "frame-10", ["known"], context), finding)
        candidate = visual_qa.shadow_candidate(proposal, context)
        self.assertFalse(candidate["dispatch_authorized"])
        self.assertEqual(candidate["kind"], "walk_to_actor")
        self.assertEqual(candidate["observation_revision"], "7")
        self.assertEqual(candidate["target"]["expected_class"], "Engine.Light")

    def test_shadow_unknown_targets_and_capability_mismatches_are_rejected(self):
        observation = self.shadow_observation()
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_shadow_context(
                observation, ["actor:missing"], ["walk_to_actor"], 60)
        light = "actor:Engine.Light:Light155#0"
        context = visual_qa.validate_shadow_context(
            observation, [light], ["walk_to_actor", "interact", "wait"], 60)
        invalid = {"schema": "surreal-visual-qa-shadow-proposal-v1",
                   "action": "interact", "target_identity": light,
                   "wait_ticks": 0, "confidence": 1.0, "rationale": "Try it"}
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_shadow_proposal(invalid, context)
        invalid["action"] = "attack"
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_shadow_proposal(invalid, context)
        invalid.update(action="wait", target_identity="", wait_ticks=61)
        with self.assertRaises(visual_qa.VisualQAError):
            visual_qa.validate_shadow_proposal(invalid, context)

    def test_shadow_replay_passes_a_hash_bound_curated_oracle_deterministically(self):
        observation, observation_raw, fixture, report = self.shadow_replay_values()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            fixture_path = root / "fixture.json"
            observation_path = root / "observation.json"
            report_path = root / "report.json"
            fixture_path.write_text(json.dumps(fixture), encoding="utf-8")
            observation_path.write_bytes(observation_raw)
            report_path.write_text(json.dumps(report), encoding="utf-8")
            first = shadow_replay.replay(fixture_path, observation_path, report_path)
            second = shadow_replay.replay(fixture_path, observation_path, report_path)
        self.assertEqual(first, second)
        self.assertEqual(first["verdict"], "passed")
        self.assertIsNone(first["shadow_candidate"])
        self.assertFalse(first["dispatch_authorized"])
        self.assertFalse(first["controls_live_player"])

    def test_shadow_replay_v2_rejects_grounding_substitution(self):
        observation, observation_raw, fixture, report = self.shadow_replay_values()
        observation_hash = hashlib.sha256(observation_raw).hexdigest()
        grounding = {
            "schema": "surreal-visual-qa-grounding-v1",
            "session_id": "session-1", "source_revision": "c" * 40,
            "binary_sha256": "d" * 64, "config_sha256": "e" * 64,
            "command_id": "walk-to-actor-1",
            "observation_sha256": observation_hash,
            "observation_revision": observation["revision"],
            "observation_tick": observation["tick"],
        }
        fixture["schema"] = "surreal-visual-qa-shadow-replay-fixture-v2"
        fixture["grounding"] = grounding
        report["provenance"]["shadow_grounding"] = "same_session"
        report["results"][0]["grounding"] = grounding
        validated = shadow_replay.validate_replay_fixture(
            fixture, observation, observation_hash)
        result = shadow_replay.evaluate_shadow_report(
            report, validated, "c" * 64, observation_hash, "d" * 64)
        self.assertEqual(result["verdict"], "passed")

        for field, replacement in (("session_id", "session-2"),
                                   ("command_id", "other-command"),
                                   ("binary_sha256", "f" * 64),
                                   ("config_sha256", "1" * 64),
                                   ("source_revision", "2" * 40)):
            substituted = json.loads(json.dumps(report))
            substituted["results"][0]["grounding"][field] = replacement
            with self.assertRaises(shadow_replay.VisualQAError, msg=field):
                shadow_replay.evaluate_shadow_report(
                    substituted, validated, "c" * 64,
                    observation_hash, "d" * 64)

        mismatched_revision = json.loads(json.dumps(fixture))
        mismatched_revision["grounding"]["observation_revision"] = "8"
        with self.assertRaises(shadow_replay.VisualQAError):
            shadow_replay.validate_replay_fixture(
                mismatched_revision, observation, observation_hash)
        empty_command = json.loads(json.dumps(fixture))
        empty_command["grounding"]["command_id"] = ""
        with self.assertRaises(shadow_replay.VisualQAError):
            shadow_replay.validate_replay_fixture(
                empty_command, observation, observation_hash)
        extra = json.loads(json.dumps(fixture))
        extra["grounding"]["unexpected"] = False
        with self.assertRaises(shadow_replay.VisualQAError):
            shadow_replay.validate_replay_fixture(extra, observation, observation_hash)

    def test_shadow_replay_records_safe_but_uncurated_proposals_as_failures(self):
        target = "actor:Engine.Light:Light155#0"
        proposal = {"schema": "surreal-visual-qa-shadow-proposal-v1",
                    "action": "walk_to_actor", "target_identity": target,
                    "wait_ticks": 0, "confidence": 0.8, "rationale": "Walk there."}
        observation, observation_raw, fixture, report = self.shadow_replay_values(proposal)
        validated = shadow_replay.validate_replay_fixture(
            fixture, observation, hashlib.sha256(observation_raw).hexdigest())
        result = shadow_replay.evaluate_shadow_report(
            report, validated, "c" * 64,
            hashlib.sha256(observation_raw).hexdigest(), "d" * 64)
        self.assertEqual(result["verdict"], "failed")
        self.assertEqual(result["shadow_candidate"]["kind"], "walk_to_actor")
        self.assertFalse(result["shadow_candidate"]["dispatch_authorized"])

    def test_shadow_replay_rejects_candidate_and_provenance_substitution(self):
        observation, observation_raw, fixture, report = self.shadow_replay_values()
        observation_hash = hashlib.sha256(observation_raw).hexdigest()
        validated = shadow_replay.validate_replay_fixture(
            fixture, observation, observation_hash)
        report["results"][0]["shadow_candidate"] = {"kind": "interact"}
        with self.assertRaises(shadow_replay.VisualQAError):
            shadow_replay.evaluate_shadow_report(
                report, validated, "c" * 64, observation_hash, "d" * 64)
        report["results"][0]["shadow_candidate"] = None
        report["provenance"]["shadow_observation_sha256"] = "e" * 64
        with self.assertRaises(shadow_replay.VisualQAError):
            shadow_replay.evaluate_shadow_report(
                report, validated, "c" * 64, observation_hash, "d" * 64)

    def test_shadow_replay_rejects_duplicate_curated_proposals(self):
        accepted = [{"action": "none", "target_identity": "", "wait_ticks": 0}] * 2
        observation, observation_raw, fixture, _ = self.shadow_replay_values(
            accepted=accepted)
        with self.assertRaises(shadow_replay.VisualQAError):
            shadow_replay.validate_replay_fixture(
                fixture, observation, hashlib.sha256(observation_raw).hexdigest())

    def test_attempt_receipts_pin_generator_and_preserve_failures(self):
        args = types.SimpleNamespace(
            attempt_id="fixture:model:1", attempt_repeat=1,
            model="qwen:test", seed=104729)
        report_hash = "a" * 64
        success = visual_qa.attempt_receipt(
            args, "report_written", 123, report_hash)
        self.assertEqual(success["generator_sha256"], hashlib.sha256(
            pathlib.Path(visual_qa.__file__).read_bytes()).hexdigest())
        self.assertEqual(success["outcome"]["report_sha256"], report_hash)
        self.assertFalse(success["dispatch_authorized"])
        self.assertFalse(success["controls_live_player"])
        failed = visual_qa.attempt_receipt(
            args, "failed", 456, error_code="validation_error",
            error="model finding was invalid")
        self.assertEqual(failed["outcome"]["status"], "failed")
        self.assertIsNone(failed["outcome"]["report_sha256"])
        self.assertEqual(visual_qa.classify_attempt_error(
            visual_qa.VisualQAError("model finding has unexpected fields")),
            "validation_error")

    def test_cli_writes_failure_receipt_when_analysis_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt_path = root / "receipt.json"
            completed = subprocess.run([
                sys.executable, str(MODULE_PATH),
                "--run-manifest", str(root / "missing-manifest.json"),
                "--profile", str(root / "missing-profile.json"),
                "--model", "synthetic:test", "--output", str(root / "report.json"),
                "--receipt", str(receipt_path), "--attempt-id", "rejected:1",
                "--attempt-repeat", "1", "--ollama-url", "https://example.com",
            ], capture_output=True, text=True, timeout=10)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(receipt["outcome"]["status"], "failed")
        self.assertIsNone(receipt["outcome"]["report_sha256"])
        self.assertFalse(receipt["controls_live_player"])
        self.assertFalse(receipt["dispatch_authorized"])


if __name__ == "__main__":
    unittest.main()
