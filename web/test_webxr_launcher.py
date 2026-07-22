#!/usr/bin/env python3
"""Deterministic unit and real-page tests for the opt-in WebXR launcher."""

from __future__ import annotations

import argparse
import contextlib
import http.server
import json
import threading
import time
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlsplit

from playwright.sync_api import Page, sync_playwright


ROOT = Path(__file__).resolve().parent

STUBS = {
    "/webxr_session.js": r"""
window.surrealXRSupported = null;
window.surrealXRGPUCompatible = null;
window.surrealXRSessionActive = false;
window.surrealXRGPUBindingAvailable = false;
window.surrealXRLifecycle = {
  visibilityHidden: false, visibilitySource: "initial", xrVisibilityState: null,
  audioPolicy: "unchanged", audioState: null, shutdown: false, deviceLost: false,
  lastExitReason: null, entryAttempts: 0, sessionsStarted: 0, sessionsEnded: 0
};
window.surrealXRGetInteractionStatus = () => ({
  schema: "surrealengine-webxr-interaction", version: 1, active: false,
  generation: 0, exitPhase: "idle", trackingPhase: "inactive",
  connectedControllers: 0, expectedControllers: 0, trackingRecoveries: 0
});
window.surrealXRGetLaunchReadiness = () => ({
  schema: "surrealengine-webxr-launch-readiness", version: 1,
  mode: window.surrealXRUseNativeWebGPU ? "native-webgpu" : "lifecycle-only",
  lifecycleOnly: !window.surrealXRUseNativeWebGPU,
  productionPathRequested: !!window.surrealXRUseNativeWebGPU,
  productionPresentation: false, canAttempt: true,
  productionSessionReady: false, nativePhase: "idle", blockers: []
});
window.surrealXRTestFrameABI = () => ({passed: true, checks: 44});
window.surrealXRCheckSupport = async () => { window.surrealXRSupported = true; return true; };
window.surrealXRCheckGPUCompatible = async () => { window.surrealXRGPUCompatible = true; return true; };
window.surrealXREnter = async () => true;
window.surrealXREnterNativeWebGPU = async () => true;
window.surrealXRRequestSafeExit = () => ({action: "inactive"});
window.surrealXRProbeWebGPUProjection = () => ({supported: false});
window.surrealXRPackFrame = () => new ArrayBuffer(0);
window.surrealXRConsumeWebGPUFrame = () => 1;
Object.defineProperty(navigator, "gpu", {configurable: true, value: {
  async requestAdapter() {
    return {
      features: new Set(),
      async requestDevice() {
        return {lost: new Promise(() => {}), addEventListener() {}, queue: {}};
      }
    };
  }
}});
""",
    "/ut99_importer.js": r"""
window.SurrealUT99Importer = {
  async start(Module, options) {
    await options.beforeLaunch();
    await options.launch("developer-preload");
    window.__mapManifestCalls = window.__mapManifestCalls || 0;
    return {controller: {kind: "fake-importer", mapManifest() { window.__mapManifestCalls++; return Object.freeze({
        schema: "surrealengine-ut99-map-manifest", version: 1, state: "ready",
        maps: Object.freeze(["DM-Zeta", "CTF-Face", "dm-zeta", "DM-Morpheus",
          "MH-Unsupported", "../DM-Escape"]), rejectedCount: 2
      }); }},
      result: {state: "launched", mode: "developer-preload", backend: "embedded",
        metadata: {datasetId: "must-not-export", localPath: "C:\\UT99"}}};
  }
};
""",
    "/mutable_persistence.js": r"""
window.SurrealMutableData = {
  async start() {
    const status = {schema: "surrealengine-mutable-data", version: 1, state: "ready",
      backend: "opfs", fileCount: 2, totalBytes: 64, allowlist: ["/gamedata/Save/Save0.usa"]};
    return {controller: {status: () => status, flush: async () => status,
      clear: async () => status}, result: status};
  }
};
""",
    "/webxr_settings.js": r"""
window.SurrealWebXRSettings = {create() { return {
  onEngineReady() {}, statusSnapshot() { return {schema: "settings", version: 1, ready: true}; }
}; }};
""",
    "/pwa_register.js": r"""
window.SurrealPWA = {register: async () => ({state: "disabled"}),
  getDiagnostics: () => ({state: "disabled"})};
""",
    "/build-emscripten/SurrealEngine.js": r"""
window.__callMainCalls = 0;
window.__callMainArgs = null;
Module.callMain = args => { window.__callMainCalls++; window.__callMainArgs = args.slice(); };
Module.ccall = () => 0;
const runtimeDelay = new URLSearchParams(location.search).get("slow-runtime") === "1" ? 350 : 0;
setTimeout(() => Module.onRuntimeInitialized(), runtimeDelay);
""",
}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        path = urlsplit(self.path).path
        if path in ("/", "/index_webxr.html", "/web/index_webxr.html"):
            payload = (ROOT / "index_webxr.html").read_bytes()
            content_type = "text/html; charset=utf-8"
        elif path == "/webxr_launcher.js":
            payload = (ROOT / "webxr_launcher.js").read_bytes()
            content_type = "text/javascript; charset=utf-8"
        elif path in STUBS:
            payload = STUBS[path].encode("utf-8")
            content_type = "text/javascript; charset=utf-8"
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, _format: str, *_args: object) -> None:
        pass


@contextlib.contextmanager
def local_server():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_port}"
    finally:
        server.shutdown()
        thread.join(timeout=5)
        server.server_close()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def collect_page_errors(page: Page) -> list[str]:
    errors: list[str] = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    return errors


def run(headless: bool) -> dict[str, object]:
    with local_server() as base_url, sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=headless)

        # Regression: absent launcher=1 retains automatic data-backed boot and
        # the exact existing main-argument order.
        automatic = browser.new_page()
        automatic_errors = collect_page_errors(automatic)
        automatic.goto(base_url + "/?build=build-emscripten", wait_until="load")
        automatic.wait_for_function("window.surrealBooted === true")
        automatic_state = automatic.evaluate("""() => ({
          calls: window.__callMainCalls,
          args: window.__callMainArgs,
          launcherHidden: document.getElementById("webxr-browser-launcher").hidden,
          state: window.surrealLauncherController.state,
          dataMode: window.surrealLauncherController.dataMode,
          mapManifestCalls: window.__mapManifestCalls || 0
        })""")
        require(automatic_state == {
            "calls": 1,
            "args": ["--autoplay", "--url=DM-Deck16][", "--render=webgpu", "/gamedata"],
            "launcherHidden": True,
            "state": "running",
            "dataMode": "developer-preload",
            "mapManifestCalls": 0,
        }, f"automatic boot regressed: {automatic_state}")
        require(not automatic_errors, f"automatic page errors: {automatic_errors}")

        # The launcher exposes only named, trustworthy phases and an
        # indeterminate progress element. Native keyboard tab order reaches
        # every operation, and keyboard-only selection can launch.
        keyboard = browser.new_page()
        keyboard_errors = collect_page_errors(keyboard)
        keyboard.goto(base_url + "/?launcher=1&build=build-emscripten&slow-runtime=1",
                      wait_until="load")
        keyboard.wait_for_function(
            "window.surrealLauncherController.loadingPhase === 'runtime-script'")
        loading = keyboard.evaluate("""() => ({
          state: surrealLauncherController.state,
          phase: document.querySelector("[data-launcher-loading-phase]").textContent,
          progressHidden: document.querySelector("[data-launcher-loading-progress]").hidden,
          progressHasValue: document.querySelector("[data-launcher-loading-progress]").hasAttribute("value"),
          busy: document.getElementById("webxr-browser-launcher").getAttribute("aria-busy")
        })""")
        require(loading["state"] == "loading" and "runtime" in loading["phase"].lower() and
                loading["progressHidden"] is False and loading["progressHasValue"] is False and
                loading["busy"] == "true", f"loading phase was dishonest or invisible: {loading}")
        keyboard.wait_for_function("surrealLauncherController.state === 'ready' && surrealLauncherController.mapPickerState === 'ready'")
        keyboard.wait_for_function("document.activeElement.matches('[data-launcher-map]')")
        focus_order = [keyboard.evaluate("document.activeElement.getAttribute('data-launcher-map') !== null ? 'map' : 'other'")]
        selectors = {
            "map-picker": "[data-launcher-map-picker]",
            "preset": "[data-launcher-preset]",
            "refresh": "[data-launcher-map-refresh]",
            "start": "[data-launcher-start]",
            "copy": "[data-launcher-copy]",
            "download": "[data-launcher-download]",
        }
        for name, selector in selectors.items():
            keyboard.keyboard.press("Tab")
            require(keyboard.evaluate("selector => document.activeElement.matches(selector)", selector),
                    f"keyboard focus order did not reach {name}")
            focus_order.append(name)
        require(focus_order == ["map", "map-picker", "preset", "refresh", "start", "copy", "download"],
                f"unexpected launcher focus order: {focus_order}")
        keyboard.focus("[data-launcher-map-picker]")
        keyboard.press("[data-launcher-map-picker]", "ArrowDown")
        keyboard.press("[data-launcher-map-picker]", "ArrowDown")
        require(keyboard.input_value("[data-launcher-map]") == "DM-Morpheus",
                "keyboard map choice did not update validated entry")
        keyboard.press("[data-launcher-map-picker]", "Tab")
        keyboard.press("[data-launcher-preset]", "ArrowDown")
        keyboard.press("[data-launcher-preset]", "Tab")
        keyboard.press("[data-launcher-map-refresh]", "Tab")
        require(keyboard.evaluate("document.activeElement.matches('[data-launcher-start]')"),
                "keyboard could not reach deliberate Start")
        keyboard.press("[data-launcher-start]", "Enter")
        keyboard.wait_for_function("window.surrealBooted === true")
        keyboard_result = keyboard.evaluate("""() => ({
          args: window.__callMainArgs,
          state: surrealLauncherController.state,
          phases: Array.from(surrealLauncherController.phaseHistory),
          loadingHidden: document.querySelector("[data-launcher-loading]").hidden,
          busy: document.getElementById("webxr-browser-launcher").getAttribute("aria-busy")
        })""")
        require(keyboard_result["args"][1] == "--url=DM-Morpheus?game=Botpack.DeathMatchPlus" and
                keyboard_result["state"] == "running" and keyboard_result["loadingHidden"] is True and
                keyboard_result["busy"] == "false", f"keyboard launch failed: {keyboard_result}")
        for phase in ("browser-preflight", "webgpu-device", "runtime-script", "runtime-initialized",
                      "import-data", "mutable-data", "ready", "engine-start", "running"):
            require(phase in keyboard_result["phases"],
                    f"trustworthy loading phase {phase} was not recorded: {keyboard_result['phases']}")
        require(not keyboard_errors, f"keyboard/loading page errors: {keyboard_errors}")

        # The explicit launcher must reach imported/mutable data readiness but
        # must not call main before a deliberate click.
        launcher = browser.new_page(accept_downloads=True)
        launcher_errors = collect_page_errors(launcher)
        launcher.goto(base_url + "/?launcher=1&build=build-emscripten&map=DM-Deck16][",
                      wait_until="load")
        launcher.wait_for_function("window.surrealLauncherController.state === 'ready'")
        launcher.wait_for_function("window.surrealLauncherController.mapPickerState === 'ready'")
        waiting = launcher.evaluate("""() => ({
          calls: window.__callMainCalls,
          booted: window.surrealBooted,
          visible: !document.getElementById("webxr-browser-launcher").hidden,
          startDisabled: document.querySelector("[data-launcher-start]").disabled,
          status: document.querySelector("[data-launcher-status]").textContent,
          importer: window.surrealUT99DataBootResult,
          mapState: window.surrealLauncherController.mapPickerState,
          maps: Array.from(document.querySelector("[data-launcher-map-picker]").options,
            option => option.value).filter(Boolean),
          rejected: window.surrealLauncherController.mapRejectedCount,
          nativeLink: document.getElementById("open-native-webxr").href,
          nativeLinkText: document.getElementById("open-native-webxr").textContent,
          nativeGuidance: document.getElementById("native-webxr-test-guidance").textContent
        })""")
        require(waiting["calls"] == 0 and waiting["booted"] is False and waiting["visible"] is True,
                f"launcher did not wait at its deliberate gate: {waiting}")
        require(waiting["startDisabled"] is False and "ready" in waiting["status"].lower(),
                f"ready launcher did not expose Start: {waiting}")
        require(waiting["importer"]["state"] == "launched",
                "importer did not finish before launcher readiness")
        require(waiting["mapState"] == "ready" and
                waiting["maps"] == ["CTF-Face", "DM-Morpheus", "DM-Zeta"] and
                waiting["rejected"] == 4,
                f"map picker did not sort/deduplicate/reject defensively: {waiting}")
        native_url = urlsplit(waiting["nativeLink"])
        require(parse_qs(native_url.query) == {
                    "build": ["build-emscripten"],
                    "native-webgpu-xr": ["1"],
                    "orientation-fix": ["1"],
                }, f"native Brave/VDXR route was not exact: {waiting['nativeLink']}")
        require("Brave" in waiting["nativeLinkText"] and
                "Virtual Desktop/VDXR" in waiting["nativeLinkText"],
                f"physical target was not named on native route: {waiting['nativeLinkText']}")
        require("Brave 150" in waiting["nativeGuidance"] and
                "Quest 3" in waiting["nativeGuidance"] and
                "does not prove" in waiting["nativeGuidance"],
                f"native guidance did not preserve its evidence boundary: {waiting['nativeGuidance']}")

        # Empty, unavailable and refresh-error states never disable manual
        # validated launch. A later refresh recovers the browsable list.
        fallback_states = launcher.evaluate(r"""async () => {
          const controller = window.surrealLauncherController;
          const original = controller.options.mapManifest;
          controller.options.mapManifest = () => ({schema: "surrealengine-ut99-map-manifest",
            version: 1, state: "empty", maps: [], rejectedCount: 0});
          await controller.refreshMaps();
          const empty = {state: controller.mapPickerState,
            manualDisabled: document.querySelector("[data-launcher-map]").disabled,
            startDisabled: document.querySelector("[data-launcher-start]").disabled};
          controller.options.mapManifest = () => ({schema: "surrealengine-ut99-map-manifest",
            version: 1, state: "unavailable", maps: [], rejectedCount: 0});
          await controller.refreshMaps();
          const unavailable = {state: controller.mapPickerState,
            manualDisabled: document.querySelector("[data-launcher-map]").disabled,
            startDisabled: document.querySelector("[data-launcher-start]").disabled};
          controller.options.mapManifest = async () => { throw new Error("synthetic refresh failure"); };
          await controller.refreshMaps();
          const error = {state: controller.mapPickerState,
            manualDisabled: document.querySelector("[data-launcher-map]").disabled,
            startDisabled: document.querySelector("[data-launcher-start]").disabled};
          let releaseOld;
          controller.options.mapManifest = () => new Promise(resolve => { releaseOld = resolve; });
          const staleRefresh = controller.refreshMaps();
          await Promise.resolve();
          controller.options.mapManifest = () => ({schema: "surrealengine-ut99-map-manifest",
            version: 1, state: "ready", maps: ["DM-Newer"], rejectedCount: 0});
          await controller.refreshMaps();
          releaseOld({schema: "surrealengine-ut99-map-manifest", version: 1,
            state: "ready", maps: ["DM-Stale"], rejectedCount: 0});
          await staleRefresh;
          const staleProtected = {state: controller.mapPickerState,
            maps: Array.from(controller.mapInventory)};
          controller.options.mapManifest = original;
          await controller.refreshMaps();
          return {empty, unavailable, error, staleProtected, recovered: controller.mapPickerState};
        }""")
        for state_name in ("empty", "unavailable", "error"):
            state = fallback_states[state_name]
            require(state["state"] == state_name and state["manualDisabled"] is False and
                    state["startDisabled"] is False,
                    f"{state_name} map-list state blocked manual launch: {fallback_states}")
        require(fallback_states["recovered"] == "ready",
                f"map-list refresh did not recover: {fallback_states}")
        require(fallback_states["staleProtected"] == {"state": "ready", "maps": ["DM-Newer"]},
                f"stale map refresh overwrote newer state: {fallback_states}")

        # Unit-level input matrix: package basenames only; fixed presets only;
        # paths, query injection, encoded delimiters and network-like values fail.
        unit = launcher.evaluate(r"""() => {
          const api = window.SurrealWebXRLauncher;
          const bad = [
            "../DM-Deck16][", "/gamedata/Maps/DM-Deck16][", "DM-Deck16][.unr",
            "DM-Deck16][?game=Evil.Game", "DM-Deck16][&listen", "DM-Foo%3Fgame",
            "C:\\UT99\\Maps\\DM-Deck16][", "127.0.0.1:7777/DM-Deck16][",
            "<img src=x onerror=alert(1)>", "DM Deck"
          ];
          let mismatch = null;
          try { api.buildLocalSelection("CTF-Face", "deathmatch"); }
          catch (error) { mismatch = error.code; }
          const dm = api.buildLocalSelection("DM-Morpheus", "deathmatch");
          const ctf = api.buildLocalSelection("CTF-Face", "ctf");
          const restart = new URL(api.restartURL(
            location.href + "&game=bad&native-webgpu-xr=1"));
          return {
            schema: [api.SCHEMA_NAME, api.SCHEMA_VERSION],
            bad: bad.map(value => api.validateMap(value)),
            mismatch, dm, ctf,
            restart: {launcher: restart.searchParams.get("launcher"),
              map: restart.searchParams.get("map"), game: restart.searchParams.get("game"),
              build: restart.searchParams.get("build"),
              native: restart.searchParams.get("native-webgpu-xr")}
          };
        }""")
        require(unit["schema"] == ["surrealengine-webxr-browser-launcher", 1],
                f"wrong launcher schema: {unit['schema']}")
        require(all(not result["ok"] for result in unit["bad"]),
                f"unsafe map input was accepted: {unit['bad']}")
        require(unit["mismatch"] == "preset-map-mismatch", "preset/map mismatch was accepted")
        require(unit["dm"]["engineURL"] == "DM-Morpheus?game=Botpack.DeathMatchPlus",
                f"deathmatch URL was not deterministic: {unit['dm']}")
        require(unit["ctf"]["engineURL"] == "CTF-Face?game=Botpack.CTFGame",
                f"CTF URL was not deterministic: {unit['ctf']}")
        require(unit["restart"] == {"launcher": "1", "map": None, "game": None,
                                    "build": "build-emscripten", "native": "1"},
                f"restart URL was not conservative: {unit['restart']}")

        # Standard gamepad navigation is neutral-latched and uses only A,
        # D-pad, and left-stick axes. B/Menu/View/system-like indices are
        # ignored, as are nonstandard mappings. It stops before gameplay.
        controller_input = launcher.evaluate(r"""() => {
          const controller = window.surrealLauncherController;
          const buttons = pressed => Array.from({length: 17}, (_, index) => ({
            pressed: pressed.includes(index), value: pressed.includes(index) ? 1 : 0
          }));
          const pad = (pressed, axes, mapping = "standard") => ({
            connected: true, mapping, buttons: buttons(pressed), axes: axes || [0, 0]
          });
          let time = 0;
          const send = (pressed = [], axes = [0, 0], mapping = "standard") =>
            controller.processGamepads([pad(pressed, axes, mapping)], time += 20);
          document.querySelector("[data-launcher-start]").focus();
          const heldBeforeNeutral = send([0]);
          const armed = send();
          const reserved = send([1, 8, 9, 16]);
          send();
          const nonstandard = send([0], [0, 0], "");
          document.querySelector("[data-launcher-map]").focus();
          const focused = [];
          send([], [0, 1]); focused.push(document.activeElement.dataset.launcherMapPicker !== undefined ? "picker" : "wrong");
          send(); send([15]); focused.push(document.querySelector("[data-launcher-map-picker]").value);
          send(); send([15]); focused.push(document.querySelector("[data-launcher-map-picker]").value);
          send(); send([13]); focused.push(document.activeElement.dataset.launcherPreset !== undefined ? "preset" : "wrong");
          send(); send([15]); focused.push(document.querySelector("[data-launcher-preset]").value);
          send(); send([13]); focused.push(document.activeElement.dataset.launcherMapRefresh !== undefined ? "refresh" : "wrong");
          send(); send([13]); focused.push(document.activeElement.dataset.launcherStart !== undefined ? "start" : "wrong");
          send(); const activated = send([0]);
          const afterGameplay = send([0, 13], [1, 1]);
          return {heldBeforeNeutral, armed, reserved, nonstandard, focused, activated,
            afterGameplay, calls: window.__callMainCalls, args: window.__callMainArgs,
            state: controller.state, frameRequest: controller.gamepadFrameRequest};
        }""")
        require(controller_input["heldBeforeNeutral"]["reason"] == "awaiting-neutral" and
                controller_input["armed"]["reason"] == "armed",
                f"gamepad neutral latch failed: {controller_input}")
        require(controller_input["reserved"]["handled"] is False and
                controller_input["nonstandard"]["reason"] == "no-standard-gamepad",
                f"reserved/nonstandard controller input was consumed: {controller_input}")
        require(controller_input["focused"] ==
                ["picker", "CTF-Face", "DM-Morpheus", "preset", "deathmatch", "refresh", "start"],
                f"controller navigation order/selection failed: {controller_input}")
        require(controller_input["activated"]["handled"] is True and
                controller_input["afterGameplay"]["reason"] == "inactive" and
                controller_input["frameRequest"] == 0,
                f"controller input leaked into active gameplay: {controller_input}")
        launcher.wait_for_function("window.surrealBooted === true")
        deliberate = launcher.evaluate("""() => ({
          calls: window.__callMainCalls, args: window.__callMainArgs,
          state: window.surrealLauncherController.state,
          attempts: window.surrealLauncherController.launchAttempts
        })""")
        require(deliberate == {
            "calls": 1,
            "args": ["--autoplay", "--url=DM-Morpheus?game=Botpack.DeathMatchPlus",
                     "--render=webgpu", "/gamedata"],
            "state": "running",
            "attempts": 1,
        }, f"deliberate launch was wrong: {deliberate}")

        # Diagnostics expose only selected scalar status. Even deliberately
        # hostile source objects/logs/crash messages cannot export paths,
        # dataset IDs, allowlists, file names, contents or stacks.
        launcher.evaluate(r"""() => {
          const controller = window.surrealLauncherController;
          controller.recordLog("[engine] SECRET FILE CONTENT Botpack.u");
          controller.recordLog("[harness] launcher failed at C:\\Users\\Alice\\UT99\\System\\Botpack.u /gamedata/Maps/DM-Secret.unr");
          controller.options.diagnosticSources = () => ({
            build: "build-emscripten", userAgent: navigator.userAgent,
            language: "en-US", secureContext: true,
            readiness: window.surrealXRGetLaunchReadiness(),
            lifecycle: window.surrealXRLifecycle,
            interaction: window.surrealXRGetInteractionStatus(),
            renderer: {webGPUDeviceReady: true, errorCount: 0, drawCalls: 50,
              textureCount: 12, bindGroupsCreated: 4, bindGroupCacheHits: 20,
              bufferRollovers: 0, secret: "FILE CONTENT"},
            importer: {state: "launched", mode: "persistent-import", backend: "opfs",
              fileCount: 500, totalBytes: 1234, datasetId: "SECRET-ID",
              path: "C:\\UT99", content: "SECRET FILE CONTENT"},
            mutable: {state: "ready", backend: "opfs", fileCount: 2,
              allowlist: ["/gamedata/System/User.ini"]}
          });
          window.__retryCalls = 0;
          window.__retryTarget = null;
          window.__retryResolve = null;
          controller.options.retry = target => {
            window.__retryCalls++;
            window.__retryTarget = target;
            return new Promise(resolve => { window.__retryResolve = resolve; });
          };
          controller.recordCrash(new Error(
            "failed C:\\Users\\Alice\\UT99\\System\\Botpack.u /gamedata/Maps/DM-Secret.unr"),
            "engine-start");
        }""")
        launcher.wait_for_function("document.activeElement.matches('[data-launcher-retry]')")
        crash_ui = launcher.evaluate("""() => ({
          retryVisible: !document.querySelector("[data-launcher-retry]").hidden,
          restartVisible: !document.querySelector("[data-launcher-restart]").hidden,
          loadingHidden: document.querySelector("[data-launcher-loading]").hidden,
          error: document.querySelector("[data-launcher-error]").textContent
        })""")
        require(crash_ui["retryVisible"] and crash_ui["restartVisible"] and crash_ui["loadingHidden"] and
                "C:\\" not in crash_ui["error"] and "/gamedata" not in crash_ui["error"],
                f"crash recovery UI was unsafe or unclear: {crash_ui}")
        diagnostics = launcher.evaluate("window.surrealGetBrowserDiagnostics()")
        serialized = json.dumps(diagnostics)
        for forbidden in ("C:\\\\", "/gamedata", "Botpack.u", "DM-Secret.unr",
                          "SECRET FILE CONTENT", "SECRET-ID", "allowlist", "CTF-Face", "DM-Zeta"):
            require(forbidden not in serialized, f"diagnostics leaked forbidden value {forbidden!r}")
        require("stack" not in diagnostics.get("crash", {}), "crash stack was exported")
        require(diagnostics["crash"]["phase"] == "engine-start", "crash phase missing")
        require(diagnostics["renderer"]["drawCalls"] == 50, "renderer status missing")
        require(diagnostics["storage"]["importer"]["fileCount"] == 500,
                "sanitized storage counts missing")
        require(diagnostics["launcher"]["networking"] == "offline-only",
                "offline-only product scope missing")
        require(diagnostics["notIncluded"] ==
                ["file contents", "local file paths", "commercial game data", "stack traces"],
                "diagnostic exclusion contract missing")

        with launcher.expect_download() as download_info:
            launcher.click("[data-launcher-download]")
        download = download_info.value
        require(download.suggested_filename == "surrealengine-webxr-diagnostics.json",
                f"wrong diagnostic filename: {download.suggested_filename}")
        downloaded = json.loads(Path(download.path()).read_text(encoding="utf-8"))
        require(downloaded["schema"] == "surrealengine-webxr-diagnostics",
                "download was not the diagnostics document")

        launcher.context.grant_permissions(["clipboard-read", "clipboard-write"], origin=base_url)
        launcher.click("[data-launcher-copy]")
        launcher.wait_for_function("document.querySelector('[data-launcher-action-status]').textContent.includes('copied')")
        clipboard = launcher.evaluate("navigator.clipboard.readText()")
        require(json.loads(clipboard)["version"] == 1, "clipboard diagnostics were invalid")

        retry_result = launcher.evaluate("""async () => {
          const controller = window.surrealLauncherController;
          const first = controller.retry();
          const second = await controller.retry();
          const during = {calls: window.__retryCalls, second, pending: controller.actionPending,
            retryDisabled: document.querySelector("[data-launcher-retry]").disabled};
          window.__retryResolve();
          const target = await first;
          return {during, target, callbackTarget: window.__retryTarget,
            pendingAfter: controller.actionPending};
        }""")
        require(retry_result["during"] == {"calls": 1, "second": False, "pending": "retry",
                                           "retryDisabled": True} and
                retry_result["pendingAfter"] is None,
                f"retry action was not generation-safe: {retry_result}")
        retry_url = retry_result["target"]
        require("launcher=1" in retry_url and "map=DM-Morpheus" in retry_url and "game=" not in retry_url,
                f"retry URL did not preserve only safe selection: {retry_url}")
        launcher.evaluate("""() => {
          window.__restartTarget = null;
          window.surrealLauncherController.options.restart = async value => { window.__restartTarget = value; };
        }""")
        launcher.click("[data-launcher-restart]")
        launcher.wait_for_function("window.__restartTarget !== null")
        restart_target = launcher.evaluate("window.__restartTarget")
        require("launcher=1" in restart_target and "map=" not in restart_target,
                f"restart-to-launcher action was wrong: {restart_target}")

        # An unsafe automatic map query must fail closed instead of reaching
        # callMain with attacker-controlled URL syntax.
        rejected = browser.new_page()
        rejected_errors = collect_page_errors(rejected)
        rejected.goto(base_url + "/?build=build-emscripten&map=DM-Foo%3Fgame%3DEvil.Game",
                      wait_until="load")
        rejected.wait_for_function("window.surrealLauncherController.state === 'crashed'")
        rejected_state = rejected.evaluate("""() => ({
          calls: window.__callMainCalls, booted: window.surrealBooted,
          crashed: window.surrealCrashed,
          phase: window.surrealLauncherController.crash.phase
        })""")
        require(rejected_state["calls"] == 0 and rejected_state["booted"] is False,
                f"unsafe automatic map reached main: {rejected_state}")
        require(rejected_state["phase"] == "automatic-map-validation" and rejected_state["crashed"],
                f"unsafe automatic map did not expose crash state: {rejected_state}")
        require(not rejected_errors, f"rejected page errors: {rejected_errors}")
        require(not launcher_errors, f"launcher page errors: {launcher_errors}")

        result = {
            "passed": True,
            "checks": 76,
            "automatic": automatic_state,
            "keyboard": keyboard_result,
            "controller": controller_input,
            "deliberate": deliberate,
            "diagnosticSchema": diagnostics["schema"],
            "rejected": rejected_state,
        }
        browser.close()
        return result


def run_real(base_url: str, build: str, headed: bool) -> dict[str, object]:
    """Exercise the actual built engine and local developer-preloaded data."""
    query = urlencode({"launcher": "1", "build": build, "launcher-ux-real": "1"})
    url = base_url.rstrip("/") + "/web/index_webxr.html?" + query
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel="chrome", headless=not headed)
        page = browser.new_page()
        errors = collect_page_errors(page)
        page.goto(url, wait_until="load")
        page.wait_for_function(
            "surrealLauncherController.state === 'ready'", timeout=120_000)
        before = page.evaluate("""() => ({
          booted: window.surrealBooted,
          state: surrealLauncherController.state,
          phase: surrealLauncherController.loadingPhase,
          progressHidden: document.querySelector("[data-launcher-loading]").hidden,
          manualFocused: document.activeElement.matches("[data-launcher-map]"),
          mapState: surrealLauncherController.mapPickerState
        })""")
        require(before["booted"] is False and before["state"] == "ready" and
                before["phase"] == "ready" and before["progressHidden"] is True and
                before["manualFocused"] is True,
                f"real deliberate launcher did not stop at ready: {before}")
        page.fill("[data-launcher-map]", "DM-Deck16][")
        page.press("[data-launcher-map]", "Enter")
        page.wait_for_function("window.surrealBooted === true", timeout=120_000)
        page.bring_to_front()
        first_tick = page.evaluate("window.surrealGetTickCount()")
        time.sleep(0.75)
        after = page.evaluate("""() => ({
          tick: window.surrealGetTickCount(),
          state: surrealLauncherController.state,
          phase: surrealLauncherController.loadingPhase,
          crashed: window.surrealCrashed,
          networking: window.surrealGetBrowserDiagnostics().launcher.networking,
          fatalLogs: window.surrealLog.filter(line =>
            typeof line === "string" && line.toLowerCase().includes("fatal error"))
        })""")
        require(after["tick"] > first_tick and after["state"] == "running" and
                after["phase"] == "running" and after["crashed"] is None and
                after["networking"] == "offline-only" and not after["fatalLogs"] and not errors,
                f"real data-backed deliberate launch failed: before={before}, after={after}, errors={errors}")
        result = {"passed": True, "url": url, "before": before, "firstTick": first_tick,
                  "after": after, "pageErrors": errors}
        browser.close()
        return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--headed", action="store_true")
    parser.add_argument("--real-base-url")
    parser.add_argument("--real-build", default="build-emscripten")
    args = parser.parse_args()
    result = run(headless=not args.headed)
    if args.real_base_url:
        result["realDataBacked"] = run_real(args.real_base_url, args.real_build, args.headed)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
