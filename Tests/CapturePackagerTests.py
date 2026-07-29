#!/usr/bin/env python3

import binascii
import json
import pathlib
import struct
import sys
import tempfile
import types
import unittest
import zlib
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "visual-qa"))

import bind_automation_capture
import shadow_eval
import shadow_replay
import visual_qa


def png_rgba(red=17, green=34, blue=51, alpha=255):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes((0, red, green, blue, alpha)))) +
            chunk(b"IEND", b""))


class CapturePackagerTests(unittest.TestCase):
    def test_artifact_sizes_use_their_own_bounds(self):
        self.assertEqual(
            bind_automation_capture.decimal(
                "2469577", "capture byte size", positive=True,
                maximum=bind_automation_capture.MAX_IMAGE_BYTES),
            "2469577")
        with self.assertRaises(visual_qa.VisualQAError):
            bind_automation_capture.decimal(
                str(bind_automation_capture.MAX_IMAGE_BYTES + 1),
                "capture byte size", positive=True,
                maximum=bind_automation_capture.MAX_IMAGE_BYTES)

    def write_json(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n",
                        encoding="utf-8", newline="\n")

    def fixture(self, root):
        capture_root = root / "visual-capture"
        capture_root.mkdir(parents=True)
        binary = root / "SurrealEngine.exe"
        binary.write_bytes(b"synthetic production binary")
        image = png_rgba()
        image_path = capture_root / "frame-tick-1.png"
        image_path.write_bytes(image)
        observation = {
            "schema": "surreal-automation-observation-v1",
            "revision": "2", "tick": "1",
            "player_identity": "actor:DeusEx.JCDentonMale:JCDentonMale0#0",
            "player_position": {"x": 0.0, "y": 0.0, "z": 0.0},
            "targets": [],
        }
        observation_path = capture_root / "observation-tick-1.json"
        self.write_json(observation_path, observation)
        observation_raw = observation_path.read_bytes()
        command = {
            "schema": "surreal-automation-command-v1", "command_id": "wait-1",
            "kind": "wait", "issued_tick": "0", "deadline_tick": "10",
            "target": None, "point": None, "arrival_radius": None,
            "wait_ticks": "1", "abort_command_id": "",
        }
        manifest = {
            "schema": "surreal-player-automation-manifest-v4",
            "mode": "deterministic-player-automation-visual-capture",
            "config_identity": "1234567890abcdef", "url": "00_TrainingFinal",
            "output_directory": str(root), "seed": "104729", "max_ticks": "10",
            "fixed_delta": 0.02,
            "visual_capture": {
                "schema": "surreal-player-automation-capture-request-v1",
                "tick": "1", "session_id": "session-1",
                "source_revision": "c" * 40, "source_dirty": True,
                "output_directory": "visual-capture",
            },
            "command_digest": "fnv1a64:0000000000000000", "command": command,
            "abort_command_digest": None, "abort_command": None,
            "followup_command_template_digest": None,
            "followup_command_template": None,
            "followup_command_artifact": None,
            "followup_observation_artifact": None,
            "sequence_summary_artifact": None,
        }
        self.write_json(root / "manifest.json", manifest)
        summary = {
            "schema": "surreal-automation-result-v1", "command_id": "wait-1",
            "kind": "wait", "state": "succeeded", "tick": "1",
            "observation_revision": "2", "target_identity": "",
            "reason": "bounded wait completed",
        }
        self.write_json(root / "summary.json", summary)
        events = [
            {"schema": "surreal-automation-telemetry-v1", "seq": "1", "tick": "0",
             "observation_revision": "0", "config_id": "1234567890abcdef",
             "event": "command_issued", "command_id": "wait-1", "kind": "wait",
             "state": None, "target_identity": "", "detail": "issued",
             "position": None, "distance": None},
            {"schema": "surreal-automation-telemetry-v1", "seq": "2", "tick": "1",
             "observation_revision": "2", "config_id": "1234567890abcdef",
             "event": "command_result", "command_id": "wait-1", "kind": "wait",
             "state": "succeeded", "target_identity": "",
             "detail": "bounded wait completed", "position": None, "distance": None},
        ]
        (root / "events.jsonl").write_text(
            "".join(json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n"
                    for item in events), encoding="utf-8", newline="\n")
        receipt = {
            "schema": "surreal-visual-qa-presented-capture-receipt-v1",
            "session": {"session_id": "session-1", "source_revision": "c" * 40,
                        "source_dirty": True},
            "capture": {
                "id": "wait-1-tick-1", "path": "frame-tick-1.png",
                "byte_size": str(len(image)),
                "fnv1a64": bind_automation_capture.fnv1a64(image), "tick": "1",
                "camera": "viewport-player-desktop", "width": 1, "height": 1,
                "renderer": "vulkan", "capture_method": "render-device-read-pixels",
                "capture_phase": "post-simulation-no-further-world-tick",
            },
            "observation": {
                "path": "observation-tick-1.json", "byte_size": str(len(observation_raw)),
                "fnv1a64": bind_automation_capture.fnv1a64(observation_raw),
                "revision": "2", "tick": "1",
            },
            "automation": {"manifest_path": "../manifest.json",
                           "events_path": "../events.jsonl",
                           "summary_path": "../summary.json", "command_id": "wait-1",
                           "config_identity": "1234567890abcdef"},
            "controls_live_player": False, "dispatch_authorized": False,
        }
        receipt_path = capture_root / "capture-receipt.json"
        self.write_json(receipt_path, receipt)
        return receipt_path, binary, receipt

    def pre_stock_fixture(self, root):
        receipt_path, binary, receipt = self.fixture(root)
        target_identity = "actor:DeusEx.Switch1:Switch1#0"
        observation_path = root / "visual-capture" / "observation-tick-1.json"
        observation = json.loads(observation_path.read_text(encoding="utf-8"))
        observation["targets"] = [{
            "identity": target_identity, "class": "DeusEx.Switch1",
            "owner_identity": "", "state_token": "Active", "tag": "Switch1",
            "event": "FirstStealthDoor", "inventory_resource": None,
            "location": {"x": -144.0, "y": -36.0, "z": 58.0},
            "deleted": False, "reachable": False, "interactable": True,
            "acquirable": False,
        }]
        self.write_json(observation_path, observation)
        observation_raw = observation_path.read_bytes()

        manifest_path = root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["schema"] = "surreal-player-automation-manifest-v5"
        manifest["mode"] = "deterministic-player-automation-pre-action-visual-capture"
        manifest["visual_capture"] = {
            **manifest["visual_capture"],
            "schema": "surreal-player-automation-capture-request-v2",
            "phase": "pre_stock_interaction",
        }
        manifest["command"] = {
            "schema": "surreal-automation-command-v1", "command_id": "interact-1",
            "kind": "interact", "issued_tick": "0", "deadline_tick": "10",
            "target": {"observation_revision": "1", "identity": target_identity,
                       "expected_class": "DeusEx.Switch1"},
            "point": None, "arrival_radius": 16.0, "wait_ticks": "0",
            "abort_command_id": "",
        }
        self.write_json(manifest_path, manifest)

        summary = {
            "schema": "surreal-automation-result-v1", "command_id": "interact-1",
            "kind": "interact", "state": "succeeded", "tick": "2",
            "observation_revision": "3", "target_identity": target_identity,
            "reason": "exact interaction receiver moved",
        }
        self.write_json(root / "summary.json", summary)
        common = {"schema": "surreal-automation-telemetry-v1",
                  "config_id": "1234567890abcdef", "command_id": "interact-1",
                  "kind": "interact", "position": None, "distance": None}
        events = [
            {**common, "seq": "1", "tick": "0", "observation_revision": "0",
             "event": "command_issued", "state": None, "target_identity": "",
             "detail": "issued"},
            {**common, "seq": "2", "tick": "0", "observation_revision": "1",
             "event": "target_resolved", "state": None,
             "target_identity": target_identity, "detail": "resolved"},
            {**common, "seq": "3", "tick": "0", "observation_revision": "1",
             "event": "command_accepted", "state": "accepted",
             "target_identity": target_identity, "detail": "accepted"},
            {**common, "seq": "4", "tick": "1", "observation_revision": "2",
             "event": "visual_capture_published", "state": "running",
             "target_identity": target_identity, "detail": "published"},
            {**common, "seq": "5", "tick": "1", "observation_revision": "2",
             "event": "interaction_attempt", "state": "running",
             "target_identity": target_identity, "detail": "pressed"},
            {**common, "seq": "6", "tick": "2", "observation_revision": "3",
             "event": "command_result", "state": "succeeded",
             "target_identity": target_identity,
             "detail": "exact interaction receiver moved"},
        ]
        (root / "events.jsonl").write_text(
            "".join(json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n"
                    for item in events), encoding="utf-8", newline="\n")

        receipt["schema"] = "surreal-visual-qa-presented-capture-receipt-v2"
        receipt["capture"]["id"] = "interact-1-tick-1"
        receipt["capture"]["capture_phase"] = \
            "pre-stock-interaction-before-input"
        receipt["observation"] = {
            **receipt["observation"], "byte_size": str(len(observation_raw)),
            "fnv1a64": bind_automation_capture.fnv1a64(observation_raw),
        }
        receipt["automation"]["command_id"] = "interact-1"
        receipt["barrier"] = {
            "schema": "surreal-player-automation-input-barrier-v1",
            "phase": "pre_stock_interaction", "telemetry_event_sequence": "4",
            "telemetry_event": "visual_capture_published",
            "synthetic_input_requests_before_render": "100",
            "synthetic_input_requests_after_render": "100",
            "synthetic_interaction_presses_before_render": "0",
            "synthetic_interaction_presses_after_render": "0",
        }
        self.write_json(receipt_path, receipt)
        return receipt_path, binary, receipt

    def pre_action_walk_fixture(self, root):
        receipt_path, binary, receipt = self.fixture(root)
        target_identity = "actor:Engine.Light:Light155#0"
        observation_path = root / "visual-capture" / "observation-tick-1.json"
        observation = json.loads(observation_path.read_text(encoding="utf-8"))
        observation.update(revision="1", tick="0")
        observation["targets"] = [{
            "identity": target_identity, "class": "Engine.Light",
            "owner_identity": "", "state_token": "", "tag": "Light155",
            "event": "", "inventory_resource": None,
            "location": {"x": -50.0, "y": 25.0, "z": 16.0},
            "deleted": False, "reachable": True, "interactable": False,
            "acquirable": False,
        }]
        self.write_json(observation_path, observation)
        observation_raw = observation_path.read_bytes()

        manifest_path = root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["schema"] = "surreal-player-automation-manifest-v5"
        manifest["mode"] = "deterministic-player-automation-pre-action-visual-capture"
        manifest["max_ticks"] = "100"
        manifest["visual_capture"] = {
            **manifest["visual_capture"], "tick": "0",
            "schema": "surreal-player-automation-capture-request-v2",
            "phase": "pre_action",
        }
        manifest["command"] = {
            "schema": "surreal-automation-command-v1", "command_id": "walk-to-actor-1",
            "kind": "walk_to_actor", "issued_tick": "0", "deadline_tick": "100",
            "target": {"observation_revision": "1", "identity": target_identity,
                       "expected_class": "Engine.Light"},
            "point": None, "arrival_radius": 40.0, "wait_ticks": "0",
            "abort_command_id": "",
        }
        self.write_json(manifest_path, manifest)
        summary = {
            "schema": "surreal-automation-result-v1",
            "command_id": "walk-to-actor-1", "kind": "walk_to_actor",
            "state": "succeeded", "tick": "53", "observation_revision": "54",
            "target_identity": target_identity,
            "reason": "player reached the configured arrival envelope",
        }
        self.write_json(root / "summary.json", summary)
        common = {"schema": "surreal-automation-telemetry-v1",
                  "config_id": "1234567890abcdef",
                  "command_id": "walk-to-actor-1", "kind": "walk_to_actor",
                  "position": None, "distance": None}
        events = [
            {**common, "seq": "1", "tick": "0", "observation_revision": "0",
             "event": "command_issued", "state": None, "target_identity": "",
             "detail": "issued"},
            {**common, "seq": "2", "tick": "0", "observation_revision": "1",
             "event": "target_resolved", "state": None,
             "target_identity": target_identity, "detail": "resolved"},
            {**common, "seq": "3", "tick": "0", "observation_revision": "1",
             "event": "command_accepted", "state": "accepted",
             "target_identity": target_identity, "detail": "accepted"},
            {**common, "seq": "4", "tick": "0", "observation_revision": "1",
             "event": "visual_capture_published", "state": "running",
             "target_identity": target_identity, "detail": "published"},
            {**common, "seq": "5", "tick": "1", "observation_revision": "2",
             "event": "command_progress", "state": "running",
             "target_identity": target_identity,
             "detail": "ordinary player axes applied"},
            {**common, "seq": "6", "tick": "53", "observation_revision": "54",
             "event": "command_result", "state": "succeeded",
             "target_identity": target_identity,
             "detail": "player reached the configured arrival envelope"},
        ]
        (root / "events.jsonl").write_text(
            "".join(json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n"
                    for item in events), encoding="utf-8", newline="\n")

        receipt["schema"] = "surreal-visual-qa-presented-capture-receipt-v2"
        receipt["capture"].update(
            id="walk-to-actor-1-tick-0", tick="0",
            capture_phase="pre-action-no-world-tick")
        receipt["observation"] = {
            **receipt["observation"], "byte_size": str(len(observation_raw)),
            "fnv1a64": bind_automation_capture.fnv1a64(observation_raw),
            "revision": "1", "tick": "0",
        }
        receipt["automation"]["command_id"] = "walk-to-actor-1"
        receipt["barrier"] = {
            "schema": "surreal-player-automation-input-barrier-v1",
            "phase": "pre_action", "telemetry_event_sequence": "4",
            "telemetry_event": "visual_capture_published",
            "synthetic_input_requests_before_render": "0",
            "synthetic_input_requests_after_render": "0",
            "synthetic_interaction_presses_before_render": "0",
            "synthetic_interaction_presses_after_render": "0",
        }
        self.write_json(receipt_path, receipt)
        return receipt_path, binary, receipt

    def pre_pickup_fixture(self, root):
        receipt_path, binary, receipt = self.fixture(root)
        target_identity = "actor:DeusEx.TechGoggles:TechGoggles0#0"
        player_identity = "actor:DeusEx.JCDentonMale:JCDentonMale0#0"
        observation_path = root / "visual-capture" / "observation-tick-1.json"
        observation = json.loads(observation_path.read_text(encoding="utf-8"))
        observation.update(schema="surreal-automation-observation-v2",
                           revision="3", tick="2")
        target = {
            "identity": target_identity, "class": "DeusEx.TechGoggles",
            "owner_identity": "", "state_token": "Pickup",
            "tag": "TechGoggles", "event": "",
            "inventory_resource": {"kind": "num_copies", "value": "1"},
            "location": {"x": -79.0, "y": 220.0, "z": 30.0},
            "deleted": False, "reachable": False, "interactable": True,
            "acquirable": True,
        }
        observation["targets"] = [target]
        self.write_json(observation_path, observation)
        observation_raw = observation_path.read_bytes()

        effect = {**observation, "revision": "4", "tick": "3",
                  "targets": [{**target, "owner_identity": player_identity,
                               "state_token": "Idle2", "interactable": False,
                               "acquirable": False}]}
        self.write_json(root / "final-observation.json", effect)

        manifest_path = root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["schema"] = "surreal-player-automation-manifest-v5"
        manifest["mode"] = "deterministic-player-automation-pre-action-visual-capture"
        manifest["visual_capture"] = {
            **manifest["visual_capture"], "tick": "2",
            "schema": "surreal-player-automation-capture-request-v2",
            "phase": "pre_pickup",
        }
        manifest["command"] = {
            "schema": "surreal-automation-command-v1",
            "command_id": "acquire-item-1", "kind": "acquire_item",
            "issued_tick": "0", "deadline_tick": "10",
            "target": {"observation_revision": "1", "identity": target_identity,
                       "expected_class": "DeusEx.TechGoggles"},
            "point": None, "arrival_radius": 40.0, "wait_ticks": "0",
            "abort_command_id": "",
        }
        self.write_json(manifest_path, manifest)
        summary = {
            "schema": "surreal-automation-result-v1",
            "command_id": "acquire-item-1", "kind": "acquire_item",
            "state": "succeeded", "tick": "3", "observation_revision": "4",
            "target_identity": target_identity,
            "reason": "exact target ownership transferred to the player",
        }
        self.write_json(root / "summary.json", summary)
        common = {"schema": "surreal-automation-telemetry-v1",
                  "config_id": "1234567890abcdef",
                  "command_id": "acquire-item-1", "kind": "acquire_item",
                  "position": None, "distance": None}
        switch_identity = "actor:DeusEx.Switch1:Switch1#0"
        events = [
            {**common, "seq": "1", "tick": "0", "observation_revision": "0",
             "event": "command_issued", "state": None, "target_identity": "",
             "detail": "issued"},
            {**common, "seq": "2", "tick": "0", "observation_revision": "1",
             "event": "target_resolved", "state": None,
             "target_identity": target_identity, "detail": "resolved"},
            {**common, "seq": "3", "tick": "0", "observation_revision": "1",
             "event": "command_accepted", "state": "accepted",
             "target_identity": target_identity, "detail": "accepted"},
            {**common, "seq": "4", "tick": "1", "observation_revision": "2",
             "event": "interaction_attempt", "state": "running",
             "target_identity": switch_identity, "detail": "intermediate switch"},
            {**common, "seq": "5", "tick": "2", "observation_revision": "3",
             "event": "visual_capture_published", "state": "running",
             "target_identity": target_identity, "detail": "published"},
            {**common, "seq": "6", "tick": "2", "observation_revision": "3",
             "event": "interaction_attempt", "state": "running",
             "target_identity": target_identity,
             "detail": "stock ParseRightClick input pressed for the exact FrobTarget"},
            {**common, "seq": "7", "tick": "3", "observation_revision": "4",
             "event": "command_result", "state": "succeeded",
             "target_identity": target_identity,
             "detail": "exact target ownership transferred to the player"},
        ]
        (root / "events.jsonl").write_text(
            "".join(json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n"
                    for item in events), encoding="utf-8", newline="\n")

        receipt["schema"] = "surreal-visual-qa-presented-capture-receipt-v3"
        receipt["capture"].update(
            id="acquire-item-1-tick-2", tick="2",
            capture_phase="pre-pickup-before-input")
        receipt["observation"] = {
            **receipt["observation"], "byte_size": str(len(observation_raw)),
            "fnv1a64": bind_automation_capture.fnv1a64(observation_raw),
            "revision": "3", "tick": "2",
        }
        receipt["automation"]["command_id"] = "acquire-item-1"
        receipt["barrier"] = {
            "schema": "surreal-player-automation-input-barrier-v2",
            "phase": "pre_pickup", "telemetry_event_sequence": "5",
            "telemetry_event": "visual_capture_published",
            "synthetic_input_requests_before_render": "101",
            "synthetic_input_requests_after_render": "101",
            "synthetic_interaction_presses_before_render": "1",
            "synthetic_interaction_presses_after_render": "1",
        }
        self.write_json(receipt_path, receipt)
        return receipt_path, binary, receipt

    def test_completed_runtime_capture_binds_deterministically(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.fixture(root)
            first = bind_automation_capture.bind_capture(receipt, binary)
            second = bind_automation_capture.bind_capture(receipt, binary)
            self.assertEqual(first, second)
            self.assertEqual(first["session"]["config_sha256"],
                             visual_qa.sha256_bytes((root / "manifest.json").read_bytes()))
            self.assertEqual(first["captures"][0]["command_id"], "wait-1")
            captures = visual_qa.validate_manifest(
                first, receipt.parent / "capture-manifest.json")
            self.assertEqual(captures[0]["_grounding"]["observation_tick"], "1")

    def test_pre_stock_interaction_binds_runtime_manifest_v3(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.pre_stock_fixture(root)
            manifest = bind_automation_capture.bind_capture(receipt, binary)
            self.assertEqual(manifest["schema"],
                             "surreal-visual-qa-capture-manifest-v3")
            self.assertTrue(manifest["session"]["source_dirty"])
            self.assertEqual(manifest["binding"]["barrier_event_sequence"], "4")
            captures = visual_qa.validate_manifest(
                manifest, root / "capture-manifest.json")
            self.assertEqual(captures[0]["_grounding"]["schema"],
                             "surreal-visual-qa-grounding-v2")
            self.assertEqual(visual_qa.validate_shadow_grounding(
                captures,
                json.loads((root / "visual-capture" /
                            "observation-tick-1.json").read_text(encoding="utf-8")),
                (root / "visual-capture" / "observation-tick-1.json").read_bytes()),
                "pre_action_bound")

    def test_pre_pickup_binds_exact_ownership_effect_manifest_v4(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.pre_pickup_fixture(root)
            manifest = bind_automation_capture.bind_capture(receipt, binary)
            self.assertEqual(manifest["schema"],
                             "surreal-visual-qa-capture-manifest-v4")
            self.assertEqual(manifest["binding"]["barrier_event_sequence"], "5")
            self.assertEqual(manifest["binding"]["action_event_sequence"], "6")
            self.assertEqual(manifest["binding"]["result_event_sequence"], "7")
            captures = visual_qa.validate_manifest(
                manifest, root / "capture-manifest.json")
            grounding = captures[0]["_grounding"]
            self.assertEqual(grounding["schema"],
                             "surreal-visual-qa-grounding-v3")
            self.assertEqual(grounding["effect_proof"],
                             "exact_target_ownership_transfer")
            self.assertEqual(visual_qa.validate_shadow_grounding(
                captures,
                json.loads((root / "visual-capture" /
                            "observation-tick-1.json").read_text(encoding="utf-8")),
                (root / "visual-capture" / "observation-tick-1.json").read_bytes()),
                "pre_effect_bound")

    def test_manifest_v4_replays_only_its_bound_pickup_effect(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.pre_pickup_fixture(root)
            manifest = bind_automation_capture.bind_capture(receipt, binary)
            manifest_path = root / "capture-manifest.json"
            self.write_json(manifest_path, manifest)
            profile = {
                "schema": "surreal-visual-qa-assertion-profile-v1",
                "profile_id": "pre-pickup-v1", "prompt": "Evaluate pickup.",
                "assertions": [{"id": "pickup", "description": "Pickup is ready"}],
            }
            profile_path = root / "profile.json"
            self.write_json(profile_path, profile)
            target_identity = "actor:DeusEx.TechGoggles:TechGoggles0#0"
            finding = {
                "schema": "surreal-visual-qa-finding-v1",
                "capture_id": "acquire-item-1-tick-2", "summary": "synthetic",
                "findings": [{"assertion_id": "pickup", "status": "pass",
                              "confidence": 1.0, "explanation": "synthetic"}],
                "proposal": {
                    "schema": "surreal-visual-qa-shadow-proposal-v1",
                    "action": "acquire_item", "target_identity": target_identity,
                    "wait_ticks": 0, "confidence": 1.0,
                    "rationale": "synthetic runtime-bound pickup",
                },
            }
            args = types.SimpleNamespace(
                seed=104729, timeout=30,
                ollama_url="http://127.0.0.1:11434",
                run_manifest=str(manifest_path), profile=str(profile_path),
                model="synthetic-vision:v1",
                shadow_observation=str(root / "visual-capture" /
                                       "observation-tick-1.json"),
                shadow_action=["acquire_item"],
                shadow_target_identity=[target_identity],
                shadow_max_wait_ticks=60,
            )
            with mock.patch.object(visual_qa, "resolve_model", return_value=(
                    "synthetic-vision:v1", "b" * 64,
                    {"capabilities": ["vision"]})), \
                    mock.patch.object(visual_qa, "http_json",
                                      side_effect=lambda url, *unused:
                                      {"version": "test"} if url.endswith("/api/version")
                                      else {"message": {"content": json.dumps(finding)}}):
                report = visual_qa.analyze(args)
            self.assertEqual(report["provenance"]["shadow_grounding"],
                             "pre_effect_bound")
            self.assertEqual(report["provenance"]["shadow_action_barrier"],
                             "pre_effect_bound")

            report_path = root / "report.json"
            self.write_json(report_path, report)
            fixture = {
                "schema": "surreal-visual-qa-shadow-replay-fixture-v4",
                "fixture_id": "pre-pickup-v1",
                "capture": {"id": "acquire-item-1-tick-2",
                            "sha256": manifest["captures"][0]["sha256"]},
                "observation_sha256": manifest["captures"][0]
                    ["observation"]["sha256"],
                "capture_manifest_sha256":
                    visual_qa.sha256_bytes(manifest_path.read_bytes()),
                "policy": {"allowed_actions": ["acquire_item"],
                           "target_identities": [target_identity],
                           "max_wait_ticks": 60},
                "accepted_proposals": [{"action": "acquire_item",
                                        "target_identity": target_identity,
                                        "wait_ticks": 0}],
            }
            fixture_path = root / "fixture.json"
            self.write_json(fixture_path, fixture)
            replay = shadow_replay.replay(
                fixture_path,
                root / "visual-capture" / "observation-tick-1.json",
                report_path, manifest_path)
            self.assertEqual(replay["schema"],
                             "surreal-visual-qa-shadow-replay-result-v3")
            self.assertEqual(replay["capture_authority"], "pre_effect_bound")
            self.assertEqual(replay["verdict"], "passed")
            self.assertFalse(replay["dispatch_authorized"])

            observation_path = root / "visual-capture" / "observation-tick-1.json"
            protocol_path = root / "labeling-protocol.md"
            protocol_path.write_bytes(b"Two independent reviewers.\n")

            def artifact(path):
                return {"path": path.relative_to(root).as_posix(),
                        "sha256": visual_qa.sha256_bytes(path.read_bytes())}

            accepted = fixture["accepted_proposals"][0]
            labels = []
            for reviewer in ("reviewer-a", "reviewer-b"):
                label_path = root / f"{reviewer}.json"
                self.write_json(label_path, {
                    "schema": "surreal-visual-qa-shadow-human-label-v1",
                    "case_id": "pickup-v1", "annotator_id": reviewer,
                    "capture_manifest_sha256": artifact(manifest_path)["sha256"],
                    "profile_sha256": artifact(profile_path)["sha256"],
                    "observation_sha256": artifact(observation_path)["sha256"],
                    "accepted_proposals": [accepted], "notes": "synthetic",
                })
                labels.append(artifact(label_path))
            adjudication_path = root / "adjudication.json"
            self.write_json(adjudication_path, {
                "schema": "surreal-visual-qa-shadow-adjudication-v1",
                "case_id": "pickup-v1",
                "label_sha256s": sorted(item["sha256"] for item in labels),
                "accepted_proposals": [accepted], "notes": "synthetic",
            })
            dataset_path = root / "dataset.json"
            self.write_json(dataset_path, {
                "schema": "surreal-visual-qa-shadow-eval-dataset-v3",
                "dataset_id": "pickup-v1",
                "labeling_protocol": artifact(protocol_path),
                "cases": [{
                    "case_id": "pickup-v1", "expected_action": "acquire_item",
                    "stratum": "canonical",
                    "capture_manifest": artifact(manifest_path),
                    "profile": artifact(profile_path),
                    "observation": artifact(observation_path),
                    "replay_fixture": artifact(fixture_path), "labels": labels,
                    "adjudication": artifact(adjudication_path),
                }],
                "controls_live_player": False, "dispatch_authorized": False,
            })
            validated_dataset = shadow_eval.validate_dataset(dataset_path)
            self.assertEqual(validated_dataset["cases"]["pickup-v1"]
                             ["grounding_status"], "pre_effect_bound")

            attempt_id = "pickup-v1:synthetic-vision:v1:1"
            attempt_path = root / "attempt-receipt.json"
            receipt_args = types.SimpleNamespace(
                attempt_id=attempt_id, attempt_repeat=1,
                model="synthetic-vision:v1", seed=104729)
            self.write_json(attempt_path, visual_qa.attempt_receipt(
                receipt_args, "report_written", 1000,
                visual_qa.sha256_bytes(report_path.read_bytes())))
            run_path = root / "evaluation-run.json"
            self.write_json(run_path, {
                "schema": "surreal-visual-qa-shadow-eval-run-v1",
                "run_id": "pickup-v1",
                "dataset_sha256": artifact(dataset_path)["sha256"],
                "generator_sha256": report["provenance"]["generator_sha256"],
                "ollama_version": "test", "seed": "104729", "repeat_count": 1,
                "models": [{"tag": "synthetic-vision:v1", "digest": "b" * 64}],
                "attempts": [{"attempt_id": attempt_id, "case_id": "pickup-v1",
                              "repeat": 1, "model": "synthetic-vision:v1",
                              "receipt": artifact(attempt_path),
                              "report": artifact(report_path)}],
                "controls_live_player": False, "dispatch_authorized": False,
            })
            evaluation = shadow_eval.evaluate(dataset_path, run_path)
            self.assertEqual(evaluation["schema"],
                             "surreal-visual-qa-shadow-eval-result-v3")
            self.assertEqual(evaluation["case_authorities"][0]
                             ["capture_authority"], "pre_effect_bound")
            self.assertEqual(evaluation["attempts"][0]["status"], "passed")

    def test_manifest_v3_allows_only_its_bound_positive_model_proposal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.pre_stock_fixture(root)
            manifest = bind_automation_capture.bind_capture(receipt, binary)
            manifest_path = root / "capture-manifest.json"
            self.write_json(manifest_path, manifest)
            profile = {
                "schema": "surreal-visual-qa-assertion-profile-v1",
                "profile_id": "pre-stock-switch-v1",
                "prompt": "Evaluate the exact bounded interaction.",
                "assertions": [{"id": "switch-actionable",
                                "description": "The selected switch is actionable"}],
            }
            profile_path = root / "profile.json"
            self.write_json(profile_path, profile)
            target_identity = "actor:DeusEx.Switch1:Switch1#0"
            proposal = {
                "schema": "surreal-visual-qa-shadow-proposal-v1",
                "action": "interact", "target_identity": target_identity,
                "wait_ticks": 0, "confidence": 1.0,
                "rationale": "synthetic runtime-bound action",
            }
            finding = {
                "schema": "surreal-visual-qa-finding-v1",
                "capture_id": "interact-1-tick-1", "summary": "synthetic",
                "findings": [{"assertion_id": "switch-actionable",
                              "status": "pass", "confidence": 1.0,
                              "explanation": "synthetic"}],
                "proposal": proposal,
            }
            args = types.SimpleNamespace(
                seed=104729, timeout=30,
                ollama_url="http://127.0.0.1:11434",
                run_manifest=str(manifest_path), profile=str(profile_path),
                model="synthetic-vision:v1",
                shadow_observation=str(root / "visual-capture" /
                                       "observation-tick-1.json"),
                shadow_action=["interact"],
                shadow_target_identity=[target_identity],
                shadow_max_wait_ticks=60,
            )
            with mock.patch.object(visual_qa, "resolve_model", return_value=(
                    "synthetic-vision:v1", "b" * 64,
                    {"capabilities": ["vision"]})), \
                    mock.patch.object(visual_qa, "http_json",
                                      side_effect=lambda url, *unused:
                                      {"version": "test"} if url.endswith("/api/version")
                                      else {"message": {"content": json.dumps(finding)}}):
                report = visual_qa.analyze(args)
            self.assertEqual(report["provenance"]["shadow_grounding"],
                             "pre_action_bound")
            self.assertEqual(report["provenance"]["shadow_action_barrier"],
                             "pre_action_bound")
            self.assertEqual(report["results"][0]["shadow_candidate"]["kind"],
                             "interact")
            self.assertFalse(report["results"][0]["shadow_candidate"]
                             ["dispatch_authorized"])
            self.assertFalse(report["controls_live_player"])

            fixture = {
                "schema": "surreal-visual-qa-shadow-replay-fixture-v3",
                "fixture_id": "pre-stock-switch-v1",
                "capture": {
                    "id": "interact-1-tick-1",
                    "sha256": manifest["captures"][0]["sha256"],
                },
                "observation_sha256": manifest["captures"][0]
                    ["observation"]["sha256"],
                "capture_manifest_sha256":
                    visual_qa.sha256_bytes(manifest_path.read_bytes()),
                "policy": {
                    "allowed_actions": ["interact"],
                    "target_identities": [target_identity],
                    "max_wait_ticks": 60,
                },
                "accepted_proposals": [{
                    "action": "interact", "target_identity": target_identity,
                    "wait_ticks": 0,
                }],
            }
            fixture_path = root / "shadow-replay-fixture.json"
            self.write_json(fixture_path, fixture)
            report_path = root / "shadow-report.json"
            self.write_json(report_path, report)
            replay = shadow_replay.replay(
                fixture_path,
                root / "visual-capture" / "observation-tick-1.json",
                report_path, manifest_path)
            self.assertEqual(replay["verdict"], "passed")
            self.assertEqual(replay["schema"],
                             "surreal-visual-qa-shadow-replay-result-v2")
            self.assertEqual(replay["capture_authority"], "pre_action_bound")
            self.assertEqual(replay["proposal"]["action"], "interact")
            self.assertFalse(replay["dispatch_authorized"])

            substituted = json.loads(json.dumps(report))
            substituted["provenance"].pop("shadow_action_barrier")
            self.write_json(report_path, substituted)
            with self.assertRaises(visual_qa.VisualQAError):
                shadow_replay.replay(
                    fixture_path,
                    root / "visual-capture" / "observation-tick-1.json",
                    report_path, manifest_path)

    def test_pre_action_walk_binds_its_first_ordinary_axis_effect(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.pre_action_walk_fixture(root)
            manifest = bind_automation_capture.bind_capture(receipt, binary)
            manifest_path = root / "capture-manifest.json"
            self.write_json(manifest_path, manifest)
            captures = visual_qa.validate_manifest(manifest, manifest_path)
            grounding = captures[0]["_grounding"]
            self.assertEqual(grounding["command_kind"], "walk_to_actor")
            self.assertEqual(grounding["observation_tick"], "0")
            self.assertEqual(manifest["binding"]["phase"], "pre_action")
            self.assertEqual(manifest["binding"]["action_event_sequence"], "5")

            target_identity = "actor:Engine.Light:Light155#0"
            profile_path = root / "walk-profile.json"
            self.write_json(profile_path, {
                "schema": "surreal-visual-qa-assertion-profile-v1",
                "profile_id": "pre-action-walk-v1",
                "prompt": "Evaluate the exact bounded walk target.",
                "assertions": [{"id": "walk-target-visible",
                                "description": "The walk target is visible"}],
            })
            proposal = {
                "schema": "surreal-visual-qa-shadow-proposal-v1",
                "action": "walk_to_actor", "target_identity": target_identity,
                "wait_ticks": 0, "confidence": 1.0, "rationale": "synthetic",
            }
            finding = {
                "schema": "surreal-visual-qa-finding-v1",
                "capture_id": "walk-to-actor-1-tick-0", "summary": "synthetic",
                "findings": [{"assertion_id": "walk-target-visible",
                              "status": "pass", "confidence": 1.0,
                              "explanation": "synthetic"}], "proposal": proposal,
            }
            args = types.SimpleNamespace(
                seed=104729, timeout=30,
                ollama_url="http://127.0.0.1:11434",
                run_manifest=str(manifest_path), profile=str(profile_path),
                model="synthetic-vision:v1",
                shadow_observation=str(root / "visual-capture" /
                                       "observation-tick-1.json"),
                shadow_action=["walk_to_actor"],
                shadow_target_identity=[target_identity],
                shadow_max_wait_ticks=60,
            )
            with mock.patch.object(visual_qa, "resolve_model", return_value=(
                    "synthetic-vision:v1", "b" * 64,
                    {"capabilities": ["vision"]})), \
                    mock.patch.object(visual_qa, "http_json",
                                      side_effect=lambda url, *unused:
                                      {"version": "test"} if url.endswith("/api/version")
                                      else {"message": {"content": json.dumps(finding)}}):
                report = visual_qa.analyze(args)
            self.assertEqual(report["results"][0]["shadow_candidate"]["kind"],
                             "walk_to_actor")
            self.assertFalse(report["results"][0]["shadow_candidate"]
                             ["dispatch_authorized"])

            fixture_path = root / "walk-fixture.json"
            self.write_json(fixture_path, {
                "schema": "surreal-visual-qa-shadow-replay-fixture-v3",
                "fixture_id": "pre-action-walk-v1",
                "capture": {"id": "walk-to-actor-1-tick-0",
                            "sha256": manifest["captures"][0]["sha256"]},
                "observation_sha256": manifest["captures"][0]
                    ["observation"]["sha256"],
                "capture_manifest_sha256":
                    visual_qa.sha256_bytes(manifest_path.read_bytes()),
                "policy": {"allowed_actions": ["walk_to_actor"],
                           "target_identities": [target_identity],
                           "max_wait_ticks": 60},
                "accepted_proposals": [{"action": "walk_to_actor",
                                        "target_identity": target_identity,
                                        "wait_ticks": 0}],
            })
            report_path = root / "walk-report.json"
            self.write_json(report_path, report)
            replay = shadow_replay.replay(
                fixture_path,
                root / "visual-capture" / "observation-tick-1.json",
                report_path, manifest_path)
            self.assertEqual(replay["verdict"], "passed")
            self.assertEqual(replay["capture_authority"], "pre_action_bound")

            events_path = root / "events.jsonl"
            events = [json.loads(line) for line in
                      events_path.read_text(encoding="utf-8").splitlines()]
            events[4]["detail"] = "movement claimed without exact axes"
            events_path.write_text(
                "".join(json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n"
                        for item in events), encoding="utf-8", newline="\n")
            with self.assertRaises(visual_qa.VisualQAError):
                bind_automation_capture.bind_capture(receipt, binary)

    def test_shadow_evaluation_v2_requires_manifest_v3_positive_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.pre_stock_fixture(root)
            manifest = bind_automation_capture.bind_capture(receipt, binary)
            manifest_path = root / "capture-manifest.json"
            self.write_json(manifest_path, manifest)
            target_identity = "actor:DeusEx.Switch1:Switch1#0"
            accepted = {"action": "interact", "target_identity": target_identity,
                        "wait_ticks": 0}
            fixture = {
                "schema": "surreal-visual-qa-shadow-replay-fixture-v3",
                "fixture_id": "pre-stock-switch-eval-v1",
                "capture": {"id": "interact-1-tick-1",
                            "sha256": manifest["captures"][0]["sha256"]},
                "observation_sha256": manifest["captures"][0]
                    ["observation"]["sha256"],
                "capture_manifest_sha256":
                    visual_qa.sha256_bytes(manifest_path.read_bytes()),
                "policy": {"allowed_actions": ["interact"],
                           "target_identities": [target_identity],
                           "max_wait_ticks": 60},
                "accepted_proposals": [accepted],
            }
            fixture_path = root / "fixture.json"
            self.write_json(fixture_path, fixture)
            profile = {
                "schema": "surreal-visual-qa-assertion-profile-v1",
                "profile_id": "pre-stock-switch-eval-v1",
                "prompt": "Evaluate the exact bounded interaction.",
                "assertions": [{"id": "switch-actionable",
                                "description": "The switch is actionable"}],
            }
            profile_path = root / "profile.json"
            self.write_json(profile_path, profile)
            observation_path = root / "visual-capture" / "observation-tick-1.json"
            protocol_path = root / "labeling-protocol.md"
            protocol_path.write_bytes(b"Two independent reviewers.\n")

            def artifact(path):
                return {"path": path.relative_to(root).as_posix(),
                        "sha256": visual_qa.sha256_bytes(path.read_bytes())}

            labels = []
            for reviewer in ("reviewer-a", "reviewer-b"):
                path = root / f"{reviewer}.json"
                self.write_json(path, {
                    "schema": "surreal-visual-qa-shadow-human-label-v1",
                    "case_id": "interact-switch-v1", "annotator_id": reviewer,
                    "capture_manifest_sha256": artifact(manifest_path)["sha256"],
                    "profile_sha256": artifact(profile_path)["sha256"],
                    "observation_sha256": artifact(observation_path)["sha256"],
                    "accepted_proposals": [accepted], "notes": "synthetic label",
                })
                labels.append(artifact(path))
            adjudication_path = root / "adjudication.json"
            self.write_json(adjudication_path, {
                "schema": "surreal-visual-qa-shadow-adjudication-v1",
                "case_id": "interact-switch-v1",
                "label_sha256s": sorted(item["sha256"] for item in labels),
                "accepted_proposals": [accepted], "notes": "synthetic agreement",
            })
            case = {
                "case_id": "interact-switch-v1", "expected_action": "interact",
                "stratum": "canonical", "capture_manifest": artifact(manifest_path),
                "profile": artifact(profile_path),
                "observation": artifact(observation_path),
                "replay_fixture": artifact(fixture_path), "labels": labels,
                "adjudication": artifact(adjudication_path),
            }
            dataset = {
                "schema": "surreal-visual-qa-shadow-eval-dataset-v2",
                "dataset_id": "pre-stock-switch-eval-v1",
                "labeling_protocol": artifact(protocol_path), "cases": [case],
                "controls_live_player": False, "dispatch_authorized": False,
            }
            dataset_path = root / "dataset.json"
            self.write_json(dataset_path, dataset)
            validated = shadow_eval.validate_dataset(dataset_path)
            self.assertEqual(validated["cases"]["interact-switch-v1"]
                             ["grounding_status"], "pre_action_bound")
            self.assertEqual(
                validated["cases"]["interact-switch-v1"]
                    ["pre_action_binding_sha256"],
                visual_qa.validate_manifest(manifest, manifest_path)[0]
                    ["_grounding"]["pre_action_binding_sha256"])

            proposal = {
                "schema": "surreal-visual-qa-shadow-proposal-v1",
                **accepted, "confidence": 1.0, "rationale": "synthetic",
            }
            finding = {
                "schema": "surreal-visual-qa-finding-v1",
                "capture_id": "interact-1-tick-1", "summary": "synthetic",
                "findings": [{"assertion_id": "switch-actionable",
                              "status": "pass", "confidence": 1.0,
                              "explanation": "synthetic"}],
                "proposal": proposal,
            }
            args = types.SimpleNamespace(
                seed=104729, timeout=30,
                ollama_url="http://127.0.0.1:11434",
                run_manifest=str(manifest_path), profile=str(profile_path),
                model="synthetic-vision:v1",
                shadow_observation=str(observation_path),
                shadow_action=["interact"],
                shadow_target_identity=[target_identity],
                shadow_max_wait_ticks=60,
            )
            with mock.patch.object(visual_qa, "resolve_model", return_value=(
                    "synthetic-vision:v1", "b" * 64,
                    {"capabilities": ["vision"]})), \
                    mock.patch.object(visual_qa, "http_json",
                                      side_effect=lambda url, *unused:
                                      {"version": "test"} if url.endswith("/api/version")
                                      else {"message": {"content": json.dumps(finding)}}):
                report = visual_qa.analyze(args)
            report_path = root / "eval-report.json"
            self.write_json(report_path, report)
            attempt_id = "interact-switch-v1:synthetic-vision:v1:1"
            receipt_args = types.SimpleNamespace(
                attempt_id=attempt_id, attempt_repeat=1,
                model="synthetic-vision:v1", seed=104729)
            attempt_path = root / "eval-receipt.json"
            self.write_json(attempt_path, visual_qa.attempt_receipt(
                receipt_args, "report_written", 1000,
                visual_qa.sha256_bytes(report_path.read_bytes())))
            run_path = root / "evaluation-run.json"
            self.write_json(run_path, {
                "schema": "surreal-visual-qa-shadow-eval-run-v1",
                "run_id": "pre-stock-switch-eval-v1",
                "dataset_sha256": visual_qa.sha256_bytes(dataset_path.read_bytes()),
                "generator_sha256": report["provenance"]["generator_sha256"],
                "ollama_version": "test", "seed": "104729", "repeat_count": 1,
                "models": [{"tag": "synthetic-vision:v1", "digest": "b" * 64}],
                "attempts": [{
                    "attempt_id": attempt_id, "case_id": "interact-switch-v1",
                    "repeat": 1, "model": "synthetic-vision:v1",
                    "receipt": artifact(attempt_path), "report": artifact(report_path),
                }],
                "controls_live_player": False, "dispatch_authorized": False,
            })
            evaluation = shadow_eval.evaluate(dataset_path, run_path)
            self.assertEqual(evaluation["schema"],
                             "surreal-visual-qa-shadow-eval-result-v2")
            self.assertEqual(evaluation["case_authorities"][0]
                             ["capture_authority"], "pre_action_bound")
            self.assertEqual(evaluation["attempts"][0]["status"], "passed")
            self.assertFalse(evaluation["dispatch_authorized"])

            legacy_dataset = {**dataset,
                              "schema": "surreal-visual-qa-shadow-eval-dataset-v1"}
            self.write_json(dataset_path, legacy_dataset)
            with self.assertRaises(visual_qa.VisualQAError):
                shadow_eval.validate_dataset(dataset_path)

    def test_pre_stock_barrier_substitutions_are_rejected(self):
        mutations = [
            ("interaction count", lambda root, receipt:
             receipt["barrier"].__setitem__(
                 "synthetic_interaction_presses_before_render", "1")),
            ("wrong phase", lambda root, receipt:
             receipt["barrier"].__setitem__("phase", "pre_action")),
            ("wrong sequence", lambda root, receipt:
             receipt["barrier"].__setitem__("telemetry_event_sequence", "3")),
        ]
        for name, mutate in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                receipt_path, binary, receipt = self.pre_stock_fixture(root)
                mutate(root, receipt)
                self.write_json(receipt_path, receipt)
                with self.assertRaises(visual_qa.VisualQAError):
                    bind_automation_capture.bind_capture(receipt_path, binary)

        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt_path, binary, _ = self.pre_stock_fixture(root)
            events_path = root / "events.jsonl"
            events = [json.loads(line) for line in
                      events_path.read_text(encoding="utf-8").splitlines()]
            events[1]["event"] = "interaction_attempt"
            events_path.write_text(
                "".join(json.dumps(item, sort_keys=True, separators=(",", ":")) + "\n"
                        for item in events), encoding="utf-8", newline="\n")
            with self.assertRaises(visual_qa.VisualQAError):
                bind_automation_capture.bind_capture(receipt_path, binary)

    def test_receipt_and_artifact_substitutions_are_rejected(self):
        mutations = [
            ("external capture", lambda root, receipt: receipt["capture"].__setitem__(
                "capture_method", "win32-print-window")),
            ("wrong command", lambda root, receipt: receipt["automation"].__setitem__(
                "command_id", "interact-1")),
            ("wrong observation tick", lambda root, receipt: receipt["observation"].__setitem__(
                "tick", "2")),
            ("path escape", lambda root, receipt: receipt["capture"].__setitem__(
                "path", "../../outside.png")),
            ("image substitution", lambda root, receipt: (root / "visual-capture" /
                "frame-tick-1.png").write_bytes(png_rgba(99, 88, 77))),
            ("incomplete events", lambda root, receipt: (root / "events.jsonl").write_text(
                "{}", encoding="utf-8")),
        ]
        for name, mutate in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                receipt_path, binary, receipt = self.fixture(root)
                mutate(root, receipt)
                self.write_json(receipt_path, receipt)
                with self.assertRaises(visual_qa.VisualQAError):
                    bind_automation_capture.bind_capture(receipt_path, binary)

    def test_post_terminal_event_and_manifest_substitution_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            receipt, binary, _ = self.fixture(root)
            events = root / "events.jsonl"
            extra = {"schema": "surreal-automation-telemetry-v1", "seq": "3",
                     "event": "command_progress"}
            with events.open("a", encoding="utf-8", newline="\n") as stream:
                stream.write(json.dumps(extra) + "\n")
            with self.assertRaises(visual_qa.VisualQAError):
                bind_automation_capture.bind_capture(receipt, binary)

            receipt, binary, _ = self.fixture(root / "second")
            manifest_path = receipt.parent.parent / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["visual_capture"]["session_id"] = "other-session"
            self.write_json(manifest_path, manifest)
            with self.assertRaises(visual_qa.VisualQAError):
                bind_automation_capture.bind_capture(receipt, binary)


if __name__ == "__main__":
    unittest.main()
