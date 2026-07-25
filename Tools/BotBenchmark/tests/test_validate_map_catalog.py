import copy
import importlib.util
import unittest


SPEC = importlib.util.spec_from_file_location(
    "validate_map_catalog", "Tools/BotBenchmark/Validate-MapCatalog.py")
VALIDATE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATE)


def catalog() -> dict:
    return {
        "schema": "surreal-map-catalog-spike-v3",
        "game": {"name": "Unreal Tournament", "version": "436"},
        "map": "DM-Test",
        "map_package": {
            "name": "DM-Test", "file_name": "DM-Test.unr", "package_version": 68,
            "licensee_mode": 0, "sha1": "0" * 40,
        },
        "counts": {"actors_exact": 1, "navigation_points_exact": 1,
                   "reachspecs_exact": 0, "zones_exact": 0},
        "actors": [{"actor_index": 0, "present": True, "name": "PathNode0", "class": "Engine.PathNode"}],
        "navigation_points": [{
            "actor_index": 0, "name": "PathNode0", "class": "Engine.PathNode",
            "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "collision_radius": 40.0,
            "collision_height": 40.0, "extra_cost": 0, "resolved_zone_actor_index": None, "end_point": False,
            "end_point_only": False, "never_use_strafing": False, "one_way": False,
            "player_only": False, "special_cost": False, "paths": [], "upstream_paths": [],
            "pruned_paths": [], "visible_no_reach_actor_indexes": [],
        }],
        "reachspecs": [],
        "traversal_actors": [{
            "actor_index": 0, "name": "PathNode0", "class": "Engine.PathNode", "kind": "player_start",
        }],
        "zones": [],
        "zone_graph": [],
    }


class MapCatalogValidatorTests(unittest.TestCase):
    def test_accepts_minimal_complete_catalog(self) -> None:
        self.assertEqual(VALIDATE.validate_catalog(catalog())["actors_exact"], 1)

    def test_rejects_dangling_traversal_actor(self) -> None:
        value = copy.deepcopy(catalog())
        value["traversal_actors"][0] = {
            "actor_index": 0, "name": "Lift", "class": "Engine.LiftCenter", "kind": "lift_center",
            "mover_actor_index": 1, "recommended_trigger_actor_index": None,
        }
        with self.assertRaisesRegex(VALIDATE.CatalogError, "present catalog actor"):
            VALIDATE.validate_catalog(value)

    def test_rejects_wrong_reachspec_direction(self) -> None:
        value = copy.deepcopy(catalog())
        value["counts"]["actors_exact"] = 2
        value["actors"].append({"actor_index": 1, "present": True,
                                "name": "PathNode1", "class": "Engine.PathNode"})
        value["counts"]["reachspecs_exact"] = 1
        value["reachspecs"] = [{
            "index": 0, "start_actor_index": 0, "end_actor_index": 1,
            "distance": 10, "collision_radius": 20, "collision_height": 30,
            "reach_flags": 1, "reach_flag_names": ["walk"], "unknown_reach_flags": 0,
            "pruned": False,
        }]
        value["navigation_points"][0]["upstream_paths"] = [0]
        with self.assertRaisesRegex(VALIDATE.CatalogError, "invalid directed reachspec"):
            VALIDATE.validate_catalog(value)

    def test_rejects_unknown_flag_mismatch(self) -> None:
        value = copy.deepcopy(catalog())
        value["counts"]["reachspecs_exact"] = 1
        value["reachspecs"] = [{
            "index": 0, "start_actor_index": 0, "end_actor_index": 0,
            "distance": 10, "collision_radius": 20, "collision_height": 30,
            "reach_flags": 128, "reach_flag_names": [], "unknown_reach_flags": 0,
            "pruned": False,
        }]
        with self.assertRaisesRegex(VALIDATE.CatalogError, "unknown_reach_flags do not reconcile"):
            VALIDATE.validate_catalog(value)

    def test_rejects_non_decimal_zone_graph_mask(self) -> None:
        value = copy.deepcopy(catalog())
        value["zone_graph"] = [{
            "zone_index": 0, "zone_actor_index": 0,
            "connectivity": "invalid", "visibility": "0",
        }]
        with self.assertRaisesRegex(VALIDATE.CatalogError, "unsigned decimal mask"):
            VALIDATE.validate_catalog(value)

    def test_accepts_v4_actor_locations_and_rejects_non_vector_location(self) -> None:
        value = copy.deepcopy(catalog())
        value["schema"] = "surreal-map-catalog-spike-v4"
        value["actors"][0]["location"] = {"x": 1.0, "y": 2.0, "z": 3.0}
        self.assertEqual(VALIDATE.validate_catalog(value)["actors_exact"], 1)
        value["actors"][0]["location"] = {"x": 1.0, "y": 2.0}
        with self.assertRaisesRegex(VALIDATE.CatalogError, "location"):
            VALIDATE.validate_catalog(value)


if __name__ == "__main__":
    unittest.main()
