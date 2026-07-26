#!/usr/bin/env python3
"""Fail-closed validation for opt-in hazard-residence pre-entry causal slices."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


REPORT_SCHEMA = "surreal-bot-benchmark-hazard-residence-preentry-causal-slices-v2"
_FLAGS = (
    "native_path_commit_observer_enabled",
    "movement_command_provenance_observer_enabled",
    "hazard_residence_command_transition_ledger_observer_enabled",
    "hazard_residence_preentry_causal_slice_observer_enabled",
)
_TERMINALS = {"cleared", "death", "life_boundary_censored", "run_end_censored"}
_ACTIVE = {"requested": True, "status": "active"}


class CausalSliceError(ValueError):
    pass


def _read(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CausalSliceError(f"cannot read {path}: {error}") from error


def _integer(value: Any, context: str, minimum: int = 0) -> int:
    if not isinstance(value, str) or not value.isdecimal() or int(value) < minimum:
        raise CausalSliceError(f"{context} must be an unsigned decimal")
    return int(value)


def _signed_integer(value: Any, context: str, minimum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise CausalSliceError(f"{context} must be an integer at least {minimum}")
    return value


def _number(value: Any, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(float(value)):
        raise CausalSliceError(f"{context} must be a finite number")
    return float(value)


def _known_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value or value.lower() in {"unknown", "none", "unavailable"}:
        raise CausalSliceError(f"{context} is unknown or malformed")
    return value


def _require_configuration(manifest: Any, summary: Any) -> None:
    if not isinstance(manifest, dict) or not isinstance(summary, dict):
        raise CausalSliceError("manifest and summary must be objects")
    if summary.get("status") != "complete" or summary.get("exit_code") != 0:
        raise CausalSliceError("summary is not complete and successful")
    config = summary.get("config")
    if not isinstance(config, dict):
        raise CausalSliceError("summary.config must be an object")
    for field in _FLAGS:
        if manifest.get(field) is not True:
            raise CausalSliceError(f"manifest does not explicitly enable {field}")
        if config.get(field) is not True:
            raise CausalSliceError(f"summary.config does not explicitly enable {field}")


def _validate_location(value: Any, context: str) -> None:
    if not isinstance(value, dict):
        raise CausalSliceError(f"{context} must be an object")
    for axis in ("x", "y", "z"):
        _number(value.get(axis), f"{context}.{axis}")


def _validate_boundary(value: Any, context: str) -> tuple[bool, int]:
    if not isinstance(value, dict):
        raise CausalSliceError(f"{context} must be an object")
    observed = value.get("observed")
    if not isinstance(observed, bool):
        raise CausalSliceError(f"{context}.observed must be boolean")
    tick = _integer(value.get("native_tick"), f"{context}.native_tick")
    physics = value.get("physics")
    if observed:
        if tick == 0:
            raise CausalSliceError(f"{context} observed boundary has no tick")
        _known_string(physics, f"{context}.physics")
    elif tick != 0 or physics != "":
        raise CausalSliceError(f"{context} absent boundary has evidence")
    return observed, tick


def _validate_identity(known: Any, actor_index: Any, name: Any, class_name: Any,
                       context: str) -> tuple[bool, int, str, str]:
    if not isinstance(known, bool):
        raise CausalSliceError(f"{context}.known must be boolean")
    index = _signed_integer(actor_index, f"{context}.actor_index", -1)
    if not isinstance(name, str) or not isinstance(class_name, str):
        raise CausalSliceError(f"{context} identity strings must be strings")
    if known:
        if index < 0:
            raise CausalSliceError(f"{context} known identity has no actor index")
        _known_string(name, f"{context}.name")
        _known_string(class_name, f"{context}.class")
    elif index != -1 or name or class_name:
        raise CausalSliceError(f"{context} unknown identity has evidence")
    return known, index, name, class_name


def _validate_zone_identity(known: Any, actor_index: Any, zone_number: Any,
                            context: str) -> tuple[bool, int, int]:
    if not isinstance(known, bool):
        raise CausalSliceError(f"{context}.known must be boolean")
    index = _signed_integer(actor_index, f"{context}.actor_index", -1)
    number = _signed_integer(zone_number, f"{context}.number", -1)
    if known and (index < 0 or number < 0):
        raise CausalSliceError(f"{context} known zone is incomplete")
    if not known and (index != -1 or number != -1):
        raise CausalSliceError(f"{context} unknown zone has evidence")
    return known, index, number


def _validate_route_trajectory(record: dict[str, Any], entry_command: dict[str, Any],
                               identity: str) -> None:
    path_known = record.get("preceding_path_commit_known")
    if path_known is not True:
        raise CausalSliceError(f"{identity}: preceding path commit is incomplete")
    path_cache_clear = record.get("preceding_path_commit_cache_clear")
    if not isinstance(path_cache_clear, bool):
        raise CausalSliceError(f"{identity}.slice.preceding_path_commit_cache_clear must be boolean")
    path_sequence = _integer(record.get("preceding_path_commit_sequence"),
                             f"{identity}.slice.preceding_path_commit_sequence", 1)
    path_reachspec = _signed_integer(record.get("preceding_path_commit_first_reachspec_index"),
                                     f"{identity}.slice.preceding_path_commit_first_reachspec_index", -1)
    if entry_command.get("last_native_path_commit_known") is not True or \
            entry_command.get("last_native_path_commit_cache_clear") is not path_cache_clear or \
            _integer(entry_command.get("last_native_path_commit_sequence"),
                     f"{identity}.ledger.entry.last_native_path_commit_sequence", 1) != path_sequence or \
            _signed_integer(entry_command.get("last_native_path_commit_first_reachspec_index"),
                            f"{identity}.ledger.entry.last_native_path_commit_first_reachspec_index", -1) != path_reachspec:
        raise CausalSliceError(f"{identity}: preceding path commit does not exactly join entry command")
    route = _validate_identity(record.get("route_head_known"), record.get("route_head_actor_index"),
                               record.get("route_head_name"), record.get("route_head_class"),
                               f"{identity}.slice.route_head")
    command_route = _validate_identity(entry_command.get("route_head_known"),
                                       entry_command.get("route_head_actor_index"),
                                       entry_command.get("route_head_name"),
                                       entry_command.get("route_head_class"),
                                       f"{identity}.ledger.entry.route_head")
    path_route = _validate_identity(entry_command.get("last_native_path_commit_route_head_known"),
                                    entry_command.get("last_native_path_commit_route_head_actor_index"),
                                    entry_command.get("last_native_path_commit_route_head_name"),
                                    entry_command.get("last_native_path_commit_route_head_class"),
                                    f"{identity}.ledger.entry.last_native_path_commit_route_head")
    route_from_path_commit = record.get("route_head_from_preceding_path_commit")
    if not isinstance(route_from_path_commit, bool):
        raise CausalSliceError(f"{identity}.slice.route_head_from_preceding_path_commit must be boolean")
    expected_route = path_route if route_from_path_commit else command_route
    if path_cache_clear:
        if route[0] or route_from_path_commit:
            raise CausalSliceError(f"{identity}: cache-clearing path commit retained a route head")
    elif route != expected_route or not route[0]:
        raise CausalSliceError(f"{identity}: route head does not exactly join its declared entry source")
    if route_from_path_commit and command_route[0]:
        raise CausalSliceError(f"{identity}: path-commit route head was selected despite a live entry route head")
    target = _validate_identity(record.get("command_target_known"),
                                record.get("command_target_actor_index"),
                                record.get("command_target_name"), record.get("command_target_class"),
                                f"{identity}.slice.command_target")
    command_target = _validate_identity(entry_command.get("target_known"),
                                        entry_command.get("target_actor_index"),
                                        entry_command.get("target_name"), entry_command.get("target_class"),
                                        f"{identity}.ledger.entry.target")
    if target != command_target:
        raise CausalSliceError(f"{identity}: command target does not exactly join entry command")
    velocity = record.get("entry_velocity")
    direction = record.get("entry_direction")
    _validate_location(velocity, f"{identity}.slice.entry_velocity")
    _validate_location(direction, f"{identity}.slice.entry_direction")
    direction_known = record.get("entry_direction_known")
    if not isinstance(direction_known, bool):
        raise CausalSliceError(f"{identity}.slice.entry_direction_known must be boolean")
    direction_squared = sum(float(direction[axis]) ** 2 for axis in ("x", "y", "z"))
    if direction_known and not math.isclose(direction_squared, 1.0, abs_tol=1e-5):
        raise CausalSliceError(f"{identity}: known entry direction is not normalized")
    if not direction_known and direction_squared != 0.0:
        raise CausalSliceError(f"{identity}: unknown entry direction has evidence")
    if record.get("trajectory_input_finite") is not True or record.get("trajectory_complete") is not True \
            or record.get("trajectory_result_finite") is not True:
        raise CausalSliceError(f"{identity}: bounded trajectory is incomplete or non-finite")
    classification = _integer(record.get("trajectory_classification"),
                              f"{identity}.slice.trajectory_classification")
    reason = _integer(record.get("trajectory_reason"), f"{identity}.slice.trajectory_reason")
    if classification > 2 or reason > 20:
        raise CausalSliceError(f"{identity}: trajectory enum is outside the declared model")
    _number(record.get("trajectory_elapsed"), f"{identity}.slice.trajectory_elapsed")
    _number(record.get("trajectory_path_distance"), f"{identity}.slice.trajectory_path_distance")
    _integer(record.get("trajectory_segment_count"), f"{identity}.slice.trajectory_segment_count")
    _integer(record.get("trajectory_sample_count"), f"{identity}.slice.trajectory_sample_count")
    entry_zone = _validate_zone_identity(record.get("trajectory_entry_zone_known"),
                                         record.get("trajectory_entry_zone_actor_index"),
                                         record.get("trajectory_entry_zone_number"),
                                         f"{identity}.slice.trajectory_entry_zone")
    if not entry_zone[0] or entry_zone[1] != _signed_integer(record.get("zone_actor_index"),
                                                               f"{identity}.slice.zone_actor_index", 0):
        raise CausalSliceError(f"{identity}: trajectory entry zone does not join harmful entry zone")
    _validate_zone_identity(record.get("trajectory_expected_harmful_foot_zone_known"),
                            record.get("trajectory_expected_harmful_foot_zone_actor_index"),
                            record.get("trajectory_expected_harmful_foot_zone_number"),
                            f"{identity}.slice.trajectory_expected_harmful_foot_zone")
    _validate_zone_identity(record.get("trajectory_expected_harmful_physics_zone_known"),
                            record.get("trajectory_expected_harmful_physics_zone_actor_index"),
                            record.get("trajectory_expected_harmful_physics_zone_number"),
                            f"{identity}.slice.trajectory_expected_harmful_physics_zone")


def _ledger_lineage(record: Any, identity: str) -> tuple[int, int, int, str, list[dict[str, Any]], dict[str, Any]]:
    if not isinstance(record, dict):
        raise CausalSliceError(f"{identity}: terminal ledger record is not an object")
    sequence = _integer(record.get("sequence"), f"{identity}.ledger.sequence", 1)
    episode_id = _integer(record.get("episode_id"), f"{identity}.ledger.episode_id", 1)
    life_id = _integer(record.get("life_id"), f"{identity}.ledger.life_id", 1)
    terminal = record.get("terminal")
    if terminal not in _TERMINALS:
        raise CausalSliceError(f"{identity}: terminal ledger disposition is invalid or unknown")
    if record.get("entry_integrity_valid") is not True or record.get("terminal_integrity_valid") is not True:
        raise CausalSliceError(f"{identity}: terminal ledger join is incomplete")
    entries = record.get("entries")
    if not isinstance(entries, list) or not entries:
        raise CausalSliceError(f"{identity}: terminal ledger entries are missing")
    projected: list[dict[str, Any]] = []
    prior_token = 0
    prior_tick = -1
    for index, entry in enumerate(entries, 1):
        if not isinstance(entry, dict):
            raise CausalSliceError(f"{identity}: terminal ledger entry is not an object")
        if _integer(entry.get("sequence"), f"{identity}.ledger.entry.sequence", 1) != index:
            raise CausalSliceError(f"{identity}: terminal ledger entry sequence is not contiguous")
        role = "entry" if index == 1 else "replacement"
        if entry.get("role") != role:
            raise CausalSliceError(f"{identity}: terminal ledger entry role is not ordered")
        if _integer(entry.get("prior_command_token"), f"{identity}.ledger.entry.prior_command_token") != prior_token:
            raise CausalSliceError(f"{identity}: terminal ledger command chain does not join")
        command = entry.get("command")
        if not isinstance(command, dict):
            raise CausalSliceError(f"{identity}: terminal ledger command is not an object")
        token = _integer(command.get("command_token"), f"{identity}.ledger.command_token", 1)
        command_life = _integer(command.get("life_id"), f"{identity}.ledger.command.life_id", 1)
        tick = _integer(command.get("native_tick"), f"{identity}.ledger.command.native_tick", 1)
        if command_life != life_id or token <= prior_token or tick < prior_tick:
            raise CausalSliceError(f"{identity}: terminal ledger command lineage is incomplete")
        if command.get("integrity_valid") is not True:
            raise CausalSliceError(f"{identity}: terminal ledger command provenance is incomplete")
        item = {
            "sequence": entry["sequence"], "role": role,
            "prior_command_token": entry["prior_command_token"], "command_token": command["command_token"],
            "native_tick": command["native_tick"], "caller_class": command.get("caller_class"),
            "caller_function": command.get("caller_function"), "kind": command.get("kind"),
        }
        _known_string(item["caller_class"], f"{identity}.ledger.command.caller_class")
        _known_string(item["caller_function"], f"{identity}.ledger.command.caller_function")
        if item["kind"] not in {"move_to", "move_toward"}:
            raise CausalSliceError(f"{identity}: terminal ledger command kind is invalid")
        projected.append(item)
        prior_token, prior_tick = token, tick
    if _integer(record.get("entry_command_token"), f"{identity}.ledger.entry_command_token", 1) !=  \
            _integer(projected[0]["command_token"], f"{identity}.ledger.projected.entry_command_token", 1):
        raise CausalSliceError(f"{identity}: terminal ledger entry token does not join")
    if _integer(record.get("terminal_command_token"), f"{identity}.ledger.terminal_command_token", 1) != prior_token:
        raise CausalSliceError(f"{identity}: terminal ledger terminal token does not join")
    return sequence, episode_id, life_id, terminal, projected, entries[0]["command"]


def _validate_slice(record: Any, identity: str, expected_sequence: int,
                    ledger: tuple[int, int, int, str, list[dict[str, Any]]]) -> str:
    if not isinstance(record, dict):
        raise CausalSliceError(f"{identity}: causal slice record is not an object")
    sequence, episode_id, life_id, terminal, ledger_lineage, entry_command = ledger
    for field, expected in (("sequence", sequence), ("episode_id", episode_id), ("life_id", life_id)):
        if _integer(record.get(field), f"{identity}.slice.{field}", 1) != expected:
            raise CausalSliceError(f"{identity}: causal slice does not reconcile with terminal ledger {field}")
    if sequence != expected_sequence:
        raise CausalSliceError(f"{identity}: causal slice sequence is not contiguous")
    if record.get("terminal") != terminal:
        raise CausalSliceError(f"{identity}: causal slice terminal does not reconcile with terminal ledger")
    if record.get("integrity_valid") is not True or record.get("entry_integrity_valid") is not True or \
            record.get("terminal_integrity_valid") is not True:
        raise CausalSliceError(f"{identity}: causal slice is incomplete")
    _signed_integer(record.get("zone_actor_index"), f"{identity}.slice.zone_actor_index", 0)
    _known_string(record.get("zone_name"), f"{identity}.slice.zone_name")
    _known_string(record.get("zone_class"), f"{identity}.slice.zone_class")
    _validate_location(record.get("entry_location"), f"{identity}.slice.entry_location")
    _known_string(record.get("pre_entry_physics"), f"{identity}.slice.pre_entry_physics")
    _known_string(record.get("entry_physics"), f"{identity}.slice.entry_physics")
    support_known = record.get("support_known")
    if not isinstance(support_known, bool):
        raise CausalSliceError(f"{identity}.slice.support_known must be boolean")
    support_index = _signed_integer(record.get("support_actor_index"), f"{identity}.slice.support_actor_index", -1)
    support_class = record.get("support_class")
    if not isinstance(support_class, str):
        raise CausalSliceError(f"{identity}.slice.support_class must be a string")
    if support_known and (support_index < 0 or not support_class):
        raise CausalSliceError(f"{identity}: known support provenance is incomplete")
    if not support_known and (support_index != -1 or support_class):
        raise CausalSliceError(f"{identity}: unknown support provenance is inconsistent")
    _known_string(record.get("transition"), f"{identity}.slice.transition")
    mayfall, mayfall_tick = _validate_boundary(record.get("mayfall_boundary"), f"{identity}.slice.mayfall_boundary")
    hitwall, hitwall_tick = _validate_boundary(record.get("hitwall_boundary"), f"{identity}.slice.hitwall_boundary")
    if mayfall and hitwall and hitwall_tick < mayfall_tick:
        raise CausalSliceError(f"{identity}: HitWall boundary precedes MayFall boundary")
    lineage = record.get("command_lineage")
    if not isinstance(lineage, list) or not lineage:
        raise CausalSliceError(f"{identity}: causal slice command lineage is missing")
    if lineage != ledger_lineage:
        raise CausalSliceError(f"{identity}: causal slice command lineage does not exactly reconcile with terminal ledger")
    _validate_route_trajectory(record, entry_command, identity)
    return terminal


def analyze(run: Path) -> dict[str, Any]:
    _require_configuration(_read(run / "manifest.json"), _read(run / "summary.json"))
    terminal_ledgers: dict[str, list[dict[str, Any]]] = {}
    slices: dict[str, list[dict[str, Any]]] = {}
    final: dict[str, tuple[int, int, int, int]] = {}
    with (run / "events.jsonl").open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise CausalSliceError(f"events line {line_number}: invalid JSON") from error
            for envelope in ("movement_command_provenance_observer",
                             "hazard_residence_command_transition_ledger_observer",
                             "hazard_residence_preentry_causal_slice_observer"):
                if event.get(envelope) != _ACTIVE:
                    raise CausalSliceError(f"events line {line_number}: {envelope} envelope is incomplete")
            bots = event.get("bots")
            if not isinstance(bots, list):
                raise CausalSliceError(f"events line {line_number}: bots is not an array")
            for bot in bots:
                if not isinstance(bot, dict) or not isinstance(bot.get("identity"), str) or not bot["identity"]:
                    raise CausalSliceError(f"events line {line_number}: participant identity is invalid")
                identity = bot["identity"]
                ledger_count = _integer(bot.get("hazard_residence_command_transition_ledger_episodes_exact"),
                                        f"events line {line_number} {identity}.ledger_episodes")
                ledger_overflow = _integer(bot.get("hazard_residence_command_transition_ledger_overflows_exact"),
                                           f"events line {line_number} {identity}.ledger_overflows")
                slice_count = _integer(bot.get("hazard_residence_preentry_causal_slice_episodes_exact"),
                                       f"events line {line_number} {identity}.slice_episodes")
                slice_overflow = _integer(bot.get("hazard_residence_preentry_causal_slice_overflows_exact"),
                                          f"events line {line_number} {identity}.slice_overflows")
                if ledger_overflow or slice_overflow:
                    raise CausalSliceError(f"events line {line_number} {identity}: observer overflow")
                ledger_payload = bot.get("hazard_residence_command_transition_ledger_records")
                slice_payload = bot.get("hazard_residence_preentry_causal_slice_records")
                if not isinstance(ledger_payload, list) or not isinstance(slice_payload, list):
                    raise CausalSliceError(f"events line {line_number} {identity}: terminal ledger or causal slices are not arrays")
                emitted_ledger = terminal_ledgers.setdefault(identity, [])
                emitted_slices = slices.setdefault(identity, [])
                emitted_ledger.extend(ledger_payload)
                emitted_slices.extend(slice_payload)
                prior = final.get(identity)
                if prior and (ledger_count < prior[0] or slice_count < prior[2]):
                    raise CausalSliceError(f"{identity}: observer counter regressed")
                final[identity] = (ledger_count, ledger_overflow, slice_count, slice_overflow)
    if not final:
        raise CausalSliceError("events are empty")
    totals = {terminal: 0 for terminal in sorted(_TERMINALS)}
    for identity, counters in final.items():
        ledgers, records = terminal_ledgers[identity], slices[identity]
        if counters[0] != len(ledgers) or counters[2] != len(records):
            raise CausalSliceError(f"{identity}: terminal-ledger or causal-slice counter does not reconcile with records")
        if len(ledgers) != len(records):
            raise CausalSliceError(f"{identity}: causal slices do not partition terminal ledger episodes")
        for sequence, (ledger, record) in enumerate(zip(ledgers, records), 1):
            terminal = _validate_slice(record, identity, sequence, _ledger_lineage(ledger, identity))
            totals[terminal] += 1
    return {
        "schema": REPORT_SCHEMA,
        "episodes_exact": str(sum(len(records) for records in slices.values())),
        "terminal_dispositions_exact": {terminal: str(totals[terminal]) for terminal in sorted(totals)},
        "participants": {identity: {"episodes_exact": str(len(slices[identity]))} for identity in sorted(slices)},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = analyze(args.run)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except CausalSliceError as error:
        print(f"hazard-residence pre-entry causal-slice analysis failed: {error}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
