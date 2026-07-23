# Game-data distribution and desktop web packaging

Status: decision record, reviewed 2026-07-22. This is a conservative engineering
and release recommendation based on public materials, not legal advice.

## Decisions

1. SurrealEngine releases contain engine code and assets created for the project,
   but no original game packages, maps, textures, music, executables, installers,
   disc images, demos, or shareware archives.
2. The launcher may link to a current acquisition page and may import files the
   user selects from their own installation. It must not silently download game
   data from mirrors or treat an Internet Archive copy as distribution authority.
3. A package is redistributable only after its own licence/readme or written
   rightsholder permission has been reviewed and recorded. “Free download,”
   “DRM-free,” “demo,” and “shareware” do not by themselves grant redistribution.
4. Publish the browser build first as one hosted WASM/WebGPU application with a
   flat desktop mode and an optional WebXR provider. Offer PWA installation as the
   first desktop-app experience.
5. Keep native OpenXR as the supported PC-VR route until desktop immersive WebXR,
   and specifically WebGPU/WebXR integration, passes an explicit support matrix.
   Electron or a native webview may later package the same flat web build, but is
   not assumed to make WebXR work.

## Game-data findings

The matrix covers games listed in [Status.md](Status.md), plus the two Harry
Potter games recognized by `GameFolder`. “No grant found” means no current,
public, package-specific permission was located in this review; it is not a legal
judgment about every historical release.

| Game | Current user acquisition found | Redistribution finding | Launcher/release policy |
| --- | --- | --- | --- |
| Unreal Tournament (1999) | [OldUnreal's UT GOTY installer](https://www.oldunreal.com/downloads/unrealtournament/full-game-installers/) downloads the original disc image and applies the current patch. | The page permits users to download it, but publishes no third-party rebundling grant. OldUnreal's general [download disclaimer](https://www.oldunreal.com/downloads/) directs claims to the relevant author/company; it is not a licence grant. No current, authoritative UT demo redistribution grant was located. | Link to the OldUnreal page or import a user-owned install. Do not mirror the installer or disc image. |
| Unreal / Unreal Gold | [OldUnreal's Unreal Gold installer](https://www.oldunreal.com/downloads/unreal/full-game-installers/) likewise downloads the original disc image and patches it. | No public grant to rebundle the base game was located. The separate [227 patch release notes](https://www.oldunreal.com/patch/unreal/oldunreal/227ReleaseNotes.pdf) do expressly allow free redistribution of that patch if it remains unmodified and the OldUnreal page is referenced; that permission does not extend to original game data. | Link/import the base game. Treat a patch as a separate artefact with its own exact licence and provenance; linking remains the simpler default. |
| Deus Ex: Game of the Year Edition | Current purchase/download pages include [GOG](https://www.gog.com/en/game/deus_ex) and [Steam](https://store.steampowered.com/app/6910/Deus_Ex_Game_of_the_Year_Edition/). | Distributor terms provide personal use, not a right for this project to redistribute the game. No current rightsholder-hosted demo with an affirmative redistribution grant was located. | Detect/import the user's GOG, Steam, or original-media install. Link to store pages; bundle no data. |
| Rune Classic | [GOG](https://www.gog.com/en/game/rune_classic) offers a current user download. | No third-party redistribution grant located. | Link/import only. |
| Clive Barker's Undying | [GOG](https://www.gog.com/en/game/clive_barkers_undying) offers a current user download. | No third-party redistribution grant located. | Link/import only. |
| The Wheel of Time | [GOG](https://www.gog.com/en/game/the_wheel_of_time) offers a current user download. | No third-party redistribution grant located. | Link/import only. |
| Klingon Honor Guard | No current rightsholder/distributor download was located. | No demo or full-game redistribution grant located. | User-owned original media/install only; do not link to abandonware mirrors. |
| Nerf Arena Blast | No current sale was located; [GOG Dreamlist](https://www.gog.com/dreamlist/game/nerf-arena-blast) is a request page, not a distribution licence. | No demo or full-game redistribution grant located. | User-owned original media/install only. |
| TNN Outdoors Pro Hunter | No current rightsholder/distributor download was located. | No demo or full-game redistribution grant located. | User-owned original media/install only. |
| Tactical Ops: Assault on Terror | Historical mod/standalone downloads exist on third-party sites, but no current rightsholder page or package-specific redistribution permission was located. | “Originally free” is not sufficient evidence for rebundling. | Import a user-provided UT mod or standalone installation only. |
| Harry Potter 1 / 2 | Recognized internally but not listed as supported in `Status.md`; no current authorized PC download was located. | No demo or game redistribution grant located. | User-owned original media/install only; do not advertise as supported yet. |

The store agreements reinforce the same default. The current [Steam Subscriber
Agreement](https://store.steampowered.com/subscriber_agreement/_agreement/?l=english)
licenses content for personal, non-commercial use and bars copying or distribution
without permission. The current [GOG User Agreement](https://items.gog.com/preview/GOG_User_Agreement_EN.pdf)
allows personal enjoyment and backups but bars distribution without permission.
The [Epic Games Store EULA](https://legal.epicgames.com/store/eula?lang=en-US)
similarly bars distribution or transfer. A DRM-free offline installer is therefore
convenient input for the owner; it is not redistributable input for our release.

### Required release controls

- Keep downloadable engine artefacts data-free and add a CI/package audit that
  rejects known UE1 game extensions and unusually large unknown files.
- Make acquisition links configuration/data, not hard-coded download behavior.
- Show the source and terms beside every external link. Never scrape, proxy, or
  cache the linked payload on project infrastructure.
- Require the user to select or confirm a local game directory. Copy only into
  user-owned browser storage when required by the web platform, and explain that
  the copy remains local to that browser profile.
- Store an evidence record before adding any redistributable demo: exact archive
  hash, publisher/rightsholder, licence/readme, permission text, source URL,
  review date, permitted territory, permitted modifications, and attribution.
- If a future licence is ambiguous, distribute no payload until the rightsholder
  supplies written permission.

## Reusing the web build on desktop

The engine-facing architecture should remain one Emscripten/WASM application and
one WebGPU renderer. Environment-specific code supplies storage, windowing,
permissions, and presentation:

```text
shared engine + WASM + WebGPU
              |
              +-- flat browser / installed PWA
              +-- WebXR provider when navigator.xr and session type are supported
              +-- optional Electron shell (flat first)
              +-- optional native webview shell (flat first, platform-specific)

native engine + native renderer
              +-- flat desktop
              +-- OpenXR provider (supported PC-VR route)
```

The flat web mode must be the baseline: keyboard, mouse, gamepad, ordinary canvas,
and game-data import cannot depend on an XR session. WebXR should activate only
after feature detection and user initiation, and exiting a session must return to
the same flat application state.

### User gesture and native startup boundary

`Module.callMain()` is a potentially long synchronous boundary, not a completed
launch notification. The shared launcher therefore publishes `native-startup`,
reveals the ordinary canvas with an explicit loading status, and yields through a
browser paint opportunity before entering it. Only after `callMain()` returns may
the state advance to `native-ready`, presentation activation, and `running`.
Exposed startup diagnostics are deliberately limited to fixed stage names and
monotonic elapsed durations; they contain no selected title, local path, imported
file, URL, or engine log data.

WebXR uses a matching two-phase provider contract. `prepareLaunch()` runs inside
the Play submit gesture and calls `surrealXRRequestSession()` immediately, which
reserves the browser session but does not create projection layers, request XR
frames, submit controller input, or transfer the engine frame loop. After native
startup returns, `activate()` calls `surrealXRActivateReservedSession()` to create
the presentation resources and begin XR frame ownership. A rejected or ended
reservation keeps the same application running in its flat canvas with existing
mouse and keyboard input. Legacy provider scripts may still use the one-step
`surrealXREnter()` wrapper.

### Packaging comparison

| Option | Reuse | Benefits | Costs and XR risk | Recommendation |
| --- | --- | --- | --- | --- |
| Browser + installed PWA | Exact hosted build; no wrapper fork | Smallest release and security-maintenance burden. [web.dev](https://web.dev/learn/pwa/installation?hl=en) documents desktop installation in Chrome and Edge, an OS window/icon, and no required executable package. | Browser-controlled permissions and storage; install behavior differs by browser. It does not improve the browser's underlying WebXR support. | **First choice** for flat desktop WASM/WebGPU and the supported browser/Quest WebXR path. |
| Electron | Same web assets inside bundled Chromium; a small preload bridge can handle native file selection if needed | Cross-platform executable and pinned browser engine. [Electron's introduction](https://www.electronjs.org/docs/latest/) confirms Chromium and Node are bundled. | Larger package and a continuing Chromium/Node update obligation. Electron gives no documented guarantee that a desktop headset/runtime is exposed to WebXR. Its [security guide](https://www.electronjs.org/docs/latest/tutorial/security) warns against enabling experimental Chromium/Blink features, requires isolation/sandboxing, and recommends rapid framework updates. | Optional second-stage **flat desktop** package only. Gate any immersive mode on a tested release/runtime matrix; do not ship it by turning on unsafe experimental flags. |
| Thin native shell (for example, WebView2 on Windows) | Same web assets, plus a narrow native bridge | Smaller Windows wrapper and native file-picker/launcher integration. Microsoft's [WebView2 overview](https://learn.microsoft.com/en-us/microsoft-edge/webview2/) explicitly supports web/native code sharing. | Platform-specific shell and test matrix. The [runtime distribution guide](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution) requires an Evergreen or fixed WebView2 runtime; fixed binaries add over 250 MB. Browser features may differ in an embedded control, and no official desktop immersive-WebXR guarantee was located. | Consider only if Windows-native launcher integration materially justifies it. Flat first. A custom native OpenXR-to-web bridge would be a new XR provider, not WebXR reuse. |

### Current WebXR constraint

[MDN's WebXR API reference](https://developer.mozilla.org/en-US/docs/Web/API/WebXR_Device_API)
still marks WebXR limited/experimental, unavailable in some widely used browsers,
and restricted to secure contexts. Applications must feature-detect `navigator.xr`,
call `isSessionSupported()`, require user activation, and retain a non-XR path.

There is an additional renderer-specific risk. Chrome's official
[WebGPU 135 release note](https://developer.chrome.com/blog/new-in-webgpu-135?hl=en)
described WebGPU integration with WebXR as experimental developer testing on
Windows and Android. This review found no later official Chrome release note that
clearly promotes that integration to a production-stable feature. That absence is
an inference, not proof of unsupported behavior, so the project must test rather
than assume it.

Minimum acceptance matrix before claiming desktop WebXR support:

- exact browser or wrapper version and operating system;
- headset and desktop XR runtime (including runtime version);
- `navigator.xr`, `immersive-vr`, and required layer/binding feature detection;
- WebGPU adapter/device creation before and after XR session entry;
- stereo presentation, controller lifecycle, focus loss, session exit/re-entry;
- flat keyboard/mouse/gamepad behavior after both successful and failed XR entry;
- local game import and persistence across application/browser upgrades.

The 2026-07-23 Quest 3 test through desktop Chrome, Virtual Desktop, and VDXR
does not satisfy this matrix. Automatic reached WebXR consent and then returned
to flat mode before confirmed presentation. The fallback canvas locked the
pointer and received mouse buttons, but relative mouse-look failed. This named
configuration therefore remains experimental with two concrete blockers rather
than merely untested; see `WEBXR_VDXR_QUALIFICATION.md`.

## Staged implementation recommendation

1. Ship the data-free native release with local-folder import and native OpenXR.
2. Finish one flat WASM/WebGPU build and validate desktop keyboard, mouse, gamepad,
   import, persistence, save data, and resize/fullscreen behavior.
3. Host that exact build over HTTPS and add a manifest/service worker so it can be
   installed as a PWA. Do not create a second desktop-web engine fork.
4. Add WebXR as an optional provider over the shared frame/view/presentation
   seams. Unsupported or failed sessions remain in flat mode with an actionable
   message.
5. Validate Quest browser WebXR as its own release target. Treat desktop browser
   WebXR as experimental until the matrix above passes on named configurations.
6. Prototype Electron only if users need an executable or browser-version pinning.
   Prototype WebView2 only if Windows launcher/file integration is worth a
   platform-specific shell. Neither prototype blocks the PWA release.
7. Recheck acquisition links, agreements, and any proposed demo licence before
   every release that changes game onboarding.
