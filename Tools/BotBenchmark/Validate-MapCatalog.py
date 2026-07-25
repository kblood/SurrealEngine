#!/usr/bin/env python3
"""Fail-closed validation for owner-local map-catalog v2 artifacts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


class CatalogError(ValueError):
    pass


REACH_FLAGS = (
    (1, "walk"), (2, "fly"), (4, "swim"), (8, "jump"),
    (16, "door"), (32, "special"), (64, "player_only"),
)
KNOWN_REACH_FLAGS = sum(flag for flag, _name in REACH_FLAGS)
TRAVERSAL_REFERENCES = {
    "lift_center": ("mover_actor_index", "recommended_trigger_actor_index"),
    "lift_exit": ("mover_actor_index", "recommended_trigger_actor_index"),
    "mover": (
        "marker_actor_index", "recommended_trigger_actor_index",
        "trigger_actor_index", "trigger_actor2_index", "leader_actor_index",
        "follower_actor_index",
    ),
    "teleporter": ("trigger_actor_index", "trigger_actor2_index"),
    "warp_zone_marker": (
        "marked_warp_zone_actor_index", "trigger_actor_index", "trigger_actor2_index",
    ),
    "warp_zone_info": ("other_side_actor_index",),
    "inventory_spot": ("marked_item_actor_index",),
    "player_start": (),
}


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CatalogError(f"{context} must be an object")
    return value


def _exact(value: Any, context: str, fields: set[str]) -> dict[str, Any]:
    result = _object(value, context)
    missing = fields - result.keys()
    extra = result.keys() - fields
    if missing:
        raise CatalogError(f"{context} is missing fields: {', '.join(sorted(missing))}")
    if extra:
        raise CatalogError(f"{context} has unexpected fields: {', '.join(sorted(extra))}")
    return result


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str):
        raise CatalogError(f"{context} must be a string")
    return value


def _boolean(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise CatalogError(f"{context} must be a boolean")
    return value


def _integer(value: Any, context: str, minimum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CatalogError(f"{context} must be an integer")
    if minimum is not None and value < minimum:
        raise CatalogError(f"{context} must be at least {minimum}")
    return value


def _number(value: Any, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CatalogError(f"{context} must be a number")
    return float(value)


def _vector(value: Any, context: str) -> None:
    fields = _exact(value, context, {"x", "y", "z"})
    for component in ("x", "y", "z"):
        _number(fields[component], f"{context}.{component}")


def _package_identity(value: Any, context: str) -> None:
    fields = _exact(value, context, {"name", "file_name", "package_version", "licensee_mode", "sha1"})
    _string(fields["name"], f"{context}.name")
    _string(fields["file_name"], f"{context}.file_name")
    _integer(fields["package_version"], f"{context}.package_version", 0)
    _integer(fields["licensee_mode"], f"{context}.licensee_mode", 0)
    sha1 = _string(fields["sha1"], f"{context}.sha1")
    if len(sha1) != 40 or any(character not in "0123456789abcdefABCDEF" for character in sha1):
        raise CatalogError(f"{context}.sha1 must be a SHA-1 hexadecimal digest")


def _actor_reference(value: Any, context: str, actors: dict[int, dict[str, Any]]) -> None:
    if value is None:
        return
    index = _integer(value, context, 0)
    if index not in actors or not actors[index]["present"]:
        raise CatalogError(f"{context} must refer to a present catalog actor")


def validate_catalog(value: Any) -> dict[str, int]:
    root = _exact(value, "catalog", {
        "schema", "game", "map", "map_package", "counts", "actors",
        "navigation_points", "reachspecs", "traversal_actors", "zones",
    })
    if root["schema"] != "surreal-map-catalog-spike-v2":
        raise CatalogError("catalog.schema must be surreal-map-catalog-spike-v2")
    game = _exact(root["game"], "catalog.game", {"name", "version"})
    _string(game["name"], "catalog.game.name")
    _string(game["version"], "catalog.game.version")
    _string(root["map"], "catalog.map")
    _package_identity(root["map_package"], "catalog.map_package")
    counts = _exact(root["counts"], "catalog.counts", {
        "actors_exact", "navigation_points_exact", "reachspecs_exact", "zones_exact",
    })
    for name, raw in counts.items():
        _integer(raw, f"catalog.counts.{name}", 0)

    raw_actors = root["actors"]
    if not isinstance(raw_actors, list) or len(raw_actors) != counts["actors_exact"]:
        raise CatalogError("catalog.actors must contain exactly actors_exact slots")
    actors: dict[int, dict[str, Any]] = {}
    for expected_index, raw_actor in enumerate(raw_actors):
        actor = _object(raw_actor, f"catalog.actors[{expected_index}]")
        present = _boolean(actor.get("present"), f"catalog.actors[{expected_index}].present")
        expected = {"actor_index", "present"}
        if present:
            expected |= {"name", "class"}
        actor = _exact(actor, f"catalog.actors[{expected_index}]", expected)
        index = _integer(actor["actor_index"], f"catalog.actors[{expected_index}].actor_index", 0)
        if index != expected_index:
            raise CatalogError("catalog.actors must be ordered by complete level actor index")
        if present:
            _string(actor["name"], f"catalog.actors[{expected_index}].name")
            _string(actor["class"], f"catalog.actors[{expected_index}].class")
        actors[index] = actor

    raw_specs = root["reachspecs"]
    if not isinstance(raw_specs, list) or len(raw_specs) != counts["reachspecs_exact"]:
        raise CatalogError("catalog.reachspecs must contain exactly reachspecs_exact records")
    specs: list[dict[str, Any]] = []
    for expected_index, raw_spec in enumerate(raw_specs):
        spec = _exact(raw_spec, f"catalog.reachspecs[{expected_index}]", {
            "index", "start_actor_index", "end_actor_index", "distance",
            "collision_radius", "collision_height", "reach_flags", "reach_flag_names",
            "unknown_reach_flags", "pruned",
        })
        if _integer(spec["index"], f"catalog.reachspecs[{expected_index}].index", 0) != expected_index:
            raise CatalogError("catalog.reachspecs must be ordered by reachspec index")
        _actor_reference(spec["start_actor_index"], f"catalog.reachspecs[{expected_index}].start_actor_index", actors)
        _actor_reference(spec["end_actor_index"], f"catalog.reachspecs[{expected_index}].end_actor_index", actors)
        for field in ("distance", "collision_radius", "collision_height"):
            _integer(spec[field], f"catalog.reachspecs[{expected_index}].{field}", 0)
        flags = _integer(spec["reach_flags"], f"catalog.reachspecs[{expected_index}].reach_flags")
        unknown = _integer(spec["unknown_reach_flags"], f"catalog.reachspecs[{expected_index}].unknown_reach_flags", 0)
        expected_names = [name for flag, name in REACH_FLAGS if (flags & flag) != 0]
        if spec["reach_flag_names"] != expected_names:
            raise CatalogError(f"catalog.reachspecs[{expected_index}].reach_flag_names do not decode reach_flags")
        if unknown != (flags & 0xffffffff) & ~KNOWN_REACH_FLAGS:
            raise CatalogError(f"catalog.reachspecs[{expected_index}].unknown_reach_flags do not reconcile")
        _boolean(spec["pruned"], f"catalog.reachspecs[{expected_index}].pruned")
        specs.append(spec)

    raw_points = root["navigation_points"]
    if not isinstance(raw_points, list) or len(raw_points) != counts["navigation_points_exact"]:
        raise CatalogError("catalog.navigation_points must contain exactly navigation_points_exact records")
    points: dict[int, dict[str, Any]] = {}
    point_fields = {
        "actor_index", "name", "class", "position", "collision_radius", "collision_height",
        "extra_cost", "end_point", "end_point_only", "never_use_strafing", "one_way",
        "player_only", "special_cost", "paths", "upstream_paths", "pruned_paths",
        "visible_no_reach_actor_indexes",
    }
    for offset, raw_point in enumerate(raw_points):
        point = _exact(raw_point, f"catalog.navigation_points[{offset}]", point_fields)
        index = _integer(point["actor_index"], f"catalog.navigation_points[{offset}].actor_index", 0)
        if index in points or index not in actors or not actors[index]["present"]:
            raise CatalogError("catalog.navigation_points must use unique present actor indexes")
        _string(point["name"], f"catalog.navigation_points[{offset}].name")
        _string(point["class"], f"catalog.navigation_points[{offset}].class")
        _vector(point["position"], f"catalog.navigation_points[{offset}].position")
        for field in ("collision_radius", "collision_height", "extra_cost"):
            _number(point[field], f"catalog.navigation_points[{offset}].{field}")
        for field in ("end_point", "end_point_only", "never_use_strafing", "one_way", "player_only", "special_cost"):
            _boolean(point[field], f"catalog.navigation_points[{offset}].{field}")
        points[index] = point
    for point_index, point in points.items():
        for field, endpoint in (("paths", "start_actor_index"), ("pruned_paths", "start_actor_index"),
                                ("upstream_paths", "end_actor_index")):
            values = point[field]
            if not isinstance(values, list):
                raise CatalogError(f"catalog.navigation_points[{point_index}].{field} must be an array")
            for spec_index in values:
                spec_index = _integer(spec_index, f"catalog.navigation_points[{point_index}].{field}[]", 0)
                if spec_index >= len(specs) or specs[spec_index][endpoint] != point_index:
                    raise CatalogError(f"catalog.navigation_points[{point_index}].{field} has an invalid directed reachspec")
        visible = point["visible_no_reach_actor_indexes"]
        if not isinstance(visible, list):
            raise CatalogError(f"catalog.navigation_points[{point_index}].visible_no_reach_actor_indexes must be an array")
        for target in visible:
            if _integer(target, f"catalog.navigation_points[{point_index}].visible_no_reach_actor_indexes[]", 0) not in points:
                raise CatalogError("visible-no-reach references must resolve to catalog navigation points")

    raw_traversal = root["traversal_actors"]
    if not isinstance(raw_traversal, list):
        raise CatalogError("catalog.traversal_actors must be an array")
    seen_traversal: set[int] = set()
    for offset, raw_record in enumerate(raw_traversal):
        record = _object(raw_record, f"catalog.traversal_actors[{offset}]")
        kind = _string(record.get("kind"), f"catalog.traversal_actors[{offset}].kind")
        if kind not in TRAVERSAL_REFERENCES:
            raise CatalogError(f"catalog.traversal_actors[{offset}].kind is not recognized")
        index = _integer(record.get("actor_index"), f"catalog.traversal_actors[{offset}].actor_index", 0)
        if index in seen_traversal or index not in actors or not actors[index]["present"]:
            raise CatalogError("catalog.traversal_actors must use unique present actor indexes")
        seen_traversal.add(index)
        _string(record.get("name"), f"catalog.traversal_actors[{offset}].name")
        _string(record.get("class"), f"catalog.traversal_actors[{offset}].class")
        for reference in TRAVERSAL_REFERENCES[kind]:
            if reference not in record:
                raise CatalogError(f"catalog.traversal_actors[{offset}] is missing {reference}")
            _actor_reference(record[reference], f"catalog.traversal_actors[{offset}].{reference}", actors)

    raw_zones = root["zones"]
    if not isinstance(raw_zones, list) or len(raw_zones) != counts["zones_exact"]:
        raise CatalogError("catalog.zones must contain exactly zones_exact records")
    for offset, raw_zone in enumerate(raw_zones):
        zone = _exact(raw_zone, f"catalog.zones[{offset}]", {
            "actor_index", "name", "class", "pain", "water", "kill",
            "damage_per_second", "gravity", "velocity",
        })
        _actor_reference(zone["actor_index"], f"catalog.zones[{offset}].actor_index", actors)
        for field in ("name", "class"):
            _string(zone[field], f"catalog.zones[{offset}].{field}")
        for field in ("pain", "water", "kill"):
            _boolean(zone[field], f"catalog.zones[{offset}].{field}")
        _number(zone["damage_per_second"], f"catalog.zones[{offset}].damage_per_second")
        _vector(zone["gravity"], f"catalog.zones[{offset}].gravity")
        _vector(zone["velocity"], f"catalog.zones[{offset}].velocity")
    return {
        "actors_exact": len(actors), "navigation_points_exact": len(points),
        "reachspecs_exact": len(specs), "traversal_actors_exact": len(raw_traversal),
        "zones_exact": len(raw_zones),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    args = parser.parse_args()
    try:
        value = json.loads(args.catalog.read_text(encoding="utf-8"))
        print(json.dumps(validate_catalog(value), sort_keys=True))
    except (OSError, json.JSONDecodeError, CatalogError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
