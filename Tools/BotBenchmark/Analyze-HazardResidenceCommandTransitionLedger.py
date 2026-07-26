#!/usr/bin/env python3
"""Fail-closed validation for hazard-residence command-transition ledgers."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


REPORT_SCHEMA = "surreal-bot-benchmark-hazard-residence-command-transition-ledger-v1"
_FLAGS = (
    "native_path_commit_observer_enabled",
    "movement_command_provenance_observer_enabled",
    "hazard_residence_command_transition_ledger_observer_enabled",
)
_TERMINALS = {"cleared", "death", "life_boundary_censored", "run_end_censored"}


class LedgerError(ValueError):
    pass


def _integer(value: Any, context: str, minimum: int = 0) -> int:
    if not isinstance(value, str) or not value.isdecimal() or int(value) < minimum:
        raise LedgerError(f"{context} must be an unsigned decimal")
    return int(value)


def _signed_integer(value: Any, context: str, minimum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise LedgerError(f"{context} must be an integer at least {minimum}")
    return value


def _read(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LedgerError(f"cannot read {path}: {error}") from error


def _require_configuration(manifest: Any, summary: Any) -> None:
    if not isinstance(manifest, dict) or not isinstance(summary, dict):
        raise LedgerError("manifest and summary must be objects")
    if summary.get("status") != "complete" or summary.get("exit_code") != 0:
        raise LedgerError("summary is not complete and successful")
    config = summary.get("config")
    if not isinstance(config, dict):
        raise LedgerError("summary.config must be an object")
    for field in _FLAGS:
        if manifest.get(field) is not True:
            raise LedgerError(f"manifest does not explicitly enable {field}")
        if config.get(field) is not True:
            raise LedgerError(f"summary.config does not explicitly enable {field}")


def _validate_command(record: Any, identity: str, *, expected_sequence: int | None = None) -> tuple[int, int]:
    if not isinstance(record, dict):
        raise LedgerError(f"{identity}: command is not an object")
    if expected_sequence is not None and _integer(record.get("sequence"), f"{identity}.command.sequence", 1) != expected_sequence:
        raise LedgerError(f"{identity}: command record sequence is not contiguous")
    for field in ("command_token", "life_id", "native_tick", "caller_invocation_token"):
        _integer(record.get(field), f"{identity}.{field}", 1)
    if record.get("integrity_valid") is not True:
        raise LedgerError(f"{identity}: command provenance is incomplete")
    if record.get("kind") not in {"move_to", "move_toward"}:
        raise LedgerError(f"{identity}: movement kind is invalid")
    _signed_integer(record.get("source_actor_index"), f"{identity}.source_actor_index", 0)
    for field in ("caller_class", "caller_function"):
        if not isinstance(record.get(field), str) or not record[field]:
            raise LedgerError(f"{identity}: caller provenance is incomplete")
    if not isinstance(record.get("target_known"), bool):
        raise LedgerError(f"{identity}: target-known state is malformed")
    _signed_integer(record.get("target_actor_index"), f"{identity}.target_actor_index", -1)
    for field in ("target_address", "target_name", "target_class", "route_head_name", "route_head_class"):
        if not isinstance(record.get(field), str):
            raise LedgerError(f"{identity}: {field} is malformed")
    if not isinstance(record.get("route_head_known"), bool):
        raise LedgerError(f"{identity}: route-head state is malformed")
    _signed_integer(record.get("route_head_actor_index"), f"{identity}.route_head_actor_index", -1)
    if record["kind"] == "move_toward" and record["target_known"] is not True:
        raise LedgerError(f"{identity}: MoveToward target provenance is incomplete")
    if not isinstance(record.get("last_native_path_commit_known"), bool):
        raise LedgerError(f"{identity}: path-commit state is malformed")
    path_sequence = _integer(record.get("last_native_path_commit_sequence"),
                             f"{identity}.last_native_path_commit_sequence")
    _signed_integer(record.get("last_native_path_commit_first_reachspec_index"),
                    f"{identity}.last_native_path_commit_first_reachspec_index", -1)
    if record["last_native_path_commit_known"] and path_sequence == 0:
        raise LedgerError(f"{identity}: path-commit provenance is incomplete")
    return (_integer(record["life_id"], f"{identity}.life_id", 1),
            _integer(record["command_token"], f"{identity}.command_token", 1))


def _validate_ledger_record(record: Any, identity: str, expected_sequence: int,
                            commands: dict[tuple[int, int], dict[str, Any]]) -> tuple[int, str]:
    if not isinstance(record, dict):
        raise LedgerError(f"{identity}: ledger record is not an object")
    if _integer(record.get("sequence"), f"{identity}.ledger.sequence", 1) != expected_sequence:
        raise LedgerError(f"{identity}: ledger record sequence is not contiguous")
    if _integer(record.get("episode_id"), f"{identity}.ledger.episode_id", 1) != expected_sequence:
        raise LedgerError(f"{identity}: ledger episode id is not contiguous")
    life_id = _integer(record.get("life_id"), f"{identity}.ledger.life_id", 1)
    if record.get("terminal") not in _TERMINALS:
        raise LedgerError(f"{identity}: ledger terminal disposition is invalid or unknown")
    if record.get("entry_integrity_valid") is not True or record.get("terminal_integrity_valid") is not True:
        raise LedgerError(f"{identity}: ledger terminal join is incomplete")
    entries = record.get("entries")
    if not isinstance(entries, list) or not entries:
        raise LedgerError(f"{identity}: ledger entries are missing")
    prior_token = 0
    prior_tick = -1
    terminal_token = 0
    for index, entry in enumerate(entries, 1):
        if not isinstance(entry, dict):
            raise LedgerError(f"{identity}: ledger entry is not an object")
        if _integer(entry.get("sequence"), f"{identity}.ledger.entry.sequence", 1) != index:
            raise LedgerError(f"{identity}: ledger entry sequence is not contiguous")
        expected_role = "entry" if index == 1 else "replacement"
        if entry.get("role") != expected_role:
            raise LedgerError(f"{identity}: ledger entry role is not ordered")
        if _integer(entry.get("prior_command_token"), f"{identity}.ledger.prior_command_token") != prior_token:
            raise LedgerError(f"{identity}: ledger command transition does not join")
        command = entry.get("command")
        command_life, command_token = _validate_command(command, identity)
        if command_life != life_id:
            raise LedgerError(f"{identity}: ledger command crosses a life boundary")
        native_tick = _integer(command.get("native_tick"), f"{identity}.native_tick", 1)
        if native_tick < prior_tick:
            raise LedgerError(f"{identity}: ledger command time regressed")
        if command_token <= prior_token:
            raise LedgerError(f"{identity}: ledger command token did not advance")
        source = commands.get((command_life, command_token))
        if source is None:
            raise LedgerError(f"{identity}: ledger command has no matching provenance record")
        if source != command:
            raise LedgerError(f"{identity}: ledger command does not exactly reconcile with provenance")
        prior_token, prior_tick, terminal_token = command_token, native_tick, command_token
    if _integer(record.get("entry_command_token"), f"{identity}.ledger.entry_command_token", 1) != \
            _integer(entries[0]["command"].get("command_token"), f"{identity}.entry.command_token", 1):
        raise LedgerError(f"{identity}: ledger entry command token does not join")
    if _integer(record.get("terminal_command_token"), f"{identity}.ledger.terminal_command_token", 1) != terminal_token:
        raise LedgerError(f"{identity}: ledger terminal command token does not join")
    return life_id, record["terminal"]


def analyze(run: Path) -> dict[str, Any]:
    _require_configuration(_read(run / "manifest.json"), _read(run / "summary.json"))
    command_records: dict[str, list[dict[str, Any]]] = {}
    command_index: dict[str, dict[tuple[int, int], dict[str, Any]]] = {}
    ledger_records: dict[str, list[dict[str, Any]]] = {}
    final_counters: dict[str, tuple[int, int, int, int]] = {}
    with (run / "events.jsonl").open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise LedgerError(f"events line {line_number}: invalid JSON") from error
            active = {"requested": True, "status": "active"}
            if event.get("movement_command_provenance_observer") != active or \
                    event.get("hazard_residence_command_transition_ledger_observer") != active:
                raise LedgerError(f"events line {line_number}: observer envelope is incomplete")
            bots = event.get("bots")
            if not isinstance(bots, list):
                raise LedgerError(f"events line {line_number}: bots is not an array")
            for bot in bots:
                if not isinstance(bot, dict) or not isinstance(bot.get("identity"), str) or not bot["identity"]:
                    raise LedgerError(f"events line {line_number}: participant identity is invalid")
                identity = bot["identity"]
                commands = command_records.setdefault(identity, [])
                index = command_index.setdefault(identity, {})
                ledger = ledger_records.setdefault(identity, [])
                observation_count = _integer(bot.get("movement_command_provenance_observations_exact"),
                                             f"events line {line_number} {identity}.observations")
                observation_overflow = _integer(bot.get("movement_command_provenance_overflows_exact"),
                                                f"events line {line_number} {identity}.overflows")
                episode_count = _integer(bot.get("hazard_residence_command_transition_ledger_episodes_exact"),
                                         f"events line {line_number} {identity}.ledger_episodes")
                ledger_overflow = _integer(bot.get("hazard_residence_command_transition_ledger_overflows_exact"),
                                           f"events line {line_number} {identity}.ledger_overflows")
                if observation_overflow != 0 or ledger_overflow != 0:
                    raise LedgerError(f"events line {line_number} {identity}: observer overflow")
                payload = bot.get("movement_command_provenance_records")
                if not isinstance(payload, list):
                    raise LedgerError(f"events line {line_number} {identity}: provenance records are not an array")
                for command in payload:
                    key = _validate_command(command, identity, expected_sequence=len(commands) + 1)
                    if key in index:
                        raise LedgerError(f"{identity}: duplicate life/token provenance")
                    index[key] = command
                    commands.append(command)
                ledger_payload = bot.get("hazard_residence_command_transition_ledger_records")
                if not isinstance(ledger_payload, list):
                    raise LedgerError(f"events line {line_number} {identity}: ledger records are not an array")
                for record in ledger_payload:
                    # Reconcile commands after the complete event stream, because a producer may
                    # serialize the terminal ledger before a same-event command batch.
                    if not isinstance(record, dict):
                        raise LedgerError(f"{identity}: ledger record is not an object")
                    ledger.append(record)
                prior = final_counters.get(identity)
                if prior and (observation_count < prior[0] or episode_count < prior[2]):
                    raise LedgerError(f"{identity}: observer counter regressed")
                final_counters[identity] = (observation_count, observation_overflow,
                                            episode_count, ledger_overflow)
    if not final_counters:
        raise LedgerError("events are empty")
    terminal_totals: dict[str, int] = {terminal: 0 for terminal in sorted(_TERMINALS)}
    for identity, counters in final_counters.items():
        commands = command_records[identity]
        ledger = ledger_records[identity]
        if counters[0] != len(commands):
            raise LedgerError(f"{identity}: provenance counter does not reconcile with records")
        if counters[2] != len(ledger):
            raise LedgerError(f"{identity}: ledger episode counter does not reconcile with terminal records")
        seen_lives: set[tuple[int, int]] = set()
        for sequence, record in enumerate(ledger, 1):
            life_id, terminal = _validate_ledger_record(record, identity, sequence, command_index[identity])
            key = (life_id, sequence)
            if key in seen_lives:
                raise LedgerError(f"{identity}: duplicate ledger life/episode terminal join")
            seen_lives.add(key)
            terminal_totals[terminal] += 1
    return {
        "schema": REPORT_SCHEMA,
        "episodes_exact": str(sum(len(records) for records in ledger_records.values())),
        "terminal_dispositions_exact": {name: str(terminal_totals[name]) for name in sorted(terminal_totals)},
        "participants": {identity: {"episodes_exact": str(len(ledger_records[identity]))}
                         for identity in sorted(ledger_records)},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = analyze(args.run)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except LedgerError as error:
        print(f"hazard-residence command-transition ledger analysis failed: {error}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
