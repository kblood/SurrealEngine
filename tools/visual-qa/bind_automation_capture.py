#!/usr/bin/env python3
"""Bind an engine-owned presented automation capture to a completed run."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
from typing import Any

import visual_qa

MAX_JSON_BYTES = 8 * 1024 * 1024
MAX_EVENTS_BYTES = 16 * 1024 * 1024
MAX_IMAGE_BYTES = 25 * 1024 * 1024
MAX_BINARY_BYTES = 1024 * 1024 * 1024


def exact_fields(value: Any, fields: set[str], name: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        raise visual_qa.VisualQAError(f"{name} has unexpected fields")
    return value


def resolve_within(root: pathlib.Path, base: pathlib.Path,
                   value: Any, name: str) -> pathlib.Path:
    relative = pathlib.Path(visual_qa.bounded_text(value, f"{name} path"))
    if relative.is_absolute():
        raise visual_qa.VisualQAError(f"{name} path must be relative")
    resolved_root = root.resolve()
    path = (base / relative).resolve()
    if path != resolved_root and resolved_root not in path.parents:
        raise visual_qa.VisualQAError(f"{name} path escapes the automation run")
    return path


def read_bounded(path: pathlib.Path, maximum: int, name: str) -> bytes:
    if not path.is_file():
        raise visual_qa.VisualQAError(f"{name} is missing")
    size = path.stat().st_size
    if size <= 0 or size > maximum:
        raise visual_qa.VisualQAError(f"{name} size is outside its bound")
    raw = path.read_bytes()
    if len(raw) != size:
        raise visual_qa.VisualQAError(f"{name} size changed while reading")
    return raw


def sha256_file(path: pathlib.Path) -> str:
    if not path.is_file():
        raise visual_qa.VisualQAError("production binary is missing")
    size = path.stat().st_size
    if size <= 0 or size > MAX_BINARY_BYTES:
        raise visual_qa.VisualQAError("production binary size is outside the 1 GiB bound")
    digest = hashlib.sha256()
    read = 0
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            read += len(chunk)
            digest.update(chunk)
    if read != size:
        raise visual_qa.VisualQAError("production binary size changed while hashing")
    return digest.hexdigest()


def fnv1a64(raw: bytes) -> str:
    value = 14695981039346656037
    for byte in raw:
        value ^= byte
        value = (value * 1099511628211) & 0xffffffffffffffff
    return f"fnv1a64:{value:016x}"


def png_dimensions(raw: bytes) -> tuple[int, int]:
    if (len(raw) < 24 or raw[:8] != b"\x89PNG\r\n\x1a\n" or
            raw[12:16] != b"IHDR"):
        raise visual_qa.VisualQAError("capture image is not a bounded PNG")
    width, height = struct.unpack(">II", raw[16:24])
    if not 1 <= width <= 32768 or not 1 <= height <= 32768:
        raise visual_qa.VisualQAError("capture PNG dimensions are invalid")
    return width, height


def decimal(value: Any, name: str, *, positive: bool = False,
            maximum: int = 1_000_000) -> str:
    if (not isinstance(value, str) or not value or not value.isascii() or
            not value.isdecimal()):
        raise visual_qa.VisualQAError(f"{name} is not an unsigned decimal string")
    parsed = int(value)
    if (positive and parsed == 0) or parsed > maximum:
        raise visual_qa.VisualQAError(f"{name} is outside the bounded range")
    return str(parsed)


def validate_receipt(receipt: dict[str, Any]) -> dict[str, Any]:
    schema = receipt.get("schema") if isinstance(receipt, dict) else None
    fields = {"schema", "session", "capture", "observation", "automation",
              "controls_live_player", "dispatch_authorized"}
    if schema in {"surreal-visual-qa-presented-capture-receipt-v2",
                  "surreal-visual-qa-presented-capture-receipt-v3"}:
        fields.add("barrier")
    elif schema != "surreal-visual-qa-presented-capture-receipt-v1":
        raise visual_qa.VisualQAError("unsupported presented capture receipt schema")
    exact_fields(receipt, fields, "capture receipt")
    if (receipt["controls_live_player"] is not False or
            receipt["dispatch_authorized"] is not False):
        raise visual_qa.VisualQAError("capture receipt claims control authority")

    session = exact_fields(receipt["session"],
                           {"session_id", "source_revision", "source_dirty"},
                           "capture receipt session")
    session_id = visual_qa.bounded_text(session["session_id"], "capture session id")
    revision = session["source_revision"]
    if (not isinstance(revision, str) or len(revision) != 40 or
            any(character not in "0123456789abcdef" for character in revision)):
        raise visual_qa.VisualQAError("capture source revision is not lowercase Git hex")
    if not isinstance(session["source_dirty"], bool):
        raise visual_qa.VisualQAError("capture source dirty state must be boolean")

    capture = exact_fields(receipt["capture"],
                           {"id", "path", "byte_size", "fnv1a64", "tick",
                            "camera", "width", "height", "renderer",
                            "capture_method", "capture_phase"},
                           "capture receipt image")
    capture_id = visual_qa.bounded_text(capture["id"], "capture id")
    capture_tick = decimal(capture["tick"], "capture tick")
    image_size = decimal(capture["byte_size"], "capture byte size", positive=True,
                         maximum=MAX_IMAGE_BYTES)
    if capture["renderer"] not in {"vulkan", "d3d11"}:
        raise visual_qa.VisualQAError("capture renderer is not synchronously supported")
    if capture["capture_method"] != "render-device-read-pixels":
        raise visual_qa.VisualQAError("capture was not produced by engine readback")
    phase = "post_simulation"
    barrier = None
    if schema == "surreal-visual-qa-presented-capture-receipt-v1":
        if capture["capture_phase"] != "post-simulation-no-further-world-tick":
            raise visual_qa.VisualQAError(
                "capture phase is not the frozen runtime boundary")
    else:
        barrier = exact_fields(
            receipt["barrier"],
            {"schema", "phase", "telemetry_event_sequence", "telemetry_event",
             "synthetic_input_requests_before_render",
             "synthetic_input_requests_after_render",
             "synthetic_interaction_presses_before_render",
             "synthetic_interaction_presses_after_render"},
            "capture receipt input barrier")
        expected_barrier_schema = (
            "surreal-player-automation-input-barrier-v2" if schema.endswith("v3")
            else "surreal-player-automation-input-barrier-v1")
        if barrier["schema"] != expected_barrier_schema:
            raise visual_qa.VisualQAError("capture barrier schema is invalid")
        phase = barrier["phase"]
        expected_capture_phase = {
            "pre_action": "pre-action-no-world-tick",
            "pre_stock_interaction": "pre-stock-interaction-before-input",
            "pre_pickup": "pre-pickup-before-input",
        }.get(phase)
        if expected_capture_phase is None or capture["capture_phase"] != expected_capture_phase:
            raise visual_qa.VisualQAError("capture barrier phase is invalid")
        if barrier["telemetry_event"] != "visual_capture_published":
            raise visual_qa.VisualQAError("capture barrier event type is invalid")
        barrier_sequence = decimal(
            barrier["telemetry_event_sequence"], "capture barrier event sequence",
            positive=True)
        input_before = decimal(
            barrier["synthetic_input_requests_before_render"],
            "synthetic input requests before render")
        input_after = decimal(
            barrier["synthetic_input_requests_after_render"],
            "synthetic input requests after render")
        interaction_before = decimal(
            barrier["synthetic_interaction_presses_before_render"],
            "synthetic interaction presses before render")
        interaction_after = decimal(
            barrier["synthetic_interaction_presses_after_render"],
            "synthetic interaction presses after render")
        if input_before != input_after or interaction_before != interaction_after:
            raise visual_qa.VisualQAError("synthetic input changed during capture rendering")
        if phase == "pre_action" and input_before != "0":
            raise visual_qa.VisualQAError("pre-action capture follows synthetic input")
        if phase == "pre_stock_interaction" and interaction_before != "0":
            raise visual_qa.VisualQAError(
                "pre-stock-interaction capture follows an interaction press")
    for dimension in ("width", "height"):
        value = capture[dimension]
        if (not isinstance(value, int) or isinstance(value, bool) or
                not 1 <= value <= 32768):
            raise visual_qa.VisualQAError(f"capture {dimension} is invalid")

    observation = exact_fields(receipt["observation"],
                               {"path", "byte_size", "fnv1a64", "revision", "tick"},
                               "capture receipt observation")
    observation_revision = decimal(
        observation["revision"], "capture observation revision", positive=True)
    observation_tick = decimal(observation["tick"], "capture observation tick")
    observation_size = decimal(
        observation["byte_size"], "capture observation byte size", positive=True,
        maximum=MAX_JSON_BYTES)
    if observation_tick != capture_tick:
        raise visual_qa.VisualQAError("receipt capture and observation ticks differ")

    automation = exact_fields(receipt["automation"],
                              {"manifest_path", "events_path", "summary_path",
                               "command_id", "config_identity"},
                              "capture receipt automation")
    command_id = visual_qa.bounded_text(
        automation["command_id"], "capture command id")
    config_identity = visual_qa.bounded_text(
        automation["config_identity"], "capture config identity")
    if capture_id != f"{command_id}-tick-{capture_tick}":
        raise visual_qa.VisualQAError("capture identity does not match its command and tick")
    return {
        "receipt_schema": schema, "phase": phase,
        "barrier_sequence": (barrier_sequence if barrier is not None else None),
        "session_id": session_id, "source_revision": revision,
        "source_dirty": session["source_dirty"], "capture_id": capture_id,
        "capture_tick": capture_tick, "image_size": image_size,
        "observation_revision": observation_revision,
        "observation_tick": observation_tick, "observation_size": observation_size,
        "command_id": command_id, "config_identity": config_identity,
    }


def bind_capture(receipt_path: pathlib.Path, binary_path: pathlib.Path) -> dict[str, Any]:
    receipt_path = receipt_path.resolve()
    capture_root = receipt_path.parent
    run_root = capture_root.parent.resolve()
    receipt_raw = read_bounded(receipt_path, MAX_JSON_BYTES, "capture receipt")
    receipt = visual_qa.strict_json_object(receipt_raw, "capture receipt")
    values = validate_receipt(receipt)

    capture_path = resolve_within(
        capture_root, capture_root, receipt["capture"]["path"], "capture image")
    observation_path = resolve_within(
        capture_root, capture_root, receipt["observation"]["path"],
        "capture observation")
    manifest_path = resolve_within(
        run_root, capture_root, receipt["automation"]["manifest_path"],
        "automation manifest")
    events_path = resolve_within(
        run_root, capture_root, receipt["automation"]["events_path"],
        "automation events")
    summary_path = resolve_within(
        run_root, capture_root, receipt["automation"]["summary_path"],
        "automation summary")

    image_raw = read_bounded(capture_path, MAX_IMAGE_BYTES, "capture image")
    if (len(image_raw) != int(values["image_size"]) or
            fnv1a64(image_raw) != receipt["capture"]["fnv1a64"]):
        raise visual_qa.VisualQAError("capture image does not match its runtime receipt")
    width, height = png_dimensions(image_raw)
    if (width != receipt["capture"]["width"] or
            height != receipt["capture"]["height"]):
        raise visual_qa.VisualQAError("capture PNG dimensions do not match its receipt")

    observation_raw = read_bounded(
        observation_path, MAX_JSON_BYTES, "capture observation")
    if (len(observation_raw) != int(values["observation_size"]) or
            fnv1a64(observation_raw) != receipt["observation"]["fnv1a64"]):
        raise visual_qa.VisualQAError("observation does not match its runtime receipt")
    observation = visual_qa.strict_json_object(
        observation_raw, "capture observation")
    if (observation.get("schema") not in {
            "surreal-automation-observation-v1",
            "surreal-automation-observation-v2"} or
            observation.get("revision") != values["observation_revision"] or
            observation.get("tick") != values["observation_tick"]):
        raise visual_qa.VisualQAError("receipt observation boundary is invalid")

    manifest_raw = read_bounded(manifest_path, MAX_JSON_BYTES, "automation manifest")
    automation_manifest = visual_qa.strict_json_object(
        manifest_raw, "automation manifest")
    expected_automation_schema = (
        "surreal-player-automation-manifest-v4"
        if values["receipt_schema"].endswith("v1") else
        "surreal-player-automation-manifest-v5")
    if (automation_manifest.get("schema") != expected_automation_schema or
            automation_manifest.get("config_identity") != values["config_identity"]):
        raise visual_qa.VisualQAError("automation manifest is not the capture configuration")
    command = automation_manifest.get("command")
    capture_request = automation_manifest.get("visual_capture")
    if (not isinstance(command, dict) or command.get("command_id") != values["command_id"] or
            not isinstance(capture_request, dict) or
            capture_request.get("tick") != values["capture_tick"] or
            capture_request.get("session_id") != values["session_id"] or
            capture_request.get("source_revision") != values["source_revision"] or
            capture_request.get("source_dirty") is not values["source_dirty"]):
        raise visual_qa.VisualQAError("automation capture request does not match its receipt")
    if not values["receipt_schema"].endswith("v1"):
        if (capture_request.get("schema") !=
                "surreal-player-automation-capture-request-v2" or
                capture_request.get("phase") != values["phase"]):
            raise visual_qa.VisualQAError(
                "automation capture phase does not match its receipt")
    elif capture_request.get("schema") != "surreal-player-automation-capture-request-v1":
        raise visual_qa.VisualQAError("terminal capture request schema is invalid")

    summary_raw = read_bounded(summary_path, MAX_JSON_BYTES, "automation summary")
    summary = visual_qa.strict_json_object(summary_raw, "automation summary")
    exact_fields(summary, {"schema", "command_id", "kind", "state", "tick",
                           "observation_revision", "target_identity", "reason"},
                 "automation summary")
    if (summary["schema"] != "surreal-automation-result-v1" or
            summary["command_id"] != values["command_id"]):
        raise visual_qa.VisualQAError("capture command does not match its completed run")
    if values["receipt_schema"].endswith("v1"):
        if (summary["tick"] != values["capture_tick"] or
                summary["observation_revision"] != values["observation_revision"]):
            raise visual_qa.VisualQAError("capture is not the completed command boundary")
    elif (summary["state"] != "succeeded" or
          int(summary["tick"]) <= int(values["capture_tick"]) or
          int(summary["observation_revision"]) <= int(values["observation_revision"])):
        raise visual_qa.VisualQAError(
            "pre-action capture does not precede a successful terminal result")

    events_raw = read_bounded(events_path, MAX_EVENTS_BYTES, "automation events")
    if not events_raw.endswith(b"\n"):
        raise visual_qa.VisualQAError("automation events are incomplete")
    event_lines = events_raw.splitlines()
    if not event_lines:
        raise visual_qa.VisualQAError("automation events are empty")
    events = [visual_qa.strict_json_object(line, f"automation event {index}")
              for index, line in enumerate(event_lines)]
    for index, event in enumerate(events, 1):
        if (event.get("schema") != "surreal-automation-telemetry-v1" or
                event.get("seq") != str(index)):
            raise visual_qa.VisualQAError("automation event sequence is invalid")
    barrier = None
    action_event = None
    effect_observation_raw = None
    effect_observation = None
    effect_observation_path = None
    if not values["receipt_schema"].endswith("v1"):
        barrier_index = int(values["barrier_sequence"]) - 1
        if barrier_index < 0 or barrier_index + 1 >= len(events):
            raise visual_qa.VisualQAError("capture barrier event sequence is incomplete")
        barrier = events[barrier_index]
        action_event = events[barrier_index + 1]
        target = command.get("target") if isinstance(command, dict) else None
        target_identity = target.get("identity") if isinstance(target, dict) else None
        action_pair = (values["phase"], command.get("kind"))
        if (action_pair not in {
                ("pre_stock_interaction", "interact"),
                ("pre_action", "walk_to_actor"),
                ("pre_pickup", "acquire_item")} or not target_identity):
            raise visual_qa.VisualQAError(
                "runtime action barrier phase does not match its exact command")
        first_effect_event = ("command_progress" if command["kind"] == "walk_to_actor"
                              else "interaction_attempt")
        if any(event.get("event") == first_effect_event and
               event.get("command_id") == values["command_id"] and
                (command["kind"] == "interact" or
                 (command["kind"] == "walk_to_actor" and
                  event.get("detail") == "ordinary player axes applied") or
                (command["kind"] == "acquire_item" and
                 event.get("target_identity") == target_identity))
               for event in events[:barrier_index]):
            raise visual_qa.VisualQAError(
                "capture barrier follows an earlier action effect")
        expected_boundary = {
            "command_id": values["command_id"],
            "kind": command["kind"], "tick": values["capture_tick"],
            "observation_revision": values["observation_revision"],
            "config_id": values["config_identity"],
            "target_identity": target_identity,
        }
        if (barrier.get("event") != "visual_capture_published" or
                barrier.get("state") != "running" or
                any(barrier.get(field) != value
                    for field, value in expected_boundary.items())):
            raise visual_qa.VisualQAError(
                "capture barrier telemetry does not match its runtime receipt")
        action_boundary = dict(expected_boundary)
        if command["kind"] == "walk_to_actor":
            action_boundary["tick"] = str(int(values["capture_tick"]) + 1)
            action_boundary["observation_revision"] = str(
                int(values["observation_revision"]) + 1)
        if (action_event.get("seq") != str(int(values["barrier_sequence"]) + 1) or
                action_event.get("event") != first_effect_event or
                action_event.get("state") != "running" or
                any(action_event.get(field) != value
                    for field, value in action_boundary.items()) or
                (command["kind"] == "walk_to_actor" and
                 action_event.get("detail") != "ordinary player axes applied") or
                (command["kind"] == "acquire_item" and
                 action_event.get("detail") !=
                    "stock ParseRightClick input pressed for the exact FrobTarget")):
            raise visual_qa.VisualQAError(
                "first action effect does not immediately follow the capture barrier")

        matching_targets = [item for item in observation.get("targets", [])
                            if isinstance(item, dict) and
                            item.get("identity") == target_identity]
        required_capability = ("reachable" if command["kind"] == "walk_to_actor"
                               else "interactable")
        if (len(matching_targets) != 1 or
                matching_targets[0].get("class") != target.get("expected_class") or
                matching_targets[0].get(required_capability) is not True or
                (command["kind"] == "acquire_item" and
                 (matching_targets[0].get("acquirable") is not True or
                  matching_targets[0].get("deleted") is not False or
                  matching_targets[0].get("owner_identity") != ""))):
            raise visual_qa.VisualQAError(
                "capture observation does not bind the exact actionable target")
        if values["receipt_schema"].endswith("v3"):
            if (summary["kind"] != "acquire_item" or
                    summary["target_identity"] != target_identity or
                    summary["reason"] !=
                        "exact target ownership transferred to the player"):
                raise visual_qa.VisualQAError(
                    "pre-pickup capture terminal proof is not exact ownership transfer")
            effect_observation_path = (run_root / "final-observation.json").resolve()
            effect_observation_raw = read_bounded(
                effect_observation_path, MAX_JSON_BYTES,
                "acquisition effect observation")
            effect_observation = visual_qa.strict_json_object(
                effect_observation_raw, "acquisition effect observation")
            if (effect_observation.get("schema") not in {
                    "surreal-automation-observation-v1",
                    "surreal-automation-observation-v2"} or
                    effect_observation.get("tick") != summary["tick"] or
                    effect_observation.get("revision") !=
                        summary["observation_revision"]):
                raise visual_qa.VisualQAError(
                    "acquisition effect observation does not match the terminal boundary")
            player_identity = effect_observation.get("player_identity")
            effect_targets = [item for item in effect_observation.get("targets", [])
                              if isinstance(item, dict) and
                              item.get("identity") == target_identity]
            before_target = matching_targets[0]
            if (not isinstance(player_identity, str) or not player_identity or
                    before_target.get("state_token") != "Pickup" or
                    before_target.get("inventory_resource") !=
                        {"kind": "num_copies", "value": "1"} or
                    len(effect_targets) != 1 or
                    effect_targets[0].get("class") != target.get("expected_class") or
                    effect_targets[0].get("owner_identity") != player_identity or
                    effect_targets[0].get("state_token") != "Idle2" or
                    effect_targets[0].get("deleted") is not False or
                    effect_targets[0].get("interactable") is not False or
                    effect_targets[0].get("acquirable") is not False or
                    effect_targets[0].get("inventory_resource") !=
                        before_target.get("inventory_resource")):
                raise visual_qa.VisualQAError(
                    "acquisition effect observation lacks exact ownership transfer")
    terminal = events[-1]
    if (terminal.get("event") != "command_result" or
            terminal.get("command_id") != summary["command_id"] or
            terminal.get("kind") != summary["kind"] or
            terminal.get("state") != summary["state"] or
            terminal.get("tick") != summary["tick"] or
            terminal.get("observation_revision") != summary["observation_revision"] or
            terminal.get("config_id") != values["config_identity"] or
            terminal.get("target_identity") != summary["target_identity"] or
            terminal.get("detail") != summary["reason"]):
        raise visual_qa.VisualQAError("terminal automation event does not match the summary")
    if (values["receipt_schema"].endswith("v3") and
            terminal.get("seq") != str(int(values["barrier_sequence"]) + 2)):
        raise visual_qa.VisualQAError(
            "acquisition result does not immediately prove the pickup effect")

    binary_hash = sha256_file(binary_path.resolve())
    runtime_bound = not values["receipt_schema"].endswith("v1")
    acquisition_bound = values["receipt_schema"].endswith("v3")
    manifest = {
        "schema": ("surreal-visual-qa-capture-manifest-v4" if acquisition_bound else
                   "surreal-visual-qa-capture-manifest-v3" if runtime_bound else
                   "surreal-visual-qa-capture-manifest-v2"),
        "session": {
            "session_id": values["session_id"],
            "source_revision": values["source_revision"],
            "binary_sha256": binary_hash,
            "config_sha256": visual_qa.sha256_bytes(manifest_raw),
        },
        "captures": [{
            "id": values["capture_id"],
            "path": capture_path.relative_to(capture_root).as_posix(),
            "sha256": visual_qa.sha256_bytes(image_raw),
            "tick": int(values["capture_tick"]),
            "camera": receipt["capture"]["camera"],
            "width": width, "height": height,
            "command_id": values["command_id"],
            "observation": {
                "path": observation_path.relative_to(capture_root).as_posix(),
                "sha256": visual_qa.sha256_bytes(observation_raw),
                "revision": values["observation_revision"],
                "tick": values["observation_tick"],
            },
        }],
    }
    manifest_root = capture_root
    if runtime_bound:
        manifest_root = run_root
        manifest["session"]["source_dirty"] = values["source_dirty"]
        manifest["binding"] = {
            "schema": ("surreal-visual-qa-pre-effect-binding-v2"
                       if acquisition_bound else
                       "surreal-visual-qa-pre-action-binding-v1"),
            "binder_sha256": visual_qa.sha256_bytes(pathlib.Path(__file__).read_bytes()),
            "receipt": {
                "path": receipt_path.relative_to(run_root).as_posix(),
                "sha256": visual_qa.sha256_bytes(receipt_raw),
            },
            "events": {
                "path": events_path.relative_to(run_root).as_posix(),
                "sha256": visual_qa.sha256_bytes(events_raw),
            },
            "summary": {
                "path": summary_path.relative_to(run_root).as_posix(),
                "sha256": visual_qa.sha256_bytes(summary_raw),
            },
            "phase": values["phase"],
            "barrier_event_sequence": values["barrier_sequence"],
            "action_event_sequence": str(int(values["barrier_sequence"]) + 1),
            "command_kind": command["kind"],
            "target_identity": command["target"]["identity"],
        }
        if acquisition_bound:
            manifest["binding"].update({
                "result_event_sequence": terminal["seq"],
                "effect_proof": "exact_target_ownership_transfer",
                "effect_observation": {
                    "path": effect_observation_path.relative_to(run_root).as_posix(),
                    "sha256": visual_qa.sha256_bytes(effect_observation_raw),
                    "revision": effect_observation["revision"],
                    "tick": effect_observation["tick"],
                },
            })
        capture = manifest["captures"][0]
        capture["path"] = capture_path.relative_to(run_root).as_posix()
        capture["observation"]["path"] = observation_path.relative_to(run_root).as_posix()
    visual_qa.validate_manifest(manifest, manifest_root / "capture-manifest.json")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--receipt", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output")
    args = parser.parse_args()
    receipt_path = pathlib.Path(args.receipt).resolve()
    try:
        manifest = bind_capture(receipt_path, pathlib.Path(args.binary))
        output_path = (pathlib.Path(args.output).resolve() if args.output else
                       (receipt_path.parent.parent if manifest["schema"] in {
                           "surreal-visual-qa-capture-manifest-v3",
                           "surreal-visual-qa-capture-manifest-v4"}
                        else receipt_path.parent) / "capture-manifest.json")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8", newline="\n")
        return 0
    except (OSError, visual_qa.VisualQAError, ValueError) as exc:
        print(f"capture binding failed: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
