"""Deterministic contract checks for the storage robustness harness."""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qualify_storage_robustness as harness


def dataset(*values: str) -> dict:
	return {
		"datasetId": "synthetic-dataset",
		"version": 1,
		"fileCount": len(values),
		"totalBytes": sum(len(value) for value in values),
		"values": {f"path-{index}": value for index, value in enumerate(values)},
	}


class StorageRobustnessContractTests(unittest.TestCase):
	def test_report_and_probe_schemas_are_explicit(self) -> None:
		self.assertEqual(harness.REPORT_SCHEMA, "surrealengine-storage-robustness-report")
		self.assertEqual(harness.REPORT_VERSION, 2)
		self.assertEqual(harness.PROBE_SCHEMA, "surrealengine-storage-robustness-probe")
		self.assertEqual(harness.PROBE_VERSION, 2)

	def test_generation_check_accepts_only_complete_synthetic_generation(self) -> None:
		marker = harness.SYNTHETIC_MARKER
		harness.assert_tag(dataset(marker + ":old", marker + ":old"), "old", "complete")
		with self.assertRaisesRegex(AssertionError, "wrong or non-synthetic generation"):
			harness.assert_tag(dataset(marker + ":old", marker + ":new"), "old", "mixed")
		with self.assertRaisesRegex(AssertionError, "wrong or non-synthetic generation"):
			harness.assert_tag(dataset("not synthetic:old"), "old", "foreign")
		with self.assertRaisesRegex(AssertionError, "missing"):
			harness.assert_tag(None, "old", "absent")

	def test_fixture_does_not_name_production_storage_namespaces(self) -> None:
		probe = harness.PROBE_PATH.read_text(encoding="utf-8")
		self.assertNotIn('"surrealengine-mutable-data-v1"', probe)
		self.assertNotIn('"surrealengine-ut99-data-v1"', probe)
		self.assertIn("se-storage-robustness-", probe)
		self.assertIn(harness.SYNTHETIC_MARKER, probe)

	def test_fixture_loads_only_runtime_apis_and_the_test_probe(self) -> None:
		fixture = harness.FIXTURE_PATH.read_text(encoding="utf-8")
		self.assertIn('src="ut99_importer.js"', fixture)
		self.assertIn('src="mutable_persistence.js"', fixture)
		self.assertIn('src="storage_robustness_probe.js"', fixture)
		self.assertNotIn("index_webxr", fixture)

	def test_migration_case_requires_real_interruption_and_namespace_separation(self) -> None:
		probe = harness.PROBE_PATH.read_text(encoding="utf-8")
		driver = harness.DRIVER_PATH.read_text(encoding="utf-8")
		self.assertIn('migrationHook: phase =>', probe)
		self.assertIn('reachedBeforePublish', probe)
		self.assertIn('inspectMigrationPointer', probe)
		self.assertIn('retryAndInspectMigration', probe)
		self.assertIn('mutable-v1-to-v2-migration-recovery', driver)
		self.assertIn('importerDatasetIdUnchanged', driver)


if __name__ == "__main__":
	unittest.main()
