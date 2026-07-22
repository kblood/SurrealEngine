#!/usr/bin/env python3
"""Deterministic browser tests for WebXR safe-exit and tracking-loss UX."""

from __future__ import annotations

import argparse
import contextlib
import http.server
import json
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright


ROOT = Path(__file__).resolve().parent


PAGE = r"""<!doctype html>
<meta charset="utf-8">
<section id="webxr-headset-controls" hidden data-xr-active="false" data-tracking="inactive">
  <p data-xr-tracking></p>
  <p data-xr-exit-help></p>
  <button type="button" data-xr-safe-exit>Exit VR</button>
  <button type="button" data-xr-cancel-exit hidden>Keep Playing</button>
</section>
<canvas></canvas>
<script>
window.__sessions = [];
window.__nextOverlayType = "screen";
class FakeSession extends EventTarget {
  constructor() {
    super();
    this.inputSources = [];
    this.visibilityState = "visible";
    this.domOverlayState = window.__nextOverlayType ? {type: window.__nextOverlayType} : null;
    this.endCalls = 0;
    this._raf = null;
  }
  async updateRenderState(state) { this.renderState = state; }
  async requestReferenceSpace(type) { return {type}; }
  requestAnimationFrame(callback) { this._raf = callback; return 1; }
  end() {
    this.endCalls++;
    queueMicrotask(() => this.dispatchEvent(new Event("end")));
    return Promise.resolve();
  }
  fire(timestamp, poses) {
    const callback = this._raf;
    if (!callback) throw new Error("no XR RAF callback is queued");
    this._raf = null;
    const session = this;
    callback(timestamp, {
      session,
      getViewerPose() { return {views: [{eye: "left"}, {eye: "right"}]}; },
      getPose(space) { return poses && poses.has(space) ? poses.get(space) : null; },
    });
  }
}
Object.defineProperty(navigator, "xr", {configurable: true, value: {
  async isSessionSupported() { return true; },
  async requestSession(mode, init) {
    window.__lastMode = mode;
    window.__lastInit = init;
    const session = new FakeSession();
    window.__sessions.push(session);
    return session;
  }
}});
HTMLCanvasElement.prototype.getContext = function () {
  return {makeXRCompatible: async function () {}};
};
window.XRWebGLLayer = class {
  constructor() { this.framebufferWidth = 1024; this.framebufferHeight = 1024; }
};
</script>
<script src="/webxr_session.js"></script>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path == "/webxr_session.js":
            payload = (ROOT / "webxr_session.js").read_bytes()
            content_type = "text/javascript; charset=utf-8"
        else:
            payload = PAGE.encode("utf-8")
            content_type = "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
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
        yield f"http://127.0.0.1:{server.server_port}/"
    finally:
        server.shutdown()
        thread.join(timeout=5)
        server.server_close()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(headless: bool) -> dict[str, object]:
    with local_server() as url, sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=headless)
        page = browser.new_page()
        errors: list[str] = []
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.goto(url, wait_until="load")

        entered = page.evaluate("window.surrealXREnter()")
        require(entered is True, "fake lifecycle session did not enter")
        entry = page.evaluate("""() => ({
          active: window.surrealXRSessionActive,
          status: window.surrealXRGetInteractionStatus(),
          optional: window.__lastInit.optionalFeatures,
          overlayRootMatches: window.__lastInit.domOverlay.root ===
            document.getElementById("webxr-headset-controls"),
          uiHidden: document.getElementById("webxr-headset-controls").hidden
        })""")
        require(entry["active"] is True, "session was not active")
        require(entry["status"]["domOverlayType"] == "screen", "DOM Overlay grant not surfaced")
        require("dom-overlay" in entry["optional"], "DOM Overlay was not requested as optional")
        require(entry["overlayRootMatches"] is True, "wrong DOM Overlay root")
        require(entry["uiHidden"] is False, "exit UI was not visible during XR")

        # A visible action arms first and Keep Playing reversibly cancels it.
        page.click("[data-xr-safe-exit]")
        armed = page.evaluate("window.surrealXRGetInteractionStatus()")
        require(armed["exitPhase"] == "armed", "first visible exit action did not arm")
        require(page.evaluate("window.__sessions[0].endCalls") == 0,
                "first visible exit action ended XR without confirmation")
        page.click("[data-xr-cancel-exit]")
        require(page.evaluate("window.surrealXRGetInteractionStatus().exitPhase") == "idle",
                "Keep Playing did not cancel the armed exit")

        # Build two ordinary xr-standard controller snapshots. Pressing only
        # B/Y is the normal menu-cancel case and must never be an exit gesture.
        page.evaluate(r"""() => {
          const pose = x => ({transform: {
            position: {x, y: 1, z: -1},
            orientation: {x: 0, y: 0, z: 0, w: 1}
          }});
          const buttons = mask => Array.from({length: 6}, (_, index) => ({
            value: (mask & (1 << index)) ? 1 : 0,
            pressed: !!(mask & (1 << index)), touched: false
          }));
          window.__leftSpace = {};
          window.__rightSpace = {};
          window.__left = {handedness: "left", targetRaySpace: window.__leftSpace,
            gamepad: {connected: true, mapping: "xr-standard", buttons: buttons(0), axes: []}};
          window.__right = {handedness: "right", targetRaySpace: window.__rightSpace,
            gamepad: {connected: true, mapping: "xr-standard", buttons: buttons(1 << 5), axes: []}};
          window.__pose = pose;
          window.__buttons = buttons;
          const session = window.__sessions[0];
          session.inputSources = [window.__left, window.__right];
          session.fire(0, new Map([[window.__leftSpace, pose(-1)], [window.__rightSpace, pose(1)]]));
        }""")
        menu_cancel = page.evaluate("""() => ({
          endCalls: window.__sessions[0].endCalls,
          status: window.surrealXRGetInteractionStatus(),
          masks: window.surrealXRInputDiagnostics.lastPressedMasks
        })""")
        require(menu_cancel["endCalls"] == 0, "ordinary B/Y menu cancel ended XR")
        require(menu_cancel["status"]["chordPhase"] == "idle",
                "one-controller menu cancel armed the chord")
        require(menu_cancel["masks"] == [0, 32], "input masks were consumed or rewritten")

        # Losing one target ray gives clear degraded feedback; restoring it
        # automatically returns to healthy. Losing all pointers after the grace
        # period is a distinct loss and also recovers automatically.
        page.evaluate(r"""() => {
          const session = window.__sessions[0];
          session.fire(100, new Map([[window.__leftSpace, window.__pose(-1)]]));
        }""")
        degraded = page.evaluate("window.surrealXRGetInteractionStatus()")
        require(degraded["trackingPhase"] == "degraded", "partial pointer loss was not reported")
        require("recovery is automatic" in degraded["trackingMessage"],
                "tracking-loss feedback omitted recovery guidance")
        page.evaluate(r"""() => window.__sessions[0].fire(200,
          new Map([[window.__leftSpace, window.__pose(-1)], [window.__rightSpace, window.__pose(1)]]))""")
        recovered = page.evaluate("window.surrealXRGetInteractionStatus()")
        require(recovered["trackingPhase"] == "healthy" and recovered["trackingRecoveries"] == 1,
                "partial pointer tracking did not recover automatically")

        # Once both hands have been observed, removal of one entire source is
        # surfaced as controller loss instead of being mistaken for a healthy
        # one-controller setup; restoring the source clears it automatically.
        page.evaluate(r"""() => {
          const session = window.__sessions[0];
          session.inputSources = [window.__left];
          session.fire(300, new Map([[window.__leftSpace, window.__pose(-1)]]));
        }""")
        controller_lost = page.evaluate("window.surrealXRGetInteractionStatus()")
        require(controller_lost["trackingPhase"] == "degraded" and
                controller_lost["connectedControllers"] == 1 and
                controller_lost["expectedControllers"] == 2,
                "whole-controller loss was not reported")
        page.evaluate(r"""() => {
          const session = window.__sessions[0];
          session.inputSources = [window.__left, window.__right];
          session.fire(400, new Map([[window.__leftSpace, window.__pose(-1)],
                                     [window.__rightSpace, window.__pose(1)]]));
        }""")
        controller_recovered = page.evaluate("window.surrealXRGetInteractionStatus()")
        require(controller_recovered["trackingPhase"] == "healthy" and
                controller_recovered["trackingRecoveries"] == 2,
                "whole-controller loss did not recover automatically")
        page.evaluate("window.__sessions[0].fire(1500, new Map())")
        lost = page.evaluate("window.surrealXRGetInteractionStatus()")
        require(lost["trackingPhase"] == "lost", "full pointer loss was not reported")
        page.evaluate(r"""() => window.__sessions[0].fire(1600,
          new Map([[window.__leftSpace, window.__pose(-1)], [window.__rightSpace, window.__pose(1)]]))""")
        recovered_again = page.evaluate("window.surrealXRGetInteractionStatus()")
        require(recovered_again["trackingPhase"] == "healthy" and
                recovered_again["trackingRecoveries"] == 3,
                "full pointer tracking did not recover automatically")

        # The fallback uses only exposed grip (1) + secondary face (5) on both
        # controllers, requires a continuous 2.5-second hold, and does not mask
        # those bits from the engine-facing input snapshot.
        page.evaluate(r"""() => {
          const chord = (1 << 1) | (1 << 5);
          window.__left.gamepad.buttons = window.__buttons(chord);
          window.__right.gamepad.buttons = window.__buttons(chord);
          const poses = new Map([[window.__leftSpace, window.__pose(-1)],
                                 [window.__rightSpace, window.__pose(1)]]);
          window.__sessions[0].fire(2000, poses);
          window.__sessions[0].fire(4499, poses);
        }""")
        before_hold = page.evaluate("""() => ({
          endCalls: window.__sessions[0].endCalls,
          status: window.surrealXRGetInteractionStatus(),
          masks: window.surrealXRInputDiagnostics.lastPressedMasks
        })""")
        require(before_hold["endCalls"] == 0, "exit chord fired before its hold threshold")
        require(before_hold["status"]["chordPhase"] == "holding", "exit chord did not show progress")
        require(before_hold["masks"] == [34, 34], "exit chord buttons were consumed")
        page.evaluate(r"""() => window.__sessions[0].fire(4500,
          new Map([[window.__leftSpace, window.__pose(-1)],
                   [window.__rightSpace, window.__pose(1)]]))""")
        page.wait_for_function("window.surrealXRSessionActive === false")
        chord_exit = page.evaluate("""() => ({
          endCalls: window.__sessions[0].endCalls,
          lifecycle: window.surrealXRLifecycle,
          status: window.surrealXRGetInteractionStatus(),
          uiHidden: document.getElementById("webxr-headset-controls").hidden
        })""")
        require(chord_exit["endCalls"] == 1, "held exit chord did not end exactly once")
        require(chord_exit["lifecycle"]["lastExitReason"] == "safe-exit-controller-chord",
                "held exit chord reason was not preserved")
        require(chord_exit["status"]["active"] is False and chord_exit["uiHidden"] is True,
                "interaction UI did not cleanly deactivate")

        # A new generation ignores stale old-session events. The button API
        # rejects too-fast and cross-control confirmations before accepting a
        # delayed second activation of the same visible control.
        require(page.evaluate("window.surrealXREnter()") is True, "second session did not enter")
        page.evaluate("window.__sessions[0].dispatchEvent(new Event('end'))")
        require(page.evaluate("window.surrealXRSessionActive") is True,
                "stale prior-generation end event cleared the new session")
        safe_results = page.evaluate(r"""() => {
          const armed = window.surrealXRRequestSafeExit("desktop-button", 10000);
          const fast = window.surrealXRRequestSafeExit("desktop-button", 10050);
          const other = window.surrealXRRequestSafeExit("headset-overlay-button", 10400);
          const confirmed = window.surrealXRRequestSafeExit("desktop-button", 10400);
          return {armed, fast, other, confirmed};
        }""")
        require(safe_results["armed"]["action"] == "armed", "safe exit did not arm")
        require(safe_results["fast"]["action"] == "confirmation-too-fast",
                "too-fast confirmation was accepted")
        require(safe_results["other"]["action"] == "different-control",
                "a different control confirmed the armed exit")
        require(safe_results["confirmed"]["action"] == "confirmed",
                "delayed same-control confirmation was rejected")
        page.wait_for_function("window.surrealXRSessionActive === false")
        require(page.evaluate("window.__sessions[1].endCalls") == 1,
                "confirmed visible exit did not call session.end exactly once")
        require(page.evaluate("window.surrealXRLifecycle.lastExitReason") ==
                "safe-exit-desktop-button", "visible safe-exit reason was not preserved")
        require(not errors, f"browser errors: {errors}")

        result = {
            "passed": True,
            "checks": 38,
            "sessions": page.evaluate("window.__sessions.map(s => ({endCalls: s.endCalls}))"),
            "lifecycle": page.evaluate("window.surrealXRLifecycle"),
            "interaction": page.evaluate("window.surrealXRGetInteractionStatus()"),
        }
        browser.close()
        return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--headed", action="store_true")
    args = parser.parse_args()
    result = run(headless=not args.headed)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
