# Deus Ex AI Perception Math

Date: 2026-07-22

## Scope and dependency

Branch: `pr/deus-ex-ai-perception`

This topic is based only on `pr/game-support-registry` at `2ccdb1d8`. The
implementation commit is `437614cc`.

The module contains only three pure scalar functions:

- `ComputeDXAIHearing`
- `ComputeDXAISight`
- `ComputeDXAIMotionVisibility`

There are no `UActor` or `NPawn` changes, native wrappers, property offsets,
traces, world access, save/text behavior, VR behavior, bot changes, or game
fixtures in this branch.

## Source mapping

Read-only evidence came from `deus-ex-surrealengine` commit `e7ff4b05`
(`Implement Deus Ex AI perception basics`). This extraction retains only:

- `SurrealEngine/UObject/DXAIPerception.h`
- `SurrealEngine/UObject/DXAIPerception.cpp`
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

- This is formula extraction, not complete Deus Ex perception behavior.
- Callers must still resolve actors, zones, lighting, collision dimensions,
  velocity, occlusion, field of view, and trace results.
- No claim is made here about exact retail ordering around line-of-sight or
  native event dispatch; those require separate integration evidence.
- Negative collision dimensions are squared by the observed formula. Normal
  engine callers are expected to supply non-negative extents.
