#!/usr/bin/env python3
"""Create a pinned, intentionally incomplete WebXR physical-run record."""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


SCHEMA = "surrealengine-webxr-physical-run-v1"
GAMES = ("ut99", "unreal-gold")
ATTACHMENT_ROLES = {
    "before-diagnostics", "running-diagnostics", "after-diagnostics",
    "electron-diagnostics", "sample-diagnostics", "screenshot", "video", "other",
}
MIME_BY_SUFFIX = {
    ".json": "application/json",
    ".txt": "text/plain",
    ".md": "text/markdown",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".mp4": "video/mp4",
    ".webm": "video/webm",
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def frozen_candidate() -> dict:
    return {
        "qualificationCard": "Docs/WEBXR_PHYSICAL_QUALIFICATION_9AA65824.md",
        "browser": {
            "buildId": "9aa65824df3f-2c33259ba0d3e82d",
            "sourceCommit": "9aa65824df3fae35c987258702808becffa1f514",
            "sourceTree": "7079a65e61ad8804e42a1e096de9fed41ae2fc36",
            "manifestSha256": "cb580e9c490e79917e6b5c10d9a905cf08d833a76eeb8ae810055712d0985bc3",
            "javascriptSha256": "e80a19471ed92f32c0a54c56531cb48bc67be39467527d9fbd1b143159d562fa",
            "wasmSha256": "2c33259ba0d3e82dacf56b85e6e07bfe4c29714cbd0aeb080c9cc547244810ab",
            "correspondingSourceSha256": "80293d19f8dcaadce04d2e2c3d722761a0de9b5b2f16efb72a10117d443f2e2a",
            "immutableGenerationUrl": "https://dionysus.dk/webxr/Ports/SurrealEngine/releases/cb580e9c490e79917e6b5c10d9a905cf08d833a76eeb8ae810055712d0985bc3/",
        },
        "electron": {
            "electron": "43.2.0",
            "chrome": "150.0.7871.129",
            "zipName": "SurrealEngine-WebXR-Test-9aa65824df3f-2c33259ba0d3e82d-win-x64.zip",
            "zipSha256": "981992d01e63ec7effb66968b39eb2062619fe61721c95d16c664b5118f69a86",
            "status": "unsigned-internal-diagnostic",
        },
    }


def row_artifact(row: str, candidate: dict) -> dict:
    if row == "Q1":
        return {
            "artifactKind": "hosted-immutable-generation",
            "artifactName": "release-manifest.json",
            "artifactSha256": candidate["browser"]["manifestSha256"],
            "presentationMode": "direct-webgl2",
        }
    if row == "D1":
        return {
            "artifactKind": "official-immersive-web-sample",
            "artifactName": None,
            "artifactSha256": None,
            "presentationMode": "official-sample",
            "officialSampleUrl": None,
        }
    return {
        "artifactKind": "electron-automatic" if row == "E1" else "electron-force-openxr",
        "artifactName": candidate["electron"]["zipName"],
        "artifactSha256": candidate["electron"]["zipSha256"],
        "presentationMode": "electron-automatic" if row == "E1" else "electron-force-openxr",
    }


def empty_cycle(index: int) -> dict:
    return {
        "index": index,
        "result": "not-run",
        "entry": {
            "requestedAtUtc": None, "activatedAtUtc": None, "firstFrameAtUtc": None,
            "firstFrameSeen": False, "viewCount": 0, "positiveViewports": False,
        },
        "exit": {
            "route": "game-ui" if index % 2 else "headset-system",
            "requestedAtUtc": None, "flatRestoredAtUtc": None, "flatRestored": False,
        },
        "metrics": {
            "entryLatencyMs": None, "firstFrameLatencyMs": None, "exitLatencyMs": None,
            "xrFrames": 0, "skippedFrames": 0,
            "frameTimeMs": {"samples": 0, "p50": None, "p95": None, "p99": None},
            "visibilityTransitions": [], "maxFrameDeltaUs": 0,
        },
        "resources": {
            "heapBytesBefore": None, "heapBytesAfter": None,
            "texturesBefore": None, "texturesAfter": None,
            "webglGenerationBefore": None, "webglGenerationAfter": None,
        },
        "continuity": {
            "engineStable": False, "levelStable": False, "playerStable": False,
            "rendererStable": False, "audioStable": False,
            "tickBefore": None, "tickAtFirstFrame": None, "tickAfter": None,
        },
        "observations": {
            "sameMap": False, "samePosition": False, "sameHealth": False,
            "sameWeapon": False, "sameAudio": False,
            "duplicateSimulation": False, "largeTimeStep": False,
            "blackOrStaleEye": False, "stuckInputOrHaptics": False,
            "pointerLockRecovered": False,
        },
        "notes": "",
    }


def empty_failure_scenarios() -> list[dict]:
    return [
        {"category": category, "result": "not-run", "flatRecovered": False,
         "laterEntrySucceeded": False, "notes": ""}
        for category in (
            "denied-entry", "headset-sleep-obscured",
            "controller-disconnect-reconnect", "forced-session-end",
        )
    ]


def safe_relative_path(value: str) -> PurePosixPath:
    normalized = value.replace("\\", "/")
    relative = PurePosixPath(normalized)
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError(f"attachment path must stay below the run directory: {value}")
    return relative


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def attachment_record(spec: str, output_directory: Path, index: int) -> dict:
    try:
        role, raw_path = spec.split("=", 1)
    except ValueError as error:
        raise ValueError("--attachment must use ROLE=RELATIVE_PATH") from error
    if role not in ATTACHMENT_ROLES:
        raise ValueError(f"unknown attachment role: {role}")
    relative = safe_relative_path(raw_path)
    absolute = (output_directory / Path(*relative.parts)).resolve()
    root = output_directory.resolve()
    if absolute != root and root not in absolute.parents:
        raise ValueError(f"attachment escapes run directory: {raw_path}")
    if not absolute.is_file():
        raise ValueError(f"attachment does not exist: {absolute}")
    mime = MIME_BY_SUFFIX.get(absolute.suffix.lower())
    if not mime:
        guessed = mimetypes.guess_type(absolute.name)[0]
        raise ValueError(f"unsupported attachment type {guessed or absolute.suffix}: {relative}")
    return {
        "id": f"{role}-{index:02d}",
        "role": role,
        "path": relative.as_posix(),
        "mimeType": mime,
        "bytes": absolute.stat().st_size,
        "sha256": sha256_file(absolute),
        "capturedAtUtc": utc_now(),
    }


def load_candidate(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    candidate = data.get("candidate", data) if isinstance(data, dict) else None
    if not isinstance(candidate, dict):
        raise ValueError("candidate file must contain a candidate object or a run with candidate")
    return candidate


def build_run(args: argparse.Namespace, output_directory: Path) -> dict:
    game = args.game
    if args.row == "D1":
        if game not in (None, "official-sample"):
            raise ValueError("D1 requires --game official-sample")
        game = "official-sample"
    elif game not in GAMES:
        raise ValueError(f"{args.row} requires --game ut99 or --game unreal-gold")

    candidate = args.candidate_data
    now = utc_now()
    slug_time = now.replace("-", "").replace(":", "").split(".")[0].lower()
    test = {
        "startedAtUtc": None,
        "completedAtUtc": None,
        "tester": args.tester,
        "clientVersion": None,
        "questModel": "Quest 3",
        "questOs": None,
        "questBrowser": "n/a" if args.row in ("D1", "E1", "E2") else None,
        "virtualDesktop": "n/a" if args.row == "Q1" else None,
        "openxrRuntime": "native-quest-webxr" if args.row == "Q1" else None,
        "gpuDriver": "n/a" if args.row == "Q1" else None,
        "refreshRateHz": None,
        **row_artifact(args.row, candidate),
    }
    run = {
        "schema": SCHEMA,
        "schemaVersion": 1,
        "runId": f"{candidate['browser']['sourceCommit'][:8]}-{args.row.lower()}-{game}-{slug_time}",
        "generatedAtUtc": now,
        "matrixRow": args.row,
        "gameCase": game,
        "candidate": candidate,
        "test": test,
        "result": {"status": "not-run", "category": "not-run", "summary": "Fill and validate this record after the physical run."},
        "attachments": [
            attachment_record(spec, output_directory, index)
            for index, spec in enumerate(args.attachment, 1)
        ],
        "notes": "Do not attach game files, saves, private paths, folder-picker captures, or raw logs.",
    }
    if args.row == "D1":
        run["officialSample"] = {
            "result": "not-run", "capabilityDetected": False, "sessionGranted": False,
            "firstFrameSeen": False, "viewCount": 0, "positiveViewports": False,
            "controllersObserved": False, "exitSucceeded": False, "notes": "",
        }
    else:
        run["resourcePolicy"] = {
            "maxHeapGrowthBytes": None,
            "maxTextureGrowth": None,
            "maxFrameDeltaUs": None,
        }
        run["cycles"] = [empty_cycle(index) for index in range(1, 11)]
        run["failureScenarios"] = empty_failure_scenarios()
    return run


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--row", choices=("Q1", "D1", "E1", "E2"), required=True)
    parser.add_argument("--game", choices=(*GAMES, "official-sample"))
    parser.add_argument("--tester", required=True, help="Tester name or stable lab identifier")
    parser.add_argument("--output", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--candidate", type=Path, help="Expected candidate JSON object or prior run")
    source.add_argument(
        "--frozen-9aa65824", action="store_true",
        help="Use the historical 9aa65824 candidate from its qualification card",
    )
    parser.add_argument(
        "--attachment", action="append", default=[], metavar="ROLE=RELATIVE_PATH",
        help="Hash an existing attachment below the output directory; repeat as needed",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        args.candidate_data = frozen_candidate() if args.frozen_9aa65824 else load_candidate(args.candidate)
        run = build_run(args, output.parent)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        raise SystemExit(f"error: {error}") from error
    if output.exists():
        raise SystemExit(f"error: refusing to overwrite existing record: {output}")
    output.write_text(json.dumps(run, indent=2) + "\n", encoding="utf-8")
    print(f"Created incomplete physical-run scaffold: {output}")
    print("Fill every not-run/null field, add required attachments, then run validate_physical_run.py.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
