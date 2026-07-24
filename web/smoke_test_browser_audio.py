"""Real-Chrome WebAudio/OpenAL graph-state smoke test using generated PCM."""
import contextlib, http.server, pathlib, sys, threading
from playwright.sync_api import sync_playwright

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASE_URL = next(
	(argument.split("=", 1)[1].rstrip("/") for argument in sys.argv[1:] if argument.startswith("--base-url=")),
	None,
)
class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()
    def log_message(self, *_): pass

server = None
if BASE_URL is None:
	with contextlib.chdir(ROOT):
		server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
	thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
	BASE_URL = f"http://127.0.0.1:{server.server_port}"
errors = []
try:
    with sync_playwright() as p:
        browser = p.chromium.launch(channel="chrome", headless=True, args=["--autoplay-policy=user-gesture-required"])
        page = browser.new_page(); page.on("pageerror", lambda error: errors.append(str(error)))
        page.goto(BASE_URL + "/web/browser_audio_probe.html")
        page.wait_for_function("window.probeReady === true")
        page.evaluate("audioController.suspend()")
        page.wait_for_function("audioController.diagnostics().state === 'suspended'")
        page.evaluate("audioController.refresh()")
        before = page.evaluate("audioController.diagnostics()")
        page.click("[data-audio-unlock]")
        page.wait_for_function("audioController.diagnostics().state === 'running'")
        page.wait_for_timeout(250)
        running = page.evaluate("({ d: audioController.diagnostics(), started: probeStartResult, playing: Module.ccall('Probe_PlayingSources', 'number'), queued: Module.ccall('Probe_QueuedMusicBuffers', 'number') })")
        page.locator("[data-audio-volume]").evaluate("e => { e.value='0.35'; e.dispatchEvent(new Event('input')); }")
        page.check("[data-audio-mute]")
        muted = page.evaluate("audioController.diagnostics()")
        page.evaluate("Object.defineProperty(document, 'visibilityState', {configurable:true, value:'hidden'}); document.dispatchEvent(new Event('visibilitychange'))")
        page.wait_for_function("audioController.diagnostics().state === 'suspended'")
        paused = page.evaluate("audioController.diagnostics()")
        page.evaluate("Object.defineProperty(document, 'visibilityState', {configurable:true, value:'visible'}); document.dispatchEvent(new Event('visibilitychange')); audioController.refresh()")
        page.click("[data-audio-unlock]")
        page.wait_for_function("audioController.diagnostics().state === 'running'")
        page.evaluate("dispatchEvent(new CustomEvent('surrealwebxrpresentation', {detail:{state:'running'}}))")
        page.wait_for_timeout(150)
        resumed = page.evaluate("audioController.diagnostics()")
        page.evaluate("dispatchEvent(new Event('pagehide'))")
        page.wait_for_function("audioController.diagnostics().shutdowns >= 1")
        shutdown = page.evaluate("audioController.diagnostics()")
        assert before["state"] == "suspended"
        assert running["d"]["currentTimeMs"] > before["currentTimeMs"] and running["started"] == 1 and running["queued"] >= 2
        assert muted["muted"] and abs(muted["volume"] - 0.35) < 0.01
        assert resumed["state"] == "running" and resumed["currentTimeMs"] > paused["currentTimeMs"]
        assert shutdown["shutdowns"] >= 1
        assert not errors, errors
        browser.close()
        print("PASS: generated SFX/music graph, unlock, mute/volume, suspend/resume and XR continuity")
finally:
	if server is not None:
		server.shutdown(); server.server_close()
