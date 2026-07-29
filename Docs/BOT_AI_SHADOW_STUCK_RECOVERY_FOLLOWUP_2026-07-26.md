# Shadow stuck-recovery follow-up (2026-07-26)

## Decision

The new read-only `stuck_seconds` observation proves that both existing shadow
policies can select `recover-from-stuck` during real movement-watchdog
episodes. It does **not** authorize a live recovery adapter. In the two
cross-game 3,600-tick probes, every native two-second watchdog detection was
already classified by stock as an intentional stop (except one UT run-end
censor). There is no demonstrated failed navigation to correct, and the
policy action still contains no safe direction, probe, or actuator.

Keep live bot control absent. Do not turn `recover-from-stuck` into an
acceleration override, route rewrite, target change, or replan.

## Read-only change

Commit `a8768ab1` exposes the already-active `UPawn` move-stall watchdog's
no-progress duration to the shadow observation. It returns zero while the
watchdog is inactive and changes neither the pawn, latent command, route,
target, weapon, or stock script. `shadow-manifest.json` now truthfully records
`"stuck_time": true`; it continues to declare `"controls_live_bots": false`.

The focused `BotBenchmarkShadowTelemetryTests` and
`BotBenchmarkProtocolTests` targets built successfully. The Release engine
was then built and used only through the no-UCC headless benchmark driver.

## Matched shadow probes

Both probes use one shadow policy (`utility-arena`), 16 stock-controlled bots,
seed `104729`, difficulty 3, fixed delta `0.016666668`, and all existing live
corrective switches disabled. They are development observations from the
attested executable shown in their artifacts; they are not release-quality
comparisons.

| Adapter | Map | Run | Recovery episodes / selected ticks | Exact watchdog detections | Watchdogs covered by shadow recovery | Stock outcome |
| --- | --- | --- | ---: | ---: | ---: | --- |
| UT436 | Deck16-II | `qa/runs/2026-07-26/ut436-deck16-shadow-stuck-observation-utility-s104729-3600-r1/` | 27 / 1,228 | 6 | 6 / 6 | 5 intentional stops; 1 run-end censor |
| Unreal Gold 226b | DeathFan | `qa/runs/2026-07-26/unreal226b-deathfan-shadow-stuck-observation-utility-s104729-3600-r1/` | 25 / 940 | 4 | 4 / 4 | 4 intentional stops |

At every joined watchdog record the utility policy first chose recovery 55
ticks earlier (56 for one UT record), corresponding to approximately 0.92
seconds at the fixed timestep. It remained selected through the two-second
watchdog crossing. The native records retain stock `decision: none`; this is
the expected proof that shadow evaluation did not control the game.

The UT joins include two `MoveToward(PathNode131)` cases, two inventory/path
cases, and two `StrafeFacing` cases. The Unreal joins are all `StrafeFacing`.
That heterogeneity further rejects a generic lateral action: the policy does
not know a collision-safe lateral direction, a valid route continuation, or
whether preempting combat movement is safe.

## What the result means

The earlier shadow baseline correctly reported no recovery selection because
its observations did not contain stuck time. This follow-up changes that
observation limit and supersedes only that negative activation finding. It
does not overturn the central safety conclusion: current exact native
outcomes are intentional stops rather than stuck-navigation failures.

The 21 UT and 21 Unreal shadow recovery episodes that never produced a native
two-second detection are not false-positive counts; they simply cleared or
became ineligible before the stricter watchdog threshold. They have no
same-life outcome classification and must not be treated as candidate success.

## Required evidence before reconsidering live control

1. Record a bounded, same-life shadow recovery episode identity, safe-probe
   admission/rejection and proposed direction—without applying it.
2. Find repeated episodes whose stock outcome is neither intentional stop nor
   censor, and establish a harmful or persistent no-progress failure.
3. Prove a one-shot, bounded actuator has a valid collision/support/hazard
   certificate and cannot replace route, target, fire, timer, or script state.
4. Add a deterministic fixture for admission, cancellation, attribution, and
   stock-command restoration; then qualify a live candidate against attested
   cross-game, multi-seed, held-out evidence.

Until all four hold, `recover-from-stuck` remains a useful diagnostic label,
not an implementation request.
