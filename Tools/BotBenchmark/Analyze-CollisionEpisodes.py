#!/usr/bin/env python3
"""Bounded, fail-closed partitioning of bot HitWall telemetry.

This is an offline attribution aid.  The telemetry contains cumulative HitWall
and pain-ledge counters, not a per-callback causal trace, so it deliberately
does not claim that a later collision was *caused* by a recovery action.  A
contact concurrent with an exact recovery-attempt counter transition is called
``recovery_transition``; contacts between that transition and its exact escape,
superseding attempt, life boundary, or run end are reported separately as a
temporal recovery-lifecycle window.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


REPORT_SCHEMA = "surreal-bot-benchmark-collision-episodes-v1"
TELEMETRY_SCHEMA = "surreal-bot-benchmark-telemetry-v2"
COUNTERS = ("hit_wall_events_exact", "pain_ledge_recovery_attempts_exact",
            "pain_ledge_recovery_escapes_exact", "deaths_exact")


class CollisionError(ValueError):
    """The event stream cannot support a safe collision partition."""


def _integer(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise CollisionError(f"{context} must be a non-negative integer")
    try:
        result = int(value)
    except ValueError as error:
        raise CollisionError(f"{context} must be a non-negative integer") from error
    if result < 0:
        raise CollisionError(f"{context} must be a non-negative integer")
    return result


def _number(value: Any, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CollisionError(f"{context} must be a finite number")
    result = float(value)
    if not math.isfinite(result):
        raise CollisionError(f"{context} must be a finite number")
    return result


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str):
        raise CollisionError(f"{context} must be a string")
    return value


def _position(value: Any, context: str) -> dict[str, float]:
    if not isinstance(value, dict):
        raise CollisionError(f"{context} must be an object")
    return {axis: _number(value.get(axis), f"{context}.{axis}") for axis in ("x", "y", "z")}


def _horizontal_distance(left: dict[str, float], right: dict[str, float]) -> float:
    return math.hypot(left["x"] - right["x"], left["y"] - right["y"])


@dataclass
class _Contact:
    tick: int
    seconds: float
    callbacks: int
    phase: str
    physics: str
    latent_action: str
    target_present: bool
    position: dict[str, float]


@dataclass
class _Episode:
    identity: str
    actor: str
    episode_id: int
    contacts: list[_Contact] = field(default_factory=list)

    def add(self, contact: _Contact) -> None:
        self.contacts.append(contact)

    def report(self, wall_loop_minimum_contacts: int,
               wall_loop_maximum_horizontal_displacement: float) -> dict[str, Any]:
        first, last = self.contacts[0], self.contacts[-1]
        callbacks = sum(contact.callbacks for contact in self.contacts)
        phases: dict[str, int] = {}
        for contact in self.contacts:
            phases[contact.phase] = phases.get(contact.phase, 0) + contact.callbacks
        horizontal_displacement = _horizontal_distance(first.position, last.position)
        is_wall_loop_candidate = (
            callbacks >= wall_loop_minimum_contacts
            and len(self.contacts) >= wall_loop_minimum_contacts
            and all(contact.phase == "ordinary_walking" for contact in self.contacts)
            and all(contact.physics == "Walking" for contact in self.contacts)
            and all(contact.latent_action == "Continue" for contact in self.contacts)
            and all(not contact.target_present for contact in self.contacts)
            and horizontal_displacement <= wall_loop_maximum_horizontal_displacement
        )
        return {
            "identity": self.identity,
            "actor": self.actor,
            "episode_id": str(self.episode_id),
            "first_tick": str(first.tick),
            "last_tick": str(last.tick),
            "first_seconds": first.seconds,
            "last_seconds": last.seconds,
            "contact_ticks": str(len(self.contacts)),
            "callbacks_exact": str(callbacks),
            "maximum_callbacks_per_tick_exact": str(max(contact.callbacks for contact in self.contacts)),
            "horizontal_displacement": horizontal_displacement,
            "phase_callbacks_exact": {key: str(value) for key, value in sorted(phases.items())},
            "wall_loop_candidate": is_wall_loop_candidate,
        }


@dataclass
class _RecoveryLifecycle:
    identity: str
    actor: str
    attempt_id: int
    attempt_tick: int
    attempt_seconds: float
    contacts_exact: int = 0
    contact_ticks: int = 0
    terminal: str = "run_end_censored"
    terminal_tick: int = 0
    terminal_seconds: float = 0.0

    def report(self) -> dict[str, Any]:
        return {
            "identity": self.identity,
            "actor": self.actor,
            "attempt_id": str(self.attempt_id),
            "attempt_tick": str(self.attempt_tick),
            "attempt_seconds": self.attempt_seconds,
            "temporal_window_contact_callbacks_exact": str(self.contacts_exact),
            "temporal_window_contact_ticks": str(self.contact_ticks),
            "terminal": self.terminal,
            "terminal_tick": str(self.terminal_tick),
            "terminal_seconds": self.terminal_seconds,
        }


def _record_sort_key(record: dict[str, Any]) -> tuple[Any, ...]:
    return (-int(record.get("callbacks_exact", "0")), record["identity"], int(record["first_tick"]))


def _bounded_records(records: list[dict[str, Any]], maximum: int,
                     key: Any) -> tuple[list[dict[str, Any]], int]:
    records.sort(key=key)
    return records[:maximum], max(0, len(records) - maximum)


def analyze_events(events: Iterable[dict[str, Any]], *, contact_gap_ticks: int = 2,
                   wall_loop_minimum_contacts: int = 3,
                   wall_loop_maximum_horizontal_displacement: float = 16.0,
                   maximum_records: int = 256) -> dict[str, Any]:
    """Return a complete-counter collision partition from a v2 event stream."""
    if contact_gap_ticks < 0:
        raise CollisionError("contact gap ticks must be non-negative")
    if wall_loop_minimum_contacts < 1:
        raise CollisionError("wall-loop minimum contacts must be positive")
    if not math.isfinite(wall_loop_maximum_horizontal_displacement) \
            or wall_loop_maximum_horizontal_displacement < 0.0:
        raise CollisionError("wall-loop maximum horizontal displacement must be non-negative")
    if maximum_records < 1:
        raise CollisionError("maximum records must be positive")

    previous_tick = -1
    previous_seconds = -1.0
    previous: dict[str, dict[str, Any]] = {}
    active_episodes: dict[str, _Episode] = {}
    episode_ids: dict[str, int] = {}
    completed_episodes: list[dict[str, Any]] = []
    active_recoveries: dict[str, _RecoveryLifecycle] = {}
    recovery_ids: dict[str, int] = {}
    completed_recoveries: list[dict[str, Any]] = []
    partition = {key: 0 for key in (
        "recovery_transition", "falling_or_landing", "ordinary_walking", "ordinary_other",
        "life_boundary_unattributable")}
    complete = False
    event_count = 0

    def close_episode(identity: str) -> None:
        episode = active_episodes.pop(identity, None)
        if episode is not None:
            completed_episodes.append(episode.report(
                wall_loop_minimum_contacts, wall_loop_maximum_horizontal_displacement))

    def close_recovery(identity: str, terminal: str, tick: int, seconds: float) -> None:
        recovery = active_recoveries.pop(identity, None)
        if recovery is not None:
            recovery.terminal = terminal
            recovery.terminal_tick = tick
            recovery.terminal_seconds = seconds
            completed_recoveries.append(recovery.report())

    for event_index, event in enumerate(events):
        event_count += 1
        context = f"events[{event_index}]"
        if not isinstance(event, dict):
            raise CollisionError(f"{context} must be an object")
        if event.get("schema") != TELEMETRY_SCHEMA:
            raise CollisionError(f"{context}.schema must be {TELEMETRY_SCHEMA}")
        tick = _integer(event.get("tick"), f"{context}.tick")
        seconds = _number(event.get("simulated_seconds"), f"{context}.simulated_seconds")
        if tick < previous_tick or seconds < previous_seconds:
            raise CollisionError(f"{context} time regressed")
        previous_tick, previous_seconds = tick, seconds
        event_type = _string(event.get("type"), f"{context}.type")
        bots = event.get("bots")
        if not isinstance(bots, list):
            raise CollisionError(f"{context}.bots must be an array")
        if event_type == "run_result":
            complete = event.get("status") == "complete" and event.get("failure_reason") == ""

        seen: set[str] = set()
        for bot_index, bot in enumerate(bots):
            bot_context = f"{context}.bots[{bot_index}]"
            if not isinstance(bot, dict):
                raise CollisionError(f"{bot_context} must be an object")
            identity = _string(bot.get("identity"), f"{bot_context}.identity")
            actor = _string(bot.get("actor"), f"{bot_context}.actor")
            if not identity or identity in seen:
                raise CollisionError(f"{bot_context}.identity is missing or duplicated")
            seen.add(identity)
            counters = {name: _integer(bot.get(name), f"{bot_context}.{name}") for name in COUNTERS}
            physics = _string(bot.get("physics_mode"), f"{bot_context}.physics_mode")
            latent = _string(bot.get("latent_action"), f"{bot_context}.latent_action")
            target_identity = _string(bot.get("move_target_identity"), f"{bot_context}.move_target_identity")
            position = _position(bot.get("position"), f"{bot_context}.position")
            prior = previous.get(identity)
            if prior is None:
                previous[identity] = {"counters": counters, "physics": physics, "actor": actor}
                continue
            if prior["actor"] != actor:
                raise CollisionError(f"{bot_context}.actor changed for {identity}")
            deltas = {name: counters[name] - prior["counters"][name] for name in COUNTERS}
            if any(value < 0 for value in deltas.values()):
                raise CollisionError(f"{bot_context} cumulative counter regressed")
            if deltas["pain_ledge_recovery_attempts_exact"] > 1 \
                    or deltas["pain_ledge_recovery_escapes_exact"] > 1:
                raise CollisionError(f"{bot_context} recovery counter advanced by more than one sample")
            death_delta = deltas["deaths_exact"]
            if death_delta:
                close_episode(identity)
                close_recovery(identity, "life_boundary_censored", tick, seconds)
            if deltas["pain_ledge_recovery_attempts_exact"]:
                close_recovery(identity, "superseded_by_next_attempt", tick, seconds)
                attempt_id = recovery_ids.get(identity, 0) + 1
                recovery_ids[identity] = attempt_id
                active_recoveries[identity] = _RecoveryLifecycle(
                    identity, actor, attempt_id, tick, seconds)
            escaped_this_sample = deltas["pain_ledge_recovery_escapes_exact"] != 0
            if escaped_this_sample:
                if identity not in active_recoveries:
                    raise CollisionError(f"{bot_context} escape has no active recovery attempt")

            contacts = deltas["hit_wall_events_exact"]
            if contacts:
                if death_delta:
                    phase = "life_boundary_unattributable"
                elif deltas["pain_ledge_recovery_attempts_exact"]:
                    phase = "recovery_transition"
                elif physics == "Falling" or prior["physics"] == "Falling":
                    phase = "falling_or_landing"
                elif physics == "Walking":
                    phase = "ordinary_walking"
                else:
                    phase = "ordinary_other"
                partition[phase] += contacts
                recovery = active_recoveries.get(identity)
                if recovery is not None:
                    recovery.contacts_exact += contacts
                    recovery.contact_ticks += 1
                episode = active_episodes.get(identity)
                if episode is not None and tick - episode.contacts[-1].tick > contact_gap_ticks:
                    close_episode(identity)
                    episode = None
                if episode is None:
                    episode_id = episode_ids.get(identity, 0) + 1
                    episode_ids[identity] = episode_id
                    episode = _Episode(identity, actor, episode_id)
                    active_episodes[identity] = episode
                episode.add(_Contact(tick, seconds, contacts, phase, physics, latent,
                                     bool(target_identity), position))
            if escaped_this_sample:
                close_recovery(identity, "escaped", tick, seconds)
            previous[identity] = {"counters": counters, "physics": physics, "actor": actor}

    if event_count == 0:
        raise CollisionError("events stream is empty")
    if not complete:
        raise CollisionError("events stream lacks a successful complete run_result")
    for identity in list(active_episodes):
        close_episode(identity)
    for identity in list(active_recoveries):
        close_recovery(identity, "run_end_censored", previous_tick, previous_seconds)

    episode_records, omitted_episodes = _bounded_records(completed_episodes, maximum_records, _record_sort_key)
    recovery_records, omitted_recoveries = _bounded_records(
        completed_recoveries, maximum_records,
        lambda record: (record["identity"], int(record["attempt_tick"])))
    total_callbacks = sum(partition.values())
    if total_callbacks != sum(int(record["callbacks_exact"]) for record in completed_episodes):
        raise CollisionError("collision episode total does not reconcile with contact partition")
    return {
        "schema": REPORT_SCHEMA,
        "contact_gap_ticks": str(contact_gap_ticks),
        "wall_loop_minimum_contacts": str(wall_loop_minimum_contacts),
        "wall_loop_maximum_horizontal_displacement": wall_loop_maximum_horizontal_displacement,
        "totals": {
            "hit_wall_callbacks_exact": str(total_callbacks),
            "recovery_transition_callbacks_exact": str(partition["recovery_transition"]),
            "falling_or_landing_callbacks_exact": str(partition["falling_or_landing"]),
            "ordinary_walking_callbacks_exact": str(partition["ordinary_walking"]),
            "ordinary_other_callbacks_exact": str(partition["ordinary_other"]),
            "life_boundary_unattributable_callbacks_exact": str(partition["life_boundary_unattributable"]),
            "collision_episodes_exact": str(len(completed_episodes)),
            "wall_loop_candidates_exact": str(sum(record["wall_loop_candidate"] for record in completed_episodes)),
            "recovery_lifecycles_exact": str(len(completed_recoveries)),
            "recovery_lifecycle_temporal_window_callbacks_exact": str(
                sum(int(record["temporal_window_contact_callbacks_exact"])
                    for record in completed_recoveries)),
        },
        "episodes": episode_records,
        "episode_records_omitted_exact": str(omitted_episodes),
        "recovery_lifecycles": recovery_records,
        "recovery_lifecycle_records_omitted_exact": str(omitted_recoveries),
        "attribution_limits": [
            "Only a contact sampled with a recovery-attempt counter transition is recovery_transition.",
            "Recovery lifecycle windows are temporal association, not causal attribution.",
            "A wall-loop candidate is a conservative stationary walking Continue contact run, not proof of a script loop.",
        ],
    }


def _event_stream(path: Path) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise CollisionError(f"{path}:{line_number}: invalid JSON") from error
            if not isinstance(event, dict):
                raise CollisionError(f"{path}:{line_number}: event must be an object")
            yield event


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--contact-gap-ticks", type=int, default=2)
    parser.add_argument("--wall-loop-minimum-contacts", type=int, default=3)
    parser.add_argument("--wall-loop-maximum-horizontal-displacement", type=float, default=16.0)
    parser.add_argument("--maximum-records", type=int, default=256)
    args = parser.parse_args()
    try:
        report = analyze_events(
            _event_stream(args.run_directory / "events.jsonl"),
            contact_gap_ticks=args.contact_gap_ticks,
            wall_loop_minimum_contacts=args.wall_loop_minimum_contacts,
            wall_loop_maximum_horizontal_displacement=args.wall_loop_maximum_horizontal_displacement,
            maximum_records=args.maximum_records)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (CollisionError, OSError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
