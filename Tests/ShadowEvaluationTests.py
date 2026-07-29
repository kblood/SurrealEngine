import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

TOOLS = pathlib.Path(__file__).parents[1] / "tools" / "visual-qa"
sys.path.insert(0, str(TOOLS))
import shadow_eval
import visual_qa
sys.path.pop(0)


class ShadowEvaluationTests(unittest.TestCase):
    generator_hash = "a" * 64
    model_digest = "b" * 64
    model_tag = "synthetic-vision:v1"
    ollama_version = "test-1"

    def write_bytes(self, root, relative, raw):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        return {"path": relative.replace("\\", "/"),
                "sha256": hashlib.sha256(raw).hexdigest()}

    def write_json(self, root, relative, value):
        raw = (json.dumps(value, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":")) + "\n").encode("utf-8")
        return self.write_bytes(root, relative, raw)

    def observation(self, action, case_id):
        target = f"actor:Fixture.Target:{case_id}#0"
        targets = [{
            "identity": target, "class": "Fixture.Target", "owner_identity": "",
            "state_token": "Active", "tag": "Target", "event": "",
            "location": {"x": 160, "y": 0, "z": 0}, "deleted": False,
            "reachable": True, "interactable": action == "interact",
            "acquirable": action == "acquire_item",
        }]
        if action == "acquire_item":
            targets.append({
                **targets[0], "identity": f"actor:Fixture.Target:{case_id}Decoy#0",
                "location": {"x": 180, "y": 0, "z": 0},
            })
        return {
            "schema": "surreal-automation-observation-v1", "revision": "7", "tick": "10",
            "player_identity": "actor:Fixture.Player:Player0#0",
            "player_position": {"x": 0, "y": 0, "z": 0}, "targets": targets,
        }

    def proposal(self, action, target, wait=0):
        return {"schema": "surreal-visual-qa-shadow-proposal-v1",
                "action": action, "target_identity": target,
                "wait_ticks": wait, "confidence": 1.0, "rationale": "fixture"}

    def add_case(self, root, action, index):
        case_id = f"{action}-{index}"
        prefix = f"cases/{case_id}"
        image = self.write_bytes(root, f"{prefix}/capture.bin",
                                 f"synthetic image {case_id}".encode())
        capture_id = f"capture-{case_id}"
        observation = self.observation(action, case_id)
        observation_artifact = self.write_json(root, f"{prefix}/observation.json", observation)
        grounding = {
            "schema": "surreal-visual-qa-grounding-v1",
            "session_id": f"session-{case_id}", "source_revision": "c" * 40,
            "binary_sha256": "d" * 64, "config_sha256": "e" * 64,
            "command_id": f"command-{case_id}",
            "observation_sha256": observation_artifact["sha256"],
            "observation_revision": observation["revision"],
            "observation_tick": observation["tick"],
        }
        manifest = {
            "schema": "surreal-visual-qa-capture-manifest-v2",
            "session": {
                "session_id": grounding["session_id"],
                "source_revision": grounding["source_revision"],
                "binary_sha256": grounding["binary_sha256"],
                "config_sha256": grounding["config_sha256"],
            },
            "captures": [{
            "id": capture_id, "path": "capture.bin", "sha256": image["sha256"],
            "tick": 10, "camera": "synthetic-fixture", "width": 640, "height": 360,
            "command_id": grounding["command_id"],
            "observation": {"path": "observation.json",
                            "sha256": observation_artifact["sha256"],
                            "revision": observation["revision"],
                            "tick": observation["tick"]},
        }]}
        manifest_artifact = self.write_json(root, f"{prefix}/manifest.json", manifest)
        profile = {
            "schema": "surreal-visual-qa-assertion-profile-v1",
            "profile_id": f"profile-{case_id}",
            "prompt": f"Evaluate the bounded synthetic {action} objective.",
            "assertions": [{"id": "scene-valid", "description": "Synthetic scene is valid"}],
        }
        profile_artifact = self.write_json(root, f"{prefix}/profile.json", profile)
        primary = observation["targets"][0]["identity"]
        if action == "none":
            accepted = {"action": "none", "target_identity": "", "wait_ticks": 0}
            allowed = ["walk_to_actor"]
            target_ids = [primary]
        elif action == "wait":
            accepted = {"action": "wait", "target_identity": "", "wait_ticks": 5}
            allowed = ["wait"]
            target_ids = []
        else:
            accepted = {"action": action, "target_identity": primary, "wait_ticks": 0}
            allowed = [action]
            target_ids = [target["identity"] for target in observation["targets"]]
        fixture = {
            "schema": "surreal-visual-qa-shadow-replay-fixture-v2",
            "fixture_id": f"fixture-{case_id}",
            "capture": {"id": capture_id, "sha256": image["sha256"]},
            "observation_sha256": observation_artifact["sha256"],
            "grounding": grounding,
            "policy": {"allowed_actions": allowed, "target_identities": target_ids,
                       "max_wait_ticks": 60},
            "accepted_proposals": [accepted],
        }
        fixture_artifact = self.write_json(root, f"{prefix}/fixture.json", fixture)
        context = visual_qa.validate_shadow_context(observation, target_ids, allowed, 60)
        request_schema, request_prompt = visual_qa.finding_request(
            capture_id, profile["prompt"], visual_qa.validate_profile(profile), context)
        label_artifacts = []
        for annotator in ("reviewer-a", "reviewer-b"):
            label = {
                "schema": "surreal-visual-qa-shadow-human-label-v1", "case_id": case_id,
                "annotator_id": annotator,
                "capture_manifest_sha256": manifest_artifact["sha256"],
                "profile_sha256": profile_artifact["sha256"],
                "observation_sha256": observation_artifact["sha256"],
                "accepted_proposals": [accepted], "notes": "synthetic unit label",
            }
            label_artifacts.append(self.write_json(
                root, f"{prefix}/{annotator}.json", label))
        adjudication = {
            "schema": "surreal-visual-qa-shadow-adjudication-v1", "case_id": case_id,
            "label_sha256s": sorted(item["sha256"] for item in label_artifacts),
            "accepted_proposals": [accepted], "notes": "synthetic unit adjudication",
        }
        adjudication_artifact = self.write_json(
            root, f"{prefix}/adjudication.json", adjudication)
        case = {
            "case_id": case_id, "expected_action": action, "stratum": "canonical",
            "capture_manifest": manifest_artifact, "profile": profile_artifact,
            "observation": observation_artifact, "replay_fixture": fixture_artifact,
            "labels": label_artifacts, "adjudication": adjudication_artifact,
        }
        return case, {"case_id": case_id, "capture_id": capture_id,
                      "image_hash": image["sha256"], "context": context,
                      "accepted": accepted, "primary": primary,
                      "manifest_hash": manifest_artifact["sha256"],
                      "profile_hash": profile_artifact["sha256"],
                      "observation_hash": observation_artifact["sha256"],
                      "grounding": grounding,
                      "schema_hash": visual_qa.sha256_bytes(
                          visual_qa.canonical_json(request_schema).encode()),
                      "prompt_hash": visual_qa.sha256_bytes(request_prompt.encode())}

    def alternate_proposal(self, meta, repeat):
        accepted = meta["accepted"]
        if repeat == 1:
            return self.proposal(accepted["action"], accepted["target_identity"],
                                 accepted["wait_ticks"])
        action = accepted["action"]
        if action == "none":
            return self.proposal("walk_to_actor", meta["primary"])
        if action == "walk_to_actor":
            return self.proposal("none", "")
        if action == "acquire_item":
            return self.proposal("acquire_item",
                                 meta["context"]["targets"][1]["identity"])
        if action == "wait":
            return self.proposal("wait", "", 6)
        return self.proposal(action, meta["primary"])

    def report(self, meta, proposal, elapsed):
        finding = {
            "schema": "surreal-visual-qa-finding-v1", "capture_id": meta["capture_id"],
            "summary": "synthetic", "findings": [{
                "assertion_id": "scene-valid", "status": "pass", "confidence": 1.0,
                "explanation": "synthetic",
            }], "proposal": proposal,
        }
        return {
            "schema": "surreal-visual-qa-report-v1", "mode": "shadow",
            "controls_live_player": False,
            "model": {"requested": self.model_tag, "tag": self.model_tag,
                      "digest": self.model_digest, "ollama_version": self.ollama_version,
                      "capabilities": ["vision"]},
            "provenance": {"manifest_sha256": meta["manifest_hash"],
                           "profile_sha256": meta["profile_hash"], "seed": "104729",
                           "generator_sha256": self.generator_hash,
                           "shadow_observation_sha256": meta["observation_hash"],
                           "shadow_grounding": "same_session"},
            "results": [{"capture_id": meta["capture_id"],
                         "image_sha256": meta["image_hash"],
                         "schema_sha256": meta["schema_hash"],
                         "prompt_sha256": meta["prompt_hash"], "elapsed_ns": str(elapsed),
                         "finding": finding, "ollama_response": {},
                         "grounding": meta["grounding"],
                         "shadow_candidate": visual_qa.shadow_candidate(
                             proposal, meta["context"])}],
            "shadow_policy": {**meta["context"], "dispatch_authorized": False,
                              "controls_live_player": False},
        }

    def receipt(self, attempt_id, repeat, report_hash, elapsed=1000,
                status="report_written", error_code=""):
        return {
            "schema": "surreal-visual-qa-shadow-eval-attempt-v1",
            "attempt_id": attempt_id, "repeat": repeat,
            "requested_model": self.model_tag, "seed": "104729",
            "generator_sha256": self.generator_hash,
            "controls_live_player": False, "dispatch_authorized": False,
            "outcome": {"status": status, "elapsed_ns": str(elapsed),
                        "report_sha256": report_hash if status == "report_written" else None,
                        "error_code": error_code,
                        "error": "" if status == "report_written" else "synthetic failure"},
        }

    def bundle(self, root):
        protocol = self.write_bytes(root, "labeling-protocol.md", b"synthetic protocol\n")
        cases = []
        metas = {}
        for index, action in enumerate(shadow_eval.ACTIONS):
            case, meta = self.add_case(root, action, index)
            cases.append(case)
            metas[meta["case_id"]] = meta
        dataset = {
            "schema": "surreal-visual-qa-shadow-eval-dataset-v1",
            "dataset_id": "synthetic-five-action-v1", "labeling_protocol": protocol,
            "cases": cases, "controls_live_player": False, "dispatch_authorized": False,
        }
        dataset_path = root / "dataset.json"
        dataset_artifact = self.write_json(root, "dataset.json", dataset)
        attempts = []
        for case in cases:
            meta = metas[case["case_id"]]
            for repeat in (1, 2):
                attempt_id = f"{case['case_id']}:{self.model_tag}:{repeat}"
                proposal = self.alternate_proposal(meta, repeat)
                report_artifact = self.write_json(
                    root, f"attempts/{case['case_id']}-{repeat}-report.json",
                    self.report(meta, proposal, repeat * 100))
                receipt_artifact = self.write_json(
                    root, f"attempts/{case['case_id']}-{repeat}-receipt.json",
                    self.receipt(attempt_id, repeat, report_artifact["sha256"],
                                 repeat * 1000))
                attempts.append({"attempt_id": attempt_id, "case_id": case["case_id"],
                                 "repeat": repeat, "model": self.model_tag,
                                 "receipt": receipt_artifact, "report": report_artifact})
        run = {
            "schema": "surreal-visual-qa-shadow-eval-run-v1",
            "run_id": "synthetic-run-v1", "dataset_sha256": dataset_artifact["sha256"],
            "generator_sha256": self.generator_hash,
            "ollama_version": self.ollama_version, "seed": "104729", "repeat_count": 2,
            "models": [{"tag": self.model_tag, "digest": self.model_digest}],
            "attempts": attempts, "controls_live_player": False,
            "dispatch_authorized": False,
        }
        run_path = root / "run.json"
        self.write_json(root, "run.json", run)
        return dataset_path, run_path, dataset, run

    def downgrade_case_to_v1(self, root, case):
        case = json.loads(json.dumps(case))
        manifest_path = root / case["capture_manifest"]["path"]
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        legacy_manifest = {
            "schema": "surreal-visual-qa-capture-manifest-v1",
            "captures": [{key: value for key, value in capture.items()
                          if key != "observation"}
                         for capture in manifest["captures"]],
        }
        case_directory = pathlib.Path(case["capture_manifest"]["path"]).parent
        case["capture_manifest"] = self.write_json(
            root, str(case_directory / "legacy-manifest.json"), legacy_manifest)

        fixture_path = root / case["replay_fixture"]["path"]
        fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
        fixture["schema"] = "surreal-visual-qa-shadow-replay-fixture-v1"
        fixture.pop("grounding")
        case["replay_fixture"] = self.write_json(
            root, f"legacy/{case['case_id']}-fixture.json", fixture)

        labels = []
        for index, artifact in enumerate(case["labels"]):
            label = json.loads((root / artifact["path"]).read_text(encoding="utf-8"))
            label["capture_manifest_sha256"] = case["capture_manifest"]["sha256"]
            labels.append(self.write_json(
                root, f"legacy/{case['case_id']}-label-{index}.json", label))
        case["labels"] = labels
        adjudication = json.loads(
            (root / case["adjudication"]["path"]).read_text(encoding="utf-8"))
        adjudication["label_sha256s"] = sorted(item["sha256"] for item in labels)
        case["adjudication"] = self.write_json(
            root, f"legacy/{case['case_id']}-adjudication.json", adjudication)
        return case

    def test_scores_all_action_families_and_semantic_repeatability(self):
        with tempfile.TemporaryDirectory() as directory:
            dataset_path, run_path, _, _ = self.bundle(pathlib.Path(directory))
            first = shadow_eval.evaluate(dataset_path, run_path)
            second = shadow_eval.evaluate(dataset_path, run_path)
        self.assertEqual(first, second)
        self.assertFalse(first["controls_live_player"])
        self.assertFalse(first["dispatch_authorized"])
        self.assertEqual(first["attempt_count"], 10)
        metrics = first["models"][0]["metrics"]
        self.assertEqual(metrics["schema_valid_count"], 10)
        self.assertEqual(metrics["exact_oracle_count"], 6)
        self.assertEqual(metrics["exact_oracle_accuracy_ppm"], 600000)
        self.assertEqual(metrics["unsafe_commission_count"], 1)
        self.assertEqual(metrics["over_abstention_count"], 1)
        self.assertEqual(metrics["target_exact_count"], 4)
        self.assertEqual(metrics["wait_exact_count"], 1)
        self.assertEqual(metrics["repeat_stable_case_count"], 1)
        self.assertEqual(metrics["pairwise_agreement_count"], 1)
        self.assertEqual(metrics["confusion"]["none"]["walk_to_actor"], 1)
        self.assertEqual(metrics["confusion"]["walk_to_actor"]["none"], 1)
        self.assertEqual(metrics["inference_latency_ns"]["p50"], "100")
        self.assertEqual(metrics["inference_latency_ns"]["p95"], "200")

    def test_failed_receipt_is_counted_instead_of_disappearing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            dataset_path, _, _, run = self.bundle(root)
            attempt = run["attempts"][0]
            failed = self.receipt(attempt["attempt_id"], attempt["repeat"], None,
                                  status="failed", error_code="validation_error")
            attempt["receipt"] = self.write_json(root, "attempts/failed-receipt.json", failed)
            attempt["report"] = None
            run_path = root / "failed-run.json"
            self.write_json(root, "failed-run.json", run)
            result = shadow_eval.evaluate(dataset_path, run_path)
        metrics = result["models"][0]["metrics"]
        self.assertEqual(metrics["invalid_count"], 1)
        self.assertEqual(metrics["schema_valid_count"], 9)
        failed_attempt = next(item for item in result["attempts"]
                              if item["attempt_id"] == attempt["attempt_id"])
        self.assertEqual(failed_attempt["predicted_action"], "invalid")

    def test_dataset_and_run_tampering_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            dataset_path, run_path, dataset, run = self.bundle(root)
            dataset["controls_live_player"] = True
            bad_dataset = root / "bad-dataset.json"
            self.write_json(root, "bad-dataset.json", dataset)
            with self.assertRaises(visual_qa.VisualQAError):
                shadow_eval.validate_dataset(bad_dataset)
            run["attempts"].pop()
            bad_run = root / "bad-run.json"
            self.write_json(root, "bad-run.json", run)
            with self.assertRaises(visual_qa.VisualQAError):
                shadow_eval.evaluate(dataset_path, bad_run)
            escaped = json.loads(dataset_path.read_text(encoding="utf-8"))
            escaped["labeling_protocol"]["path"] = "../outside.md"
            escaped_path = root / "escaped.json"
            self.write_json(root, "escaped.json", escaped)
            with self.assertRaises((visual_qa.VisualQAError, OSError)):
                shadow_eval.validate_dataset(escaped_path)
            self.assertTrue(run_path.is_file())

    def test_duplicate_keys_and_adjudication_substitution_are_rejected(self):
        with self.assertRaises(visual_qa.VisualQAError):
            shadow_eval.strict_json(b'{"schema":"a","schema":"b"}', "duplicate")
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            dataset_path, _, dataset, _ = self.bundle(root)
            case = dataset["cases"][0]
            adjudication_path = root / case["adjudication"]["path"]
            adjudication = json.loads(adjudication_path.read_text(encoding="utf-8"))
            adjudication["accepted_proposals"] = [
                {"action": "wait", "target_identity": "", "wait_ticks": 1}]
            case["adjudication"] = self.write_json(
                root, "cases/substituted-adjudication.json", adjudication)
            substituted = root / "substituted-dataset.json"
            self.write_json(root, "substituted-dataset.json", dataset)
            with self.assertRaises(visual_qa.VisualQAError):
                shadow_eval.validate_dataset(substituted)
            self.assertTrue(dataset_path.is_file())

    def test_prompt_hash_substitution_is_counted_as_invalid_model_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            dataset_path, _, _, run = self.bundle(root)
            attempt = run["attempts"][0]
            report_path = root / attempt["report"]["path"]
            report = json.loads(report_path.read_text(encoding="utf-8"))
            report["results"][0]["prompt_sha256"] = "e" * 64
            attempt["report"] = self.write_json(
                root, "attempts/substituted-prompt-report.json", report)
            attempt["receipt"] = self.write_json(
                root, "attempts/substituted-prompt-receipt.json",
                self.receipt(attempt["attempt_id"], attempt["repeat"],
                             attempt["report"]["sha256"]))
            run_path = root / "substituted-prompt-run.json"
            self.write_json(root, "substituted-prompt-run.json", run)
            result = shadow_eval.evaluate(dataset_path, run_path)
        scored = next(item for item in result["attempts"]
                      if item["attempt_id"] == attempt["attempt_id"])
        self.assertEqual(scored["status"], "invalid")
        self.assertIn("prompt or schema hash", scored["error_code"])

    def test_legacy_none_case_remains_valid_but_positive_case_requires_v2(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            _, _, dataset, _ = self.bundle(root)
            legacy_none = self.downgrade_case_to_v1(root, dataset["cases"][0])
            none_dataset = {**dataset, "cases": [legacy_none]}
            none_path = root / "legacy-none-dataset.json"
            self.write_json(root, none_path.name, none_dataset)
            validated = shadow_eval.validate_dataset(none_path)
            self.assertEqual(validated["cases"][legacy_none["case_id"]]
                             ["grounding_status"], "legacy_unbound")

            positive = next(case for case in dataset["cases"]
                            if case["expected_action"] == "interact")
            legacy_positive = self.downgrade_case_to_v1(root, positive)
            positive_path = root / "legacy-positive-dataset.json"
            self.write_json(root, positive_path.name,
                            {**dataset, "cases": [legacy_positive]})
            with self.assertRaises(visual_qa.VisualQAError):
                shadow_eval.validate_dataset(positive_path)

    def test_grounding_session_command_and_observation_substitution_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            _, _, dataset, _ = self.bundle(root)
            base_case = next(case for case in dataset["cases"]
                             if case["expected_action"] == "interact")
            base_manifest = json.loads(
                (root / base_case["capture_manifest"]["path"])
                .read_text(encoding="utf-8"))
            mutations = {
                "session": lambda value: value["session"].update(
                    session_id="substituted-session"),
                "command": lambda value: value["captures"][0].update(
                    command_id="substituted-command"),
                "revision": lambda value: value["captures"][0]["observation"].update(
                    revision="8"),
                "tick": lambda value: value["captures"][0]["observation"].update(
                    tick="11"),
                "observation-hash": lambda value: value["captures"][0]["observation"].update(
                    sha256="f" * 64),
            }
            for name, mutate in mutations.items():
                manifest = json.loads(json.dumps(base_manifest))
                mutate(manifest)
                case = json.loads(json.dumps(base_case))
                case_directory = pathlib.Path(
                    base_case["capture_manifest"]["path"]).parent
                case["capture_manifest"] = self.write_json(
                    root, str(case_directory / f"substituted-{name}-manifest.json"),
                    manifest)
                path = root / f"substituted-{name}-dataset.json"
                self.write_json(root, path.name,
                                {**dataset, "cases": [case]})
                with self.assertRaises(visual_qa.VisualQAError, msg=name):
                    shadow_eval.validate_dataset(path)


if __name__ == "__main__":
    unittest.main()
