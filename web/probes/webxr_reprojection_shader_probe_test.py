import http.server
import json
import pathlib
import socketserver
import threading

from playwright.sync_api import sync_playwright


ROOT = pathlib.Path(__file__).resolve().parents[2]


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, _format, *_args):
        pass


with socketserver.TCPServer(("127.0.0.1", 0), lambda *args, **kwargs: QuietHandler(*args, directory=ROOT, **kwargs)) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel="chrome", headless=True)
        page = browser.new_page()
        page.goto(
            f"http://127.0.0.1:{server.server_address[1]}/web/probes/webxr_reprojection_shader_probe.html",
            wait_until="load",
        )
        page.wait_for_function("window.probeResult !== undefined")
        result = page.evaluate("window.probeResult")
        browser.close()
    server.shutdown()

print(json.dumps(result, indent=2))
if not result.get("ok"):
    raise SystemExit("real Chrome WebGL2 reprojection shader probe failed")
