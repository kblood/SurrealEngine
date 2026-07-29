#!/usr/bin/env python3
"""Deterministically score a complete, human-labelled shadow proposal run."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
from typing import Any

import shadow_replay
import visual_qa

MAX_JSON_BYTES = 8 * 1024 * 1024
MAX_JSON_DEPTH = 32
MAX_CASES = 64
MAX_MODELS = 8
MAX_REPEATS = 16
MAX_ATTEMPTS = MAX_CASES * MAX_MODELS * MAX_REPEATS
ACTIONS = ("none", "walk_to_actor", "acquire_item", "interact", "wait")
PREDICTIONS = ACTIONS + ("invalid",)


class DuplicateKeyError(ValueError):
    pass


def exact_fields(value: Any, fields: set[str], name: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        raise visual_qa.VisualQAError(f"{name} has unexpected fields")
    return value


def canonical_sha256(value: Any, name: str) -> str:
    if (not isinstance(value, str) or len(value) != 64 or
            any(character not in "0123456789abcdef" for character in value)):
        raise visual_qa.VisualQAError(f"{name} must be a lowercase SHA-256")
    return value


def strict_json(raw: bytes, name: str) -> dict[str, Any]:
    def object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise DuplicateKeyError(f"duplicate key in {name}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ValueError(f"non-finite number in {name}: {value}")

    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=object_pairs,
                           parse_constant=reject_constant)
    except (UnicodeDecodeError, json.JSONDecodeError, DuplicateKeyError,
            RecursionError, ValueError) as exc:
        raise visual_qa.VisualQAError(f"invalid JSON in {name}: {exc}") from exc
    if not isinstance(value, dict):
        raise visual_qa.VisualQAError(f"{name} must be a JSON object")

    nodes = 0

    def visit(item: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if depth > MAX_JSON_DEPTH or nodes > 100_000:
            raise visual_qa.VisualQAError(f"{name} exceeds structural bounds")
        if isinstance(item, dict):
            for key, child in item.items():
                if not isinstance(key, str):
                    raise visual_qa.VisualQAError(f"{name} contains a non-string key")
                visit(child, depth + 1)
        elif isinstance(item, list):
            for child in item:
                visit(child, depth + 1)
        elif isinstance(item, float) and not math.isfinite(item):
            raise visual_qa.VisualQAError(f"{name} contains a non-finite number")

    visit(value, 0)
    return value


def read_artifact(root: pathlib.Path, value: Any,
                  name: str) -> tuple[bytes, str]:
    artifact = exact_fields(value, {"path", "sha256"}, name)
    relative_text = visual_qa.bounded_text(artifact["path"], f"{name} path")
    relative = pathlib.Path(relative_text)
    if relative.is_absolute():
        raise visual_qa.VisualQAError(f"{name} path must be relative")
    path = (root / relative).resolve()
    if root != path and root not in path.parents:
        raise visual_qa.VisualQAError(f"{name} path escapes its manifest directory")
    expected = canonical_sha256(artifact["sha256"], f"{name} hash")
    size = path.stat().st_size
    if size <= 0 or size > MAX_JSON_BYTES:
        raise visual_qa.VisualQAError(f"{name} size is outside the 8 MiB bound")
    raw = path.read_bytes()
    if len(raw) != size:
        raise visual_qa.VisualQAError(f"{name} size changed while reading")
    actual = hashlib.sha256(raw).hexdigest()
    if actual != expected:
        raise visual_qa.VisualQAError(f"{name} SHA-256 mismatch")
    return raw, actual


def load_artifact(root: pathlib.Path, value: Any,
                  name: str) -> tuple[dict[str, Any], bytes, str]:
    raw, digest = read_artifact(root, value, name)
    return strict_json(raw, name), raw, digest


def decimal_ns(value: Any, name: str) -> int:
    if (not isinstance(value, str) or not value or not value.isascii() or
            not value.isdigit()):
        raise visual_qa.VisualQAError(f"{name} must be a decimal string")
    parsed = int(value)
    if parsed > 3_600_000_000_000:
        raise visual_qa.VisualQAError(f"{name} exceeds the one-hour bound")
    return parsed


def proposal_list(value: Any, context: dict[str, Any],
                  name: str) -> list[dict[str, Any]]:
    if not isinstance(value, list) or not value or len(value) > 64:
        raise visual_qa.VisualQAError(f"{name} must contain 1..64 proposals")
    proposals = [shadow_replay.proposal_spec(item, context, f"{name} {index}")
                 for index, item in enumerate(value)]
    keys = [visual_qa.canonical_json(item) for item in proposals]
    if len(set(keys)) != len(keys):
        raise visual_qa.VisualQAError(f"{name} contains duplicate proposals")
    return proposals


def validate_capture_binding(manifest: dict[str, Any], captures: list[dict[str, Any]],
                             fixture: dict[str, Any], manifest_sha256: str,
                             name: str) -> str:
    if manifest.get("schema") not in {
            "surreal-visual-qa-capture-manifest-v1",
            "surreal-visual-qa-capture-manifest-v2",
            "surreal-visual-qa-capture-manifest-v3",
            "surreal-visual-qa-capture-manifest-v4"}:
        raise visual_qa.VisualQAError(f"{name} capture manifest schema is invalid")
    if not isinstance(captures, list) or not 1 <= len(captures) <= 64:
        raise visual_qa.VisualQAError(f"{name} capture manifest is unbounded")
    matches = [capture for capture in captures
               if capture.get("id") == fixture["capture_id"]]
    if len(matches) != 1 or matches[0].get("_sha256") != fixture["capture_sha256"]:
        raise visual_qa.VisualQAError(f"{name} capture binding does not match its fixture")
    if fixture["grounding"] is None:
        if manifest.get("schema") != "surreal-visual-qa-capture-manifest-v1":
            raise visual_qa.VisualQAError(
                f"{name} fixture v1 cannot claim grounded capture evidence")
        return "legacy_unbound"
    if fixture.get("schema") == "surreal-visual-qa-shadow-replay-fixture-v3":
        if (manifest.get("schema") != "surreal-visual-qa-capture-manifest-v3" or
                fixture.get("capture_manifest_sha256") != manifest_sha256 or
                matches[0].get("_grounding") != fixture["grounding"]):
            raise visual_qa.VisualQAError(
                f"{name} pre-action capture grounding does not match its fixture")
        return "pre_action_bound"
    if fixture.get("schema") == "surreal-visual-qa-shadow-replay-fixture-v4":
        if (manifest.get("schema") != "surreal-visual-qa-capture-manifest-v4" or
                fixture.get("capture_manifest_sha256") != manifest_sha256 or
                matches[0].get("_grounding") != fixture["grounding"]):
            raise visual_qa.VisualQAError(
                f"{name} pre-effect capture grounding does not match its fixture")
        return "pre_effect_bound"
    if (manifest.get("schema") != "surreal-visual-qa-capture-manifest-v2" or
            matches[0].get("_grounding") != fixture["grounding"]):
        raise visual_qa.VisualQAError(
            f"{name} same-session capture grounding does not match its fixture")
    return "same_session"


def validate_dataset(path: pathlib.Path) -> dict[str, Any]:
    root = path.resolve().parent
    raw = path.read_bytes()
    if not 0 < len(raw) <= MAX_JSON_BYTES:
        raise visual_qa.VisualQAError("shadow evaluation dataset size is invalid")
    dataset = strict_json(raw, "shadow evaluation dataset")
    exact_fields(dataset, {"schema", "dataset_id", "labeling_protocol", "cases",
                           "controls_live_player", "dispatch_authorized"},
                 "shadow evaluation dataset")
    if dataset["schema"] not in {"surreal-visual-qa-shadow-eval-dataset-v1",
                                  "surreal-visual-qa-shadow-eval-dataset-v2",
                                  "surreal-visual-qa-shadow-eval-dataset-v3"}:
        raise visual_qa.VisualQAError("unsupported shadow evaluation dataset schema")
    dataset_schema = dataset["schema"]
    dataset_id = visual_qa.bounded_text(dataset["dataset_id"], "dataset id")
    if dataset["controls_live_player"] is not False or dataset["dispatch_authorized"] is not False:
        raise visual_qa.VisualQAError("shadow evaluation dataset claims control authority")
    _, protocol_hash = read_artifact(root, dataset["labeling_protocol"],
                                     "labeling protocol")
    cases_value = dataset["cases"]
    if not isinstance(cases_value, list) or not 1 <= len(cases_value) <= MAX_CASES:
        raise visual_qa.VisualQAError("shadow evaluation dataset must contain 1..64 cases")

    cases: dict[str, dict[str, Any]] = {}
    for index, value in enumerate(cases_value):
        name = f"evaluation case {index}"
        case = exact_fields(value, {"case_id", "expected_action", "stratum",
                                    "capture_manifest", "profile", "observation",
                                    "replay_fixture", "labels", "adjudication"}, name)
        case_id = visual_qa.bounded_text(case["case_id"], f"{name} id")
        if case_id in cases:
            raise visual_qa.VisualQAError("shadow evaluation case identity is duplicated")
        expected_action = case["expected_action"]
        if expected_action not in ACTIONS:
            raise visual_qa.VisualQAError(f"{name} expected action is invalid")
        stratum = visual_qa.bounded_text(case["stratum"], f"{name} stratum")
        capture, _, capture_hash = load_artifact(
            root, case["capture_manifest"], f"{name} capture manifest")
        capture_manifest_path = (root / pathlib.Path(
            case["capture_manifest"]["path"])).resolve()
        validated_captures = visual_qa.validate_manifest(capture, capture_manifest_path)
        profile, _, profile_hash = load_artifact(root, case["profile"], f"{name} profile")
        assertions = visual_qa.validate_profile(profile)
        observation, observation_raw, observation_hash = load_artifact(
            root, case["observation"], f"{name} observation")
        fixture, _, fixture_hash = load_artifact(
            root, case["replay_fixture"], f"{name} replay fixture")
        validated_fixture = shadow_replay.validate_replay_fixture(
            fixture, observation, observation_hash,
            capture, capture_manifest_path, capture_hash)
        grounding_status = validate_capture_binding(
            capture, validated_captures, validated_fixture, capture_hash, name)
        if expected_action != "none":
            expected_grounding = ({
                "surreal-visual-qa-shadow-eval-dataset-v2": "pre_action_bound",
                "surreal-visual-qa-shadow-eval-dataset-v3": "pre_effect_bound",
            }.get(dataset_schema, "same_session"))
            if grounding_status != expected_grounding:
                raise visual_qa.VisualQAError(
                    f"{name} positive action requires {expected_grounding} evidence")
        request_schema, request_prompt = visual_qa.finding_request(
            validated_fixture["capture_id"], profile["prompt"], assertions,
            validated_fixture["context"])
        accepted = validated_fixture["accepted_proposals"]
        if any(proposal["action"] != expected_action for proposal in accepted):
            raise visual_qa.VisualQAError(f"{name} mixes accepted action families")

        labels_value = case["labels"]
        if not isinstance(labels_value, list) or len(labels_value) != 2:
            raise visual_qa.VisualQAError(f"{name} requires exactly two independent labels")
        label_hashes: list[str] = []
        annotators: set[str] = set()
        for label_index, label_artifact in enumerate(labels_value):
            label_name = f"{name} label {label_index}"
            label, _, label_hash = load_artifact(root, label_artifact, label_name)
            exact_fields(label, {"schema", "case_id", "annotator_id",
                                 "capture_manifest_sha256", "profile_sha256",
                                 "observation_sha256", "accepted_proposals", "notes"},
                         label_name)
            if label["schema"] != "surreal-visual-qa-shadow-human-label-v1" or label["case_id"] != case_id:
                raise visual_qa.VisualQAError(f"{label_name} identity is invalid")
            annotator = visual_qa.bounded_text(label["annotator_id"],
                                               f"{label_name} annotator")
            if annotator in annotators:
                raise visual_qa.VisualQAError(f"{name} annotator identity is duplicated")
            annotators.add(annotator)
            if (label["capture_manifest_sha256"] != capture_hash or
                    label["profile_sha256"] != profile_hash or
                    label["observation_sha256"] != observation_hash):
                raise visual_qa.VisualQAError(f"{label_name} provenance is substituted")
            visual_qa.bounded_text(label["notes"], f"{label_name} notes", allow_empty=True)
            proposal_list(label["accepted_proposals"], validated_fixture["context"],
                          f"{label_name} accepted proposals")
            label_hashes.append(label_hash)

        adjudication, _, adjudication_hash = load_artifact(
            root, case["adjudication"], f"{name} adjudication")
        exact_fields(adjudication, {"schema", "case_id", "label_sha256s",
                                    "accepted_proposals", "notes"}, f"{name} adjudication")
        if (adjudication["schema"] != "surreal-visual-qa-shadow-adjudication-v1" or
                adjudication["case_id"] != case_id or
                adjudication["label_sha256s"] != sorted(label_hashes)):
            raise visual_qa.VisualQAError(f"{name} adjudication binding is invalid")
        visual_qa.bounded_text(adjudication["notes"], f"{name} adjudication notes",
                               allow_empty=True)
        adjudicated = proposal_list(adjudication["accepted_proposals"],
                                    validated_fixture["context"],
                                    f"{name} adjudicated proposals")
        if ({visual_qa.canonical_json(item) for item in adjudicated} !=
                {visual_qa.canonical_json(item) for item in accepted}):
            raise visual_qa.VisualQAError(f"{name} fixture does not match adjudication")

        cases[case_id] = {
            "case_id": case_id,
            "expected_action": expected_action,
            "stratum": stratum,
            "manifest_sha256": capture_hash,
            "profile_sha256": profile_hash,
            "observation_sha256": observation_hash,
            "fixture_sha256": fixture_hash,
            "adjudication_sha256": adjudication_hash,
            "schema_sha256": visual_qa.sha256_bytes(
                visual_qa.canonical_json(request_schema).encode("utf-8")),
            "prompt_sha256": visual_qa.sha256_bytes(request_prompt.encode("utf-8")),
            "fixture": validated_fixture,
            "grounding_status": grounding_status,
            "pre_action_binding_sha256": (
                validated_fixture["grounding"].get("pre_action_binding_sha256")
                if grounding_status == "pre_action_bound" else None),
            "pre_effect_binding_sha256": (
                validated_fixture["grounding"].get("pre_effect_binding_sha256")
                if grounding_status == "pre_effect_bound" else None),
            "effect_observation_sha256": (
                validated_fixture["grounding"].get("effect_observation_sha256")
                if grounding_status == "pre_effect_bound" else None),
            "report_fixture": fixture,
            "observation": observation,
        }
    return {
        "schema": dataset_schema,
        "dataset_id": dataset_id,
        "dataset_sha256": hashlib.sha256(raw).hexdigest(),
        "labeling_protocol_sha256": protocol_hash,
        "cases": cases,
    }


def validate_model(value: Any, name: str) -> dict[str, str]:
    model = exact_fields(value, {"tag", "digest"}, name)
    return {
        "tag": visual_qa.bounded_text(model["tag"], f"{name} tag"),
        "digest": canonical_sha256(model["digest"], f"{name} digest"),
    }


def validate_receipt(value: dict[str, Any], attempt: dict[str, Any],
                     run: dict[str, Any]) -> dict[str, Any]:
    exact_fields(value, {"schema", "attempt_id", "repeat", "requested_model", "seed",
                         "generator_sha256", "controls_live_player",
                         "dispatch_authorized", "outcome"}, "attempt receipt")
    if value["schema"] != "surreal-visual-qa-shadow-eval-attempt-v1":
        raise visual_qa.VisualQAError("attempt receipt schema is invalid")
    if (value["attempt_id"] != attempt["attempt_id"] or
            value["repeat"] != attempt["repeat"] or
            value["requested_model"] != attempt["model"] or
            value["seed"] != run["seed"] or
            value["generator_sha256"] != run["generator_sha256"]):
        raise visual_qa.VisualQAError("attempt receipt provenance is substituted")
    if value["controls_live_player"] is not False or value["dispatch_authorized"] is not False:
        raise visual_qa.VisualQAError("attempt receipt claims control authority")
    outcome = exact_fields(value["outcome"], {"status", "elapsed_ns", "report_sha256",
                                              "error_code", "error"}, "attempt outcome")
    if outcome["status"] not in {"report_written", "failed"}:
        raise visual_qa.VisualQAError("attempt outcome status is invalid")
    elapsed = decimal_ns(outcome["elapsed_ns"], "attempt elapsed time")
    visual_qa.bounded_text(outcome["error_code"], "attempt error code", allow_empty=True)
    visual_qa.bounded_text(outcome["error"], "attempt error", allow_empty=True)
    if outcome["status"] == "report_written":
        canonical_sha256(outcome["report_sha256"], "attempt report hash")
        if outcome["error_code"] or outcome["error"]:
            raise visual_qa.VisualQAError("successful attempt contains an error")
    elif outcome["report_sha256"] is not None:
        raise visual_qa.VisualQAError("failed attempt binds a report")
    elif not outcome["error_code"] or not outcome["error"]:
        raise visual_qa.VisualQAError("failed attempt omits its bounded error")
    return {"status": outcome["status"], "elapsed_ns": elapsed,
            "error_code": outcome["error_code"]}


def report_elapsed(report: dict[str, Any], capture_id: str) -> int:
    results = report.get("results")
    if not isinstance(results, list):
        raise visual_qa.VisualQAError("report results are invalid")
    matches = [item for item in results if isinstance(item, dict) and
               item.get("capture_id") == capture_id]
    if len(matches) != 1:
        raise visual_qa.VisualQAError("report capture timing is ambiguous")
    return decimal_ns(matches[0].get("elapsed_ns"), "report inference elapsed time")


def report_capture(report: dict[str, Any], capture_id: str) -> dict[str, Any]:
    results = report.get("results")
    if not isinstance(results, list):
        raise visual_qa.VisualQAError("report results are invalid")
    matches = [item for item in results if isinstance(item, dict) and
               item.get("capture_id") == capture_id]
    if len(matches) != 1:
        raise visual_qa.VisualQAError("report capture evidence is ambiguous")
    return matches[0]


def bounded_error(exc: Exception) -> str:
    text = str(exc).encode("utf-8", errors="replace")[:512].decode("utf-8", errors="ignore")
    return text


def score_attempt(root: pathlib.Path, attempt: dict[str, Any], run: dict[str, Any],
                  dataset: dict[str, Any], models: dict[str, dict[str, str]]) -> dict[str, Any]:
    case = dataset["cases"][attempt["case_id"]]
    receipt, _, receipt_hash = load_artifact(root, attempt["receipt"],
                                             f"attempt {attempt['attempt_id']} receipt")
    receipt_result = validate_receipt(receipt, attempt, run)
    base = {
        "attempt_id": attempt["attempt_id"], "case_id": attempt["case_id"],
        "repeat": attempt["repeat"], "model": models[attempt["model"]],
        "receipt_sha256": receipt_hash,
        "expected_action": case["expected_action"],
        "end_to_end_elapsed_ns": str(receipt_result["elapsed_ns"]),
        "controls_live_player": False, "dispatch_authorized": False,
    }
    if dataset["schema"] in {"surreal-visual-qa-shadow-eval-dataset-v2",
                              "surreal-visual-qa-shadow-eval-dataset-v3"}:
        base.update({
            "capture_authority": case["grounding_status"],
            "capture_manifest_sha256": case["manifest_sha256"],
        })
        if dataset["schema"].endswith("v2"):
            base["pre_action_binding_sha256"] = case["pre_action_binding_sha256"]
        else:
            base.update({
                "pre_effect_binding_sha256": case["pre_effect_binding_sha256"],
                "effect_observation_sha256": case["effect_observation_sha256"],
            })
    if receipt_result["status"] == "failed":
        if attempt["report"] is not None:
            raise visual_qa.VisualQAError("failed attempt unexpectedly references a report")
        return {**base, "status": "invalid", "predicted_action": "invalid",
                "proposal": None, "report_sha256": None, "inference_elapsed_ns": None,
                "error_code": receipt_result["error_code"]}
    if attempt["report"] is None:
        raise visual_qa.VisualQAError("successful attempt omitted its report artifact")
    report_raw, report_hash = read_artifact(root, attempt["report"],
                                            f"attempt {attempt['attempt_id']} report")
    if receipt["outcome"]["report_sha256"] != report_hash:
        raise visual_qa.VisualQAError("attempt receipt/report hash mismatch")
    try:
        report = strict_json(report_raw, f"attempt {attempt['attempt_id']} report")
        provenance = report.get("provenance")
        if not isinstance(provenance, dict) or (
                provenance.get("generator_sha256") != run["generator_sha256"] or
                provenance.get("manifest_sha256") != case["manifest_sha256"] or
                provenance.get("profile_sha256") != case["profile_sha256"] or
                provenance.get("shadow_observation_sha256") != case["observation_sha256"] or
                provenance.get("seed") != run["seed"]):
            raise visual_qa.VisualQAError("report provenance does not match the labelled case")
        model_value = report.get("model")
        expected_model = models[attempt["model"]]
        if (not isinstance(model_value, dict) or
                model_value.get("requested") != expected_model["tag"] or
                model_value.get("tag") != expected_model["tag"] or
                model_value.get("digest") != expected_model["digest"] or
                model_value.get("ollama_version") != run["ollama_version"] or
                not isinstance(model_value.get("capabilities"), list) or
                "vision" not in model_value["capabilities"]):
            raise visual_qa.VisualQAError("report model provenance is substituted")
        capture_result = report_capture(report, case["fixture"]["capture_id"])
        if (capture_result.get("schema_sha256") != case["schema_sha256"] or
                capture_result.get("prompt_sha256") != case["prompt_sha256"]):
            raise visual_qa.VisualQAError("report prompt or schema hash is substituted")
        result = shadow_replay.evaluate_shadow_report(
            report, case["fixture"], case["fixture_sha256"],
            case["observation_sha256"], report_hash)
        inference_elapsed = report_elapsed(report, case["fixture"]["capture_id"])
        proposal = result["proposal"]
        return {**base, "status": result["verdict"],
                "predicted_action": proposal["action"], "proposal": proposal,
                "report_sha256": report_hash,
                "inference_elapsed_ns": str(inference_elapsed), "error_code": ""}
    except (visual_qa.VisualQAError, KeyError, TypeError, ValueError) as exc:
        return {**base, "status": "invalid", "predicted_action": "invalid",
                "proposal": None, "report_sha256": report_hash,
                "inference_elapsed_ns": None, "error_code": bounded_error(exc)}


def ppm(numerator: int, denominator: int) -> int:
    return 0 if denominator == 0 else numerator * 1_000_000 // denominator


def percentile(values: list[int], percentile_value: int) -> str | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(percentile_value * len(ordered) / 100) - 1)
    return str(ordered[index])


def model_metrics(model_attempts: list[dict[str, Any]],
                  case_ids: list[str], repeat_count: int) -> dict[str, Any]:
    count = len(model_attempts)
    valid = [item for item in model_attempts if item["status"] != "invalid"]
    passed = [item for item in model_attempts if item["status"] == "passed"]
    confusion = {expected: {predicted: 0 for predicted in PREDICTIONS}
                 for expected in ACTIONS}
    for item in model_attempts:
        confusion[item["expected_action"]][item["predicted_action"]] += 1

    per_action = {}
    for action in ACTIONS:
        true_positive = confusion[action][action]
        predicted = sum(confusion[expected][action] for expected in ACTIONS)
        expected = sum(confusion[action].values())
        precision = ppm(true_positive, predicted)
        recall = ppm(true_positive, expected)
        f1 = 0 if precision + recall == 0 else 2 * precision * recall // (precision + recall)
        exact = sum(1 for item in passed if item["expected_action"] == action)
        per_action[action] = {
            "expected_count": expected, "action_true_positive_count": true_positive,
            "exact_oracle_count": exact, "precision_ppm": precision,
            "recall_ppm": recall, "f1_ppm": f1,
        }

    repeat_stable = 0
    pair_agree = 0
    pair_total = 0
    for case_id in case_ids:
        items = sorted((item for item in model_attempts if item["case_id"] == case_id),
                       key=lambda item: item["repeat"])
        fingerprints = [("invalid:" + item["error_code"] if item["proposal"] is None else
                         visual_qa.canonical_json(item["proposal"])) for item in items]
        if len(items) == repeat_count and len(set(fingerprints)) == 1:
            repeat_stable += 1
        for left in range(len(fingerprints)):
            for right in range(left + 1, len(fingerprints)):
                pair_total += 1
                pair_agree += fingerprints[left] == fingerprints[right]

    targeted = {"walk_to_actor", "acquire_item", "interact"}
    target_expected = [item for item in model_attempts if item["expected_action"] in targeted]
    target_exact = sum(1 for item in passed if item["expected_action"] in targeted)
    wait_expected = [item for item in model_attempts if item["expected_action"] == "wait"]
    wait_exact = sum(1 for item in passed if item["expected_action"] == "wait")
    inference = [int(item["inference_elapsed_ns"]) for item in valid]
    end_to_end = [int(item["end_to_end_elapsed_ns"]) for item in model_attempts]
    return {
        "attempt_count": count,
        "schema_valid_count": len(valid),
        "schema_validity_ppm": ppm(len(valid), count),
        "exact_oracle_count": len(passed),
        "exact_oracle_accuracy_ppm": ppm(len(passed), count),
        "oracle_mismatch_count": sum(item["status"] == "failed" for item in model_attempts),
        "invalid_count": count - len(valid),
        "unsafe_commission_count": sum(item["expected_action"] == "none" and
                                       item["predicted_action"] not in {"none", "invalid"}
                                       for item in model_attempts),
        "over_abstention_count": sum(item["expected_action"] != "none" and
                                     item["predicted_action"] == "none"
                                     for item in model_attempts),
        "target_exact_count": target_exact,
        "target_expected_count": len(target_expected),
        "wait_exact_count": wait_exact,
        "wait_expected_count": len(wait_expected),
        "repeat_stable_case_count": repeat_stable,
        "repeat_case_count": len(case_ids),
        "pairwise_agreement_count": pair_agree,
        "pairwise_comparison_count": pair_total,
        "confusion": confusion,
        "per_action": per_action,
        "inference_latency_ns": {
            "count": len(inference), "p50": percentile(inference, 50),
            "p95": percentile(inference, 95),
            "min": None if not inference else str(min(inference)),
            "max": None if not inference else str(max(inference)),
        },
        "end_to_end_latency_ns": {
            "count": len(end_to_end), "p50": percentile(end_to_end, 50),
            "p95": percentile(end_to_end, 95),
            "min": None if not end_to_end else str(min(end_to_end)),
            "max": None if not end_to_end else str(max(end_to_end)),
        },
    }


def evaluate(dataset_path: pathlib.Path, run_path: pathlib.Path) -> dict[str, Any]:
    dataset = validate_dataset(dataset_path.resolve())
    root = run_path.resolve().parent
    run_raw = run_path.read_bytes()
    if not 0 < len(run_raw) <= MAX_JSON_BYTES:
        raise visual_qa.VisualQAError("shadow evaluation run size is invalid")
    run = strict_json(run_raw, "shadow evaluation run")
    exact_fields(run, {"schema", "run_id", "dataset_sha256", "generator_sha256",
                       "ollama_version", "seed", "repeat_count", "models", "attempts",
                       "controls_live_player", "dispatch_authorized"},
                 "shadow evaluation run")
    if run["schema"] != "surreal-visual-qa-shadow-eval-run-v1":
        raise visual_qa.VisualQAError("unsupported shadow evaluation run schema")
    run_id = visual_qa.bounded_text(run["run_id"], "evaluation run id")
    if run["dataset_sha256"] != dataset["dataset_sha256"]:
        raise visual_qa.VisualQAError("evaluation run dataset hash mismatch")
    generator_hash = canonical_sha256(run["generator_sha256"], "generator hash")
    ollama_version = visual_qa.bounded_text(run["ollama_version"], "Ollama version")
    seed = visual_qa.bounded_uint_string(run["seed"], "evaluation seed")
    repeat_count = run["repeat_count"]
    if not isinstance(repeat_count, int) or isinstance(repeat_count, bool) or not 1 <= repeat_count <= MAX_REPEATS:
        raise visual_qa.VisualQAError("evaluation repeat count is invalid")
    if run["controls_live_player"] is not False or run["dispatch_authorized"] is not False:
        raise visual_qa.VisualQAError("shadow evaluation run claims control authority")

    models_value = run["models"]
    if not isinstance(models_value, list) or not 1 <= len(models_value) <= MAX_MODELS:
        raise visual_qa.VisualQAError("evaluation run must contain 1..8 models")
    models: dict[str, dict[str, str]] = {}
    model_digests: set[str] = set()
    for index, value in enumerate(models_value):
        model = validate_model(value, f"evaluation model {index}")
        if model["tag"] in models or model["digest"] in model_digests:
            raise visual_qa.VisualQAError("evaluation model tag or digest is duplicated")
        models[model["tag"]] = model
        model_digests.add(model["digest"])

    attempts_value = run["attempts"]
    expected_count = len(dataset["cases"]) * len(models) * repeat_count
    if (not isinstance(attempts_value, list) or len(attempts_value) != expected_count or
            len(attempts_value) > MAX_ATTEMPTS):
        raise visual_qa.VisualQAError("evaluation attempt ledger is incomplete or unbounded")
    attempts: list[dict[str, Any]] = []
    slots: set[tuple[str, str, int]] = set()
    attempt_ids: set[str] = set()
    for index, value in enumerate(attempts_value):
        attempt = exact_fields(value, {"attempt_id", "case_id", "repeat", "model",
                                       "receipt", "report"}, f"evaluation attempt {index}")
        attempt_id = visual_qa.bounded_text(attempt["attempt_id"], f"attempt {index} id")
        case_id = attempt["case_id"]
        model_tag = attempt["model"]
        repeat = attempt["repeat"]
        if case_id not in dataset["cases"] or model_tag not in models:
            raise visual_qa.VisualQAError(f"evaluation attempt {index} binding is unknown")
        if not isinstance(repeat, int) or isinstance(repeat, bool) or not 1 <= repeat <= repeat_count:
            raise visual_qa.VisualQAError(f"evaluation attempt {index} repeat is invalid")
        slot = (case_id, model_tag, repeat)
        if slot in slots or attempt_id in attempt_ids:
            raise visual_qa.VisualQAError("evaluation attempt slot or identity is duplicated")
        slots.add(slot)
        attempt_ids.add(attempt_id)
        if attempt["report"] is not None:
            exact_fields(attempt["report"], {"path", "sha256"}, f"attempt {index} report")
        attempts.append({**attempt, "attempt_id": attempt_id})

    run_context = {"generator_sha256": generator_hash, "ollama_version": ollama_version,
                   "seed": seed}
    scored = [score_attempt(root, attempt, run_context, dataset, models)
              for attempt in attempts]
    scored.sort(key=lambda item: (item["model"]["tag"], item["case_id"], item["repeat"]))
    case_ids = sorted(dataset["cases"])
    model_results = []
    for tag in sorted(models):
        items = [item for item in scored if item["model"]["tag"] == tag]
        model_results.append({"model": models[tag],
                              "metrics": model_metrics(items, case_ids, repeat_count)})
    result = {
        "schema": ("surreal-visual-qa-shadow-eval-result-v3"
                   if dataset["schema"].endswith("v3") else
                   "surreal-visual-qa-shadow-eval-result-v2"
                   if dataset["schema"].endswith("v2") else
                   "surreal-visual-qa-shadow-eval-result-v1"),
        "dataset_id": dataset["dataset_id"],
        "dataset_sha256": dataset["dataset_sha256"],
        "labeling_protocol_sha256": dataset["labeling_protocol_sha256"],
        "run_id": run_id,
        "run_sha256": hashlib.sha256(run_raw).hexdigest(),
        "generator_sha256": generator_hash,
        "evaluator_sha256": hashlib.sha256(
            pathlib.Path(__file__).resolve().read_bytes()).hexdigest(),
        "ollama_version": ollama_version,
        "seed": seed,
        "case_count": len(case_ids),
        "model_count": len(models),
        "repeat_count": repeat_count,
        "attempt_count": len(scored),
        "controls_live_player": False,
        "dispatch_authorized": False,
        "models": model_results,
        "attempts": scored,
    }
    if dataset["schema"] in {"surreal-visual-qa-shadow-eval-dataset-v2",
                              "surreal-visual-qa-shadow-eval-dataset-v3"}:
        result["case_authorities"] = []
        for case_id in case_ids:
            case = dataset["cases"][case_id]
            authority = {
                "case_id": case_id,
                "capture_authority": case["grounding_status"],
                "capture_manifest_sha256": case["manifest_sha256"],
            }
            if dataset["schema"].endswith("v2"):
                authority["pre_action_binding_sha256"] = \
                    case["pre_action_binding_sha256"]
            else:
                authority.update({
                    "pre_effect_binding_sha256": case["pre_effect_binding_sha256"],
                    "effect_observation_sha256": case["effect_observation_sha256"],
                })
            result["case_authorities"].append(authority)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        result = evaluate(pathlib.Path(args.dataset), pathlib.Path(args.run))
        output = pathlib.Path(args.output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2,
                                     sort_keys=True) + "\n", encoding="utf-8")
        print(output)
        return 0
    except (visual_qa.VisualQAError, OSError, ValueError) as exc:
        print(f"shadow evaluation failed: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
