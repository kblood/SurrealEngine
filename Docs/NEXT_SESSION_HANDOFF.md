# Next-session handoff

Date: 2026-07-23 (Europe/Copenhagen)

## Start here

The active integration worktree is:

```text
C:\Devstuff\QuestGames\surreal-webxr-release-gates
branch: integration/unified-engine
HEAD: 483055722d42486f62afe9e3909e334d57c44775
```

The worktree was clean when this handoff was written. Push integration changes
to the `fork` remote (`kblood/SurrealEngine`), not `origin`; `origin` is
`dpjudas/SurrealEngine` and rejects direct pushes. The branch is pushed through
the HEAD above.

The product binaries are intentionally frozen at commit
`71650dd7e80411532cc2baaca6623ad9404b25a9`. Commits after that frozen point are
tests and documentation only. Do not rebuild merely to make the package match
the later documentation HEAD unless production inputs change.

The persistent goal tracker currently says `blocked` because an ordinary C++
test subagent triggered a false cybersecurity classification. Engineering is
not blocked. Resume the goal in the UI if offered; do not interpret that stale
status as a product failure.

## User intent and operating rules

The user wants one modular Surreal Engine foundation supporting desktop,
native OpenXR, flat browser WASM, and WebXR without turning each port into a
separate engine fork. VR/WebXR should be optional providers/extensions over
shared engine, view, input, UI, audio, and game behavior.

Continue to preserve these rules:

- keep generic upstream candidates small and understandable to the maintainer;
- keep native OpenXR, WebXR, Deus Ex, bots, and release infrastructure on
  independently reviewable topic layers;
- do not deploy WebXR publicly until a real Quest 3 + VDXR physical pass;
- do not package or copy commercial game data;
- never launch a visible game/browser automatically; give the user an explicit
  candidate and let them start it;
- keep keyboard/mouse active alongside XR input;
- use isolated worktrees for parallel writing agents;
- do not touch `C:\Devstuff\QuestGames\surreal-unified` or its
  `feature/fullbody-vr-avatar` work unless the user explicitly switches tasks;
- update docs, commit, and push periodically, but push to `fork`.

The maintainer, dpJudas, will consider small fixes the contributor understands
and can explain. Avoid large AI refactors or bundled PRs. See
`Docs/UPSTREAM_PR_CANDIDATES.md`.

## Final candidate artifacts

### Native OpenXR

Use this exact archive:

```text
C:\Devstuff\QuestGames\release-candidates\SurrealEngine-Native-OpenXR-71650dd7-headless-rc.zip
size: 31,142,083 bytes
SHA-256: 42d64b2660ef7c7dc208979ee1199a0b0b645a950a432ebc1d97aaa5cde6cf56
source SHA-256: 2533a389bfa8baa046960fa6ab03c88d3052856c6a22f2052a28c7d0f67dacdf
```

It passed the full native Release registry (40/40), the focused OpenXR/XR/input/
VM/default-controls selection (21/21), an independent 42/42 internal manifest
audit, x64/dependency checks, embedded Git-archive commit verification, and a
zero-commercial-data scan. It has not been executed for physical qualification.

### WebXR

Use the corrected revisioned package, not the older non-revisioned directory:

```text
C:\Devstuff\QuestGames\release-candidates\SurrealEngine-WebXR-71650dd7-revisioned
size: 36,946,289 bytes (25 files)
intended base: /webxr/Ports/SurrealEngine-Candidate-71650dd7/
manifest SHA-256: 8aca19284b53ba8765be89511f4cc24f11d04c30926ef8b4710d90451b8d5c39
HOSTING.txt SHA-256: 2690a271deadc3ea7e2c06be4c70d62beb6433e36867ef6ee8f777990c461a57
```

Content hashes:

```text
JS:     22dd5ec417ea5debdb0d9d9a8bd5b0bd5d6d2f759e81c22ae5b1ddba4ce128dd
WASM:   1ca34875dbfbd822904195ea90bceb2727a91552c210d563c3a5ede7c04acb77
source: 25c47c542c26943adb7b01e928f25f2987959fd4c2ddc728c6761a79f27af101
```

Checksums and audit:

```text
C:\Devstuff\QuestGames\release-candidates\SurrealEngine-WebXR-71650dd7-revisioned.SHA256SUMS.txt
C:\Devstuff\QuestGames\artifacts\SurrealEngine-WebXR-71650dd7-revisioned-audit.md
```

The corrected package passed its staged headless Chrome smoke, 24/24 manifest
payload verification, no-data scan, corresponding-source and all 46 declared
SurrealVideo-file checks, LGPL/relinking/source-offer/link validation, and
hosting-config inspection. JS, WASM, and source are byte-identical to the
previously validated package; only `HOSTING.txt` and `release-manifest.json`
changed to record the correct revisioned lowercase site path.

Do not deploy the older package at
`release-candidates\SurrealEngine-WebXR-71650dd7`; it records the wrong
uppercase, non-revisioned base path.

## Automated validation already completed

The frozen candidate passed:

- native Release build and 40/40 CTest;
- focused OpenXR/XR/input/VM/default-controls 21/21;
- browser launcher 18/18;
- importer 30/30;
- mutable IndexedDB/OPFS persistence 17/17;
- bootstrap 3/3;
- all 11 Node suites and JavaScript syntax checks;
- real and synthetic pointer-lock/relative-motion gates;
- Asyncify main-loop/native callback and audio probes;
- WebXR ABI v4, two-phase/current-pose rendering, WebGPU-to-WebGL bridge,
  async texture lifetime, and reprojection probes;
- no-data, package, source, provenance, and LGPL gates.

Unreal Gold flat WebGPU also rendered Vortex2 and Bluff successfully in the
owner-data harness. Bluff produced nonzero stereo music buffers; Vortex2
produced nonzero mono buffers. This establishes decoding/queueing, not audible
headset output or WebXR presentation.

## Physical qualification is the next product gate

Follow `Docs/RC_PHYSICAL_QUALIFICATION.md`. The next visible run must be
user-started from the exact native archive above. Start with native OpenXR
through the launcher and Quest 3 + Virtual Desktop + VDXR, then test the exact
revisioned WebXR directory from an isolated secure local host or the eventual
revisioned HTTPS candidate URL.

Do not silently count a flat-window fallback as WebXR success. Record:

- projection/FOV, eye orientation, world swim, convergence, and draw distance;
- intro/menu topmost quad, Escape/trigger handoff, and no black-screen trap;
- both controller proxies plus dominant laser/contact/highlight agreement;
- right- and left-dominant settings;
- head-relative left-stick locomotion and right-stick turning;
- Rocket Launcher and Flak Cannon primary/alternate controller aim;
- mouse fallback and persistent WASD/non-inverted desktop controls;
- audio across intro/menu/map changes, focus loss, immersive exit, and re-entry;
- UT99 ladder start/transition (the `ClearSkins` VM crash is fixed);
- Unreal Gold title/menu, Vortex Rikers rendering, ambient audio, and music;
- WebXR headset report before entry, while running, failure/exit, and re-entry.

Only deploy publicly after this physical pass.

## Desktop control issue and unrelated visible launches

The user repeatedly saw games with old arrow/inverted controls. Those processes
were not this integration candidate. They were launched by separate worktrees:

```text
C:\Devstuff\QuestGames\surreal-unified\build\Release\SurrealEngine.exe
  --avatar-autorig-debug --avatar-ik-synthetic ...

C:\Devstuff\QuestGames\surreal-unified\build-demo-integration\Release\SurrealEngine.exe
  --autoplay --url=Unreal C:\Devstuff\QuestGames\demos\UnrealSpecialEdition
```

Do not interfere with those projects, but coordinate with their agents before
physical testing so they do not open visible windows.

The live GOG profiles were explicitly corrected on disk:

```text
C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY\System\SE-User.ini
C:\Program Files (x86)\GOG Galaxy\Games\Unreal Gold\System\SE-User.ini
```

Both now have `W=MoveForward`, `A=StrafeLeft`, `S=MoveBackward`,
`D=StrafeRight`, and `bInvertMouse=False`; arrows remain secondary. The frozen
candidate also contains the one-time fresh/recognized-legacy migration and
browser persistence tests. Do not overwrite genuine custom layouts.

## Important implemented fixes

The integration includes:

- OpenXR extension/device diagnostics and correct Vulkan selection;
- asymmetric vertical projection fix and exact runtime swapchain extent;
- focused-session startup routing, Escape/menu route, head-relative movement,
  and controller-scoped weapon aim;
- native and WebXR controller UI visuals, exact laser hit contact, optional
  native overlay fallback, and provider-neutral UI surfaces;
- WebXR same-rAF current-pose render path with ABI v4;
- shared persistent right/left dominant-hand setting across OpenXR and WebXR;
- flat WASM launcher/import, pointer lock, WebAudio lifecycle, and mutable OPFS/
  IndexedDB overlay;
- persistent modern desktop-control migration;
- UnrealScript array evaluation preserving `AccessedNone`, fixing the UT99
  ladder `UMenuPlayerMeshClient.ClearSkins` crash;
- data-free release packaging with LGPL source/relinking materials.

See `Docs/UnifiedEngine.md`, `Docs/XR_DOMINANT_HAND.md`,
`Docs/INTEGRATION_BRANCH_MAP.md`, and
`Docs/RELEASE_NOTES_71650dd7_DRAFT.md`.

## Upstream PR preparation

The strongest small upstream candidate is the VM `AccessedNone` fix. It is
reconstructed independently from current upstream:

```text
worktree: C:\Devstuff\QuestGames\worktrees\pr-accessed-none-array
branch: pr/accessed-none-array
base: origin/master c2599d51ecde93b2308f8165de33e9296feca685
commit: e62a9ab55c85bad2a7819f0a0137a67a4fc02bf4
```

It changes the two evaluator methods plus an opt-in test target and covers all
four cases: fixed/dynamic array with `AccessedNone` from index/array operands.
Release engine build and the focused test pass. It is not pushed and no PR is
open. The remaining human gate is the real owned-data UMenu path plus a final
line-by-line review before submission.

Keep desktop-control policy and browser audio/package harnesses fork-only.
Other possible small upstream fixes are listed in
`Docs/UPSTREAM_PR_CANDIDATES.md`.

## Website deployment state

Nothing is deployed. The eventual public candidate target is:

```text
https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-71650dd7/
server candidate path: /var/www/html/webxr/Ports/SurrealEngine-Candidate-71650dd7/
```

The server path remains provisional until the real document root is confirmed.
Follow `Docs/WEBXR_DEPLOYMENT_DRY_RUN_71650dd7.md`. Confirm PHP/directory-index
behavior, Apache override/header/MIME support, permissions/free space, proxy or
CDN cache/header behavior, and source-offer/legal duration before upload. The
`Ports` root must not gain an HTML index that hides the desired PHP file
listing; the individual SurrealEngine application directory does require its
own `index.html`.

Deploy atomically to the revisioned directory, verify real HTTPS response
headers, and keep rollback possible. Do not promote or overwrite a stable path
before headset qualification.

## Configure model-specific subagents before the next session

Current Codex configuration has no `[agents]` section and no
`C:\Users\Caldor\.codex\agents\` directory. Current official Codex docs support
`agents.default_subagent_model`, `agents.default_subagent_reasoning_effort`, and
custom agent TOML files with their own `model` and `model_reasoning_effort`.

Recommended addition to `C:\Users\Caldor\.codex\config.toml`:

```toml
[agents]
enabled = true
max_concurrent_threads_per_session = 6
default_subagent_model = "gpt-5.6"
default_subagent_reasoning_effort = "high"
```

Create `C:\Users\Caldor\.codex\agents\terra_explorer.toml`:

```toml
name = "terra_explorer"
description = "Fast read-heavy explorer for repository scans, logs, documentation, and preliminary test analysis."
model = "gpt-5.6-terra"
model_reasoning_effort = "medium"
sandbox_mode = "read-only"
developer_instructions = """
Explore the assigned question without modifying files.
Gather concrete evidence, cite files and symbols, and return a concise report.
Do not launch visible applications or games.
"""
```

Create `C:\Users\Caldor\.codex\agents\cpp_reviewer.toml`:

```toml
name = "cpp_reviewer"
description = "Deep C++ correctness and regression-test reviewer."
model = "gpt-5.6"
model_reasoning_effort = "high"
sandbox_mode = "read-only"
developer_instructions = """
Review C++ changes like a project maintainer.
Trace execution and ownership, check undefined behavior and regressions,
inspect tests, and run only headless verification.
Do not modify files or launch visible applications unless explicitly asked.
"""
```

Restart Codex or open a fresh session after creating these files. Then explicitly
request named agents, for example:

```text
Use cpp_reviewer to independently review commit e62a9ab5 and rerun its four
regression cases. Use terra_explorer for read-heavy branch comparison.
```

Official references:

- https://learn.chatgpt.com/docs/agent-configuration/subagents
- https://learn.chatgpt.com/docs/config-file/config-basic

Do not claim a named/model-specific agent was used unless its configuration was
loaded in the current session. The current `spawn_agent` tool schema did not
expose a per-call `model` field.

## Suggested first actions next session

1. Add the `[agents]` defaults and the two named agent files above, then restart
   Codex/open a new session.
2. Read this handoff and `Docs/RC_PHYSICAL_QUALIFICATION.md`.
3. Confirm no unrelated `surreal-unified` process is launching visible games.
4. Have `cpp_reviewer` independently inspect `e62a9ab5` if desired.
5. Give the user the exact native `71650dd7` ZIP and wait for their explicit
   Quest 3/VDXR launch and feedback.
6. Fix only evidenced physical failures, rebuild/re-audit only when production
   inputs change, and keep the WebXR public deployment gated.

