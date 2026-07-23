"""Real-Chrome proof that SDL/HTML5 callbacks cannot reenter Wasm during Asyncify suspension."""
import contextlib
import http.server
import pathlib
import threading

from playwright.sync_api import sync_playwright


ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATED = ROOT / "build-webxr-native-callback-gate-em" / "WebXRNativeCallbackGateProbe.js"


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


with contextlib.chdir(ROOT):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
page_errors = []
console_errors = []
try:
    generated = GENERATED.read_text(encoding="utf-8")
    assert "SDL.receiveEvent" not in generated, "Unexpected legacy SDL1 event path was linked"
    assert "SurrealNativeCallGate" in generated, "Native callback gate was not linked"
    assert "JSEvents.registerOrRemoveHandler" in generated, "Emscripten HTML5 callback path was not linked"
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel="chrome", headless=True)
        page = browser.new_page()
        page.on("pageerror", lambda error: page_errors.append(str(error)))
        page.on("console", lambda message: console_errors.append(message.text) if message.type == "error" else None)
        page.goto(f"http://127.0.0.1:{server.server_port}/web/webxr_native_callback_gate_probe.html")
        page.wait_for_function("globalThis.webXRNativeCallbackGateProbeReady === true")
        result = page.evaluate("runWebXRNativeCallbackGateProbe()")
        assert result["zeroEntriesDuringSuspension"], result
        assert result["diagnosticsDuring"]["deferred"] >= 19, result
        assert result["resumedAfterUnblock"], result
        assert not result["missing"], result
        assert result["canceledDeferredTimer"] == 1, result
        assert not result["canceledTimerFired"], result
        assert result["sdlEvents"] > 0, result
        assert result["sdlWatchEntries"] > 0, result
        assert not page_errors, page_errors
        assert not console_errors, console_errors
        overflow_page = browser.new_page()
        overflow_errors = []
        overflow_page.on("pageerror", lambda error: overflow_errors.append(str(error)))
        overflow_page.goto(f"http://127.0.0.1:{server.server_port}/web/webxr_native_callback_gate_probe.html?overflow")
        overflow_page.wait_for_function("globalThis.webXRNativeCallbackGateProbeReady === true")
        overflow = overflow_page.evaluate("runWebXRNativeCallbackGateOverflowProbe()")
        assert overflow["overflowEvents"] == 1, overflow
        assert overflow["atOverflow"]["queued"] <= 4, overflow
        assert overflow["atOverflow"]["overflowed"], overflow
        assert overflow["afterMoreEvents"]["queued"] == overflow["atOverflow"]["queued"], overflow
        assert overflow["afterMoreEvents"]["overflows"] == 1, overflow
        assert overflow["callsAfterBurstBeforeResolve"] == overflow["callsAtOverflow"], overflow
        assert overflow["failedState"]["lastErrorCode"] == "native-callback-overflow", overflow
        assert not overflow["failedState"]["active"], overflow
        assert not overflow["afterCleanup"]["overflowed"], overflow
        assert overflow["afterCleanup"]["queued"] == 0, overflow
        assert overflow["freshSession"] and overflow["freshState"]["active"], overflow
        assert not overflow_errors, overflow_errors
        browser.close()
        print("PASS: DOM and SDL timer callbacks had zero Wasm entries during Asyncify suspension and resumed after unblock")
        print(result)
finally:
    server.shutdown()
    server.server_close()
