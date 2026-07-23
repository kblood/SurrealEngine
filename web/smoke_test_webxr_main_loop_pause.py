"""Real-Chrome proof that a paused Emscripten main loop does not reenter Wasm during Asyncify suspension."""
import contextlib
import http.server
import pathlib
import threading

from playwright.sync_api import sync_playwright


ROOT = pathlib.Path(__file__).resolve().parents[1]


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def log_message(self, *_):
        pass


with contextlib.chdir(ROOT):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
page_errors = []
console_errors = []
try:
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel="chrome", headless=True)
        page = browser.new_page()
        page.on("pageerror", lambda error: page_errors.append(str(error)))
        page.on("console", lambda message: console_errors.append(message.text) if message.type == "error" else None)
        page.goto(f"http://127.0.0.1:{server.server_port}/web/webxr_main_loop_pause_probe.html")
        page.wait_for_function("globalThis.webXRMainLoopPauseProbeReady === true")
        result = page.evaluate("runWebXRMainLoopPauseProbe()")
        assert result["before"] >= 3, result
        assert result["zeroEntriesDuringSuspension"], result
        assert result["resumedAfterResolution"], result
        assert not page_errors, page_errors
        assert not console_errors, console_errors
        browser.close()
        print("PASS: EngineMainLoopCallback had zero entries during unresolved Asyncify render and resumed afterward")
        print(result)
finally:
    server.shutdown()
    server.server_close()
