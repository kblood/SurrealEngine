import sys
import time
from pathlib import Path
from playwright.sync_api import sync_playwright

sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

BUILD_DIR = next((arg.split("=", 1)[1] for arg in sys.argv[1:] if arg.startswith("--build=")), "build-emscripten")
URL = "http://localhost:8091/web/index_webxr.html?build=" + BUILD_DIR
EXPERIMENTAL_WEBGPU_XR = "--experimental-webgpu-xr" in sys.argv[1:]

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
  if (%(experimental)s && !dev.supportedFeatures.includes('webgpu')) {
    dev.supportedFeatures.push('webgpu');
  }
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
        launch_args = []
        if EXPERIMENTAL_WEBGPU_XR:
            # Chromium exposes these as separate about:flags entries. Keep
            # this opt-in: the production path must continue to describe
            # what an ordinary shipping browser provides.
            launch_args.append("--enable-features=WebXRWebGPUBinding,WebXRLayers")
        browser = p.chromium.launch(channel="chrome", args=launch_args)
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
        iwer_init = IWER_INIT_SCRIPT % {"experimental": "true" if EXPERIMENTAL_WEBGPU_XR else "false"}
        page.add_init_script(iwer_src + iwer_init)

        page.goto(URL, wait_until="load")

        # Pure-JS M8 ABI/input coverage is independent of XRGPUBinding and therefore
        # remains deterministic under IWER. It also catches JS/C++ layout drift
        # before a real headset is needed.
        frame_abi = page.evaluate("window.surrealXRFrameABIDiagnostic")
        print(f"[harness] XR frame ABI v2 diagnostic = {frame_abi}")
        if (not frame_abi or not frame_abi.get("passed") or
                frame_abi.get("byteSize") != 532 or frame_abi.get("inputBytes") != 128):
            print("FAIL: packed XR frame ABI offsets/stride diagnostic failed")
            sys.exit(1)

        input_collector = page.evaluate("window.surrealXRTestInputCollector()")
        print(f"[harness] M8 copied input-source collector = {input_collector}")
        if not input_collector or not input_collector.get("passed"):
            print("FAIL: WebXR input collector did not copy/normalize/change/clear sources")
            sys.exit(1)

        haptics = page.evaluate("window.surrealXRTestHaptics()")
        print(f"[harness] M8 fake-actuator haptic policy = {haptics}")
        if (not haptics or not haptics.get("passed") or
                haptics.get("fakeActuatorsOnly") is not True):
            print("FAIL: WebXR haptic routing/policy fake-actuator diagnostic failed")
            sys.exit(1)

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
        xr_gpu_binding = page.evaluate("window.surrealXRGPUBindingAvailable")
        print(f"[harness] navigator.xr.isSessionSupported('immersive-vr') via IWER = {xr_supported}")
        print(f"[harness] navigator.gpu.requestAdapter({{xrCompatible:true}}) = {gpu_compatible}")
        print(f"[harness] XRGPUBinding exposed by browser = {xr_gpu_binding}")
        print(f"[harness] experimental WebGPU/XR flags requested = {EXPERIMENTAL_WEBGPU_XR}")
        print("\n".join(page.evaluate("window.surrealXRLog") or []))

        if not xr_supported:
            print("FAIL: IWER-emulated navigator.xr did not report immersive-vr support")
            sys.exit(1)

        # Session entry also resumes Emscripten OpenAL from the same trusted
        # click, so wait for callMain to finish creating the engine and its
        # AudioContext in both the default and experimental test paths.
        print("[harness] waiting for engine boot and Web Audio context...")
        deadline = time.time() + 120
        while time.time() < deadline:
            if page.evaluate("window.surrealBooted === true && window.surrealGetWebAudioState() !== 0"):
                break
            crashed = page.evaluate("window.surrealCrashed")
            if crashed:
                print(f"FAIL: engine boot failed: {crashed}")
                sys.exit(1)
            time.sleep(0.5)
        else:
            print("FAIL: engine or Web Audio context did not become ready")
            sys.exit(1)

        native_frame_abi = page.evaluate("window.surrealGetNativeWebXRFrameABI()")
        print(f"[harness] native XR frame ABI v2 = {native_frame_abi}")
        if native_frame_abi != {"version": 2, "headerBytes": 44, "viewBytes": 116,
                               "inputBytes": 128, "maxInputs": 2}:
            print("FAIL: JS and native packed XR frame ABI definitions disagree")
            sys.exit(1)

        if EXPERIMENTAL_WEBGPU_XR and xr_gpu_binding:
            print("[harness] waiting for engine XR-compatible GPUDevice...")
            deadline = time.time() + 120
            while time.time() < deadline:
                if page.evaluate("!!window.surrealWebGPUDevice && window.surrealBooted === true"):
                    break
                time.sleep(0.5)
            else:
                print("FAIL: engine XR-compatible GPUDevice was not acquired")
                sys.exit(1)

            texture_import = page.evaluate("window.surrealTestWebGPUTextureImport()")
            print(f"[harness] JS GPUTexture -> C++ WGPUTexture import = {texture_import}")
            if texture_import != 1:
                print("FAIL: Emdawnwebgpu could not import the browser-owned GPUTexture")
                sys.exit(1)

            texture_readback = page.evaluate("window.surrealVerifyWebGPUTextureImport()")
            print(f"[harness] imported-texture C++ clear/readback: {texture_readback}")
            if not texture_readback.get("matches"):
                print("FAIL: C++ render pass output did not survive the JS/WGPU texture bridge")
                sys.exit(1)
            error_count = page.evaluate("window.surrealGetWebGPUErrorCount()")
            if error_count != 0:
                print(f"FAIL: {error_count} WebGPU uncaptured error(s) after interop test")
                sys.exit(1)

            # Queue one normal UT frame into layer 1 of a browser-owned 2D-array
            # texture. This exercises WebGPURenderDevice::Lock/Unlock and the
            # complete scene pipeline across the same ownership boundary that an
            # XRGPUSubImage color texture will use.
            external_frames_before = page.evaluate("window.surrealGetWebGPUExternalRenderTargetFrames()")
            queued = page.evaluate("window.surrealQueueWebGPUExternalRenderTarget()")
            external_state = page.evaluate("window.surrealGetWebGPUExternalRenderTargetState()")
            print(f"[harness] browser-owned full-frame target queued = {queued} (state={external_state})")
            if queued != 1:
                print("FAIL: engine rejected the browser-owned external render target")
                sys.exit(1)

            deadline = time.time() + 10
            while time.time() < deadline:
                external_frames_after = page.evaluate("window.surrealGetWebGPUExternalRenderTargetFrames()")
                if external_frames_after > external_frames_before:
                    break
                time.sleep(0.05)
            else:
                print("FAIL: queued external target was not consumed by an engine frame")
                sys.exit(1)

            external_readback = page.evaluate("window.surrealVerifyWebGPUExternalRenderTarget()")
            print(f"[harness] full UT frame in browser-owned array texture: {external_readback}")
            if not external_readback.get("rendered"):
                print("FAIL: browser-owned array layer did not contain a rendered UT frame")
                sys.exit(1)
            if page.evaluate("window.surrealGetWebGPUErrorCount()") != 0:
                print("FAIL: WebGPU uncaptured error after external full-frame render")
                sys.exit(1)

            # Prove frame-loop ownership can move from Emscripten's window
            # RAF to a future native XRSession RAF and back. XR subimages are
            # only valid during their XR callback, so this is a prerequisite
            # for consuming them synchronously rather than on a later frame.
            xr_loop_active = page.evaluate("window.surrealSetXRFrameLoopActive(true)")
            time.sleep(0.15)  # allow any already-dispatched window RAF to finish
            paused_tick = page.evaluate("window.surrealGetTickCount()")
            time.sleep(0.25)
            paused_tick_after = page.evaluate("window.surrealGetTickCount()")
            print(f"[harness] XR loop pause tick stability: {paused_tick} -> {paused_tick_after}")
            if xr_loop_active != 1 or paused_tick_after != paused_tick:
                print("FAIL: normal Emscripten frame loop did not pause for XR ownership")
                sys.exit(1)

            for _ in range(3):
                manual_tick = page.evaluate("window.surrealRunXRFrame()")
            print(f"[harness] three XR-driven engine frames: {paused_tick_after} -> {manual_tick}")
            if manual_tick != paused_tick_after + 3:
                print("FAIL: XR-driven frame export did not advance exactly one tick per call")
                sys.exit(1)

            # While the window RAF remains paused, advance simulation exactly once
            # and render two fake-eye projections into array layers 0 and 1. This is
            # the M5 phase-ordering and attachment-switch proof; native XRView
            # matrices replace the fake projections in M6/M7.
            stereo_result = page.evaluate("window.surrealTestWebGPUExternalStereoFrame()")
            stereo_tick = page.evaluate("window.surrealGetTickCount()")
            print(f"[harness] one-tick/two-eye stereo frame: result={stereo_result}, tick {manual_tick} -> {stereo_tick}")
            if stereo_result != 1 or stereo_tick != manual_tick + 1:
                print("FAIL: stereo render did not use exactly one simulation tick")
                sys.exit(1)
            stereo_readback = page.evaluate("window.surrealVerifyWebGPUExternalStereoTarget()")
            print(f"[harness] two browser-owned eye layers: {stereo_readback}")
            if not stereo_readback.get("rendered"):
                print("FAIL: layered stereo target did not contain two distinct rendered views")
                sys.exit(1)
            if page.evaluate("window.surrealGetWebGPUErrorCount()") != 0:
                print("FAIL: WebGPU uncaptured error after layered stereo render")
                sys.exit(1)
            manual_tick = stereo_tick

            # Exercise the production-shaped M6 ABI rather than only the M5
            # fake-eye C++ diagnostic: JS packs two XR-shaped views, C++
            # validates/copies them, imports the shared texture synchronously,
            # advances once, and consumes each supplied layer/viewport/projection.
            packed_result = page.evaluate("window.surrealTestPackedWebXRFrame()")
            packed_tick = page.evaluate("window.surrealGetTickCount()")
            packed_error = page.evaluate("window.surrealGetWebXRFrameLastError()")
            print(f"[harness] packed M6 XR frame: result={packed_result}, error={packed_error}, tick {manual_tick} -> {packed_tick}")
            if packed_result != 1 or packed_error != 0 or packed_tick != manual_tick + 1:
                print("FAIL: packed WebXR frame bridge did not consume exactly one stereo simulation frame")
                sys.exit(1)
            packed_readback = page.evaluate("window.surrealVerifyWebGPUExternalStereoTarget()")
            print(f"[harness] packed M6 eye layers: {packed_readback}")
            if not packed_readback.get("rendered"):
                print("FAIL: packed WebXR frame did not render distinct output into both supplied layers")
                sys.exit(1)
            if page.evaluate("window.surrealGetWebGPUErrorCount()") != 0:
                print("FAIL: WebGPU uncaptured error after packed WebXR frame")
                sys.exit(1)

            input_diagnostics = page.evaluate("window.surrealGetWebXRInputDiagnostics()")
            print(f"[harness] M8 native input publish/map diagnostics: {input_diagnostics}")
            expected_axes = [0.25, 0.75, -0.5, 0.25]
            if (input_diagnostics.get("selfTest") != 1 or
                    input_diagnostics.get("publishedSources") != 2 or
                    input_diagnostics.get("processedSources") != 2 or
                    input_diagnostics.get("publishedGeneration") !=
                    input_diagnostics.get("processedGeneration") or
                    input_diagnostics.get("buttonsHeld") != 129 or
                    any(abs(actual - expected) > 0.0001 for actual, expected in
                        zip(input_diagnostics.get("axes", []), expected_axes)) or
                    any(abs(actual - expected) > 0.0001 for actual, expected in
                        zip(input_diagnostics.get("triggers", []), [0.6, 0.9])) or
                    input_diagnostics.get("poseFlags") != [4, 7]):
                print("FAIL: M8 packed input was not published and mapped exactly once")
                sys.exit(1)

            controller_pose_diagnostics = page.evaluate("""() => ({
                selfTest: Module.ccall('Surreal_RunWebXRControllerPoseSelfTest', 'number', [], []),
                dominantIndex: Module.ccall('Surreal_GetWebXRDominantControllerIndex', 'number', [], [])
            })""")
            print(f"[harness] M8 world controller-pose diagnostics: {controller_pose_diagnostics}")
            if (controller_pose_diagnostics.get("selfTest") != 1 or
                    controller_pose_diagnostics.get("dominantIndex") != 1):
                print("FAIL: M8 controller poses were not composed or dominant hand was lost")
                sys.exit(1)

            weapon_bridge_diagnostics = page.evaluate("""() => ({
                aimSelfTest: Module.ccall('Surreal_RunWebXRWeaponAimSelfTest', 'number', [], []),
                hapticsSelfTest: Module.ccall('Surreal_RunWebXRHapticsBridgeSelfTest', 'number', [], []),
                disableAccepted: Module.ccall('Surreal_SetWebXRHapticsEnabled', 'number', ['number'], [0]),
                enableAccepted: Module.ccall('Surreal_SetWebXRHapticsEnabled', 'number', ['number'], [1]),
                expectedWeaponEyes: Module.ccall('Surreal_GetWebXRWeaponOverlayExpectedEyePasses', 'number', [], []),
                weaponEyePasses: Module.ccall('Surreal_GetWebXRWeaponOverlayEyePasses', 'number', [], []),
                weaponCalls: Module.ccall('Surreal_GetWebXRWeaponOverlayCalls', 'number', [], [])
            })""")
            print(f"[harness] M8 weapon-aim/haptic bridge diagnostics: {weapon_bridge_diagnostics}")
            if any(weapon_bridge_diagnostics.get(name) != 1 for name in
                   ("aimSelfTest", "hapticsSelfTest", "disableAccepted", "enableAccepted")) or \
                    weapon_bridge_diagnostics.get("expectedWeaponEyes") != 2 or \
                    weapon_bridge_diagnostics.get("weaponEyePasses") != 2 or \
                    not 0 <= weapon_bridge_diagnostics.get("weaponCalls", -1) <= 2:
                print("FAIL: M8 weapon classifier/restoration or haptic bridge is invalid")
                sys.exit(1)
            page.evaluate("window.surrealResetWebXRPose()")

            pose_diagnostics = page.evaluate("window.surrealGetWebXRPoseDiagnostics()")
            print(f"[harness] M7 pose conversion diagnostics: {pose_diagnostics}")
            if (pose_diagnostics.get("selfTest") != 1 or
                    abs(pose_diagnostics.get("worldUnitsPerMeter", 0) - 39.3701) > 0.001):
                print("FAIL: WebXR pose math self-test or Unreal-unit scale is invalid")
                sys.exit(1)

            color_formats = page.evaluate("window.surrealTestWebGPUExternalColorFormats()")
            color_error_count = page.evaluate("window.surrealGetWebGPUErrorCount()")
            print(f"[harness] XR projection color-format families: {color_formats}, "
                  f"GPU errors={color_error_count}")
            format_counts = [
                color_formats.get("before", 0),
                color_formats.get("afterBGRA8", 0),
                color_formats.get("afterRGBA8", 0),
                color_formats.get("afterRGBA16F", 0),
            ]
            if (color_formats.get("bgra8") != 1 or color_formats.get("rgba8") != 1 or
                    color_formats.get("rgba16f") != 1 or
                    format_counts != sorted(format_counts) or format_counts[-1] < 3 or
                    color_formats.get("lastFormat", 0) == 0 or color_error_count != 0):
                print("FAIL: required WebXR projection formats did not render/cache cleanly")
                sys.exit(1)
            manual_tick = page.evaluate("window.surrealGetTickCount()")

            if page.evaluate("window.surrealSetXRFrameLoopActive(false)") != 0:
                print("FAIL: normal Emscripten frame loop did not accept ownership back")
                sys.exit(1)
            deadline = time.time() + 2
            while time.time() < deadline:
                resumed_tick = page.evaluate("window.surrealGetTickCount()")
                if resumed_tick > manual_tick:
                    break
                time.sleep(0.05)
            else:
                print("FAIL: normal Emscripten frame loop did not resume after XR handoff")
                sys.exit(1)
            print(f"[harness] window frame loop resumed: {manual_tick} -> {resumed_tick}")

            projection_probe = page.evaluate("window.surrealXRProbeWebGPUProjection()")
            print(f"[harness] WebGPU projection-layer probe: {projection_probe}")

            # IWER supplies a JavaScript XRSession, not Blink's native wrapper.
            # Exercise the production entry point anyway and require an honest,
            # recoverable failure at that boundary. This protects the native
            # path's setup cleanup without pretending IWER proves presentation.
            native_start_tick = page.evaluate("window.surrealGetTickCount()")
            native_start = page.evaluate("window.surrealXREnterNativeWebGPU()")
            native_diagnostics = page.evaluate("window.surrealXRNativeDiagnostics")
            deadline = time.time() + 2
            native_recovery_tick = native_start_tick
            while time.time() < deadline:
                native_recovery_tick = page.evaluate("window.surrealGetTickCount()")
                if native_recovery_tick > native_start_tick:
                    break
                time.sleep(0.05)
            print(f"[harness] native WebGPU XR entry under IWER: result={native_start}, "
                  f"diagnostics={native_diagnostics}, tick {native_start_tick} -> {native_recovery_tick}")
            if (native_start is not False or
                    native_diagnostics.get("phase") != "error" or
                    native_diagnostics.get("engineLoopOwned") is not False or
                    not native_diagnostics.get("error") or
                    page.evaluate("window.surrealXRSessionActive") is not False):
                print("FAIL: native WebGPU XR setup failure did not restore the canvas loop cleanly")
                sys.exit(1)
            # Phase 2 intentionally verifies the default IWER lifecycle after
            # native cleanup, so do not let the expected boundary error poison it.
            page.evaluate("window.surrealXRError = null; window.surrealXRNativeError = null")

        # --- Phase 2: request a session and drive its frame loop ----------
        print("[harness] requesting immersive-vr session...")
        # A real button click supplies the trusted gesture required by both
        # requestSession() and the Emscripten OpenAL AudioContext resume.
        # UT captures the pointer after boot. Release it before a normal
        # Playwright mouse click so XR entry receives the trusted event.
        page.evaluate("document.exitPointerLock()")
        page.click("#entervr")

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

        audio_state = page.evaluate("window.surrealGetWebAudioState()")
        print(f"[harness] Web Audio state after Enter VR click = {audio_state}")
        if audio_state != 2:
            audio_error = page.evaluate("Module.surrealWebAudioLastError || ''")
            print(f"FAIL: Web Audio did not resume from the XR entry gesture: {audio_error}")
            sys.exit(1)

        print("[harness] session active, watching XR frame counter for 3s...")
        f1 = page.evaluate("window.surrealXRFrameCount")
        engine_f1 = page.evaluate("window.surrealGetTickCount()")
        time.sleep(3)
        f2 = page.evaluate("window.surrealXRFrameCount")
        engine_f2 = page.evaluate("window.surrealGetTickCount()")
        print(f"[harness] XR frame count: {f1} -> {f2}; engine tick: {engine_f1} -> {engine_f2}")
        print("\n".join(page.evaluate("window.surrealXRLog") or []))

        if f2 <= f1:
            print(f"FAIL: XR session frame loop did not advance ({f1} -> {f2})")
            sys.exit(1)

        # An immersive session may hide its companion DOM page. That must not
        # mute game audio or stop XR frames; visibility only pauses audio when
        # no immersive session is active.
        active_hidden = page.evaluate(
            "window.surrealXRApplyVisibilityPolicy(true, 'playwright-active-xr')")
        hidden_frame_before = page.evaluate("window.surrealXRFrameCount")
        time.sleep(0.25)
        hidden_frame_after = page.evaluate("window.surrealXRFrameCount")
        hidden_audio_state = page.evaluate("window.surrealGetWebAudioState()")
        print(f"[harness] active-XR hidden policy: {active_hidden}, frames "
              f"{hidden_frame_before} -> {hidden_frame_after}, audio={hidden_audio_state}")
        if (active_hidden.get("audioPolicy") != "kept-running-for-xr" or
                hidden_audio_state != 2 or hidden_frame_after <= hidden_frame_before):
            print("FAIL: hidden companion page interrupted immersive XR audio or frames")
            sys.exit(1)
        page.evaluate("window.surrealXRApplyVisibilityPolicy(false, 'playwright-active-xr-visible')")

        # XRSession visibility is independent of document visibility. Hidden
        # presentation suspends audio; visible-blurred is still presentation-
        # active and resumes it, matching visible.
        xr_hidden = page.evaluate(
            "window.surrealXRApplySessionVisibilityPolicy('hidden', 'playwright-xr-hidden')")
        xr_hidden_audio = page.evaluate("window.surrealGetWebAudioState()")
        dom_visible_during_xr_hidden = page.evaluate(
            "window.surrealXRApplyVisibilityPolicy(false, 'playwright-dom-visible-xr-hidden')")
        dom_visible_during_xr_hidden_audio = page.evaluate("window.surrealGetWebAudioState()")
        xr_blurred = page.evaluate(
            "window.surrealXRApplySessionVisibilityPolicy('visible-blurred', "
            "'playwright-xr-blurred')")
        xr_blurred_audio = page.evaluate("window.surrealGetWebAudioState()")
        print(f"[harness] XR visibility audio policy: hidden={xr_hidden}, "
              f"DOM-visible-while-XR-hidden={dom_visible_during_xr_hidden}, "
              f"blurred={xr_blurred}, audio={xr_hidden_audio}/"
              f"{dom_visible_during_xr_hidden_audio}/{xr_blurred_audio}")
        if (xr_hidden.get("audioPolicy") != "suspended-xr-hidden" or
                xr_hidden_audio != 1 or
                dom_visible_during_xr_hidden.get("audioPolicy") != "suspended-xr-hidden" or
                dom_visible_during_xr_hidden_audio != 1 or
                xr_blurred.get("audioPolicy") != "running-xr-visible-blurred" or
                xr_blurred_audio != 2):
            print("FAIL: XR hidden/visible-blurred audio policy was not enforced")
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

        # An immersive runtime is allowed to suppress the companion window's
        # RAF while presentation is active. The engine canvas loop must be
        # observable again after session end, including after the earlier
        # native setup rejection.
        deadline = time.time() + 5
        recovered_engine_tick = engine_f2
        while time.time() < deadline:
            recovered_engine_tick = page.evaluate("window.surrealGetTickCount()")
            if recovered_engine_tick > engine_f2:
                break
            time.sleep(0.05)
        print(f"[harness] post-session canvas loop recovery: {engine_f2} -> {recovered_engine_tick}")
        if recovered_engine_tick <= engine_f2:
            print("FAIL: canvas frame loop did not recover after immersive session end")
            sys.exit(1)

        # Outside XR, a hidden page suspends Web Audio. Returning visible only
        # resumes when this lifecycle policy performed that suspension.
        hidden_idle = page.evaluate(
            "window.surrealXRApplyVisibilityPolicy(true, 'playwright-idle-hidden')")
        visible_idle = page.evaluate(
            "window.surrealXRApplyVisibilityPolicy(false, 'playwright-idle-visible')")
        idle_audio_state = page.evaluate("window.surrealGetWebAudioState()")
        print(f"[harness] idle visibility audio policy: hidden={hidden_idle}, "
              f"visible={visible_idle}, audio={idle_audio_state}")
        if (hidden_idle.get("audioPolicy") != "suspended-hidden" or
                visible_idle.get("audioPolicy") != "running-visible" or
                idle_audio_state != 2):
            print("FAIL: idle visibility policy did not suspend and restore Web Audio")
            sys.exit(1)

        # Force failure after requestSession succeeds but before render-state
        # setup completes. The returned XRSession must receive end(), globals
        # must be cleared, and a subsequent real entry must still succeed.
        setup_failure = page.evaluate("""
            (async function () {
                const xr = navigator.xr;
                const original = xr.requestSession.bind(xr);
                let acquired = null;
                let ended = false;
                Object.defineProperty(xr, 'requestSession', {
                    configurable: true,
                    value: async function(mode, options) {
                        acquired = await original(mode, options);
                        acquired.addEventListener('end', function() { ended = true; });
                        Object.defineProperty(acquired, 'updateRenderState', {
                            configurable: true,
                            value: async function() {
                                throw new Error('playwright injected render-state failure');
                            }
                        });
                        return acquired;
                    }
                });
                let result;
                try { result = await window.surrealXREnter(); }
                finally {
                    Object.defineProperty(xr, 'requestSession', {
                        configurable: true, value: original
                    });
                }
                await new Promise(resolve => setTimeout(resolve, 50));
                return {
                    result: result,
                    acquired: !!acquired,
                    ended: ended,
                    active: window.surrealXRSessionActive,
                    error: window.surrealXRError
                };
            })()
        """)
        print(f"[harness] injected post-request setup failure cleanup: {setup_failure}")
        if (setup_failure.get("result") is not False or
                not setup_failure.get("acquired") or not setup_failure.get("ended") or
                setup_failure.get("active") is not False or not setup_failure.get("error")):
            print("FAIL: post-request XR setup failure leaked its acquired session")
            sys.exit(1)
        page.evaluate("window.surrealXRError = null")

        # Repeat the full entry/exit cycle. Generation-scoped callbacks must
        # keep the old session's delayed end/RAF events from clearing the new
        # session's globals.
        print("[harness] repeating XR entry/exit cycle...")
        page.evaluate("window.surrealXRError = null")
        page.evaluate("document.exitPointerLock()")
        page.click("#entervr")
        deadline = time.time() + 20
        while time.time() < deadline:
            if page.evaluate("window.surrealXRSessionActive === true"):
                break
            if page.evaluate("window.surrealXRError"):
                print(f"FAIL: repeat XR entry failed: {page.evaluate('window.surrealXRError')}")
                sys.exit(1)
            time.sleep(0.1)
        else:
            print("FAIL: repeat XR entry did not become active")
            sys.exit(1)
        repeat_f1 = page.evaluate("window.surrealXRFrameCount")
        time.sleep(0.5)
        repeat_f2 = page.evaluate("window.surrealXRFrameCount")
        page.evaluate("window.surrealXRExit()")
        deadline = time.time() + 10
        while time.time() < deadline:
            if page.evaluate("window.surrealXRSessionActive === false"):
                break
            time.sleep(0.1)
        else:
            print("FAIL: repeat XR exit did not complete")
            sys.exit(1)
        repeat_lifecycle = page.evaluate("window.surrealXRLifecycle")
        print(f"[harness] repeat XR lifecycle: frames {repeat_f1} -> {repeat_f2}, "
              f"diagnostics={repeat_lifecycle}")
        if (repeat_f2 <= repeat_f1 or
                repeat_lifecycle.get("sessionsStarted") != 2 or
                repeat_lifecycle.get("sessionsEnded") != 2):
            print("FAIL: repeated XR entry/exit was not generation-safe")
            sys.exit(1)

        print("[harness] running third clean XR entry/exit cycle...")
        page.evaluate("window.surrealXRError = null; document.exitPointerLock()")
        page.click("#entervr")
        deadline = time.time() + 20
        while time.time() < deadline:
            if page.evaluate("window.surrealXRSessionActive === true"):
                break
            if page.evaluate("window.surrealXRError"):
                print(f"FAIL: third XR entry failed: {page.evaluate('window.surrealXRError')}")
                sys.exit(1)
            time.sleep(0.1)
        else:
            print("FAIL: third XR entry did not become active")
            sys.exit(1)
        third_f1 = page.evaluate("window.surrealXRFrameCount")
        time.sleep(0.5)
        third_f2 = page.evaluate("window.surrealXRFrameCount")
        page.evaluate("window.surrealXRExit()")
        deadline = time.time() + 10
        while time.time() < deadline:
            if page.evaluate("window.surrealXRSessionActive === false"):
                break
            time.sleep(0.1)
        else:
            print("FAIL: third XR exit did not complete")
            sys.exit(1)
        third_lifecycle = page.evaluate("window.surrealXRLifecycle")
        print(f"[harness] third XR lifecycle: frames {third_f1} -> {third_f2}, "
              f"diagnostics={third_lifecycle}")
        if (third_f2 <= third_f1 or
                third_lifecycle.get("sessionsStarted") != 3 or
                third_lifecycle.get("sessionsEnded") != 3):
            print("FAIL: third clean XR cycle was not generation-safe")
            sys.exit(1)

        # Enter once more, then destroy the real engine GPUDevice. This runs
        # last because device loss is irreversible. The production device.lost
        # observer must end the session and permanently reject future entries.
        print("[harness] testing active-session WebGPU device loss...")
        page.evaluate("window.surrealXRError = null")
        page.evaluate("document.exitPointerLock()")
        page.click("#entervr")
        deadline = time.time() + 20
        while time.time() < deadline:
            if page.evaluate("window.surrealXRSessionActive === true"):
                break
            time.sleep(0.1)
        else:
            print("FAIL: device-loss XR session did not become active")
            sys.exit(1)
        page.evaluate("window.surrealWebGPUDevice.destroy()")
        deadline = time.time() + 10
        while time.time() < deadline:
            if page.evaluate(
                    "window.surrealXRLifecycle.deviceLost === true && "
                    "window.surrealXRSessionActive === false"):
                break
            time.sleep(0.1)
        else:
            print("FAIL: GPUDevice loss did not clean up the active XR session")
            sys.exit(1)
        device_loss_lifecycle = page.evaluate("window.surrealXRLifecycle")
        rejected_after_loss = page.evaluate("window.surrealXREnter()")
        shutdown_first = page.evaluate("window.surrealXRShutdown('playwright-shutdown')")
        shutdown_second = page.evaluate("window.surrealXRShutdown('playwright-shutdown-repeat')")
        print(f"[harness] device-loss/shutdown lifecycle: {device_loss_lifecycle}, "
              f"reentry={rejected_after_loss}, shutdown={shutdown_first}/{shutdown_second}")
        if (rejected_after_loss is not False or shutdown_first is not True or
                shutdown_second is not False or
                device_loss_lifecycle.get("lastExitReason") != "device-lost"):
            print("FAIL: device loss or idempotent page shutdown policy failed")
            sys.exit(1)

        print("\n".join(page.evaluate("window.surrealXRLog") or []))

        real_errors = [e for e in page_errors]
        if real_errors:
            print(f"FAIL: {len(real_errors)} unhandled page error(s): {real_errors}")
            sys.exit(1)

        print(
            "PASS: IWER-emulated Meta Quest 3 sessions handled visibility/audio, "
            f"repeat entry ({repeat_f1} -> {repeat_f2}), device loss, and shutdown; "
            f"xrCompatible GPUDevice probe = {gpu_compatible}"
        )
        browser.close()


if __name__ == "__main__":
    main()
