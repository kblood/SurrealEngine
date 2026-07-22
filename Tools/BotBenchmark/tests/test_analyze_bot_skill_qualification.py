#!/usr/bin/env python3
"""Synthetic contract tests for Analyze-BotSkillQualification.py."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).resolve().parents[1] / "Analyze-BotSkillQualification.py"
SPEC = importlib.util.spec_from_file_location("skill_qualification", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def run_row(map_name: str, seed: str, repetition: int, high_candidate: bool, digest: str) -> dict:
    if high_candidate:
        candidate_score, opponent_score = 3, 0
        candidate_damage, opponent_damage = 90, 30
        candidate_deaths, opponent_deaths = 0, 2
        candidate_weapon, opponent_weapon = 90, 180
        skill, opponent_skill = 2, 1
    else:
        candidate_score, opponent_score = 0, 3
        candidate_damage, opponent_damage = 30, 90
        candidate_deaths, opponent_deaths = 2, 0
        candidate_weapon, opponent_weapon = 180, 90
        skill, opponent_skill = 1, 2
    return {
        "case_id": f"{map_name}-{skill}v{opponent_skill}-{seed}",
        "run_id": f"{map_name}-{skill}v{opponent_skill}-{seed}-run-{repetition}",
        "map": map_name,
        "skill": skill,
        "opponent_skill": opponent_skill,
        "seed": seed,
        "repetition": repetition,
        "requested_bots": 2,
        "requested_ticks": 5400,
        "fixed_delta": 1.0 / 60.0,
        "valid": True,
        "deterministic": True,
        "digest_fnv1a64": digest,
        "candidate_score": candidate_score,
        "opponent_score": opponent_score,
        "candidate_deaths": candidate_deaths,
        "opponent_deaths": opponent_deaths,
        "candidate_first_nonstarter_weapon_tick": candidate_weapon,
        "opponent_first_nonstarter_weapon_tick": opponent_weapon,
        "candidate_damage_dealt_exact": candidate_damage,
        "opponent_damage_dealt_exact": opponent_damage,
        "self_damage_exact_total": 0,
        "environmental_damage_exact_total": 0,
        "external_damage_taken_exact_total": 0,
        "external_damage_dealt_exact_total": 0,
        "fatal_damage_kills_exact_total": 2,
        "fatal_damage_deaths_exact_total": 2,
        "self_fatal_damage_deaths_exact_total": 0,
        "environmental_fatal_damage_deaths_exact_total": 0,
        "external_fatal_damage_deaths_exact_total": 0,
        "adjudicated_deaths_exact_total": 2,
        "adjudicated_opponent_kills_exact_total": 2,
        "adjudicated_self_deaths_exact_total": 0,
        "adjudicated_environmental_deaths_exact_total": 0,
        "adjudicated_external_deaths_exact_total": 0,
        "adjudicated_external_kills_exact_total": 0,
        "adjudicated_direct_deaths_exact_total": 0,
        "hitscan_shots_total": 20,
        "hitscan_hits_total": 10,
        "projectile_launches_total": 12,
        "projectile_hits_finalized_total": 6,
        "projectile_misses_finalized_total": 6,
        "firing_intent_seconds_total": 15,
        "no_progress_seconds_proxy_total": 0.25,
        "stuck_events_proxy_total": 0,
        "fixture_id": "",
        "scenario": "skill-qualification",
        "candidate_profile_id": "profile-candidate-slot",
        "opponent_profile_id": "profile-opponent-slot",
        "candidate_profile_name": "Loque",
        "opponent_profile_name": "Tamerlane",
    }


def write_matrix(
    path: Path, high_candidate: bool, *, corrupt_digest: bool = False,
    configuration_ticks: int = 5400, maps: tuple[str, ...] = ("DM-TestA", "DM-TestB"),
    reverse_maps: tuple[str, ...] = (),
) -> None:
    rows = []
    for map_index, map_name in enumerate(maps):
        for seed_index in range(8):
            seed = str(1000 + seed_index)
            digest = f"{map_index + 1:08x}{seed_index + 1:08x}"
            first = run_row(map_name, seed, 1, high_candidate, digest)
            if map_name in reverse_maps:
                for left, right in (
                    ("candidate_score", "opponent_score"), ("candidate_damage_dealt_exact", "opponent_damage_dealt_exact"),
                    ("candidate_deaths", "opponent_deaths"),
                    ("candidate_first_nonstarter_weapon_tick", "opponent_first_nonstarter_weapon_tick"),
                ):
                    first[left], first[right] = first[right], first[left]
            rows.append(first)
            second_digest = "ffffffffffffffff" if corrupt_digest and map_index == 0 and seed_index == 0 else digest
            second = run_row(map_name, seed, 2, high_candidate, second_digest)
            if map_name in reverse_maps:
                for left, right in (
                    ("candidate_score", "opponent_score"), ("candidate_damage_dealt_exact", "opponent_damage_dealt_exact"),
                    ("candidate_deaths", "opponent_deaths"),
                    ("candidate_first_nonstarter_weapon_tick", "opponent_first_nonstarter_weapon_tick"),
                ):
                    second[left], second[right] = second[right], second[left]
            rows.append(second)
    path.write_text(json.dumps({
        "schema": 1,
        "passed": True,
        "engine_path": "C:/synthetic/SurrealEngine.exe",
        "game_root": "C:/synthetic/UT",
        "build_commit": "0123456789abcdef",
        "configuration": {
            "seconds": 90.0,
            "ticks": configuration_ticks,
            "fixed_delta": 1.0 / 60.0,
            "bots": 2,
            "bot_name": "Loque",
            "fixture_id": "",
            "scenario": "skill-qualification",
        },
        "runs": rows,
    }), encoding="utf-8")


def write_manifest(path: Path, include_lower: bool = True, mode: str = "synthetic_test") -> None:
    inputs = [{
        "label": "2v1-higher-candidate",
        "path": "higher.json",
        "higher_tier": 2,
        "lower_tier": 1,
        "orientation": "higher_candidate",
    }]
    if include_lower:
        inputs.append({
            "label": "2v1-lower-candidate",
            "path": "lower.json",
            "higher_tier": 2,
            "lower_tier": 1,
            "orientation": "lower_candidate",
        })
    if mode == "qualification":
        for entry in inputs:
            entry["sha256"] = hashlib.sha256((path.parent / entry["path"]).read_bytes()).hexdigest()
    path.write_text(json.dumps({"schema": 1, "mode": mode, "inputs": inputs}), encoding="utf-8")


def qualification_row(
    map_name: str, seed: str, repetition: int, high: int, low: int, high_candidate: bool,
    digest: str, engine_sha: str, content_sha: str,
) -> dict:
    row = run_row(map_name, seed, repetition, high_candidate, digest)
    row.update({
        "case_id": f"{map_name}-{high}-{low}-{seed}",
        "run_id": f"{map_name}-{high}-{low}-{'hc' if high_candidate else 'lc'}-{seed}-{repetition}",
        "skill": high if high_candidate else low,
        "opponent_skill": low if high_candidate else high,
        "protocol_id": MODULE.QUALIFICATION_PROTOCOL_ID,
        "game_class": MODULE.QUALIFICATION_GAME_CLASS,
        "engine_binary_sha256": engine_sha,
        "content_manifest_sha256": content_sha,
        "damage_dealt_exact_total": 120,
        "damage_taken_exact_total": 120,
        "pri_deaths_total": 2,
        "final_score_total": 3,
        "candidate_score_margin": row["candidate_score"] - row["opponent_score"],
        "candidate_death_advantage": row["opponent_deaths"] - row["candidate_deaths"],
        "hitscan_accuracy": 0.5,
        "projectile_finalized_accuracy": 0.5,
    })
    return row


def write_qualification_family(root: Path, *, runs_per_case: int = 1) -> tuple[Path, list[Path]]:
    engine = root / "SurrealEngine.exe"
    content = root / "content-manifest.json"
    game_root = root / "UT"
    for directory in ("System", "Textures", "Sounds", "Music", "Maps"):
        (game_root / directory).mkdir(parents=True, exist_ok=True)
    engine.write_bytes(b"synthetic immutable engine")
    packages = []
    for logical_path in MODULE.QUALIFICATION_CONTENT_PATHS:
        package = game_root.joinpath(*logical_path.split("/"))
        package.parent.mkdir(parents=True, exist_ok=True)
        package.write_bytes(f"synthetic immutable content: {logical_path}".encode())
        packages.append({"path": logical_path, "sha256": hashlib.sha256(package.read_bytes()).hexdigest()})
    content.write_text(json.dumps({
        "schema": 1, "protocol_id": MODULE.QUALIFICATION_PROTOCOL_ID,
        "scope": MODULE.QUALIFICATION_CONTENT_SCOPE,
        "packages": packages, "content_closure": packages,
    }) + "\n", encoding="utf-8")
    engine_sha = hashlib.sha256(engine.read_bytes()).hexdigest()
    content_sha = hashlib.sha256(content.read_bytes()).hexdigest()
    inputs = []
    matrix_paths = []
    for high, low in sorted(MODULE.ADJACENT_FAMILY):
        for orientation in sorted(MODULE.ORIENTATIONS):
            high_candidate = orientation == "higher_candidate"
            label = f"{high}v{low}-{orientation}"
            matrix_path = root / f"{label}.json"
            rows = []
            cases = []
            for map_name in MODULE.QUALIFICATION_MAPS:
                for seed in MODULE.QUALIFICATION_SEEDS:
                    digest = hashlib.sha256(f"{label}/{map_name}/{seed}".encode()).hexdigest()[:16]
                    case_id = f"{map_name}-{high}-{low}-{seed}"
                    cases.append({
                        "case_id": case_id, "map": map_name,
                        "skill": high if high_candidate else low,
                        "opponent_skill": low if high_candidate else high,
                        "fixture_id": "", "seed": seed, "expected_runs": runs_per_case,
                        "valid_runs": runs_per_case, "unique_valid_digests": 1,
                        "digest_fnv1a64": digest, "deterministic": True,
                        "candidate_profile_name": "Loque", "opponent_profile_name": "Tamerlane",
                        "candidate_profile_id": "profile-candidate-slot",
                        "opponent_profile_id": "profile-opponent-slot",
                    })
                    for repetition in range(1, runs_per_case + 1):
                        row = qualification_row(
                            map_name, seed, repetition, high, low, high_candidate, digest, engine_sha, content_sha
                        )
                        run_directory = root / row["run_id"]
                        run_directory.mkdir()
                        evidence_files = {
                            "events": run_directory / "events.jsonl",
                            "summary": run_directory / "summary.json",
                            "invocation": run_directory / "invocation.txt",
                            "trace_validation": run_directory / "trace-validation.json",
                        }
                        evidence_files["events"].write_text(
                            json.dumps({"synthetic_run_id": row["run_id"]}) + "\n", encoding="utf-8"
                        )
                        evidence_files["summary"].write_text(json.dumps({
                            "synthetic_run_id": row["run_id"], "map": map_name, "seed": seed,
                            "digest_fnv1a64": digest,
                            "bot_metrics": [
                                {
                                    "roster_index": 0, "requested_external_skill": row["skill"],
                                    "last_score": row["candidate_score"],
                                    "adjudicated_deaths_exact": row["candidate_deaths"],
                                    "player_name": "Loque", "profile_id": "profile-candidate-slot",
                                    "first_nonstarter_weapon_tick": row["candidate_first_nonstarter_weapon_tick"],
                                    "damage_dealt_exact": row["candidate_damage_dealt_exact"],
                                },
                                {
                                    "roster_index": 1, "requested_external_skill": row["opponent_skill"],
                                    "last_score": row["opponent_score"],
                                    "adjudicated_deaths_exact": row["opponent_deaths"],
                                    "player_name": "Tamerlane", "profile_id": "profile-opponent-slot",
                                    "first_nonstarter_weapon_tick": row["opponent_first_nonstarter_weapon_tick"],
                                    "damage_dealt_exact": row["opponent_damage_dealt_exact"],
                                },
                            ],
                        }) + "\n", encoding="utf-8")
                        evidence_files["invocation"].write_text(
                            f"synthetic invocation {row['run_id']}\n", encoding="utf-8"
                        )
                        events_sha = hashlib.sha256(evidence_files["events"].read_bytes()).hexdigest()
                        summary_sha = hashlib.sha256(evidence_files["summary"].read_bytes()).hexdigest()
                        evidence_files["trace_validation"].write_text(json.dumps({
                            "schema": 1, "passed": True, "validation_mode": "qualification",
                            "protocol_id": MODULE.QUALIFICATION_PROTOCOL_ID,
                            "summary_validated": True,
                            "events_sha256": events_sha, "summary_sha256": summary_sha,
                            "run_identity": {
                                "map": map_name, "seed": seed,
                                "requested_skills": [row["skill"], row["opponent_skill"]],
                                "requested_bot_names": ["Loque", "Tamerlane"],
                                "digest_fnv1a64": digest, "scenario": "skill-qualification",
                                "requested_bots": 2, "ticks": 5400, "fixed_delta": 1.0 / 60.0,
                            },
                        }), encoding="utf-8")
                        evidence_hashes = {
                            name: hashlib.sha256(path.read_bytes()).hexdigest()
                            for name, path in evidence_files.items()
                        }
                        for evidence_name, evidence_path in evidence_files.items():
                            row[f"{evidence_name}_path"] = evidence_path.relative_to(root).as_posix()
                            row[f"{evidence_name}_sha256"] = evidence_hashes[evidence_name]
                        row["trace_validation_passed"] = True
                        rows.append(row)
            matrix_path.write_text(json.dumps({
                "schema": 1, "passed": True, "engine_path": str(engine),
                "engine_binary_sha256": engine_sha,
                "engine_binary_sha256_after": engine_sha, "immutable_identity_reverified": True,
                "content_manifest_path": "content-manifest.json", "content_manifest_sha256": content_sha,
                "game_root": str(game_root), "batch_root": ".",
                "protocol_id": MODULE.QUALIFICATION_PROTOCOL_ID,
                "game_class": MODULE.QUALIFICATION_GAME_CLASS, "scenario": "skill-qualification",
                "configuration": {
                    "protocol_id": MODULE.QUALIFICATION_PROTOCOL_ID,
                    "game_class": MODULE.QUALIFICATION_GAME_CLASS,
                    "maps": list(MODULE.QUALIFICATION_MAPS), "seeds": list(MODULE.QUALIFICATION_SEEDS),
                    "skills": [high if high_candidate else low],
                    "opponent_skill": low if high_candidate else high,
                    "bots": 2, "runs_per_case": runs_per_case, "seconds": 90.0,
                    "fixed_delta": 1.0 / 60.0, "ticks": 5400,
                    "fixture_id": "", "scenario": "skill-qualification", "bot_name": "Loque",
                    "candidate_profile_name": "Loque", "opponent_profile_name": "Tamerlane",
                    "candidate_profile_id": "profile-candidate-slot",
                    "opponent_profile_id": "profile-opponent-slot",
                    "engine_binary_sha256": engine_sha, "content_manifest_sha256": content_sha,
                },
                "totals": {"cases": 72, "runs": 72 * runs_per_case, "failed_runs": 0,
                           "nondeterministic_cases": 0},
                "cases": cases, "runs": rows,
            }), encoding="utf-8")
            matrix_paths.append(matrix_path)
            inputs.append({
                "label": label, "path": matrix_path.name, "higher_tier": high, "lower_tier": low,
                "orientation": orientation,
                "sha256": hashlib.sha256(matrix_path.read_bytes()).hexdigest(),
            })
    manifest = root / "qualification-manifest.json"
    manifest.write_text(json.dumps({"schema": 1, "mode": "qualification", "inputs": inputs}), encoding="utf-8")
    return manifest, matrix_paths


def rewrite_matrix_content_identity(matrix_path: Path, manifest_path: Path) -> None:
    content_sha = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    # Qualification content manifests are deliberately matrix-relative.
    matrix["content_manifest_path"] = manifest_path.name
    matrix["content_manifest_sha256"] = content_sha
    for row in matrix["runs"]:
        row["content_manifest_sha256"] = content_sha
    matrix_path.write_text(json.dumps(matrix), encoding="utf-8")


def refresh_outer_matrix_hash(manifest_path: Path, matrix_path: Path) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for entry in manifest["inputs"]:
        if Path(entry["path"]).name == matrix_path.name:
            entry["sha256"] = hashlib.sha256(matrix_path.read_bytes()).hexdigest()
            break
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")


def mutate_first_trace_report(root: Path, matrix_path: Path, mutation) -> None:
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    row = matrix["runs"][0]
    report_path = root / row["trace_validation_path"]
    report = json.loads(report_path.read_text(encoding="utf-8"))
    mutation(report)
    report_path.write_text(json.dumps(report), encoding="utf-8")
    row["trace_validation_sha256"] = hashlib.sha256(report_path.read_bytes()).hexdigest()
    matrix_path.write_text(json.dumps(matrix), encoding="utf-8")


def mutate_json(path: Path, mutation) -> None:
    document = json.loads(path.read_text(encoding="utf-8"))
    mutation(document)
    path.write_text(json.dumps(document), encoding="utf-8")


class SkillQualificationTests(unittest.TestCase):
    def assertQualificationMutationRejected(self, mutation, pattern: str) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            mutate_json(matrices[0], mutation)
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, pattern):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_exact_canonical_qualification_family_can_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, _ = write_qualification_family(root)
            report = MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=9)
            self.assertEqual(report["independent_orientation_trials"], 1008)
            self.assertEqual(report["determinism_duplicates_collapsed"], 0)
            self.assertEqual(report["crossover_units"], 504)
            self.assertEqual(len(report["pairs"]), 7)
            self.assertTrue(report["readiness"]["qualification_claim_allowed"])
            self.assertTrue(report["all_pairs_separated"])
            self.assertEqual(report["claim_scope"], "adjacent_skill_monotonicity")
            self.assertFalse(report["godlike_evaluated"])
            self.assertFalse(report["godlike_qualified"])
            self.assertEqual(report["provenance"]["observed"]["protocol_id"], MODULE.QUALIFICATION_PROTOCOL_ID)

    def test_qualification_rejects_noncanonical_map_or_seed_grid(self) -> None:
        mutations = (
            lambda doc: doc["configuration"]["maps"].reverse(),
            lambda doc: doc["configuration"]["seeds"].reverse(),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                self.assertQualificationMutationRejected(mutation, "canonical protocol value")

    def test_qualification_rejects_protocol_duration_game_or_fixture_drift(self) -> None:
        mutations = (
            lambda doc: doc["configuration"].__setitem__("protocol_id", "other"),
            lambda doc: doc["configuration"].__setitem__("ticks", 5399),
            lambda doc: doc["configuration"].__setitem__("fixed_delta", 0.02),
            lambda doc: doc["configuration"].__setitem__("seconds", 89.0),
            lambda doc: doc["configuration"].__setitem__("game_class", "Botpack.TeamGamePlus"),
            lambda doc: doc["configuration"].__setitem__("fixture_id", "HitWall"),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                self.assertQualificationMutationRejected(mutation, "must equal")

    def test_qualification_rejects_missing_or_mismatched_immutable_identity(self) -> None:
        self.assertQualificationMutationRejected(
            lambda doc: doc.pop("engine_binary_sha256"), "canonical lowercase SHA-256"
        )
        self.assertQualificationMutationRejected(
            lambda doc: doc.__setitem__("engine_binary_sha256", "0" * 64), "does not match the declared file"
        )
        self.assertQualificationMutationRejected(
            lambda doc: doc.__setitem__("content_manifest_sha256", "0" * 64), "does not match the declared file"
        )

    def test_qualification_rejects_content_package_identity_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, _ = write_qualification_family(root)
            (root / "UT" / "System" / "BotPack.u").write_bytes(b"mutated after content identity capture")
            with self.assertRaisesRegex(MODULE.QualificationError, "package SHA-256 does not match"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_unrelated_missing_or_extra_content_entries(self) -> None:
        def unrelated_only(root: Path, document: dict) -> None:
            unrelated = root / "UT" / "README.txt"
            unrelated.write_bytes(b"unrelated")
            document["packages"] = [{
                "path": "README.txt", "sha256": hashlib.sha256(unrelated.read_bytes()).hexdigest(),
            }]

        def missing_map(_root: Path, document: dict) -> None:
            document["packages"] = [
                item for item in document["packages"] if item["path"] != "Maps/DM-Phobos.unr"
            ]

        def extra_file(root: Path, document: dict) -> None:
            extra = root / "UT" / "System" / "Unexpected.u"
            extra.write_bytes(b"unexpected")
            document["packages"].append({
                "path": "System/Unexpected.u", "sha256": hashlib.sha256(extra.read_bytes()).hexdigest(),
            })

        for mutation in (unrelated_only, missing_map, extra_file):
            with self.subTest(mutation=mutation.__name__), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest, matrices = write_qualification_family(root)
                content = root / "content-manifest.json"
                document = json.loads(content.read_text(encoding="utf-8"))
                mutation(root, document)
                content.write_text(json.dumps(document) + "\n", encoding="utf-8")
                rewrite_matrix_content_identity(matrices[0], content)
                refresh_outer_matrix_hash(manifest, matrices[0])
                with self.assertRaisesRegex(MODULE.QualificationError, "exact protocol-required|missing"):
                    MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_content_traversal_aliases_and_wrong_casing(self) -> None:
        replacements = ("../outside.u", "System\\Core.u", "system/Core.u", "System/../System/Core.u")
        for replacement in replacements:
            with self.subTest(replacement=replacement), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest, matrices = write_qualification_family(root)
                (root / "outside.u").write_bytes(b"outside root")
                content = root / "content-manifest.json"
                document = json.loads(content.read_text(encoding="utf-8"))
                document["packages"][0]["path"] = replacement
                content.write_text(json.dumps(document) + "\n", encoding="utf-8")
                rewrite_matrix_content_identity(matrices[0], content)
                refresh_outer_matrix_hash(manifest, matrices[0])
                with self.assertRaisesRegex(
                    MODULE.QualificationError, "traversal|aliases|canonical|wrong physical casing|missing"
                ):
                    MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_symlink_escape_when_supported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, _ = write_qualification_family(root)
            outside = root / "outside-map.unr"
            outside.write_bytes(b"outside root")
            required_map = root / "UT" / "Maps" / "DM-Phobos.unr"
            required_map.unlink()
            try:
                required_map.symlink_to(outside)
            except OSError:
                self.skipTest("filesystem does not permit test symlink creation")
            with self.assertRaisesRegex(MODULE.QualificationError, "escapes the canonical game_root"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_nonexistent_or_cross_input_game_root(self) -> None:
        self.assertQualificationMutationRejected(
            lambda doc: doc.__setitem__("game_root", "does-not-exist"), "game_root does not exist"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            copied_root = root / "UT-copy"
            shutil.copytree(root / "UT", copied_root)
            mutate_json(matrices[1], lambda doc: doc.__setitem__("game_root", str(copied_root)))
            refresh_outer_matrix_hash(manifest, matrices[1])
            with self.assertRaisesRegex(MODULE.QualificationError, "provenance mismatch for 'game_root'"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_cross_input_content_manifest_sha_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            original = json.loads((root / "content-manifest.json").read_text(encoding="utf-8"))
            original["capture_note"] = "different immutable manifest"
            second = root / "qualification-content-second.json"
            second.write_text(json.dumps(original) + "\n", encoding="utf-8")
            rewrite_matrix_content_identity(matrices[1], second)
            refresh_outer_matrix_hash(manifest, matrices[1])
            with self.assertRaisesRegex(MODULE.QualificationError, "content_manifest"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_mutated_required_map(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, _ = write_qualification_family(root)
            (root / "UT" / "Maps" / "DM-Deck16][.unr").write_bytes(b"mutated required map")
            with self.assertRaisesRegex(MODULE.QualificationError, "package SHA-256 does not match"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_content_closure_is_live_and_case_insensitive_by_extension(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, _ = write_qualification_family(root)
            (root / "UT" / "Textures" / "Added.UTX").write_bytes(b"late closure addition")
            with self.assertRaisesRegex(MODULE.QualificationError, "content closure differs"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_content_closure_requires_canonical_directories(self) -> None:
        for directory in ("Textures", "Sounds", "Music"):
            with self.subTest(directory=directory), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest, _ = write_qualification_family(root)
                (root / "UT" / directory).rename(root / "UT" / directory.lower())
                with self.assertRaisesRegex(MODULE.QualificationError, "missing or has noncanonical casing"):
                    MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_content_protocol_scope_and_absolute_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            content = root / "content-manifest.json"
            document = json.loads(content.read_text(encoding="utf-8"))
            document["scope"] = "weaker"
            content.write_text(json.dumps(document), encoding="utf-8")
            rewrite_matrix_content_identity(matrices[0], content)
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, "protocol/scope"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            mutate_json(matrices[0], lambda doc: doc["runs"][0].__setitem__(
                "events_path", str((root / "events.jsonl").resolve())
            ))
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, "contained matrix-relative"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_shared_or_mislaid_per_run_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            matrix = json.loads(matrices[0].read_text(encoding="utf-8"))
            matrix["runs"][0]["events_path"] = matrix["runs"][1]["events_path"]
            matrices[0].write_text(json.dumps(matrix), encoding="utf-8")
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, "exact per-run evidence layout"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_trace_report_raw_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            mutate_first_trace_report(
                root, matrices[0], lambda report: report.__setitem__("events_sha256", "0" * 64)
            )
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, "does not bind the declared raw evidence"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_report_without_validated_summary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            mutate_first_trace_report(
                root, matrices[0], lambda report: report.__setitem__("summary_validated", False)
            )
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, "not a passing report"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_trace_report_row_identity_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            mutate_first_trace_report(
                root, matrices[0], lambda report: report["run_identity"].__setitem__("seed", "999")
            )
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, "run_identity.seed"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_accepts_serialized_delta_but_rejects_wrong_delta(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            mutate_first_trace_report(
                root, matrices[0], lambda report: report["run_identity"].__setitem__("fixed_delta", 0.016667)
            )
            refresh_outer_matrix_hash(manifest, matrices[0])
            MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, matrices = write_qualification_family(root)
            mutate_first_trace_report(
                root, matrices[0], lambda report: report["run_identity"].__setitem__("fixed_delta", 0.02)
            )
            refresh_outer_matrix_hash(manifest, matrices[0])
            with self.assertRaisesRegex(MODULE.QualificationError, "run_identity.fixed_delta"):
                MODULE.analyze_manifest(manifest, draws=100, bootstrap_seed=1)

    def test_qualification_rejects_external_contamination(self) -> None:
        for field in (
            "external_damage_taken_exact_total", "external_damage_dealt_exact_total",
            "adjudicated_external_deaths_exact_total", "adjudicated_external_kills_exact_total",
        ):
            with self.subTest(field=field):
                self.assertQualificationMutationRejected(
                    lambda doc, field=field: doc["runs"][0].__setitem__(field, 1),
                    "external/nonparticipant",
                )

    def test_qualification_rejects_root_totals_and_array_drift(self) -> None:
        mutations = (
            lambda doc: doc["totals"].__setitem__("runs", doc["totals"]["runs"] - 1),
            lambda doc: doc["cases"].pop(),
            lambda doc: doc["runs"].pop(),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                self.assertQualificationMutationRejected(mutation, "totals|arrays")

    def test_qualification_rejects_case_and_run_cartesian_corruption(self) -> None:
        mutations_and_patterns = (
            (lambda doc: doc["cases"][1].__setitem__("seed", doc["cases"][0]["seed"]), "duplicate or noncanonical"),
            (lambda doc: doc["cases"][0].__setitem__("digest_fnv1a64", "f" * 16), "run digest does not match"),
            (lambda doc: doc["runs"][0].__setitem__("case_id", "wrong"), "run/case linkage"),
            (lambda doc: doc["runs"][1].update({
                "map": doc["runs"][0]["map"], "seed": doc["runs"][0]["seed"],
                "case_id": doc["runs"][0]["case_id"], "repetition": 1,
            }), "duplicate repetition"),
            (lambda doc: doc["runs"][1].__setitem__("run_id", doc["runs"][0]["run_id"]), "run_id must be"),
        )
        for mutation, pattern in mutations_and_patterns:
            with self.subTest(pattern=pattern):
                self.assertQualificationMutationRejected(mutation, pattern)

    def test_qualification_rejects_noncanonical_types_and_encodings(self) -> None:
        mutations_and_patterns = (
            (lambda doc: doc["configuration"].__setitem__("ticks", 5400.0), "JSON integer"),
            (lambda doc: doc["runs"][0].__setitem__("candidate_deaths", 0.0), "JSON integer"),
            (lambda doc: (
                doc["cases"][0].__setitem__("digest_fnv1a64", doc["cases"][0]["digest_fnv1a64"].upper()),
                [row.__setitem__("digest_fnv1a64", row["digest_fnv1a64"].upper()) for row in doc["runs"]
                 if row["case_id"] == doc["cases"][0]["case_id"]],
            ), "canonical lowercase digest"),
            (lambda doc: doc["configuration"].__setitem__("seeds", ["0" + value for value in MODULE.QUALIFICATION_SEEDS]),
             "canonical protocol value"),
        )
        for mutation, pattern in mutations_and_patterns:
            with self.subTest(pattern=pattern):
                self.assertQualificationMutationRejected(mutation, pattern)

    def test_qualification_rejects_negative_or_impossible_exposures(self) -> None:
        mutations_and_patterns = (
            (lambda doc: doc["runs"][0].__setitem__("candidate_damage_dealt_exact", -1), "nonnegative"),
            (lambda doc: doc["runs"][0].__setitem__("hitscan_hits_total", 21), "hitscan hits cannot exceed"),
            (lambda doc: doc["runs"][0].__setitem__("projectile_misses_finalized_total", 7),
             "finalized projectiles cannot exceed"),
            (lambda doc: doc["runs"][0].__setitem__("no_progress_seconds_proxy_total", 181), "bot-seconds"),
        )
        for mutation, pattern in mutations_and_patterns:
            with self.subTest(pattern=pattern):
                self.assertQualificationMutationRejected(mutation, pattern)

    def test_qualification_rejects_reconciliation_false_passes(self) -> None:
        mutations_and_patterns = (
            (lambda doc: doc["runs"][0].__setitem__("damage_dealt_exact_total", 119), "participant damage"),
            (lambda doc: doc["runs"][0].__setitem__("damage_taken_exact_total", 119), "damage taken"),
            (lambda doc: doc["runs"][0].__setitem__("adjudicated_deaths_exact_total", 3), "participant deaths"),
            (lambda doc: doc["runs"][0].__setitem__("fatal_damage_kills_exact_total", 1),
             "fatal deaths do not reconcile"),
            (lambda doc: doc["runs"][0].__setitem__("adjudicated_direct_deaths_exact_total", 1),
             "damage-mediated fatal deaths plus direct deaths"),
            (lambda doc: doc["runs"][0].__setitem__("final_score_total", 4), "participant scores"),
            (lambda doc: doc["runs"][0].__setitem__("candidate_score_margin", 99), "candidate_score_margin"),
            (lambda doc: doc["runs"][0].__setitem__("candidate_death_advantage", 99), "candidate_death_advantage"),
            (lambda doc: doc["runs"][0].__setitem__("hitscan_accuracy", 0.6), "does not reconcile"),
            (lambda doc: doc["runs"][0].__setitem__("projectile_finalized_accuracy", 0.6), "does not reconcile"),
            (lambda doc: doc["runs"][0].update({
                "candidate_score": 4, "final_score_total": 4, "candidate_score_margin": 4,
            }), "bound summary candidate last_score"),
        )
        for mutation, pattern in mutations_and_patterns:
            with self.subTest(pattern=pattern):
                self.assertQualificationMutationRejected(mutation, pattern)

    def test_repetition_equivalence_includes_protocol_and_identity(self) -> None:
        fields = (
            "protocol_id", "game_class", "engine_binary_sha256", "content_manifest_sha256",
            "scenario", "fixture_id", "candidate_profile_id", "opponent_profile_id",
            "damage_taken_exact_total", "candidate_score_margin", "projectile_finalized_accuracy",
        )
        for field in fields:
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                write_matrix(root / "higher.json", True)
                write_matrix(root / "lower.json", False)
                mutate_json(root / "higher.json", lambda doc, field=field: (
                    doc["runs"][0].__setitem__(field, "captured-a"),
                    doc["runs"][1].__setitem__(field, "captured-b"),
                ))
                write_manifest(root / "manifest.json")
                with self.assertRaisesRegex(MODULE.QualificationError, "repetition metrics disagree"):
                    MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_crossover_join_deduplicates_and_statistics_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True)
            write_matrix(root / "lower.json", False)
            write_manifest(root / "manifest.json")

            first = MODULE.analyze_manifest(root / "manifest.json", draws=500, bootstrap_seed=12345)
            second = MODULE.analyze_manifest(root / "manifest.json", draws=500, bootstrap_seed=12345)
            self.assertEqual(first["process_rows"], 64)
            self.assertEqual(first["independent_orientation_trials"], 32)
            self.assertEqual(first["determinism_duplicates_collapsed"], 32)
            self.assertEqual(first["crossover_units"], 16)
            pair = first["pairs"][0]
            self.assertEqual(pair["score"]["wins"], 16)
            self.assertEqual(pair["score"]["losses"], 0)
            self.assertEqual(pair["score"]["cliff_sign_delta"], 1.0)
            self.assertEqual(pair["score"]["paired_rank_biserial"], 1.0)
            self.assertEqual(pair["superiority_cluster_bootstrap"], second["pairs"][0]["superiority_cluster_bootstrap"])
            self.assertEqual(pair["superiority_cluster_bootstrap"]["lower_95"], 1.0)
            self.assertEqual(pair["damage_share_cluster_bootstrap"]["estimate"], 0.75)
            self.assertEqual(pair["seed_cluster_sign_test"]["one_sided_p"], 1 / 256)
            self.assertTrue(pair["holm_reject_at_0_05"])
            self.assertTrue(pair["no_map_material_reversal"])
            self.assertTrue(pair["no_role_material_reversal"])
            self.assertEqual(pair["positive_map_point_effects"], 2)
            self.assertEqual(pair["required_positive_map_point_effects"], 2)
            self.assertTrue(pair["positive_map_requirement_pass"])
            self.assertTrue(pair["statistical_rules_pass"])
            self.assertFalse(pair["qualification_readiness_pass"])
            self.assertFalse(pair["separated_pass"])
            self.assertFalse(first["readiness"]["qualification_claim_allowed"])
            self.assertFalse(first["all_pairs_separated"])
            self.assertEqual(len(first["manifest_sha256"]), 64)
            self.assertEqual(len(first["tool"]["sha256"]), 64)
            self.assertTrue(all(len(item["sha256"]) == 64 for item in first["inputs"]))

            output = root / "output"
            MODULE.write_outputs(output, first)
            self.assertTrue((output / "qualification-analysis.json").is_file())
            self.assertEqual(len((output / "crossover-units.csv").read_text(encoding="utf-8").splitlines()), 17)
            self.assertEqual(len((output / "pairs.csv").read_text(encoding="utf-8").splitlines()), 2)

    def test_missing_role_swap_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True)
            write_manifest(root / "manifest.json", include_lower=False)
            with self.assertRaisesRegex(MODULE.QualificationError, "missing role-swapped orientation"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_nondeterministic_repetitions_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True, corrupt_digest=True)
            write_matrix(root / "lower.json", False)
            write_manifest(root / "manifest.json")
            with self.assertRaisesRegex(MODULE.QualificationError, "repetition digests disagree"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_orientation_metadata_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", False)
            write_matrix(root / "lower.json", False)
            write_manifest(root / "manifest.json")
            with self.assertRaisesRegex(MODULE.QualificationError, "do not match explicit"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_holm_adjustment_controls_the_family(self) -> None:
        results = [
            {"seed_cluster_sign_test": {"one_sided_p": 0.01}},
            {"seed_cluster_sign_test": {"one_sided_p": 0.04}},
            {"seed_cluster_sign_test": {"one_sided_p": 0.20}},
        ]
        MODULE.apply_holm(results)
        self.assertAlmostEqual(results[0]["holm_adjusted_one_sided_p"], 0.03)
        self.assertAlmostEqual(results[1]["holm_adjusted_one_sided_p"], 0.08)
        self.assertAlmostEqual(results[2]["holm_adjusted_one_sided_p"], 0.20)
        self.assertTrue(results[0]["holm_reject_at_0_05"])
        self.assertFalse(results[1]["holm_reject_at_0_05"])

    def test_empty_matrix_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "higher.json").write_text(json.dumps({"schema": 1, "passed": True, "runs": []}), encoding="utf-8")
            write_matrix(root / "lower.json", False)
            write_manifest(root / "manifest.json")
            with self.assertRaisesRegex(MODULE.QualificationError, "zero usable trials"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_qualification_subset_family_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True)
            write_matrix(root / "lower.json", False)
            write_manifest(root / "manifest.json", mode="qualification")
            with self.assertRaisesRegex(MODULE.QualificationError, "full seven adjacent-pair Holm family"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_duration_configuration_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True)
            write_matrix(root / "lower.json", False, configuration_ticks=6000)
            write_manifest(root / "manifest.json")
            with self.assertRaisesRegex(MODULE.QualificationError, "row/config requested tick mismatch"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_damage_gate_uses_median_not_mean(self) -> None:
        units = [
            {"seed": "1", "damage_share": 0.10},
            {"seed": "2", "damage_share": 0.60},
            {"seed": "3", "damage_share": 0.60},
        ]
        result = MODULE.bootstrap_clustered(
            units, 500, 99,
            lambda sample: __import__("statistics").median(
                [float(unit["damage_share"]) for unit in sample]
            ),
        )
        self.assertAlmostEqual(sum(unit["damage_share"] for unit in units) / 3, 0.43333333333333335)
        self.assertEqual(result["estimate"], 0.60)
        stratum = MODULE.stratum_summary([
            {"score_margin": 1, "damage_share": unit["damage_share"]} for unit in units
        ])
        self.assertFalse(stratum["material_reversal"])

    def test_two_of_three_positive_maps_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            maps = ("DM-Positive", "DM-ReverseA", "DM-ReverseB")
            reverse = ("DM-ReverseA", "DM-ReverseB")
            write_matrix(root / "higher.json", True, maps=maps, reverse_maps=reverse)
            write_matrix(root / "lower.json", False, maps=maps, reverse_maps=reverse)
            write_manifest(root / "manifest.json")
            report = MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)
            pair = report["pairs"][0]
            self.assertEqual(pair["positive_map_point_effects"], 1)
            self.assertEqual(pair["required_positive_map_point_effects"], 2)
            self.assertFalse(pair["positive_map_requirement_pass"])
            self.assertFalse(pair["statistical_rules_pass"])

    def test_minimum_design_and_profile_identity_are_blocking(self) -> None:
        pair_results = [{"higher_tier": high, "lower_tier": low} for high, low in sorted(MODULE.ADJACENT_FAMILY)]
        underpowered = []
        for high, low in MODULE.ADJACENT_FAMILY:
            for seed in range(23):
                for map_name in ("A", "B"):
                    underpowered.append({
                        "higher_tier": high, "lower_tier": low, "seed": str(seed), "map": map_name,
                        "stable_profile_identity": True,
                    })
        readiness = MODULE.qualification_readiness("qualification", underpowered, pair_results)
        self.assertFalse(readiness["qualification_claim_allowed"])
        self.assertTrue(any("requires >=24 unique seeds" in blocker for blocker in readiness["blockers"]))
        self.assertTrue(any("requires >=3 maps" in blocker for blocker in readiness["blockers"]))

        complete_but_profileless = []
        for high, low in MODULE.ADJACENT_FAMILY:
            for seed in range(24):
                for map_name in ("A", "B", "C"):
                    complete_but_profileless.append({
                        "higher_tier": high, "lower_tier": low, "seed": str(seed), "map": map_name,
                        "stable_profile_identity": False,
                    })
        profile_readiness = MODULE.qualification_readiness("qualification", complete_but_profileless, pair_results)
        self.assertFalse(profile_readiness["qualification_claim_allowed"])
        self.assertTrue(profile_readiness["stable_profile_identity_is_blocking"])
        self.assertTrue(any("profile identity" in blocker for blocker in profile_readiness["blockers"]))

    def test_provenance_hashes_and_identity_are_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True)
            write_matrix(root / "lower.json", False)
            write_manifest(root / "manifest.json")
            report = MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=55)
            self.assertEqual(report["provenance"]["observed"]["requested_ticks"], 5400)
            self.assertEqual(report["provenance"]["observed"]["build_identity"], "build_commit:0123456789abcdef")
            self.assertTrue(report["provenance"]["equality_validated"])
            self.assertEqual(report["tool"]["version"], 3)
            self.assertEqual(report["command_parameters"]["bootstrap_seed"], 55)

    def test_partial_profile_provenance_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True)
            write_matrix(root / "lower.json", False)
            lower = json.loads((root / "lower.json").read_text(encoding="utf-8"))
            for row in lower["runs"]:
                row["candidate_profile_id"] = None
            (root / "lower.json").write_text(json.dumps(lower), encoding="utf-8")
            write_manifest(root / "manifest.json")
            with self.assertRaisesRegex(MODULE.QualificationError, "profile provenance is only partially exposed"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)

    def test_engine_identity_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_matrix(root / "higher.json", True)
            write_matrix(root / "lower.json", False)
            lower = json.loads((root / "lower.json").read_text(encoding="utf-8"))
            lower["engine_path"] = "C:/synthetic/DifferentEngine.exe"
            (root / "lower.json").write_text(json.dumps(lower), encoding="utf-8")
            write_manifest(root / "manifest.json")
            with self.assertRaisesRegex(MODULE.QualificationError, "provenance mismatch for 'engine_path'"):
                MODULE.analyze_manifest(root / "manifest.json", draws=100, bootstrap_seed=1)


if __name__ == "__main__":
    unittest.main()
