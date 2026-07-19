import sys
import time
from pathlib import Path
from playwright.sync_api import sync_playwright

sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

URL = "http://localhost:8091/web/index_webxr.html"

# M4-groundwork harness (see web/webxr_session.js's header comment and
# WEBXR_IMPLEMENTATION_PLAN.md's M4 milestone). Ported from the sibling
# webxr-port/ (QuakeQuest) project's headless WebXR test pattern
# (src/web-host/test/m4-session-cycle-test.mjs): inject IWER (Immersive Web
# Emulation Runtime) before the page's scripts run, install a Meta Quest 3
# emulated device, then drive navigator.xr.requestSession() the same way a
# real headset browser would - proving the session-request/lifecycle
# plumbing works headlessly in this project's build, before the real
# stereo-render path (M4 proper) is built on top of it. This does NOT
# render stereo UT99 geometry - the XR session's frame loop is not wired to
# SurrealEngine's WebGPU renderer yet (see webxr_session.js).
IWER_JS_PATH = Path(__file__).parent / "node_modules" / "iwer" / "build" / "iwer.min.js"

# IWER installs a Meta Quest 3 emulated XRDevice by overriding navigator.xr
# (forceInstall: true, since real Chrome already defines navigator.xr as a
# WebXR-Device-API object even with no headset attached - see
# iwer.js:10289's own comment about not clobbering a real one unless asked).
IWER_INIT_SCRIPT = """
(function() {
  const dev = new IWER.XRDevice(IWER.metaQuest3);
  dev.installRuntime({ forceInstall: true });
  dev.stereoEnabled = true;
  window.__xrdevice = dev;
})();
"""


def main():
    if not IWER_JS_PATH.exists():
        print(f"FAIL: iwer not found at {IWER_JS_PATH} - run 'npm install' in web/ first")
        sys.exit(2)
    iwer_src = IWER_JS_PATH.read_text(encoding="utf-8")

    with sync_playwright() as p:
        # Same constraint as smoke_test_webgpu.py: Playwright's bundled
        # Chromium cannot acquire a WebGPU adapter headlessly under any
        # launch-flag combination tried during M2. A real Chrome/Edge
        # install works with zero extra flags. IWER's navigator.xr override
        # works the same way in a real Chrome install (confirmed here) -
        # no special XR-related launch flags needed since IWER replaces the
        # navigator.xr object at the JS level rather than relying on any
        # underlying browser XR device support.
        browser = p.chromium.launch(channel="chrome")
        page = browser.new_page()

        console_lines = []
        page.on("console", lambda msg: console_lines.append(msg.text))
        page_errors = []
        page.on("pageerror", lambda exc: page_errors.append(str(exc)))

        # Playwright's add_init_script is the sync-API equivalent of
        # puppeteer's page.evaluateOnNewDocument used by the sibling
        # project's m4-session-cycle-test.mjs - runs before any of the
        # page's own <script> tags, so window.navigator.xr is already
        # IWER's emulated device by the time webxr_session.js executes.
        page.add_init_script(iwer_src + IWER_INIT_SCRIPT)

        page.goto(URL, wait_until="load")

        # --- Phase 1: capability checks (no engine boot dependency) -------
        deadline = time.time() + 30
        checked = False
        while time.time() < deadline:
            checked = page.evaluate("window.surrealXRSupported !== null && window.surrealXRGPUCompatible !== null")
            if checked:
                break
            time.sleep(0.5)

        if not checked:
            print("FAIL: TIMEOUT waiting for surrealXRCheckSupport()/surrealXRCheckGPUCompatible() to resolve")
            print("\n".join(page.evaluate("window.surrealXRLog") or []))
            sys.exit(1)

        xr_supported = page.evaluate("window.surrealXRSupported")
        gpu_compatible = page.evaluate("window.surrealXRGPUCompatible")
        print(f"[harness] navigator.xr.isSessionSupported('immersive-vr') via IWER = {xr_supported}")
        print(f"[harness] navigator.gpu.requestAdapter({{xrCompatible:true}}) = {gpu_compatible}")
        print("\n".join(page.evaluate("window.surrealXRLog") or []))

        if not xr_supported:
            print("FAIL: IWER-emulated navigator.xr did not report immersive-vr support")
            sys.exit(1)

        # --- Phase 2: request a session and drive its frame loop ----------
        print("[harness] requesting immersive-vr session...")
        page.evaluate("window.surrealXREnter()")

        deadline = time.time() + 20
        active = False
        while time.time() < deadline:
            active = page.evaluate("window.surrealXRSessionActive === true")
            err = page.evaluate("window.surrealXRError")
            if err:
                print(f"FAIL: surrealXRError set during session request: {err}")
                print("\n".join(page.evaluate("window.surrealXRLog") or []))
                sys.exit(1)
            if active:
                break
            time.sleep(0.25)

        if not active:
            print("FAIL: TIMEOUT waiting for surrealXRSessionActive after surrealXREnter()")
            print("\n".join(page.evaluate("window.surrealXRLog") or []))
            sys.exit(1)

        print("[harness] session active, watching XR frame counter for 3s...")
        f1 = page.evaluate("window.surrealXRFrameCount")
        time.sleep(3)
        f2 = page.evaluate("window.surrealXRFrameCount")
        print(f"[harness] XR frame count: {f1} -> {f2}")
        print("\n".join(page.evaluate("window.surrealXRLog") or []))

        if f2 <= f1:
            print(f"FAIL: XR session frame counter did not advance ({f1} -> {f2})")
            sys.exit(1)

        # --- Phase 3: end the session cleanly ------------------------------
        print("[harness] ending XR session...")
        page.evaluate("window.surrealXRExit()")

        deadline = time.time() + 10
        ended = False
        while time.time() < deadline:
            ended = page.evaluate("window.surrealXRSessionActive === false")
            if ended:
                break
            time.sleep(0.25)

        if not ended:
            print("FAIL: TIMEOUT waiting for surrealXRSessionActive to clear after surrealXRExit()")
            sys.exit(1)

        print("\n".join(page.evaluate("window.surrealXRLog") or []))

        real_errors = [e for e in page_errors]
        if real_errors:
            print(f"FAIL: {len(real_errors)} unhandled page error(s): {real_errors}")
            sys.exit(1)

        print(
            "PASS: IWER-emulated Meta Quest 3 session requested, produced frames "
            f"({f1} -> {f2}), and ended cleanly; xrCompatible GPUDevice probe = {gpu_compatible}"
        )
        browser.close()


if __name__ == "__main__":
    main()
