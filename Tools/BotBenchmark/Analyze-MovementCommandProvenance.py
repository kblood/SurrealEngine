#!/usr/bin/env python3
"""Fail-closed validation for opt-in native movement-command provenance."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


class ProvenanceError(ValueError):
    pass


def _integer(value: Any, context: str, minimum: int = 0) -> int:
    if not isinstance(value, str) or not value.isdecimal() or int(value) < minimum:
        raise ProvenanceError(f"{context} must be an unsigned decimal")
    return int(value)


def _signed_integer(value: Any, context: str, minimum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ProvenanceError(f"{context} must be an integer at least {minimum}")
    return value


def _read(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProvenanceError(f"cannot read {path}: {error}") from error


def analyze(run: Path) -> dict[str, Any]:
    manifest = _read(run / "manifest.json")
    if manifest.get("movement_command_provenance_observer_enabled") is not True:
        raise ProvenanceError("manifest does not explicitly enable movement-command provenance observer")
    if manifest.get("native_path_commit_observer_enabled") is not True:
        raise ProvenanceError("movement-command provenance requires native path-commit observer")
    summary = _read(run / "summary.json")
    if summary.get("status") != "complete" or summary.get("exit_code") != 0:
        raise ProvenanceError("summary is not complete and successful")
    records: dict[str, list[dict[str, Any]]] = {}
    commands_by_token: dict[str, dict[tuple[int, int], dict[str, Any]]] = {}
    final: dict[str, tuple[int, int]] = {}
    pain_timer_hazard_deaths_exact = 0
    with (run / "events.jsonl").open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise ProvenanceError(f"events line {line_number}: invalid JSON") from error
            if event.get("movement_command_provenance_observer") != {"requested": True, "status": "active"}:
                raise ProvenanceError(f"events line {line_number}: observer envelope is incomplete")
            bots = event.get("bots")
            if not isinstance(bots, list):
                raise ProvenanceError(f"events line {line_number}: bots is not an array")
            for bot in bots:
                if not isinstance(bot, dict) or not isinstance(bot.get("identity"), str):
                    raise ProvenanceError(f"events line {line_number}: participant identity is invalid")
                identity = bot["identity"]
                count = _integer(bot.get("movement_command_provenance_observations_exact"),
                    f"events line {line_number} {identity}.observations")
                overflow = _integer(bot.get("movement_command_provenance_overflows_exact"),
                    f"events line {line_number} {identity}.overflows")
                if overflow != 0:
                    raise ProvenanceError(f"events line {line_number} {identity}: observer overflow")
                payload = bot.get("movement_command_provenance_records")
                if not isinstance(payload, list):
                    raise ProvenanceError(f"events line {line_number} {identity}: records are not an array")
                emitted = records.setdefault(identity, [])
                tokens = commands_by_token.setdefault(identity, {})
                for record in payload:
                    if not isinstance(record, dict):
                        raise ProvenanceError(f"events line {line_number} {identity}: record is not an object")
                    sequence = _integer(record.get("sequence"), f"{identity}.sequence", 1)
                    if sequence != len(emitted) + 1:
                        raise ProvenanceError(f"{identity}: record sequence is not contiguous")
                    for field in ("command_token", "life_id", "native_tick", "caller_invocation_token"):
                        if _integer(record.get(field), f"{identity}.{field}", 1) == 0:
                            raise ProvenanceError(f"{identity}: {field} is zero")
                    if record.get("integrity_valid") is not True:
                        raise ProvenanceError(f"{identity}: command provenance is incomplete")
                    if record.get("kind") not in {"move_to", "move_toward"}:
                        raise ProvenanceError(f"{identity}: movement kind is invalid")
                    _signed_integer(record.get("source_actor_index"),
                                    f"{identity}.source_actor_index", 0)
                    if not isinstance(record.get("caller_class"), str) or not record["caller_class"] \
                            or not isinstance(record.get("caller_function"), str) or not record["caller_function"]:
                        raise ProvenanceError(f"{identity}: caller provenance is incomplete")
                    if not isinstance(record.get("target_known"), bool):
                        raise ProvenanceError(f"{identity}: target-known state is malformed")
                    _signed_integer(record.get("target_actor_index"),
                                    f"{identity}.target_actor_index", -1)
                    for field in ("target_name", "target_class", "route_head_name", "route_head_class"):
                        if not isinstance(record.get(field), str):
                            raise ProvenanceError(f"{identity}: {field} is malformed")
                    if not isinstance(record.get("route_head_known"), bool):
                        raise ProvenanceError(f"{identity}: route-head state is malformed")
                    _signed_integer(record.get("route_head_actor_index"),
                                    f"{identity}.route_head_actor_index", -1)
                    if record["kind"] == "move_toward" and record["target_known"] is not True:
                        raise ProvenanceError(f"{identity}: MoveToward target provenance is incomplete")
                    if not isinstance(record.get("last_native_path_commit_known"), bool):
                        raise ProvenanceError(f"{identity}: path-commit state is malformed")
                    path_commit_sequence = _integer(record.get("last_native_path_commit_sequence"),
                                                    f"{identity}.last_native_path_commit_sequence")
                    _signed_integer(record.get("last_native_path_commit_first_reachspec_index"),
                                    f"{identity}.last_native_path_commit_first_reachspec_index", -1)
                    if record["last_native_path_commit_known"] and path_commit_sequence == 0:
                        raise ProvenanceError(f"{identity}: path-commit provenance is incomplete")
                    token_key = (_integer(record["life_id"], f"{identity}.life_id", 1),
                                 _integer(record["command_token"], f"{identity}.command_token", 1))
                    if token_key in tokens:
                        raise ProvenanceError(f"{identity}: duplicate life/token provenance")
                    tokens[token_key] = record
                    emitted.append(record)
                deaths = bot.get("hazard_death_partition_records", [])
                if not isinstance(deaths, list):
                    raise ProvenanceError(f"events line {line_number} {identity}: hazard deaths are not an array")
                for death in deaths:
                    if not isinstance(death, dict):
                        raise ProvenanceError(f"events line {line_number} {identity}: hazard death is not an object")
                    if death.get("environmental_source") != "pain_timer":
                        continue
                    if death.get("hazard_residence_terminal_exact") is not True:
                        continue
                    life_id = _integer(death.get("hazard_residence_command_ownership_life_id"),
                                       f"{identity}.hazard_residence_command_ownership_life_id", 1)
                    token = _integer(death.get("hazard_residence_movement_command_token"),
                                     f"{identity}.hazard_residence_movement_command_token", 1)
                    if death.get("hazard_residence_movement_command_provenance_exact") is not True:
                        raise ProvenanceError(f"{identity}: pain-timer hazard death lacks exact command provenance")
                    caller_class = death.get("hazard_residence_movement_command_caller_class")
                    caller_function = death.get("hazard_residence_movement_command_caller_function")
                    if not isinstance(caller_class, str) or not caller_class or \
                            not isinstance(caller_function, str) or not caller_function:
                        raise ProvenanceError(f"{identity}: pain-timer hazard death lacks caller provenance")
                    command = tokens.get((life_id, token))
                    if command is None:
                        raise ProvenanceError(f"{identity}: pain-timer hazard death has no matching command record")
                    if command["caller_class"] != caller_class or command["caller_function"] != caller_function:
                        raise ProvenanceError(f"{identity}: pain-timer hazard death caller does not match command")
                    pain_timer_hazard_deaths_exact += 1
                final[identity] = (count, overflow)
    if not final:
        raise ProvenanceError("events are empty")
    for identity, (count, overflow) in final.items():
        if overflow != 0 or count != len(records[identity]):
            raise ProvenanceError(f"{identity}: counter does not reconcile with records")
    return {"schema": "surreal-bot-benchmark-movement-command-provenance-v1",
            "pain_timer_hazard_deaths_exact": str(pain_timer_hazard_deaths_exact),
            "participants": {identity: {"records_exact": str(len(records[identity]))}
                             for identity in sorted(records)}}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = analyze(args.run)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except ProvenanceError as error:
        print(f"movement-command provenance analysis failed: {error}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
