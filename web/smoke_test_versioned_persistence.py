"""Qualify mutable-save continuity across a local canonical promote and rollback.

This intentionally exercises two immutable, data-free release generations at one
loopback origin. It refuses remote URLs and always attempts to restore the original
pointer, so it cannot be used as a live deployment command.
"""

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from urllib.parse import urlparse

from playwright.sync_api import sync_playwright


def parse_args():
	parser = argparse.ArgumentParser()
	parser.add_argument("--base-url", required=True)
	parser.add_argument("--site-root", required=True)
	parser.add_argument("--live-relative", default="Ports/SurrealEngine")
	parser.add_argument("--intended-base-path", default="/webxr/Ports/SurrealEngine/")
	parser.add_argument("--current-manifest-sha256", required=True)
	parser.add_argument("--target-manifest-sha256", required=True)
	parser.add_argument("--output")
	return parser.parse_args()


def validate_hash(value, name):
	if len(value) != 64 or any(character not in "0123456789abcdef" for character in value):
		raise ValueError(f"{name} must be 64 lowercase hexadecimal characters")


def install_native_stub(page):
	page.wait_for_function("window.Module && (typeof Module.ccall === 'function' || typeof Module.callMain === 'function')")
	page.evaluate("""() => {
		if (window.__versionedPersistenceNativeStubInstalled) return;
		window.__versionedPersistenceNativeStubInstalled = true;
		if (typeof Module.ccall === 'function') {
			const original = Module.ccall.bind(Module);
			Module.ccall = (name, returnType, argumentTypes, args, options) => {
				if (name === 'Surreal_StartBrowserGame') return Promise.resolve(0);
				if (name === 'Surreal_SetXRDominantHand') return Promise.resolve(1);
				if (name === 'Surreal_GetWebGL2FrameCount') return 1;
				return original(name, returnType, argumentTypes, args, options);
			};
		}
		Module.callMain = () => {};
	}""")


def wait_for_shell(page):
	page.wait_for_function("window.Module && document.querySelector('[data-game-pick]')", timeout=60000)


def select_webgl2_and_launch(page):
	page.wait_for_selector("#game-launcher:not([hidden])", timeout=60000)
	install_native_stub(page)
	page.select_option("[data-launcher-renderer]", "webgl2")
	page.click("#game-launcher button[type=submit]")
	page.wait_for_function("window.surrealBooted === true && window.surrealApp", timeout=60000)


def import_synthetic_unreal(page):
	page.wait_for_function("window.surrealApp && window.surrealApp.result.import.state === 'waiting-for-import'", timeout=60000)
	page.evaluate("""() => {
		const files = [
			['System/Core.u','core'], ['System/Engine.u','engine'], ['System/UnrealShare.u','share'],
			['System/UnrealI.u','unreali'], ['System/Unreal.ini','ini'], ['System/Unreal.exe','exe'],
			['Maps/Vortex2.unr','map'], ['Textures/Test.utx','texture'],
			['Sounds/Test.uax','sound'], ['Music/Test.umx','music'],
		];
		const entries = files.map(([path, value]) => {
			const blob = new Blob([value]);
			return { path, size: blob.size, getBlob: async () => blob };
		});
		window.__versionedPersistenceImport = surrealApp.dataController.importController.importEntries(entries);
	}""")
	select_webgl2_and_launch(page)
	page.evaluate("window.__versionedPersistenceImport")


def write_and_flush_sentinel(page, sentinel):
	return page.evaluate("""async sentinel => {
		Module.FS.mkdirTree('/gamedata/Save');
		Module.FS.writeFile('/gamedata/Save/Save0.usa', new TextEncoder().encode(sentinel));
		const status = await surrealApp.dataController.flush('versioned-promote-rollback-test');
		return { value: new TextDecoder().decode(Module.FS.readFile('/gamedata/Save/Save0.usa')),
			state: status.state, backend: status.backend, schema: status.schema,
			version: status.version, fileCount: status.fileCount };
	}""", sentinel)


def read_sentinel(page):
	return page.evaluate("""() => ({
		value: new TextDecoder().decode(Module.FS.readFile('/gamedata/Save/Save0.usa')),
		mutable: surrealApp.dataController.mutableController.status(),
		buildId: surrealReleaseDiagnostics.report().build.id,
		url: location.pathname,
	})""")


def reload_saved_generation(page):
	page.goto(page.url.split("/releases/", 1)[0].rstrip("/") + "/", wait_until="load")
	wait_for_shell(page)
	select_webgl2_and_launch(page)


def pointer_command(args, operation, current_hash, target_hash):
	cli = Path(__file__).with_name("versioned_release.mjs")
	command = [
		"node", str(cli), operation, "--execute=yes", f"--site-root={Path(args.site_root).resolve()}",
		f"--live-relative={args.live_relative}", f"--intended-base-path={args.intended_base_path}",
		f"--current-manifest-sha256={current_hash}", f"--target-manifest-sha256={target_hash}",
	]
	completed = subprocess.run(command, check=True, capture_output=True, text=True)
	return json.loads(completed.stdout)


def main():
	args = parse_args()
	parsed = urlparse(args.base_url)
	if parsed.scheme != "http" or parsed.hostname not in ("127.0.0.1", "localhost"):
		raise ValueError("--base-url must be an HTTP loopback origin; live sites are refused")
	validate_hash(args.current_manifest_sha256, "--current-manifest-sha256")
	validate_hash(args.target_manifest_sha256, "--target-manifest-sha256")
	if args.current_manifest_sha256 == args.target_manifest_sha256:
		raise ValueError("current and target generations must differ")

	report = {
		"schema": "surrealengine-versioned-persistence-smoke-v1",
		"generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
		"origin": f"{parsed.scheme}://{parsed.netloc}",
		"currentManifestSha256": args.current_manifest_sha256,
		"targetManifestSha256": args.target_manifest_sha256,
		"steps": [],
	}
	pointer_is_target = False
	page_errors = []
	try:
		with sync_playwright() as playwright:
			browser = playwright.chromium.launch(channel="chrome", headless=True)
			context = browser.new_context(service_workers="block")
			page = context.new_page()
			page.on("pageerror", lambda error: page_errors.append(str(error)))
			page.goto(args.base_url.rstrip("/") + "/", wait_until="load")
			wait_for_shell(page)
			import_synthetic_unreal(page)
			sentinel = "surrealengine-versioned-save-continuity-v1"
			written = write_and_flush_sentinel(page, sentinel)
			if written["value"] != sentinel or written["state"] != "ready" or not written["backend"]:
				raise RuntimeError(f"initial mutable checkpoint was not durable: {written}")
			report["steps"].append({"stage": "checkpoint-current", "result": written})

			promotion = pointer_command(args, "promote", args.current_manifest_sha256, args.target_manifest_sha256)
			pointer_is_target = True
			reload_saved_generation(page)
			promoted = read_sentinel(page)
			if promoted["value"] != sentinel or args.target_manifest_sha256 not in promoted["url"]:
				raise RuntimeError(f"save was not restored after promotion: {promoted}")
			report["steps"].append({"stage": "restore-promoted", "transaction": promotion, "result": promoted})

			rollback = pointer_command(args, "rollback", args.target_manifest_sha256, args.current_manifest_sha256)
			pointer_is_target = False
			reload_saved_generation(page)
			rolled_back = read_sentinel(page)
			if rolled_back["value"] != sentinel or args.current_manifest_sha256 not in rolled_back["url"]:
				raise RuntimeError(f"save was not restored after rollback: {rolled_back}")
			report["steps"].append({"stage": "restore-rolled-back", "transaction": rollback, "result": rolled_back})
			if page_errors:
				raise RuntimeError("browser page errors: " + " | ".join(page_errors))
			context.close()
			browser.close()
		report["result"] = "passed"
	finally:
		if pointer_is_target:
			try:
				pointer_command(args, "rollback", args.target_manifest_sha256, args.current_manifest_sha256)
			except Exception as error:  # Preserve the original error while making cleanup failure explicit.
				report["cleanupError"] = str(error)
		if args.output:
			output = Path(args.output)
			output.parent.mkdir(parents=True, exist_ok=True)
			output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
	print(json.dumps(report, indent=2))
	if report.get("result") != "passed" or report.get("cleanupError"):
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
