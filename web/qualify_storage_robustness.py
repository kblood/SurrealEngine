"""Qualify browser-storage recovery using an owned, disposable Chromium profile.

This driver deliberately never visits a production storage profile. It uses
unique test-only database/directory names and synthetic byte markers, then
emits a versioned JSON report that distinguishes controlled desktop evidence
from physical Quest/browser qualification.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.parse
import uuid

from playwright.sync_api import Error as PlaywrightError
from playwright.sync_api import sync_playwright


REPORT_SCHEMA = "surrealengine-storage-robustness-report"
REPORT_VERSION = 1
PROBE_SCHEMA = "surrealengine-storage-robustness-probe"
PROBE_VERSION = 1
SYNTHETIC_MARKER = "SURREALENGINE_SYNTHETIC_STORAGE_TEST_ONLY"
DRIVER_PATH = Path(__file__).resolve()
FIXTURE_PATH = DRIVER_PATH.with_name("storage_robustness_fixture.html")
PROBE_PATH = DRIVER_PATH.with_name("storage_robustness_probe.js")
MUTABLE_RUNTIME_PATH = DRIVER_PATH.with_name("mutable_persistence.js")
IMPORTER_RUNTIME_PATH = DRIVER_PATH.with_name("ut99_importer.js")


def utc_now() -> str:
	return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as source:
		for block in iter(lambda: source.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def find_browser_executable(explicit: str | None) -> Path:
	if explicit:
		candidate = Path(explicit).expanduser().resolve()
		if candidate.is_file():
			return candidate
		raise FileNotFoundError(f"browser executable does not exist: {candidate}")

	candidates: list[Path] = []
	if os.name == "nt":
		for base in filter(None, [os.environ.get("PROGRAMFILES"), os.environ.get("PROGRAMFILES(X86)"), os.environ.get("LOCALAPPDATA")]):
			candidates.extend([
				Path(base) / "Google/Chrome/Application/chrome.exe",
				Path(base) / "BraveSoftware/Brave-Browser/Application/brave.exe",
				Path(base) / "Microsoft/Edge/Application/msedge.exe",
			])
	else:
		for command in ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "brave-browser"]:
			resolved = shutil.which(command)
			if resolved:
				candidates.append(Path(resolved))
	for candidate in candidates:
		if candidate.is_file():
			return candidate.resolve()
	raise FileNotFoundError("no supported Chrome/Chromium/Brave executable found; pass --browser-executable")


class OwnedBrowser:
	"""An externally launched browser whose exact process tree we may kill."""

	def __init__(self, playwright, executable: Path, profile: Path):
		self.playwright = playwright
		self.executable = executable
		self.profile = profile
		self.process: subprocess.Popen[bytes] | None = None
		self.browser = None
		self.context = None
		self.product = None

	def launch(self) -> None:
		if self.process is not None:
			raise RuntimeError("owned browser is already launched")
		active_port = self.profile / "DevToolsActivePort"
		try:
			active_port.unlink()
		except FileNotFoundError:
			pass
		command = [
			str(self.executable),
			f"--user-data-dir={self.profile}",
			"--remote-debugging-port=0",
			"--remote-debugging-address=127.0.0.1",
			"--headless=new",
			"--no-first-run",
			"--no-default-browser-check",
			"--disable-background-networking",
			"--disable-component-update",
			"about:blank",
		]
		popen_options = {
			"stdin": subprocess.DEVNULL,
			"stdout": subprocess.DEVNULL,
			"stderr": subprocess.DEVNULL,
		}
		if os.name == "nt":
			popen_options["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
		else:
			popen_options["start_new_session"] = True
		self.process = subprocess.Popen(command, **popen_options)
		deadline = time.monotonic() + 20.0
		last_error = "DevToolsActivePort was not created"
		while time.monotonic() < deadline:
			if self.process.poll() is not None:
				raise RuntimeError(f"browser exited during launch with code {self.process.returncode}")
			try:
				lines = active_port.read_text(encoding="utf-8").splitlines()
				port = int(lines[0])
				self.browser = self.playwright.chromium.connect_over_cdp(f"http://127.0.0.1:{port}")
				self.context = self.browser.contexts[0]
				session = self.browser.new_browser_cdp_session()
				self.product = session.send("Browser.getVersion")
				session.detach()
				return
			except (OSError, ValueError, IndexError, PlaywrightError) as error:
				last_error = str(error)
				time.sleep(0.1)
		self.force_kill()
		raise RuntimeError(f"could not attach to owned browser: {last_error}")

	@property
	def pid(self) -> int:
		if self.process is None:
			raise RuntimeError("browser is not launched")
		return self.process.pid

	def page(self, url: str):
		if self.context is None:
			raise RuntimeError("browser is not connected")
		page = self.context.new_page()
		page.goto(url, wait_until="load", timeout=30_000)
		page.wait_for_function("window.SurrealStorageRobustnessProbe !== undefined", timeout=10_000)
		return page

	def graceful_close(self) -> None:
		if self.browser is not None:
			# Browser.close on Playwright's CDP wrapper only disconnects the
			# client. Send Chromium's explicit shutdown command first so this is
			# a real orderly browser exit rather than a simulated context close.
			try:
				session = self.browser.new_browser_cdp_session()
				session.send("Browser.close")
			except PlaywrightError:
				# The transport commonly closes before the command response arrives.
				pass
		if self.process is not None:
			try:
				self.process.wait(timeout=15)
			except subprocess.TimeoutExpired as error:
				self.force_kill()
				raise RuntimeError("browser did not exit after Browser.close") from error
		self._forget()

	def force_kill(self) -> dict:
		if self.process is None:
			return {"method": "none", "pid": None, "exitCode": None}
		pid = self.process.pid
		method = "taskkill-process-tree" if os.name == "nt" else "SIGKILL-process-group"
		if self.process.poll() is None:
			if os.name == "nt":
				subprocess.run(
					["taskkill", "/PID", str(pid), "/T", "/F"],
					stdin=subprocess.DEVNULL,
					stdout=subprocess.DEVNULL,
					stderr=subprocess.DEVNULL,
					check=False,
					timeout=15,
				)
			else:
				try:
					os.killpg(pid, signal.SIGKILL)
				except ProcessLookupError:
					pass
		try:
			self.process.wait(timeout=15)
		except subprocess.TimeoutExpired as error:
			raise RuntimeError(f"owned browser process tree {pid} survived forced termination") from error
		result = {"method": method, "pid": pid, "exitCode": self.process.returncode}
		self._forget()
		return result

	def _forget(self) -> None:
		self.browser = None
		self.context = None
		self.process = None


def evaluate(page, expression: str, run_id: str, **values):
	argument = {"runId": run_id, **values}
	return page.evaluate(expression, argument)


def assert_probe(result: dict) -> None:
	if result.get("schema") != PROBE_SCHEMA or result.get("version") != PROBE_VERSION:
		raise AssertionError("browser probe schema/version mismatch")


def assert_tag(dataset: dict | None, tag: str, label: str) -> None:
	if not dataset:
		raise AssertionError(f"{label} dataset is missing")
	values = list(dataset.get("values", {}).values())
	if not values or not all(SYNTHETIC_MARKER in value and f":{tag}" in value for value in values):
		raise AssertionError(f"{label} contains the wrong or non-synthetic generation")


def add_case(report: dict, case_id: str, scope: str, evidence: dict, limitation: str | None = None) -> None:
	case = {"id": case_id, "result": "pass", "scope": scope, "evidence": evidence}
	if limitation:
		case["limitation"] = limitation
	report["cases"].append(case)


def run(args: argparse.Namespace) -> dict:
	base_url = args.base_url.rstrip("/")
	fixture_url = base_url + "/web/storage_robustness_fixture.html"
	parsed = urllib.parse.urlsplit(base_url)
	if parsed.scheme not in {"http", "https"} or not parsed.netloc:
		raise ValueError("--base-url must be an HTTP(S) origin")
	origin = f"{parsed.scheme}://{parsed.netloc}"
	executable = find_browser_executable(args.browser_executable)
	run_id = "run-" + uuid.uuid4().hex
	started_at = utc_now()
	report = {
		"schema": REPORT_SCHEMA,
		"version": REPORT_VERSION,
		"status": "running",
		"startedAt": started_at,
		"finishedAt": None,
		"run": {
			"id": run_id,
			"origin": origin,
			"fixture": fixture_url,
			"profilePolicy": "new-disposable-profile-created-by-this-run",
			"dataPolicy": "synthetic-markers-only; unique test namespaces; production namespaces never opened",
			"browserExecutable": str(executable),
			"host": {"platform": platform.platform(), "python": platform.python_version()},
			"artifacts": {
				"driverSha256": sha256(DRIVER_PATH),
				"fixtureSha256": sha256(FIXTURE_PATH),
				"probeSha256": sha256(PROBE_PATH),
				"mutableRuntimeSha256": sha256(MUTABLE_RUNTIME_PATH),
				"importerRuntimeSha256": sha256(IMPORTER_RUNTIME_PATH),
			},
		},
		"cases": [],
		"coverage": {},
		"limitations": [
			"No commercial UT99 file was read, copied, requested, or embedded; all stored bytes are synthetic.",
			"The forced-kill result is desktop Chromium evidence, not a Quest OS/browser kill.",
			"Origin clearing is a deterministic eviction simulation, not proof of storage-pressure eviction policy.",
			"Future schema rejection proves fail-closed compatibility policy; no schema migration implementation exists yet.",
			"This run does not qualify full-install size, quota pressure, thermal behavior, or physical Quest persistence.",
		],
	}

	with tempfile.TemporaryDirectory(prefix="surreal-storage-robustness-") as temporary:
		profile = Path(temporary) / "browser-profile"
		profile.mkdir()
		if any(profile.iterdir()):
			raise AssertionError("disposable browser profile was not clean at creation")

		with sync_playwright() as playwright:
			owned = OwnedBrowser(playwright, executable, profile)
			try:
				# Seed through both mutable backends and the actual importer API.
				owned.launch()
				report["run"]["browser"] = owned.product
				page = owned.page(fixture_url)
				seed = evaluate(
					page,
					"arg => SurrealStorageRobustnessProbe.seed(arg.runId, arg.tag)",
					run_id,
					tag="clean-restart",
				)
				assert_probe(seed)
				initial_ids = {key: value["datasetId"] for key, value in seed["result"].items()}
				owned.graceful_close()

				owned.launch()
				page = owned.page(fixture_url)
				clean = evaluate(page, "arg => SurrealStorageRobustnessProbe.inspect(arg.runId)", run_id)
				assert_probe(clean)
				assert_tag(clean["opfs"], "clean-restart", "OPFS after clean restart")
				assert_tag(clean["indexeddb"], "clean-restart", "IndexedDB after clean restart")
				assert_tag(clean["importer"], "clean-restart", "importer after clean restart")
				if {"opfs": clean["opfs"]["datasetId"], "indexeddb": clean["indexeddb"]["datasetId"], "importer": clean["importer"]["datasetId"]} != initial_ids:
					raise AssertionError("clean restart changed committed dataset ids")
				add_case(report, "clean-browser-restart", "controlled desktop browser close and reopen", {"datasetIds": initial_ids})

				separation = evaluate(page, "arg => SurrealStorageRobustnessProbe.clearMutableOnly(arg.runId)", run_id)
				if separation["mutableOPFS"] is not None or separation["mutableIDB"] is not None:
					raise AssertionError("mutable-only clear left mutable data")
				assert_tag(separation["importer"], "clean-restart", "importer after mutable-only clear")
				add_case(
					report,
					"mutable-importer-separation",
					"actual mutable/importer APIs in unique synthetic namespaces",
					{"importerDatasetId": separation["importer"]["datasetId"], "mutableAbsent": True},
				)

				seed = evaluate(
					page,
					"arg => SurrealStorageRobustnessProbe.seed(arg.runId, arg.tag)",
					run_id,
					tag="pre-crash",
				)
				assert_probe(seed)
				try:
					# chrome://crash terminates the fixture's renderer. Unlike the
					# Page.crash CDP command, navigation returns promptly when the
					# transport reports the intentional renderer loss.
					page.goto("chrome://crash", wait_until="commit", timeout=5_000)
				except PlaywrightError:
					pass
				try:
					page.close()
				except PlaywrightError:
					pass
				page = owned.page(fixture_url)
				crash = evaluate(page, "arg => SurrealStorageRobustnessProbe.inspect(arg.runId)", run_id)
				assert_tag(crash["opfs"], "pre-crash", "OPFS after renderer crash")
				assert_tag(crash["indexeddb"], "pre-crash", "IndexedDB after renderer crash")
				assert_tag(crash["importer"], "pre-crash", "importer after renderer crash")
				add_case(
					report,
					"renderer-process-crash",
					"chrome://crash renderer termination with browser process kept alive",
					{"storageGeneration": "pre-crash"},
					"A renderer crash is not a browser-process or operating-system kill.",
				)

				evaluate(
					page,
					"arg => SurrealStorageRobustnessProbe.beginAbruptWrites(arg.runId, arg.tag)",
					run_id,
					tag="abrupt-new",
				)
				page.wait_for_function(
					"(() => { const s = SurrealStorageRobustnessProbe.interruptionStatus(); "
					"return s.opfsReachedPartialSnapshot === true && s.importerReachedPartialDataset === true; })()",
					timeout=30_000,
				)
				cut_status = page.evaluate("SurrealStorageRobustnessProbe.interruptionStatus()")
				if not cut_status["idbSaveStarted"]:
					raise AssertionError("IndexedDB replacement save was not started before kill")
				kill_evidence = owned.force_kill()
				time.sleep(0.5)

				owned.launch()
				page = owned.page(fixture_url)
				after_kill = evaluate(page, "arg => SurrealStorageRobustnessProbe.inspect(arg.runId)", run_id)
				assert_probe(after_kill)
				assert_tag(after_kill["opfs"], "pre-crash", "OPFS after browser kill")
				assert_tag(after_kill["importer"], "pre-crash", "importer after browser kill")
				idb_values = list(after_kill["indexeddb"]["values"].values()) if after_kill["indexeddb"] else []
				idb_generation = None
				for candidate in ["pre-crash", "abrupt-new"]:
					if idb_values and all(SYNTHETIC_MARKER in value and f":{candidate}" in value for value in idb_values):
						idb_generation = candidate
				if idb_generation is None:
					raise AssertionError("IndexedDB exposed a missing or mixed generation after forced browser kill")
				add_case(
					report,
					"forced-browser-process-tree-kill",
					"owned desktop browser process tree forcibly terminated during replacement saves",
					{
						"termination": kill_evidence,
						"cutPoint": cut_status,
						"opfsGeneration": "pre-crash",
						"indexeddbGeneration": idb_generation,
						"importerGeneration": "pre-crash",
					},
					"The OPFS cut point is exact; the IndexedDB cut point is intentionally indeterminate, so either complete transaction is accepted.",
				)

				corruption = evaluate(
					page,
					"arg => SurrealStorageRobustnessProbe.injectAndCheckCorruption(arg.runId)",
					run_id,
				)
				assert_probe(corruption)
				for key in [
					"futureOPFS", "missingOPFS", "futureIDB", "missingIDB",
					"futureImporterOPFS", "missingImporterOPFS",
				]:
					if not corruption["results"][key]["failedClosed"]:
						raise AssertionError(f"{key} did not fail closed")
				if corruption["results"]["futureOPFS"]["pointerVersionAfterRefusal"] != 2:
					raise AssertionError("OPFS future schema was overwritten")
				if corruption["results"]["futureIDB"]["pointerVersionAfterRefusal"] != 2:
					raise AssertionError("IndexedDB future schema was overwritten")
				if corruption["results"]["futureImporterOPFS"]["pointerVersionAfterRefusal"] != 2:
					raise AssertionError("importer OPFS future schema was overwritten")
				add_case(
					report,
					"corruption-and-schema-policy",
					"synthetic future-schema and missing-data injection through both production backends",
					corruption["results"],
					"This proves exact-v1 acceptance and future-schema refusal, not a future migration implementation.",
				)

				pre_eviction = evaluate(
					page,
					"arg => SurrealStorageRobustnessProbe.seed(arg.runId, arg.tag)",
					run_id,
					tag="pre-eviction",
				)
				assert_probe(pre_eviction)
				# Release live OPFS/IndexedDB handles before asking Chromium to clear
				# the disposable origin, matching a user clearing site data while
				# the app is closed rather than racing open transactions.
				page.close()
				eviction_page = owned.page(fixture_url)
				eviction_session = owned.context.new_cdp_session(eviction_page)
				eviction_session.send("Storage.clearDataForOrigin", {"origin": origin, "storageTypes": "all"})
				eviction_session.detach()
				eviction_page.close()
				page = owned.page(fixture_url)
				evicted = evaluate(page, "arg => SurrealStorageRobustnessProbe.inspect(arg.runId)", run_id)
				if any(evicted[key] is not None for key in ["opfs", "indexeddb", "importer"]):
					raise AssertionError("simulated whole-origin eviction left test data")
				add_case(
					report,
					"whole-origin-eviction-simulation",
					"CDP Storage.clearDataForOrigin(all) in disposable profile",
					{"mutableOPFS": "absent", "mutableIndexedDB": "absent", "importerOPFS": "absent"},
					"Deterministic site-data clearing does not reproduce browser storage-pressure heuristics.",
				)
				owned.graceful_close()
			except Exception:
				try:
					owned.force_kill()
				except Exception:
					pass
				raise

	report["coverage"] = {
		"cleanDesktopRestart": "proven",
		"rendererCrashRecovery": "proven",
		"forcedDesktopBrowserProcessKill": "proven",
		"opfsInterruptedPointerCommitRecovery": "proven",
		"importerOPFSInterruptedPointerCommitRecovery": "proven",
		"indexedDBAtomicGenerationAfterKill": "proven",
		"corruptMetadataAndMissingPayloadFailClosed": "proven",
		"unsupportedFutureSchemaRefusedWithoutOverwrite": "proven",
		"migrationExecution": "not-implemented-not-proven",
		"deterministicWholeOriginClear": "proven-as-eviction-simulation",
		"storagePressureEviction": "not-proven",
		"questBrowserRestartOrOSKill": "not-proven",
		"fullCommercialInstall": "out-of-scope-and-never-accessed",
	}
	report["status"] = "pass"
	report["finishedAt"] = utc_now()
	return report


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--base-url", default=os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091"))
	parser.add_argument("--browser-executable", help="Chrome, Chromium, Edge, or Brave executable to launch and own")
	parser.add_argument("--output", type=Path, default=Path("storage-robustness-report.json"))
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	try:
		report = run(args)
	except Exception as error:
		report = {
			"schema": REPORT_SCHEMA,
			"version": REPORT_VERSION,
			"status": "fail",
			"finishedAt": utc_now(),
			"error": {"type": type(error).__name__, "message": str(error)},
			"limitations": [
				"The qualification stopped early; no unreported case may be treated as passed.",
				"No commercial UT99 data is used by this harness.",
			],
		}
		args.output.parent.mkdir(parents=True, exist_ok=True)
		args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
		print(f"FAIL: {error}", file=sys.stderr)
		print(f"Failure evidence: {args.output.resolve()}", file=sys.stderr)
		return 1
	args.output.parent.mkdir(parents=True, exist_ok=True)
	args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	print(json.dumps({
		"status": report["status"],
		"cases": len(report["cases"]),
		"output": str(args.output.resolve()),
		"browser": report["run"].get("browser", {}).get("product"),
	}, indent=2))
	print(f"PASS: {len(report['cases'])} storage robustness cases; see versioned evidence for scope and limitations")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
