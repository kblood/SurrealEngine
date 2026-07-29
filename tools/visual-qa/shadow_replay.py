#!/usr/bin/env python3
"""Deterministically evaluate a shadow proposal against a curated fixture."""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any

from visual_qa import (VisualQAError, bounded_text, canonical_json,
                       bounded_uint_string, sha256_bytes, shadow_candidate,
                       strict_json_object, validate_shadow_context,
                       validate_shadow_proposal, validate_manifest)

MAX_REPLAY_JSON_BYTES = 8 * 1024 * 1024
MAX_ACCEPTED_PROPOSALS = 64
GROUNDING_FIELDS = {"schema", "session_id", "source_revision", "binary_sha256",
                    "config_sha256", "command_id", "observation_sha256",
                    "observation_revision", "observation_tick"}


def validate_sha256(value: Any, name: str) -> str:
    if (not isinstance(value, str) or len(value) != 64 or
            any(character not in "0123456789abcdefABCDEF" for character in value)):
        raise VisualQAError(f"{name} must be a SHA-256")
    return value.lower()


def load_bounded_json(path: pathlib.Path, name: str) -> tuple[dict[str, Any], bytes]:
    size = path.stat().st_size
    if size <= 0 or size > MAX_REPLAY_JSON_BYTES:
        raise VisualQAError(f"{name} size is outside the 8 MiB bound")
    raw = path.read_bytes()
    if len(raw) != size:
        raise VisualQAError(f"{name} size changed while reading")
    value = strict_json_object(raw, name)
    return value, raw


def proposal_spec(value: Any, context: dict[str, Any], name: str) -> dict[str, Any]:
    fields = {"action", "target_identity", "wait_ticks"}
    if not isinstance(value, dict) or set(value) != fields:
        raise VisualQAError(f"{name} has unexpected fields")
    proposal = {
        "schema": "surreal-visual-qa-shadow-proposal-v1",
        "action": value["action"],
        "target_identity": value["target_identity"],
        "wait_ticks": value["wait_ticks"],
        "confidence": 1.0,
        "rationale": "",
    }
    validate_shadow_proposal(proposal, context)
    return {field: value[field] for field in ("action", "target_identity", "wait_ticks")}


def validate_grounding(value: Any, observation: dict[str, Any],
                       observation_sha256: str) -> dict[str, str]:
    if not isinstance(value, dict) or set(value) != GROUNDING_FIELDS:
        raise VisualQAError("shadow replay grounding has unexpected fields")
    if value["schema"] != "surreal-visual-qa-grounding-v1":
        raise VisualQAError("shadow replay grounding schema is invalid")
    source_revision = value["source_revision"]
    if (not isinstance(source_revision, str) or len(source_revision) != 40 or
            any(character not in "0123456789abcdef" for character in source_revision)):
        raise VisualQAError("shadow replay grounding source revision is invalid")
    binary_hash = validate_sha256(value["binary_sha256"], "grounding binary hash")
    config_hash = validate_sha256(value["config_sha256"], "grounding config hash")
    bound_observation_hash = validate_sha256(
        value["observation_sha256"], "grounding observation hash")
    if (binary_hash != value["binary_sha256"] or config_hash != value["config_sha256"] or
            bound_observation_hash != value["observation_sha256"]):
        raise VisualQAError("shadow replay grounding hashes must be lowercase")
    revision = bounded_uint_string(
        value["observation_revision"], "grounding observation revision", positive=True)
    tick = bounded_uint_string(value["observation_tick"], "grounding observation tick")
    if (bound_observation_hash != observation_sha256 or
            revision != observation.get("revision") or tick != observation.get("tick")):
        raise VisualQAError("shadow replay grounding does not match its observation")
    return {
        "schema": value["schema"],
        "session_id": bounded_text(value["session_id"], "grounding session id"),
        "source_revision": source_revision,
        "binary_sha256": binary_hash,
        "config_sha256": config_hash,
        "command_id": bounded_text(value["command_id"], "grounding command id"),
        "observation_sha256": bound_observation_hash,
        "observation_revision": revision,
        "observation_tick": tick,
    }


def validate_replay_fixture(fixture: dict[str, Any], observation: dict[str, Any],
                            observation_sha256: str,
                            capture_manifest: dict[str, Any] | None = None,
                            capture_manifest_path: pathlib.Path | None = None,
                            capture_manifest_sha256: str | None = None) -> dict[str, Any]:
    schema = fixture.get("schema") if isinstance(fixture, dict) else None
    fields = {"schema", "fixture_id", "capture", "observation_sha256",
              "policy", "accepted_proposals"}
    if schema == "surreal-visual-qa-shadow-replay-fixture-v2":
        fields = fields | {"grounding"}
    elif schema == "surreal-visual-qa-shadow-replay-fixture-v3":
        fields = fields | {"capture_manifest_sha256"}
    elif schema == "surreal-visual-qa-shadow-replay-fixture-v4":
        fields = fields | {"capture_manifest_sha256"}
    elif schema != "surreal-visual-qa-shadow-replay-fixture-v1":
        raise VisualQAError("unsupported shadow replay fixture schema")
    if not isinstance(fixture, dict) or set(fixture) != fields:
        raise VisualQAError("shadow replay fixture has unexpected fields")
    fixture_id = bounded_text(fixture["fixture_id"], "shadow replay fixture id")
    if validate_sha256(fixture["observation_sha256"], "fixture observation hash") != observation_sha256:
        raise VisualQAError("shadow replay observation SHA-256 mismatch")

    capture = fixture["capture"]
    if not isinstance(capture, dict) or set(capture) != {"id", "sha256"}:
        raise VisualQAError("shadow replay capture binding is invalid")
    capture_id = bounded_text(capture["id"], "shadow replay capture id")
    capture_sha256 = validate_sha256(capture["sha256"], "fixture capture hash")

    policy = fixture["policy"]
    if (not isinstance(policy, dict) or
            set(policy) != {"allowed_actions", "target_identities", "max_wait_ticks"}):
        raise VisualQAError("shadow replay policy is invalid")
    context = validate_shadow_context(
        observation, policy["target_identities"], policy["allowed_actions"],
        policy["max_wait_ticks"])
    grounding = None
    validated_manifest_hash = None
    if schema == "surreal-visual-qa-shadow-replay-fixture-v2":
        grounding = validate_grounding(
            fixture["grounding"], observation, observation_sha256)
    elif schema in {"surreal-visual-qa-shadow-replay-fixture-v3",
                    "surreal-visual-qa-shadow-replay-fixture-v4"}:
        if (capture_manifest is None or capture_manifest_path is None or
                capture_manifest_sha256 is None):
            raise VisualQAError("runtime-bound shadow replay fixture requires its capture manifest")
        validated_manifest_hash = validate_sha256(
            capture_manifest_sha256, "supplied shadow replay capture manifest hash")
        if validate_sha256(
                fixture["capture_manifest_sha256"],
                "shadow replay capture manifest hash") != validated_manifest_hash:
            raise VisualQAError("shadow replay capture manifest SHA-256 mismatch")
        captures = validate_manifest(capture_manifest, capture_manifest_path)
        matches = [item for item in captures if item.get("id") == capture_id]
        expected_manifest_schema = ("surreal-visual-qa-capture-manifest-v4"
                                    if schema.endswith("v4") else
                                    "surreal-visual-qa-capture-manifest-v3")
        expected_grounding_schema = ("surreal-visual-qa-grounding-v3"
                                     if schema.endswith("v4") else
                                     "surreal-visual-qa-grounding-v2")
        if (capture_manifest.get("schema") != expected_manifest_schema or
                len(matches) != 1 or matches[0].get("_sha256") != capture_sha256):
            raise VisualQAError("runtime-bound shadow replay capture binding is invalid")
        grounding = matches[0].get("_grounding")
        if (not isinstance(grounding, dict) or
                grounding.get("schema") != expected_grounding_schema or
                grounding.get("observation_sha256") != observation_sha256 or
                grounding.get("observation_revision") != observation.get("revision") or
                grounding.get("observation_tick") != observation.get("tick")):
            raise VisualQAError(
                "runtime-bound shadow replay fixture is not bound to its observation")
        if (context["allowed_actions"] != [grounding["command_kind"]] or
                [item["identity"] for item in context["targets"]] !=
                    [grounding["target_identity"]]):
            raise VisualQAError(
                "runtime-bound shadow replay policy exceeds its action binding")

    accepted = fixture["accepted_proposals"]
    if (not isinstance(accepted, list) or not accepted or
            len(accepted) > MAX_ACCEPTED_PROPOSALS):
        raise VisualQAError("shadow replay fixture must accept 1..64 proposals")
    accepted_specs = []
    accepted_keys: set[str] = set()
    for index, value in enumerate(accepted):
        spec = proposal_spec(value, context, f"accepted proposal {index}")
        key = canonical_json(spec)
        if key in accepted_keys:
            raise VisualQAError("shadow replay accepted proposal is duplicated")
        accepted_keys.add(key)
        accepted_specs.append(spec)
    if schema in {"surreal-visual-qa-shadow-replay-fixture-v3",
                  "surreal-visual-qa-shadow-replay-fixture-v4"}:
        for spec in accepted_specs:
            if (spec["action"] != "none" and
                    (spec["action"] != grounding["command_kind"] or
                     spec["target_identity"] != grounding["target_identity"])):
                raise VisualQAError(
                    "shadow replay positive oracle does not match its runtime action binding")

    return {
        "schema": schema,
        "fixture_id": fixture_id,
        "capture_id": capture_id,
        "capture_sha256": capture_sha256,
        "capture_manifest_sha256": validated_manifest_hash,
        "grounding": grounding,
        "context": context,
        "accepted_proposals": accepted_specs,
        "accepted_keys": accepted_keys,
    }


def evaluate_shadow_report(report: dict[str, Any], fixture: dict[str, Any],
                           fixture_sha256: str, observation_sha256: str,
                           report_sha256: str) -> dict[str, Any]:
    fixture_sha256 = validate_sha256(fixture_sha256, "replay fixture hash")
    observation_sha256 = validate_sha256(observation_sha256, "replay observation hash")
    report_sha256 = validate_sha256(report_sha256, "replay report hash")
    fields = {"schema", "mode", "controls_live_player", "model", "provenance",
              "results", "shadow_policy"}
    if (not isinstance(report, dict) or set(report) != fields or
            report.get("schema") != "surreal-visual-qa-report-v1"):
        raise VisualQAError("shadow replay report schema or fields are invalid")
    if report["mode"] != "shadow" or report["controls_live_player"] is not False:
        raise VisualQAError("shadow replay requires a non-controlling shadow report")

    model = report["model"]
    if not isinstance(model, dict):
        raise VisualQAError("shadow replay report model provenance is invalid")
    model_tag = bounded_text(model.get("tag"), "shadow replay model tag")
    model_digest = validate_sha256(model.get("digest"), "shadow replay model digest")

    provenance = report["provenance"]
    if not isinstance(provenance, dict) or validate_sha256(
            provenance.get("shadow_observation_sha256"),
            "report observation hash") != observation_sha256:
        raise VisualQAError("shadow replay report observation binding is invalid")
    if fixture["grounding"] is not None:
        grounding_schema = fixture["grounding"].get("schema")
        expected_grounding = ({"surreal-visual-qa-grounding-v2": "pre_action_bound",
                               "surreal-visual-qa-grounding-v3": "pre_effect_bound"}
                              .get(grounding_schema, "same_session"))
        if provenance.get("shadow_grounding") != expected_grounding:
            raise VisualQAError("shadow replay report omits its grounding provenance")
        if (expected_grounding in {"pre_action_bound", "pre_effect_bound"} and
                provenance.get("shadow_action_barrier") != expected_grounding):
            raise VisualQAError("shadow replay report omits its action barrier provenance")

    expected_policy = {
        **fixture["context"],
        "dispatch_authorized": False,
        "controls_live_player": False,
    }
    if report["shadow_policy"] != expected_policy:
        raise VisualQAError("shadow replay report policy does not match the fixture")

    results = report["results"]
    if not isinstance(results, list) or not results or len(results) > 64:
        raise VisualQAError("shadow replay report results are invalid or unbounded")
    matches = []
    seen: set[str] = set()
    for index, result in enumerate(results):
        if not isinstance(result, dict):
            raise VisualQAError(f"shadow replay report result {index} is invalid")
        capture_id = bounded_text(result.get("capture_id"), f"shadow replay result {index} capture id")
        if capture_id in seen:
            raise VisualQAError("shadow replay report capture identity is duplicated")
        seen.add(capture_id)
        if capture_id == fixture["capture_id"]:
            matches.append(result)
    if len(matches) != 1:
        raise VisualQAError("shadow replay report does not contain the fixture capture exactly once")

    result = matches[0]
    if validate_sha256(result.get("image_sha256"), "report capture hash") != fixture["capture_sha256"]:
        raise VisualQAError("shadow replay report capture SHA-256 mismatch")
    if fixture["grounding"] is None:
        if "grounding" in result:
            raise VisualQAError("shadow replay fixture v1 does not bind report grounding")
    elif result.get("grounding") != fixture["grounding"]:
        raise VisualQAError("shadow replay report grounding does not match the fixture")
    finding = result.get("finding")
    if (not isinstance(finding, dict) or
            finding.get("schema") != "surreal-visual-qa-finding-v1" or
            finding.get("capture_id") != fixture["capture_id"]):
        raise VisualQAError("shadow replay finding identity is invalid")
    proposal = finding.get("proposal")
    validate_shadow_proposal(proposal, fixture["context"])
    if (fixture["grounding"] is not None and
            fixture["grounding"].get("schema") in {
                "surreal-visual-qa-grounding-v2",
                "surreal-visual-qa-grounding-v3"} and
            proposal["action"] != "none" and
            (proposal["action"] != fixture["grounding"]["command_kind"] or
             proposal["target_identity"] !=
                fixture["grounding"]["target_identity"])):
        raise VisualQAError(
            "shadow replay proposal does not match its runtime action binding")
    derived_candidate = shadow_candidate(proposal, fixture["context"])
    if "shadow_candidate" not in result or result["shadow_candidate"] != derived_candidate:
        raise VisualQAError("shadow replay report candidate was not derived from its proposal")

    spec = {field: proposal[field] for field in ("action", "target_identity", "wait_ticks")}
    passed = canonical_json(spec) in fixture["accepted_keys"]
    result = {
        "schema": ("surreal-visual-qa-shadow-replay-result-v3"
                   if fixture["schema"].endswith("v4") else
                   "surreal-visual-qa-shadow-replay-result-v2"
                   if fixture["schema"].endswith("v3") else
                   "surreal-visual-qa-shadow-replay-result-v1"),
        "fixture_id": fixture["fixture_id"],
        "capture_id": fixture["capture_id"],
        "fixture_sha256": fixture_sha256,
        "observation_sha256": observation_sha256,
        "report_sha256": report_sha256,
        "model": {"tag": model_tag, "digest": model_digest},
        "verdict": "passed" if passed else "failed",
        "reason": ("proposal matched the curated acceptance oracle" if passed else
                   "proposal did not match the curated acceptance oracle"),
        "proposal": spec,
        "shadow_candidate": derived_candidate,
        "dispatch_authorized": False,
        "controls_live_player": False,
    }
    if fixture["schema"].endswith("v3"):
        result.update({
            "capture_manifest_sha256": fixture["capture_manifest_sha256"],
            "pre_action_binding_sha256":
                fixture["grounding"]["pre_action_binding_sha256"],
            "capture_authority": "pre_action_bound",
        })
    elif fixture["schema"].endswith("v4"):
        result.update({
            "capture_manifest_sha256": fixture["capture_manifest_sha256"],
            "pre_effect_binding_sha256":
                fixture["grounding"]["pre_effect_binding_sha256"],
            "capture_authority": "pre_effect_bound",
            "effect_observation_sha256":
                fixture["grounding"]["effect_observation_sha256"],
        })
    return result


def replay(fixture_path: pathlib.Path, observation_path: pathlib.Path,
           report_path: pathlib.Path,
           capture_manifest_path: pathlib.Path | None = None) -> dict[str, Any]:
    fixture_value, fixture_raw = load_bounded_json(fixture_path, "shadow replay fixture")
    observation_value, observation_raw = load_bounded_json(
        observation_path, "shadow replay observation")
    report_value, report_raw = load_bounded_json(report_path, "shadow replay report")
    manifest_value = None
    manifest_hash = None
    if capture_manifest_path is not None:
        manifest_value, manifest_raw = load_bounded_json(
            capture_manifest_path, "shadow replay capture manifest")
        manifest_hash = sha256_bytes(manifest_raw)
    if ((fixture_value.get("schema") in {
         "surreal-visual-qa-shadow-replay-fixture-v3",
         "surreal-visual-qa-shadow-replay-fixture-v4"}) !=
            (capture_manifest_path is not None)):
        raise VisualQAError(
            "shadow replay capture manifest is required only for runtime-bound fixtures")
    observation_hash = sha256_bytes(observation_raw)
    validated_fixture = validate_replay_fixture(
        fixture_value, observation_value, observation_hash,
        manifest_value, capture_manifest_path, manifest_hash)
    return evaluate_shadow_report(
        report_value, validated_fixture, sha256_bytes(fixture_raw),
        observation_hash, sha256_bytes(report_raw))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--observation", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--capture-manifest")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        result = replay(pathlib.Path(args.fixture).resolve(),
                        pathlib.Path(args.observation).resolve(),
                        pathlib.Path(args.report).resolve(),
                        pathlib.Path(args.capture_manifest).resolve()
                        if args.capture_manifest else None)
        output = pathlib.Path(args.output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2,
                                     sort_keys=True) + "\n", encoding="utf-8")
        print(output)
        return 0 if result["verdict"] == "passed" else 1
    except (VisualQAError, OSError, ValueError) as exc:
        print(f"shadow replay failed: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
