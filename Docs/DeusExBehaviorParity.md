# Deus Ex behavior parity

This document records the effort to make the Deus Ex (GOTY) game support behave
like the retail `DeusEx.exe` engine, the evidence behind each correction, and the
work that is still open.

Every entry here was decided by measurement, not by reading retail's binary. The
method is an A/B capture: the same map, the same scripted probe, run once under
retail and once under Surreal Engine, then compared row by row.

## Method

`Tools/AllianceTelemetry` is an UnrealScript mutator that both engines load. It
drives the player to a fixed spot, samples engine state on a timer, and writes a
tab-separated capture. Because it is script, it observes only what the game can
observe, so a divergence in a capture is a divergence a mod or the game itself
would see. See `Tools/AllianceTelemetry/README.md` for how to build and run it.

Two rules that repeatedly proved necessary:

- Isolate before concluding. Most first hypotheses in this work were wrong, and
  the cheap way to find that out is a probe that varies one input.
- Prefer a controlled emitter over ambient content. Comparing "which actor
  happened to make a noise" is not comparable across engines; comparing a noise
  of known volume and radius from a known point is.

## Corrections made

### Perception and AI events

- `AICanSee` defaulted fail-closed, so NPCs never approached the player. The
  light term used zone `AmbientBrightness` as a proxy, which is not usable; it
  now samples the real lightmap. A later pass fixed over-reporting of up to 2.4x.
- `AIVisibility` was stubbed to 0.0. It now reports intrinsic detectability.
- `SeePlayer` / `EnemyNotVisible` were never dispatched.
- An AI event sent without a radius arrived as radius zero and was treated as
  unlimited, handing half the level a loud noise to investigate whenever a crate
  broke. Zero radius now reaches nobody.
- **Audio AI events did not attenuate.** Surreal Engine delivered a `LoudNoise`
  to the full `AISendEvent` radius; retail stops short of it. A probe that emits
  a noise of known volume and radius from a known point, paired with the
  listener's state on the next sample, separated the candidate models over 387
  retail events:

  | model | threshold | misclassified |
  | --- | --- | --- |
  | `value * (1 - dist/radius)` | 0.1494-0.1555 | 2 |
  | `value * (1 - (dist/radius)^2)` | 0.254 | 5 |
  | `dist/radius <= C` | 0.694 | 31 |
  | inverse, inverse-square | - | 69 |

  Retail's cutoff moves with volume - 0.689 at volume 0.5, 0.845 at volume 1.0
  (identical at radius 400 and 1000), 0.914 at volume 2.0 - which rules out a
  pure distance ratio. The fitted threshold is not a constant: `ScriptedPawn`
  defaults `HearingThreshold=0.150000`, so the listener's own property is read.

  `CarriesToListener` in `Native/NActor.cpp` now requires
  `value * (1 - dist/radius) >= HearingThreshold` for `EAITYPE_Audio` events to
  `Pawn` listeners. Visual and olfactory events keep the plain radius test; only
  audio was measured. `params.Volume`/`Score`/`Visibility`/`Smell` are still
  passed unattenuated, because no Deus Ex script reads those fields numerically -
  only `params.bestActor` - so retail cannot be distinguished on that point.

  Validated against retail on identical geometry, 400 of 400 events agreeing:

  | volume, radius | retail delivered / refused | fixed engine |
  | --- | --- | --- |
  | 0.5, 1000 | <= 0.688 / >= 0.704 | <= 0.690 / >= 0.703 |
  | 1.0, 700 | <= 0.844 / >= 0.856 | <= 0.846 / >= 0.850 |
  | 2.0, 700 | <= 0.923 / >= 0.938 | <= 0.921 / >= 0.927 |

### Pawn behavior

- Pawns acquired an enemy but never released it, and separately never acquired
  one despite correct perception inputs.
- NPC-vs-NPC combat occurred that retail does not have.
- Weapon damage was correct per hit; the gap was hit frequency.
- `StrafeFacing` did not initialize `DesiredSpeed`, `MoveTarget` or
  `bReducedSpeed`.
- State distribution diverged: pawns sought constantly and never sat, and
  patrolled where retail idles.
- `AIPickRandomDestination` sampled candidate points and rejected them. Retail
  picks a heading and keeps wherever the pawn stops, so it never fails; the
  engine now walks the ray out the same way. This also restored animal retention
  from a generator, where the engine had been losing pigeons retail keeps.
- `Actor.Base` was never set for actors standing on world geometry.

### VM, properties and tracing

- Struct layout dropped trailing padding, breaking every C++ property overlay.
- `GetPropertyText` diverged from retail on strings, structs and objects, and
  bool-to-string returned `1`/`0` instead of `True`/`False`.
- The bytecode nesting limit rejected scripts that `ucc` compiles.
- `TraceActors` dropped world hits, reported the wrong hit location and returned
  results in reverse order. `TraceAnyHit` skipped actors on long traces.
- `GetStateName()` returned `None` for an actor in no state.

### Timing and rendering

- Actor occlusion culling drew distant actors through solid geometry.
- `LastRendered()` after a map load reported the first frame as drawn at level
  time zero, which matters because Deus Ex gates AI on it.
- Retail's 5 ms `DeltaTime` floor at high frame rates explains the NPC fire-rate
  gap; this is retail behavior, not an engine defect.

## Retail semantics worth keeping

Measured facts that are easy to get wrong and expensive to re-derive:

- `TraceActors` runs start to end, world hits arrive as the `Level` actor, and
  the list stops at the wall.
- Both engines tick event, then state code, then physics. This was measured; do
  not reorder it to explain a timing gap.
- Deus Ex script shrinks a pawn's collision cylinder after it lands, which is why
  retail pawns rest 4.5 units above the floor.
- `LastRendered` gates whether Deus Ex NPCs scan for NPC enemies, so renderer
  culling changes AI reach.
- `GotoState` called on a pawn from outside does not take effect before that
  pawn's next tick.

## Open work

### Explain retail's far-field animal freeze (#48)

Past roughly 1200 units of player distance, retail stops ticking its birds
entirely - position, velocity and `DistanceFromPlayer` all stop updating - while
this engine keeps simulating them. Wandering birds top out at exactly 1200.1.

Ruled out by measurement: `bStasis` / `LastRendered` gating (retail birds moved
in 74.7% of samples while never rendered), zone differences (both in
`LevelInfo0`), a general tick freeze at that range (651 of 2420 moving
transitions beyond 1200 unrendered), and an `AIPickRandomDestination` distance
limit (100% success out to 23000).

Plan: the remaining candidates are a distance check inside the generator or the
Deus Ex per-actor `DistanceFromPlayer` update rather than the tick loop. Probe by
parking the player at a fixed offset and stepping it outward in 100-unit
increments within one run, logging per-bird movement against exact player
distance, to find whether the boundary is sharp and whether it tracks the player
or the generator. The Flying-occupancy half of this task is now largely
explained - the trigger is `LoudNoise`, not startling - and needs a re-measure
now that audio events attenuate.

### Pawn resting height (#44)

This engine settles pawns onto the floor after Deus Ex shrinks their cylinder, so
they stand lower than retail's. Needs engine-side instrumentation of load and
`BeginPlay` settling to see where the two diverge.

### SecurityBot3 stalls at PathNode887 (#41)

Retail's bot stalls there and this engine's walks through, which makes retail the
outlier. It is blocked on #44, since resting height changes what the bot's
cylinder touches. It also feeds #48: this engine's `SecurityBot0` and
`UNATCOTroop0` patrol nearer the pigeon generator than retail's, and a security
bot's mass gives its footsteps the maximum 2048 noise range.

### Conversations never terminating (#11)

The premise is doubtful; it was recorded before `InitEventManager` was examined
and has not been reproduced. Needs a repro where the mutator frobs an NPC and
watches conversation state and `ConPlay` progress before any engine change.

### Broaden the comparison sweep (#9)

Everything measured so far is NPC and AI behavior. Menus, dialogue flow, trigger
and event ordering, and frame timing are unmeasured.

## Upstream

Nothing here has been submitted. `integration/unified-engine` must never be the
source of an upstream PR, and the contribution gate in the repository
instructions has not been run for any of these corrections. The AI event
attenuation fix is the most self-contained candidate: one function, a stated
model, a reproduction, and before/after numbers from both engines.
