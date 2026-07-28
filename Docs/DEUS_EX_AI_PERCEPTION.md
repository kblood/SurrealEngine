# Deus Ex AI Perception Math

Date: 2026-07-22

## Scope and dependency

Branch: `pr/deus-ex-ai-perception`

This topic is based only on `pr/game-support-registry` at `2ccdb1d8`. The pure
formula implementation is `437614cc`; the subsequent relocation commit places
it behind the explicit `GameSupport/DeusEx` module boundary.

The current unified module contains four pure scalar/direction functions plus
a bounded, callback-injected LOS ordering kernel and endpoint planner:

- `ComputeDXAIHearing`
- `ComputeDXAISight`
- `PassesDXAISightDirection`
- `ComputeDXAIMotionVisibility`
- `BuildDXAISightTracePlan`
- `TraceDXAISightLineOfSight`

There are no `UActor` or `NPawn` changes, native wrappers, property offsets,
traces, world access, save/text behavior, VR behavior, bot changes, or game
fixtures in this branch.

## Source mapping

Read-only evidence came from `deus-ex-surrealengine` commit `e7ff4b05`
(`Implement Deus Ex AI perception basics`). This extraction retains only:

- `SurrealEngine/GameSupport/DeusEx/AIPerception.h`
- `SurrealEngine/GameSupport/DeusEx/AIPerception.cpp`
- the focused synthetic perception test concept
- CMake and `Configure.js` source registration

Every actor/native integration file from that evidence commit was excluded.
The tests were expanded to cover exact boundaries, output range, large finite
inputs, and non-finite inputs.

## Formula semantics

### Hearing

- Non-positive volume is inaudible.
- Non-positive radius uses the observed Deus Ex default of 800 units.
- Vertical separation is doubled before Euclidean distance attenuation.
- Distance at or beyond the radius is inaudible.
- Hearing threshold is subtracted after volume/distance attenuation.
- The result is clamped to `[0, 1]`.

### Sight

- Non-positive supplied visibility or light visibility produces zero.
- Squared distance is floored to 1.
- Apparent angular size is
  `(collisionRadius² + collisionHeight²) / distanceSquared`.
- Values strictly below `minAngularSize` are rejected; equality is visible.
- Visibility is `visibility * angularSize * 64 * lightVisibility`, followed by
  threshold subtraction and `[0, 1]` clamping.

### Sight direction

- Non-positive horizontal FOV disables the direction gate.
- Otherwise horizontal and vertical target angles are compared with half the
  horizontal FOV; the vertical half-FOV is divided by the positive aspect
  ratio, which defaults to one when non-positive.
- The target's apparent angular radius expands both boundaries, so a cylinder
  overlapping the edge remains visible.
- Boundary equality is visible, targets behind the observer are rejected, and
  non-finite inputs fail closed.

### Motion visibility

- With velocity enabled, motion contribution interpolates from zero at speed
  30 to its maximum at speed 200.
- Maximum motion adds 50% of the supplied light visibility.
- With velocity disabled, only the supplied light visibility is clamped.

### Defensive contract

All finite inputs produce a finite result in `[0, 1]`. Any NaN or infinite
input fails closed to zero. The evidence implementation allowed NaN to escape
through `std::clamp`; the explicit finite guard is justified by the module's
boundary tests and prevents invalid VM or native-wrapper values from poisoning
later AI calculations.

## Validation

Focused configure, build, and test:

```text
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build --config Release --target DXAIPerceptionTests --parallel
ctest --test-dir build -C Release -R DXAIPerception --output-on-failure
```

Result: 1/1 focused test passed.

Full native compatibility:

```text
cmake --build build --config Release --target SurrealEngine --parallel
build\Release\SurrealEngine.exe --help
```

Result: full Release build/link passed and `--help` exited successfully.

Additional checks:

```text
node --check Configure.js
git diff --check
```

Both passed.

## Limitations

- Hearing now has a narrow live pawn/native integration in the unified product
  tree. It applies the detectable-actor gate, optional argument defaults,
  listener threshold, and the tested pure formula.
- Sight now has narrow live scalar, direction, and LOS integration. The native
  wrapper delegates to `UPawn::AICanSee`, which applies the detectable-actor
  gate, eye-relative distance, optional tested direction/FOV gate, apparent
  size, minimum angular size, visibility threshold, caller-supplied visibility,
  and the bounded world trace sequence described below. Callers must explicitly
  disable light sampling because that input remains unresolved; default calls
  therefore still fail closed to zero.
- Smell and motion/light visibility remain formula extraction rather than
  complete live Deus Ex perception behavior.
- Callers must still resolve actors, zones, lighting, collision dimensions,
  velocity, and field of view. The live wrapper now resolves bounded BSP/mover
  occlusion through the engine collision system.
- The LOS ordering matches preserved Surreal Deus Ex patch evidence. It is not
  independently claimed as exact retail executable behavior.
- Negative collision dimensions are squared by the observed formula. Normal
  engine callers are expected to supply non-negative extents.

## Unified live sight checkpoint

The 2026-07-24 unified slice deliberately excludes the historical patch's
full-brightness light substitute. Pure direction tests cover FOV boundaries,
aspect ratio, apparent cylinder overlap, behind-target rejection, disabled
direction gating, and non-finite inputs. The LOS kernel tests the exact bounded
short-circuit order with an injected trace callback:

- scalar rejection performs no trace;
- primary success performs one trace;
- primary failure without cylinder checking performs one trace;
- a clear cylinder top succeeds after two traces without testing the bottom;
- the bottom is tested only after primary and top both fail, for three traces
  total.

The observer start is `Location + EyeHeight`. A pawn's primary endpoint is its
eye position; a non-pawn uses its center. Cylinder fallback uses target center
plus and minus the full collision height. Non-finite endpoint geometry fails
closed. Runtime traces use `FastTrace` against BSP and blocking movers with
actor-cylinder tracing disabled. They do not mutate gameplay properties, but
they do advance collision check counters and mark visited actors, so the probe
contract explicitly records `collision_bookkeeping_mutation=true` rather than
calling the operation read-only.

Two final `00_TrainingFinal` owner-data runs,
`deus-ex-trainingfinal-soldier0-sight-los-v2-final-a` and `-b`, bind
`actor:DeusEx.Soldier:Soldier0#0` by exact stable identity and class, apply no
probe navigation or synthetic input, and do not advance the level. The v2
artifact records `bDetectable=true`, collision extents 20/43, scalar result
`0.083278768`, direction result `0.0`, one blocked primary non-cylinder trace,
and three blocked primary/top/bottom cylinder traces. Both LOS evaluations are
therefore zero. The two artifacts are byte-identical with SHA-256
`fff326a0e41305cf4e005d0580060ad10355aed86f0c9ae2d4bf22ad1793484c`.

The RelWithDebInfo executable used for that LOS evidence has SHA-256
`b9467609f8874e9a825fde0c45868d6735e53076b103cb8123514d2ae9e40317`.
With the exact earlier fixed-step contract (`0.02` seconds), its repeated
Light155 walk and Switch1 interaction still succeeds at ticks 53 and 249
respectively. Event and summary bytes remain identical to the pre-LOS baseline;
the initial/final observations are repeat-identical schema-v2 artifacts because
they now include optional inventory-resource evidence. Real light sampling and
visibility-cache behavior remain future slices; no faithful point-light sampler
was found in current or preserved source.
