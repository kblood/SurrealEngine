#!/usr/bin/env python3
"""Validate one complete, privacy-bounded WebXR physical qualification run.

Exit 0 means a complete passing run, 2 means structurally complete failure
evidence, and 1 means invalid or incomplete evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any

from new_physical_run import MIME_BY_SUFFIX, SCHEMA, frozen_candidate, load_candidate


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
PRIVATE_PATH_RE = re.compile(
    r"(?:\b[A-Za-z]:[\\/]|(?:^|\s)\\\\[^\\\s]+\\|file://|/(?:home|users|storage/emulated|sdcard)/)",
    re.IGNORECASE,
)
PRIVATE_TOKEN_RE = re.compile(r"%(?:appdata|userprofile)%|\\appdata\\|/appdata/", re.IGNORECASE)
GAME_FILE_RE = re.compile(
    r"\b[^\s/\\]+\.(?:u|unr|utx|uax|umx|usa|sav|save|pak|pk3|7z|rar|exe|dll)\b",
    re.IGNORECASE,
)
PLACEHOLDER_RE = re.compile(r"(?:fill[-_ ]?me|not[-_ ]?run|<[^>]+>|\btbd\b)", re.IGNORECASE)
FAILURE_CATEGORIES = {
    "denied-entry", "headset-sleep-obscured",
    "controller-disconnect-reconnect", "forced-session-end",
}
ROOT_KEYS = {
    "schema", "schemaVersion", "runId", "generatedAtUtc", "matrixRow", "gameCase",
    "candidate", "test", "result", "resourcePolicy", "cycles", "failureScenarios",
    "officialSample", "attachments", "notes",
}
TEST_KEYS = {
    "startedAtUtc", "completedAtUtc", "tester", "clientVersion", "questModel",
    "questOs", "questBrowser", "virtualDesktop", "openxrRuntime", "gpuDriver",
    "refreshRateHz", "artifactKind", "artifactName", "artifactSha256",
    "presentationMode", "officialSampleUrl",
}


class Validation:
    def __init__(self, record_path: Path, expected_candidate: dict):
        self.record_path = record_path.resolve()
        self.root = self.record_path.parent
        self.expected_candidate = expected_candidate
        self.errors: list[str] = []

    def error(self, location: str, message: str) -> None:
        self.errors.append(f"{location}: {message}")

    def mapping(self, value: Any, location: str) -> dict:
        if not isinstance(value, dict):
            self.error(location, "must be an object")
            return {}
        return value

    def required(self, obj: dict, names: set[str] | tuple[str, ...], location: str) -> None:
        for name in sorted(names):
            if name not in obj:
                self.error(f"{location}.{name}", "is required")

    def exact_keys(self, obj: dict, allowed: set[str], location: str) -> None:
        for name in sorted(set(obj) - allowed):
            self.error(f"{location}.{name}", "unknown field")

    def timestamp(self, value: Any, location: str, allow_none: bool = False) -> datetime | None:
        if value is None and allow_none:
            return None
        if not isinstance(value, str):
            self.error(location, "must be an ISO-8601 UTC timestamp")
            return None
        try:
            parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        except ValueError:
            self.error(location, "must be an ISO-8601 timestamp")
            return None
        if parsed.tzinfo is None or parsed.utcoffset().total_seconds() != 0:
            self.error(location, "must include the UTC offset")
            return None
        return parsed

    def nonnegative(self, value: Any, location: str, integer: bool = False,
                    allow_none: bool = False) -> float | int | None:
        if value is None and allow_none:
            return None
        expected = int if integer else (int, float)
        if not isinstance(value, expected) or isinstance(value, bool) or value < 0:
            self.error(location, f"must be a nonnegative {'integer' if integer else 'number'}")
            return None
        return value

    def boolean(self, value: Any, location: str) -> bool | None:
        if not isinstance(value, bool):
            self.error(location, "must be boolean")
            return None
        return value

    def validate(self, data: Any) -> list[str]:
        root = self.mapping(data, "$")
        self.required(root, {
            "schema", "schemaVersion", "runId", "generatedAtUtc", "matrixRow",
            "gameCase", "candidate", "test", "result", "attachments",
        }, "$")
        self.exact_keys(root, ROOT_KEYS, "$")
        if root.get("schema") != SCHEMA:
            self.error("$.schema", f"must equal {SCHEMA}")
        if root.get("schemaVersion") != 1:
            self.error("$.schemaVersion", "must equal 1")
        run_id = root.get("runId")
        if not isinstance(run_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]{7,95}", run_id):
            self.error("$.runId", "must be a safe 8-96 character identifier")
        self.timestamp(root.get("generatedAtUtc"), "$.generatedAtUtc")

        row = root.get("matrixRow")
        game = root.get("gameCase")
        if row not in {"Q1", "D1", "E1", "E2"}:
            self.error("$.matrixRow", "must be Q1, D1, E1, or E2")
        if row == "D1" and game != "official-sample":
            self.error("$.gameCase", "D1 must use official-sample")
        if row in {"Q1", "E1", "E2"} and game not in {"ut99", "unreal-gold"}:
            self.error("$.gameCase", f"{row} must use ut99 or unreal-gold")

        self.validate_candidate(root.get("candidate"))
        if root.get("candidate") != self.expected_candidate:
            self.error("$.candidate", "does not exactly match the explicitly expected candidate")
        self.validate_test(root.get("test"), row)
        status = self.validate_result(root.get("result"), "$.result")
        attachment_roles = self.validate_attachments(root.get("attachments"), row)

        subordinate_failures = 0
        if row == "D1":
            if "cycles" in root or "failureScenarios" in root or "resourcePolicy" in root:
                self.error("$", "D1 must use officialSample rather than game cycles or failure scenarios")
            subordinate_failures += self.validate_official_sample(root.get("officialSample"))
            required_roles = {"sample-diagnostics"}
        else:
            policy = self.validate_resource_policy(root.get("resourcePolicy"))
            subordinate_failures += self.validate_cycles(root.get("cycles"), policy)
            subordinate_failures += self.validate_failure_scenarios(root.get("failureScenarios"))
            if "officialSample" in root:
                self.error("$.officialSample", "is only valid for D1")
            required_roles = {"before-diagnostics", "running-diagnostics", "after-diagnostics"}
            if row in {"E1", "E2"}:
                required_roles.add("electron-diagnostics")

        for role in sorted(required_roles - attachment_roles):
            self.error("$.attachments", f"missing required {role} attachment")
        if not attachment_roles.intersection({"screenshot", "video"}):
            self.error("$.attachments", "at least one sanitized screenshot or video is required")

        if status == "pass" and subordinate_failures:
            self.error("$.result.status", "cannot pass while a cycle, failure scenario, or sample check failed")
        if status == "fail" and not subordinate_failures:
            self.error("$.result.status", "must identify at least one failed subordinate check")
        self.reject_placeholders(root)
        return self.errors

    def validate_candidate(self, value: Any) -> None:
        candidate = self.mapping(value, "$.candidate")
        self.required(candidate, {"qualificationCard", "browser", "electron"}, "$.candidate")
        self.exact_keys(candidate, {"qualificationCard", "browser", "electron"}, "$.candidate")
        card = candidate.get("qualificationCard")
        if not isinstance(card, str) or not re.fullmatch(r"Docs/[^/].*\.md", card):
            self.error("$.candidate.qualificationCard", "must be a repository-relative Docs Markdown card")
        browser = self.mapping(candidate.get("browser"), "$.candidate.browser")
        browser_names = {"buildId", "sourceCommit", "sourceTree", "manifestSha256", "javascriptSha256", "wasmSha256", "correspondingSourceSha256", "immutableGenerationUrl"}
        self.required(browser, browser_names, "$.candidate.browser")
        self.exact_keys(browser, browser_names, "$.candidate.browser")
        patterns = {
            "buildId": r"[0-9a-f]{12}-[0-9a-f]{16}",
            "sourceCommit": r"[0-9a-f]{40}",
            "sourceTree": r"[0-9a-f]{40}",
            "manifestSha256": r"[0-9a-f]{64}",
            "javascriptSha256": r"[0-9a-f]{64}",
            "wasmSha256": r"[0-9a-f]{64}",
            "correspondingSourceSha256": r"[0-9a-f]{64}",
        }
        for name, pattern in patterns.items():
            if not isinstance(browser.get(name), str) or not re.fullmatch(pattern, browser[name]):
                self.error(f"$.candidate.browser.{name}", "has an invalid immutable identity")
        generation = browser.get("immutableGenerationUrl")
        if not isinstance(generation, str) or not re.fullmatch(r"https://.*/releases/[0-9a-f]{64}/", generation):
            self.error("$.candidate.browser.immutableGenerationUrl", "must be the immutable full-manifest hash URL")
        elif browser.get("manifestSha256") not in generation:
            self.error("$.candidate.browser.immutableGenerationUrl", "must contain manifestSha256")
        electron = self.mapping(candidate.get("electron"), "$.candidate.electron")
        electron_names = {"electron", "chrome", "zipName", "zipSha256", "status"}
        self.required(electron, electron_names, "$.candidate.electron")
        self.exact_keys(electron, electron_names, "$.candidate.electron")
        if not isinstance(electron.get("electron"), str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", electron["electron"]):
            self.error("$.candidate.electron.electron", "must be an exact semantic version")
        if not isinstance(electron.get("chrome"), str) or not re.fullmatch(r"[0-9]+(?:\.[0-9]+){3}", electron["chrome"]):
            self.error("$.candidate.electron.chrome", "must be an exact four-part version")
        if not isinstance(electron.get("zipName"), str) or "/" in electron["zipName"] or "\\" in electron["zipName"] or not electron["zipName"].endswith(".zip"):
            self.error("$.candidate.electron.zipName", "must be a ZIP filename without a path")
        if not isinstance(electron.get("zipSha256"), str) or not SHA256_RE.fullmatch(electron["zipSha256"]):
            self.error("$.candidate.electron.zipSha256", "must be lowercase SHA-256")
        if electron.get("status") not in {"unsigned-internal-diagnostic", "signed-internal-diagnostic"}:
            self.error("$.candidate.electron.status", "must identify the diagnostic signing status")

    def validate_test(self, value: Any, row: Any) -> None:
        test = self.mapping(value, "$.test")
        self.required(test, TEST_KEYS - {"officialSampleUrl"}, "$.test")
        self.exact_keys(test, TEST_KEYS, "$.test")
        started = self.timestamp(test.get("startedAtUtc"), "$.test.startedAtUtc")
        completed = self.timestamp(test.get("completedAtUtc"), "$.test.completedAtUtc")
        if started and completed and completed < started:
            self.error("$.test.completedAtUtc", "must not precede startedAtUtc")
        for name in ("tester", "clientVersion", "questOs", "questBrowser", "virtualDesktop", "openxrRuntime", "gpuDriver", "artifactName"):
            value = test.get(name)
            if not isinstance(value, str) or not value.strip():
                self.error(f"$.test.{name}", "must be a nonempty exact value")
        if test.get("questModel") != "Quest 3":
            self.error("$.test.questModel", "must equal Quest 3")
        refresh = test.get("refreshRateHz")
        if not isinstance(refresh, (int, float)) or isinstance(refresh, bool) or not 60 <= refresh <= 240:
            self.error("$.test.refreshRateHz", "must be between 60 and 240")
        sha = test.get("artifactSha256")
        if not isinstance(sha, str) or not SHA256_RE.fullmatch(sha):
            self.error("$.test.artifactSha256", "must be lowercase SHA-256")

        browser_sha = self.expected_candidate.get("browser", {}).get("manifestSha256")
        electron = self.expected_candidate.get("electron", {})
        expected = {
            "Q1": ("hosted-immutable-generation", "release-manifest.json", browser_sha, "direct-webgl2"),
            "E1": ("electron-automatic", electron["zipName"], electron["zipSha256"], "electron-automatic"),
            "E2": ("electron-force-openxr", electron["zipName"], electron["zipSha256"], "electron-force-openxr"),
        }
        if row in expected:
            actual = tuple(test.get(name) for name in ("artifactKind", "artifactName", "artifactSha256", "presentationMode"))
            if actual != expected[row]:
                self.error("$.test", f"artifact fields do not match frozen {row} candidate")
            if "officialSampleUrl" in test:
                self.error("$.test.officialSampleUrl", "is only valid for D1")
        elif row == "D1":
            if test.get("artifactKind") != "official-immersive-web-sample" or test.get("presentationMode") != "official-sample":
                self.error("$.test", "D1 must identify the official immersive Web sample")
            url = test.get("officialSampleUrl")
            if not isinstance(url, str) or not url.startswith("https://"):
                self.error("$.test.officialSampleUrl", "must pin an HTTPS official-sample URL")

    def validate_result(self, value: Any, location: str) -> str | None:
        result = self.mapping(value, location)
        self.required(result, {"status", "category", "summary"}, location)
        self.exact_keys(result, {"status", "category", "summary"}, location)
        status = result.get("status")
        if status not in {"pass", "fail"}:
            self.error(f"{location}.status", "must be pass or fail; not-run is never valid evidence")
            return None
        category = result.get("category")
        if not isinstance(category, str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", category):
            self.error(f"{location}.category", "must be a short lowercase category")
        summary = result.get("summary")
        if not isinstance(summary, str) or not summary.strip() or len(summary) > 1000:
            self.error(f"{location}.summary", "must be a nonempty summary of at most 1000 characters")
        return status

    def validate_resource_policy(self, value: Any) -> dict[str, int]:
        policy = self.mapping(value, "$.resourcePolicy")
        names = {"maxHeapGrowthBytes", "maxTextureGrowth", "maxFrameDeltaUs"}
        self.required(policy, names, "$.resourcePolicy")
        self.exact_keys(policy, names, "$.resourcePolicy")
        for name in names:
            measured = self.nonnegative(policy.get(name), f"$.resourcePolicy.{name}", integer=True)
            if name == "maxFrameDeltaUs" and measured == 0:
                self.error(f"$.resourcePolicy.{name}", "must be greater than zero")
        return policy

    def validate_cycles(self, value: Any, policy: dict[str, int]) -> int:
        if not isinstance(value, list):
            self.error("$.cycles", "must contain exactly ten cycle objects")
            return 0
        if len(value) != 10:
            self.error("$.cycles", "must contain exactly ten cycles")
        indexes = [cycle.get("index") for cycle in value if isinstance(cycle, dict)]
        if sorted(indexes) != list(range(1, 11)):
            self.error("$.cycles", "indexes must be unique values 1 through 10")
        routes: set[str] = set()
        failed = 0
        valid_cycles: list[dict] = []
        for offset, raw in enumerate(value):
            location = f"$.cycles[{offset}]"
            cycle = self.mapping(raw, location)
            valid_cycles.append(cycle)
            result = cycle.get("result")
            if result not in {"pass", "fail"}:
                self.error(f"{location}.result", "must be pass or fail")
            elif result == "fail":
                failed += 1
            self.validate_cycle(cycle, location, policy, result == "pass")
            exit_data = cycle.get("exit")
            if isinstance(exit_data, dict) and isinstance(exit_data.get("route"), str):
                routes.add(exit_data["route"])
        for route in {"game-ui", "headset-system"} - routes:
            self.error("$.cycles", f"must include at least one {route} exit")
        if len(valid_cycles) == 10 and all(isinstance(c, dict) for c in valid_cycles):
            first_resources = valid_cycles[0].get("resources", {})
            last_resources = valid_cycles[-1].get("resources", {})
            heap_before, heap_after = first_resources.get("heapBytesBefore"), last_resources.get("heapBytesAfter")
            textures_before, textures_after = first_resources.get("texturesBefore"), last_resources.get("texturesAfter")
            if all(isinstance(v, int) and not isinstance(v, bool) for v in (heap_before, heap_after)) and isinstance(policy.get("maxHeapGrowthBytes"), int):
                if heap_after - heap_before > policy["maxHeapGrowthBytes"]:
                    self.error("$.cycles", "overall WASM heap growth exceeds resourcePolicy.maxHeapGrowthBytes")
            if all(isinstance(v, int) and not isinstance(v, bool) for v in (textures_before, textures_after)) and isinstance(policy.get("maxTextureGrowth"), int):
                if textures_after - textures_before > policy["maxTextureGrowth"]:
                    self.error("$.cycles", "overall texture growth exceeds resourcePolicy.maxTextureGrowth")
        return failed

    def validate_cycle(self, cycle: dict, location: str, policy: dict[str, int], passing: bool) -> None:
        required = {"index", "result", "entry", "exit", "metrics", "resources", "continuity", "observations"}
        self.required(cycle, required, location)
        self.exact_keys(cycle, required | {"notes"}, location)

        entry = self.mapping(cycle.get("entry"), f"{location}.entry")
        entry_names = {"requestedAtUtc", "activatedAtUtc", "firstFrameAtUtc", "firstFrameSeen", "viewCount", "positiveViewports"}
        self.required(entry, entry_names, f"{location}.entry")
        self.exact_keys(entry, entry_names, f"{location}.entry")
        requested = self.timestamp(entry.get("requestedAtUtc"), f"{location}.entry.requestedAtUtc")
        activated = self.timestamp(entry.get("activatedAtUtc"), f"{location}.entry.activatedAtUtc", allow_none=not passing)
        first_frame = self.timestamp(entry.get("firstFrameAtUtc"), f"{location}.entry.firstFrameAtUtc", allow_none=not passing)
        first_seen = self.boolean(entry.get("firstFrameSeen"), f"{location}.entry.firstFrameSeen")
        view_count = self.nonnegative(entry.get("viewCount"), f"{location}.entry.viewCount", integer=True)
        positive_viewports = self.boolean(entry.get("positiveViewports"), f"{location}.entry.positiveViewports")
        if requested and activated and activated < requested:
            self.error(f"{location}.entry.activatedAtUtc", "must not precede requestedAtUtc")
        if activated and first_frame and first_frame < activated:
            self.error(f"{location}.entry.firstFrameAtUtc", "must not precede activatedAtUtc")

        exit_data = self.mapping(cycle.get("exit"), f"{location}.exit")
        exit_names = {"route", "requestedAtUtc", "flatRestoredAtUtc", "flatRestored"}
        self.required(exit_data, exit_names, f"{location}.exit")
        self.exact_keys(exit_data, exit_names, f"{location}.exit")
        if exit_data.get("route") not in {"game-ui", "headset-system"}:
            self.error(f"{location}.exit.route", "must be game-ui or headset-system")
        exit_requested = self.timestamp(exit_data.get("requestedAtUtc"), f"{location}.exit.requestedAtUtc")
        flat_at = self.timestamp(exit_data.get("flatRestoredAtUtc"), f"{location}.exit.flatRestoredAtUtc", allow_none=not passing)
        flat_restored = self.boolean(exit_data.get("flatRestored"), f"{location}.exit.flatRestored")
        if first_frame and exit_requested and exit_requested < first_frame:
            self.error(f"{location}.exit.requestedAtUtc", "must not precede firstFrameAtUtc")
        if exit_requested and flat_at and flat_at < exit_requested:
            self.error(f"{location}.exit.flatRestoredAtUtc", "must not precede exit request")

        metrics = self.mapping(cycle.get("metrics"), f"{location}.metrics")
        metric_names = {"entryLatencyMs", "firstFrameLatencyMs", "exitLatencyMs", "xrFrames", "skippedFrames", "frameTimeMs", "visibilityTransitions", "maxFrameDeltaUs"}
        self.required(metrics, metric_names, f"{location}.metrics")
        self.exact_keys(metrics, metric_names, f"{location}.metrics")
        for name in ("entryLatencyMs", "firstFrameLatencyMs", "exitLatencyMs"):
            self.nonnegative(metrics.get(name), f"{location}.metrics.{name}", allow_none=not passing)
        xr_frames = self.nonnegative(metrics.get("xrFrames"), f"{location}.metrics.xrFrames", integer=True)
        self.nonnegative(metrics.get("skippedFrames"), f"{location}.metrics.skippedFrames", integer=True)
        max_delta = self.nonnegative(metrics.get("maxFrameDeltaUs"), f"{location}.metrics.maxFrameDeltaUs", integer=True)
        frame_time = self.mapping(metrics.get("frameTimeMs"), f"{location}.metrics.frameTimeMs")
        frame_names = {"samples", "p50", "p95", "p99"}
        self.required(frame_time, frame_names, f"{location}.metrics.frameTimeMs")
        self.exact_keys(frame_time, frame_names, f"{location}.metrics.frameTimeMs")
        samples = self.nonnegative(frame_time.get("samples"), f"{location}.metrics.frameTimeMs.samples", integer=True)
        percentiles = [self.nonnegative(frame_time.get(name), f"{location}.metrics.frameTimeMs.{name}", allow_none=not passing) for name in ("p50", "p95", "p99")]
        transitions = metrics.get("visibilityTransitions")
        if not isinstance(transitions, list) or any(item not in {"visible", "visible-blurred", "hidden"} for item in transitions):
            self.error(f"{location}.metrics.visibilityTransitions", "must contain only WebXR visibility states")

        resources = self.mapping(cycle.get("resources"), f"{location}.resources")
        resource_names = {"heapBytesBefore", "heapBytesAfter", "texturesBefore", "texturesAfter", "webglGenerationBefore", "webglGenerationAfter"}
        self.required(resources, resource_names, f"{location}.resources")
        self.exact_keys(resources, resource_names, f"{location}.resources")
        for name in resource_names:
            self.nonnegative(resources.get(name), f"{location}.resources.{name}", integer=True)

        continuity = self.mapping(cycle.get("continuity"), f"{location}.continuity")
        continuity_names = {"engineStable", "levelStable", "playerStable", "rendererStable", "audioStable", "tickBefore", "tickAtFirstFrame", "tickAfter"}
        self.required(continuity, continuity_names, f"{location}.continuity")
        self.exact_keys(continuity, continuity_names, f"{location}.continuity")
        for name in ("engineStable", "levelStable", "playerStable", "rendererStable", "audioStable"):
            self.boolean(continuity.get(name), f"{location}.continuity.{name}")
        ticks = [self.nonnegative(continuity.get(name), f"{location}.continuity.{name}", integer=True) for name in ("tickBefore", "tickAtFirstFrame", "tickAfter")]

        observations = self.mapping(cycle.get("observations"), f"{location}.observations")
        observation_names = {"sameMap", "samePosition", "sameHealth", "sameWeapon", "sameAudio", "duplicateSimulation", "largeTimeStep", "blackOrStaleEye", "stuckInputOrHaptics", "pointerLockRecovered"}
        self.required(observations, observation_names, f"{location}.observations")
        self.exact_keys(observations, observation_names, f"{location}.observations")
        for name in observation_names:
            self.boolean(observations.get(name), f"{location}.observations.{name}")

        if passing:
            for name, value in (("firstFrameSeen", first_seen), ("positiveViewports", positive_viewports)):
                if value is not True:
                    self.error(f"{location}.entry.{name}", "must be true for a passing cycle")
            if view_count != 2:
                self.error(f"{location}.entry.viewCount", "must equal 2 for a passing immersive-vr cycle")
            if flat_restored is not True:
                self.error(f"{location}.exit.flatRestored", "must be true for a passing cycle")
            if not isinstance(xr_frames, int) or xr_frames <= 0:
                self.error(f"{location}.metrics.xrFrames", "must be positive for a passing cycle")
            if not isinstance(samples, int) or samples <= 0:
                self.error(f"{location}.metrics.frameTimeMs.samples", "must be positive for a passing cycle")
            if all(value is not None for value in percentiles) and not (percentiles[0] <= percentiles[1] <= percentiles[2]):
                self.error(f"{location}.metrics.frameTimeMs", "must satisfy p50 <= p95 <= p99")
            if isinstance(max_delta, int) and isinstance(policy.get("maxFrameDeltaUs"), int) and max_delta > policy["maxFrameDeltaUs"]:
                self.error(f"{location}.metrics.maxFrameDeltaUs", "exceeds the predeclared resource policy")
            for name in ("engineStable", "levelStable", "playerStable", "rendererStable", "audioStable"):
                if continuity.get(name) is not True:
                    self.error(f"{location}.continuity.{name}", "must be true for a passing cycle")
            if all(tick is not None for tick in ticks) and not (ticks[0] <= ticks[1] <= ticks[2] and ticks[2] > ticks[0]):
                self.error(f"{location}.continuity", "ticks must be monotonic and advance across the cycle")
            for name in ("sameMap", "samePosition", "sameHealth", "sameWeapon", "sameAudio", "pointerLockRecovered"):
                if observations.get(name) is not True:
                    self.error(f"{location}.observations.{name}", "must be true for a passing cycle")
            for name in ("duplicateSimulation", "largeTimeStep", "blackOrStaleEye", "stuckInputOrHaptics"):
                if observations.get(name) is not False:
                    self.error(f"{location}.observations.{name}", "must be false for a passing cycle")
            if resources.get("webglGenerationBefore") != resources.get("webglGenerationAfter"):
                self.error(f"{location}.resources", "WebGL context generation changed during a passing cycle")

    def validate_failure_scenarios(self, value: Any) -> int:
        if not isinstance(value, list):
            self.error("$.failureScenarios", "must contain the four required scenarios")
            return 0
        if len(value) != 4:
            self.error("$.failureScenarios", "must contain exactly four scenarios")
        categories = [item.get("category") for item in value if isinstance(item, dict)]
        if set(categories) != FAILURE_CATEGORIES or len(categories) != len(set(categories)):
            self.error("$.failureScenarios", "must contain each required category exactly once")
        failed = 0
        names = {"category", "result", "flatRecovered", "laterEntrySucceeded", "notes"}
        for index, raw in enumerate(value):
            location = f"$.failureScenarios[{index}]"
            scenario = self.mapping(raw, location)
            self.required(scenario, names, location)
            self.exact_keys(scenario, names, location)
            status = scenario.get("result")
            if status not in {"pass", "fail"}:
                self.error(f"{location}.result", "must be pass or fail")
            elif status == "fail":
                failed += 1
            self.boolean(scenario.get("flatRecovered"), f"{location}.flatRecovered")
            self.boolean(scenario.get("laterEntrySucceeded"), f"{location}.laterEntrySucceeded")
            if status == "pass":
                if scenario.get("flatRecovered") is not True:
                    self.error(f"{location}.flatRecovered", "must be true for a passing recovery scenario")
                if scenario.get("laterEntrySucceeded") is not True:
                    self.error(f"{location}.laterEntrySucceeded", "must be true for a passing recovery scenario")
        return failed

    def validate_official_sample(self, value: Any) -> int:
        location = "$.officialSample"
        sample = self.mapping(value, location)
        names = {"result", "capabilityDetected", "sessionGranted", "firstFrameSeen", "viewCount", "positiveViewports", "controllersObserved", "exitSucceeded", "notes"}
        self.required(sample, names - {"notes"}, location)
        self.exact_keys(sample, names, location)
        status = sample.get("result")
        if status not in {"pass", "fail"}:
            self.error(f"{location}.result", "must be pass or fail")
            return 0
        for name in ("capabilityDetected", "sessionGranted", "firstFrameSeen", "positiveViewports", "controllersObserved", "exitSucceeded"):
            self.boolean(sample.get(name), f"{location}.{name}")
        self.nonnegative(sample.get("viewCount"), f"{location}.viewCount", integer=True)
        if status == "pass":
            for name in ("capabilityDetected", "sessionGranted", "firstFrameSeen", "positiveViewports", "controllersObserved", "exitSucceeded"):
                if sample.get(name) is not True:
                    self.error(f"{location}.{name}", "must be true for a passing official sample")
            if sample.get("viewCount") != 2:
                self.error(f"{location}.viewCount", "must equal 2 for a passing immersive-vr sample")
        return int(status == "fail")

    def validate_attachments(self, value: Any, row: Any) -> set[str]:
        if not isinstance(value, list) or not value:
            self.error("$.attachments", "must be a nonempty attachment inventory")
            return set()
        roles: set[str] = set()
        ids: set[str] = set()
        paths: set[str] = set()
        names = {"id", "role", "path", "mimeType", "bytes", "sha256", "capturedAtUtc", "cycle", "description"}
        for index, raw in enumerate(value):
            location = f"$.attachments[{index}]"
            item = self.mapping(raw, location)
            self.required(item, {"id", "role", "path", "mimeType", "bytes", "sha256", "capturedAtUtc"}, location)
            self.exact_keys(item, names, location)
            attachment_id = item.get("id")
            if not isinstance(attachment_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]{2,63}", attachment_id):
                self.error(f"{location}.id", "must be a safe identifier")
            elif attachment_id in ids:
                self.error(f"{location}.id", "must be unique")
            else:
                ids.add(attachment_id)
            role = item.get("role")
            if role not in {"before-diagnostics", "running-diagnostics", "after-diagnostics", "electron-diagnostics", "sample-diagnostics", "screenshot", "video", "other"}:
                self.error(f"{location}.role", "is not an allowed evidence role")
            else:
                roles.add(role)
            relative = self.safe_attachment_path(item.get("path"), f"{location}.path")
            if relative and relative.as_posix() in paths:
                self.error(f"{location}.path", "must be unique")
            elif relative:
                paths.add(relative.as_posix())
            self.timestamp(item.get("capturedAtUtc"), f"{location}.capturedAtUtc")
            expected_bytes = self.nonnegative(item.get("bytes"), f"{location}.bytes", integer=True)
            if expected_bytes == 0:
                self.error(f"{location}.bytes", "must be positive")
            expected_sha = item.get("sha256")
            if not isinstance(expected_sha, str) or not SHA256_RE.fullmatch(expected_sha):
                self.error(f"{location}.sha256", "must be lowercase SHA-256")
            if "cycle" in item and (not isinstance(item["cycle"], int) or isinstance(item["cycle"], bool) or not 1 <= item["cycle"] <= 10):
                self.error(f"{location}.cycle", "must be between 1 and 10")
            if relative:
                self.validate_attachment_file(item, relative, expected_bytes, expected_sha, location)
        return roles

    def safe_attachment_path(self, value: Any, location: str) -> PurePosixPath | None:
        if not isinstance(value, str) or not value:
            self.error(location, "must be a nonempty relative path")
            return None
        if "\\" in value:
            self.error(location, "must use forward slashes")
            return None
        relative = PurePosixPath(value)
        if relative.is_absolute() or ".." in relative.parts or not relative.parts or re.match(r"^[A-Za-z]:", value):
            self.error(location, "must stay below the run directory")
            return None
        if relative.suffix.lower() not in MIME_BY_SUFFIX:
            self.error(location, "has an unsupported or unsafe attachment extension")
            return None
        return relative

    def validate_attachment_file(self, item: dict, relative: PurePosixPath,
                                 expected_bytes: Any, expected_sha: Any, location: str) -> None:
        absolute = (self.root / Path(*relative.parts)).resolve()
        if absolute != self.root and self.root not in absolute.parents:
            self.error(f"{location}.path", "resolves outside the run directory")
            return
        if not absolute.is_file():
            self.error(f"{location}.path", f"file is missing: {relative.as_posix()}")
            return
        if absolute.is_symlink():
            self.error(f"{location}.path", "symbolic-link attachments are not allowed")
            return
        actual_bytes = absolute.stat().st_size
        if actual_bytes != expected_bytes:
            self.error(f"{location}.bytes", f"expected {expected_bytes}, found {actual_bytes}")
        digest = hashlib.sha256()
        with absolute.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != expected_sha:
            self.error(f"{location}.sha256", "does not match the attachment")
        expected_mime = MIME_BY_SUFFIX[relative.suffix.lower()]
        if item.get("mimeType") != expected_mime:
            self.error(f"{location}.mimeType", f"must equal {expected_mime}")
        if expected_mime in {"application/json", "text/plain", "text/markdown"}:
            if actual_bytes > 5 * 1024 * 1024:
                self.error(f"{location}.path", "text diagnostics are bounded to 5 MiB")
                return
            try:
                text = absolute.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                self.error(f"{location}.path", "text evidence must be UTF-8")
                return
            if PRIVATE_PATH_RE.search(text) or PRIVATE_TOKEN_RE.search(text):
                self.error(f"{location}.path", "text evidence contains a private absolute path")
            if GAME_FILE_RE.search(text):
                self.error(f"{location}.path", "text evidence names a commercial game, executable, or save payload")
            if expected_mime == "application/json":
                try:
                    json.loads(text)
                except json.JSONDecodeError:
                    self.error(f"{location}.path", "application/json attachment is not valid JSON")

    def reject_placeholders(self, value: Any, location: str = "$") -> None:
        if value is None:
            self.error(location, "null is incomplete evidence")
        elif isinstance(value, str) and PLACEHOLDER_RE.search(value):
            self.error(location, "contains an incomplete/not-run placeholder")
        elif isinstance(value, dict):
            for key, item in value.items():
                self.reject_placeholders(item, f"{location}.{key}")
        elif isinstance(value, list):
            for index, item in enumerate(value):
                self.reject_placeholders(item, f"{location}[{index}]")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("record", type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--expected-candidate", type=Path, help="Expected candidate JSON object or prior run")
    source.add_argument(
        "--frozen-9aa65824", action="store_true",
        help="Expect the historical 9aa65824 candidate from its qualification card",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        data = json.loads(args.record.read_text(encoding="utf-8"))
        expected_candidate = frozen_candidate() if args.frozen_9aa65824 else load_candidate(args.expected_candidate)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"INVALID: cannot read {args.record}: {error}", file=sys.stderr)
        return 1
    validation = Validation(args.record, expected_candidate)
    errors = validation.validate(data)
    if errors:
        print(f"INVALID: {args.record} ({len(errors)} issue(s))", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    status = data["result"]["status"]
    if status == "fail":
        print(f"VALID FAILURE EVIDENCE: {args.record}")
        return 2
    print(f"PASS: complete physical qualification evidence: {args.record}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
