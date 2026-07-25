#!/usr/bin/env python3
"""Fail-closed causal summaries for positive-DPS bot residences.

The benchmark already emits authoritative per-sample hazard, movement, and
exact-counter data.  This tool turns those samples into bounded residence
episodes without changing gameplay.  It is deliberately an attribution aid,
not a policy selector: a candidate can be observed, absent, or superseded,
but the report never claims that an unobserved route was safe.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


REPORT_SCHEMA = "surreal-hazard-residence-analysis-v1"
TELEMETRY_SCHEMAS = {
    "surreal-bot-benchmark-telemetry-v1",
    "surreal-bot-benchmark-telemetry-v2",
}
COUNTERS = (
    "hazard_residence_episodes_exact",
    "hazard_residence_cleared_exact",
    "hazard_residence_deaths_exact",
    "hazard_residence_life_boundary_censored_exact",
    "hazard_residence_run_end_censored_exact",
    "hazard_residence_unknown_exact",
    "hazard_residence_reentries_exact",
    "hazard_residence_command_changes_exact",
    "hazard_residence_candidates_observed_exact",
    "hazard_residence_candidate_other_commands_exact",
)

TERMINAL_COUNTERS = {
    "cleared": "hazard_residence_cleared_exact",
    "death": "hazard_residence_deaths_exact",
    "run_end_censored": "hazard_residence_run_end_censored_exact",
}


class ResidenceError(ValueError):
    """The input is incomplete, malformed, or cannot support an attribution."""


def _integer(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ResidenceError(f"{context} must be a non-negative integer")
    try:
        result = int(value)
    except ValueError as error:
        raise ResidenceError(f"{context} must be a non-negative integer") from error
    if result < 0:
        raise ResidenceError(f"{context} must be a non-negative integer")
    return result


def _signed_integer(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ResidenceError(f"{context} must be an integer")
    try:
        return int(value)
    except ValueError as error:
        raise ResidenceError(f"{context} must be an integer") from error


def _number(value: Any, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ResidenceError(f"{context} must be a finite number")
    result = float(value)
    if not math.isfinite(result):
        raise ResidenceError(f"{context} must be a finite number")
    return result


def _boolean(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise ResidenceError(f"{context} must be boolean")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str):
        raise ResidenceError(f"{context} must be a string")
    return value


def _position(value: Any, context: str) -> dict[str, float]:
    if not isinstance(value, dict):
        raise ResidenceError(f"{context} must be an object")
    return {axis: _number(value.get(axis), f"{context}.{axis}") for axis in ("x", "y", "z")}


def _counter_snapshot(bot: dict[str, Any], context: str) -> dict[str, int]:
    return {name: _integer(bot.get(name), f"{context}.{name}") for name in COUNTERS}


@dataclass
class _Episode:
    identity: str
    actor: str
    episode_id: int
    entry_tick: int
    entry_seconds: float
    entry_position: dict[str, float]
    entry_health: int
    entry_physics: str
    entry_state: str
    entry_latent_action: str
    entry_move_target: str
    counters: dict[str, int]
    last_harmful_seconds: float
    terminal: str = "run_end_censored"
    terminal_tick: int = 0
    terminal_seconds: float = 0.0
    terminal_position: dict[str, float] = field(default_factory=dict)
    terminal_health: int = 0
    terminal_move_target: str = ""
    terminal_latent_action: str = ""
    hazard_samples: int = 0
    reentries: int = 0
    target_changes: int = 0
    latent_action_changes: int = 0
    previous_target: str = ""
    previous_latent_action: str = ""
    clearance_started_seconds: float | None = None
    candidate_names: set[str] = field(default_factory=set)

    def observe(self, *, harmful: bool, tick: int, seconds: float, position: dict[str, float],
                health: int, move_target: str, latent_action: str) -> None:
        if harmful:
            self.hazard_samples += 1
            self.last_harmful_seconds = seconds
            if self.clearance_started_seconds is not None:
                self.reentries += 1
                self.clearance_started_seconds = None
        elif self.clearance_started_seconds is None:
            self.clearance_started_seconds = seconds
        if move_target != self.previous_target:
            self.target_changes += 1
            self.previous_target = move_target
        if latent_action != self.previous_latent_action:
            self.latent_action_changes += 1
            self.previous_latent_action = latent_action
        self.terminal_tick = tick
        self.terminal_seconds = seconds
        self.terminal_position = position
        self.terminal_health = health
        self.terminal_move_target = move_target
        self.terminal_latent_action = latent_action

    def observe_candidate_name(self, name: str) -> None:
        if name:
            self.candidate_names.add(name)

    def report(self, counters: dict[str, int]) -> dict[str, Any]:
        deltas = {name: counters[name] - self.counters[name] for name in COUNTERS}
        if any(value < 0 for value in deltas.values()):
            raise ResidenceError(f"{self.identity}: residence counters regressed within an episode")
        return {
            "identity": self.identity,
            "actor": self.actor,
            "episode_id": str(self.episode_id),
            "entry_tick": str(self.entry_tick),
            "entry_seconds": self.entry_seconds,
            "entry_position": self.entry_position,
            "entry_health": self.entry_health,
            "entry_physics": self.entry_physics,
            "entry_state": self.entry_state,
            "entry_latent_action": self.entry_latent_action,
            "entry_move_target": self.entry_move_target,
            "harmful_seconds": max(0.0, self.last_harmful_seconds - self.entry_seconds),
            "hazard_samples": str(self.hazard_samples),
            "reentries": str(self.reentries),
            "target_changes": str(self.target_changes),
            "latent_action_changes": str(self.latent_action_changes),
            "candidate_observed": deltas[
                "hazard_residence_candidates_observed_exact"] > 0,
            "candidate_superseded": deltas[
                "hazard_residence_candidate_other_commands_exact"] > 0,
            "candidate_names": sorted(self.candidate_names),
            "command_change_counter_delta": str(deltas[
                "hazard_residence_command_changes_exact"]),
            "terminal": self.terminal,
            "terminal_tick": str(self.terminal_tick),
            "terminal_seconds": self.terminal_seconds,
            "terminal_position": self.terminal_position,
            "terminal_health": self.terminal_health,
            "terminal_move_target": self.terminal_move_target,
            "terminal_latent_action": self.terminal_latent_action,
        }


def analyze_events(events: list[dict[str, Any]], clearance_grace_seconds: float = 0.25) -> dict[str, Any]:
    if not math.isfinite(clearance_grace_seconds) or clearance_grace_seconds < 0.0:
        raise ResidenceError("clearance grace must be a finite non-negative number")
    if not events:
        raise ResidenceError("events stream is empty")
    active: dict[str, _Episode] = {}
    sequence: dict[str, int] = {}
    prior_deaths: dict[str, int] = {}
    final_counters: dict[str, dict[str, int]] = {}
    completed: list[dict[str, Any]] = []
    prior_tick = -1
    prior_seconds = -1.0
    complete = False

    for event_index, event in enumerate(events):
        context = f"events[{event_index}]"
        if not isinstance(event, dict):
            raise ResidenceError(f"{context} must be an object")
        if event.get("schema") not in TELEMETRY_SCHEMAS:
            raise ResidenceError(f"{context}.schema is unsupported")
        tick = _integer(event.get("tick"), f"{context}.tick")
        seconds = _number(event.get("simulated_seconds"), f"{context}.simulated_seconds")
        if tick < prior_tick or seconds < prior_seconds:
            raise ResidenceError(f"{context} time regressed")
        prior_tick, prior_seconds = tick, seconds
        event_type = _string(event.get("type"), f"{context}.type")
        bots = event.get("bots")
        if not isinstance(bots, list):
            raise ResidenceError(f"{context}.bots must be an array")
        if event_type == "run_result":
            complete = event.get("status") == "complete" and not event.get("failure_reason")

        for bot_index, bot in enumerate(bots):
            bot_context = f"{context}.bots[{bot_index}]"
            if not isinstance(bot, dict):
                raise ResidenceError(f"{bot_context} must be an object")
            identity = _string(bot.get("identity"), f"{bot_context}.identity")
            actor = _string(bot.get("actor"), f"{bot_context}.actor")
            harmful = _boolean(bot.get("in_hazard_zone"), f"{bot_context}.in_hazard_zone")
            position = _position(bot.get("position"), f"{bot_context}.position")
            health = _signed_integer(bot.get("health"), f"{bot_context}.health")
            deaths = _integer(bot.get("deaths_exact"), f"{bot_context}.deaths_exact")
            physics = _string(bot.get("physics_mode"), f"{bot_context}.physics_mode")
            state = _string(bot.get("state"), f"{bot_context}.state")
            latent = _string(bot.get("latent_action"), f"{bot_context}.latent_action")
            target = _string(bot.get("move_target_name"), f"{bot_context}.move_target_name")
            counters = _counter_snapshot(bot, bot_context)
            final_counters[identity] = counters
            candidate_name = _string(bot.get("hazard_swim_egress_direct_nav_best_candidate_name"),
                f"{bot_context}.hazard_swim_egress_direct_nav_best_candidate_name")
            if deaths < prior_deaths.get(identity, deaths):
                raise ResidenceError(f"{bot_context}.deaths_exact regressed")
            died = deaths > prior_deaths.get(identity, deaths)
            prior_deaths[identity] = deaths
            episode = active.get(identity)
            if episode is None and harmful and health > 0:
                episode_id = sequence.get(identity, 0) + 1
                sequence[identity] = episode_id
                episode = _Episode(identity, actor, episode_id, tick, seconds, position, health,
                    physics, state, latent, target, counters, seconds,
                    previous_target=target, previous_latent_action=latent)
                active[identity] = episode
            if episode is None:
                continue
            episode.observe(harmful=harmful, tick=tick, seconds=seconds, position=position,
                health=health, move_target=target, latent_action=latent)
            episode.observe_candidate_name(candidate_name)
            terminal = "death" if died else None
            if terminal is None and episode.clearance_started_seconds is not None and (
                    seconds - episode.clearance_started_seconds >= clearance_grace_seconds):
                terminal = "cleared"
            if terminal:
                episode.terminal = terminal
                completed.append(episode.report(counters))
                del active[identity]

    if not complete:
        raise ResidenceError("events stream has no successful complete run_result")
    for identity, episode in active.items():
        episode.terminal = "run_end_censored"
        completed.append(episode.report(final_counters[identity]))
    _certify_native_reconstruction(completed, final_counters)
    totals = {
        "episodes": str(len(completed)),
        "deaths": str(sum(item["terminal"] == "death" for item in completed)),
        "cleared": str(sum(item["terminal"] == "cleared" for item in completed)),
        "run_end_censored": str(sum(item["terminal"] == "run_end_censored" for item in completed)),
        "candidate_observed": str(sum(item["candidate_observed"] for item in completed)),
        "candidate_superseded": str(sum(item["candidate_superseded"] for item in completed)),
        "no_candidate": str(sum(not item["candidate_observed"] for item in completed)),
    }
    return {"schema": REPORT_SCHEMA, "clearance_grace_seconds": clearance_grace_seconds,
            "totals": totals, "episodes": completed}


def _certify_native_reconstruction(episodes: list[dict[str, Any]],
                                   final_counters: dict[str, dict[str, int]]) -> None:
    """Require the independent Python reconstruction to partition native totals."""
    reconstructed: dict[str, dict[str, int]] = {}
    for episode in episodes:
        identity = _string(episode.get("identity"), "episode.identity")
        totals = reconstructed.setdefault(identity, {name: 0 for name in COUNTERS})
        totals["hazard_residence_episodes_exact"] += 1
        terminal = _string(episode.get("terminal"), f"{identity}.terminal")
        counter = TERMINAL_COUNTERS.get(terminal)
        if counter is None:
            raise ResidenceError(f"{identity}: unsupported reconstructed terminal {terminal!r}")
        totals[counter] += 1
        totals["hazard_residence_reentries_exact"] += _integer(
            episode.get("reentries"), f"{identity}.reentries")
        totals["hazard_residence_command_changes_exact"] += _integer(
            episode.get("command_change_counter_delta"),
            f"{identity}.command_change_counter_delta")
        if _boolean(episode.get("candidate_observed"), f"{identity}.candidate_observed"):
            totals["hazard_residence_candidates_observed_exact"] += 1
        if _boolean(episode.get("candidate_superseded"), f"{identity}.candidate_superseded"):
            totals["hazard_residence_candidate_other_commands_exact"] += 1

    for identity, counters in final_counters.items():
        actual = reconstructed.get(identity, {name: 0 for name in COUNTERS})
        for unsupported in (
                "hazard_residence_life_boundary_censored_exact",
                "hazard_residence_unknown_exact"):
            if counters[unsupported] != 0:
                raise ResidenceError(
                    f"{identity}: native {unsupported} is not reconstructable from telemetry")
        for name in COUNTERS:
            if name in (
                    "hazard_residence_life_boundary_censored_exact",
                    "hazard_residence_unknown_exact"):
                continue
            if actual[name] != counters[name]:
                raise ResidenceError(
                    f"{identity}: reconstructed {name}={actual[name]} does not match "
                    f"native {counters[name]}")


def _read_events(path: Path) -> list[dict[str, Any]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ResidenceError(f"cannot read {path}: {error}") from error
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(lines, 1):
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise ResidenceError(f"{path}:{line_number}: invalid JSON: {error.msg}") from error
    return events


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path, help="benchmark run directory containing events.jsonl")
    parser.add_argument("--output", type=Path, required=True, help="external report path")
    parser.add_argument("--clearance-grace-seconds", type=float, default=0.25)
    args = parser.parse_args(argv)
    try:
        report = analyze_events(_read_events(args.run / "events.jsonl"), args.clearance_grace_seconds)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except ResidenceError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
