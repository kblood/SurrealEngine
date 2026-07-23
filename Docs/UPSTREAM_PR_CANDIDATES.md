# Upstream pull-request candidates

Audit date: 2026-07-23

This is a read-only comparison of integration commit `a4a4b737` with freshly
fetched `dpjudas/SurrealEngine` `origin/master` at `c2599d51`. It identifies
possible source material; it does not declare any branch ready to submit. Every
candidate must be reconstructed from current upstream, understood line by
line, manually reproduced, and human-curated before a pull request is opened.

## Recommended disposition

| Priority | Source | Disposition | Current-upstream result |
| --- | --- | --- | --- |
| 1 | `bdd4ac66` accessed-None array evaluation | Reconstruct as the first small bug fix after a real-script/manual reproduction | Still present; upstream `ExpressionEvaluator.cpp` is byte-identical to the fix's preimage |
| 2 | `3ac1162c` OpenAL gain update | Rewrite as a substantially smaller independent fix, then reproduce with an active sound | Still present; `SetGlobalVolume` changes state without updating the OpenAL source |
| 3 | `ad58cd6e` cardinal-axis movement | Keep as an established candidate, but strengthen gameplay evidence before proposing | Still present in walking, swimming, and flying code |
| 4 | `f1591d05` aggregate boolean serialization | Keep separate and require a load/save round trip before proposing | Still present; aggregate writers still call the tagged boolean no-op writer |
| Hold | `95dcca7c`, `d8309d8f`, `a4a4b737` desktop controls | Fork policy, not an upstream bug fix | Upstream intentionally still reads game/user bindings and defaults inversion to true |
| Hold | `b570e7f0` owner-game browser audio gates | Fork-only qualification infrastructure | Upstream has no browser launcher, WebAudio bridge, Playwright harness, or `web/` tree |

None of the first four is submission-ready today because the required human
runtime reproduction is not recorded against `c2599d51`.

## 1. Preserve accessed-None through array expressions

`bdd4ac66` corrects a narrow VM result-propagation defect. A
`ContextExpression` whose object is `None` returns
`StatementResult::AccessedNone`, but fixed and dynamic array element evaluators
discard that result and keep only its empty value. Array-operand failure then
becomes `Array is not a variable...` instead of reaching `Frame`'s existing
`AccessedNone` handling.

Upstream status is unusually strong: current upstream
`SurrealEngine/VM/ExpressionEvaluator.cpp` has blob
`a4736b14ecc2824129c5126466c3403b3b4eabcd`, exactly the blob used as the
parent of `bdd4ac66`. No upstream reimplementation or conflict exists.

Minimal production scope:

- `SurrealEngine/VM/ExpressionEvaluator.cpp` only;
- retain evaluation order (index, then array); and
- return the complete failed `ExpressionEvalResult` from both operands in
  `ArrayElementExpression` and `DynArrayElementExpression`.

Focused regression scope should cover four cases: accessed-None in the index
and array operand for both fixed and dynamic arrays. Each must return
`AccessedNone` with a `Nothing` value and must not throw the non-variable-array
exception. The integration test currently covers only the two array-operand
cases, so it should be extended before replay.

Dependency and risk: the production correction has no VR, browser, game, or
new engine dependency. The integration CMake test hunk is not independently
portable because current upstream has no CTest/`Tests` registration. Do not
smuggle the fork's full test infrastructure into this PR; agree on a minimal
test target or provide the synthetic expression-tree reproduction alongside a
real UnrealScript callstack. VM result propagation is sensitive, so verify one
owned UT99 or Unreal Gold script that previously reaches the bad exception and
show the before/after log.

## 2. Reapply OpenAL gain when global volume changes

Current upstream `ALSoundSource::SetVolume` writes `AL_GAIN` and
`AL_MAX_GAIN` using `volume * globalVolume`, while `SetGlobalVolume` merely
stores the new multiplier. Since callers set source volume before global
volume, a newly started or updated sound can retain the preceding global gain
indefinitely when its source volume remains unchanged.

The behavior is still present after upstream's recent audio work. It is generic
native OpenAL behavior, not a WebAudio-only issue.

Do not replay `3ac1162c` as-is. Its 97-line policy/test refactor also removes
the existing `AL_MAX_GAIN` update and includes browser documentation. A
reviewable upstream patch should instead change only
`SurrealEngine/Audio/AudioDevice.cpp`: when `globalVolume` changes, update the
stored value and reapply the same effective-gain OpenAL calls already used by
`SetVolume`. Preserve existing attenuation/clamping policy unless separately
proven wrong.

Reproduce with a constant-volume looping sound: record source gain, change the
global sound-volume setting while it plays, and verify the gain changes without
requiring a source-volume change or restart. A focused regression can use a
small gain-calculation helper only if the maintainer considers that less
complex than an OpenAL call seam; avoid creating a new abstraction merely to
test multiplication.

Risk: OpenAL gain and maximum-gain semantics affect every title and positional
attenuation. Test mute, restoration, a volume above one if the engine emits
one, and music/sound separation. Keep browser lifecycle and owner-data probes
out of this PR.

## 3. Existing generic correctness candidates

### Cardinal-axis movement

`ad58cd6e` changes three movement predicates from `x != 0 && y != 0` to the
appropriate any-axis test. Current upstream still contains all three `&&`
expressions. The smallest production patch is the three operator corrections
in `SurrealEngine/UObject/UActor.cpp`; the extracted `ActorMovement.h` helper
and its truth-table test mostly restate the operators and need not be submitted.

Before proposing it, produce actor-level evidence for exact X-only and Y-only
walking plus X/Y/Z-only swimming or flying. This is collision/movement code, so
manual in-game behavior and preservation of diagonal movement matter more than
the current pure boolean test.

### Aggregate boolean serialization

`f1591d05` separates tagged boolean serialization (the value lives in the
property header) from untagged aggregate serialization (the value must be a
payload byte). Current upstream still calls the tagged `SaveValue` no-op from
fixed arrays, dynamic arrays, and structs.

Minimal production scope is limited to `SurrealEngine/UObject/UProperty.h` and
`.cpp`: add an aggregate-member writer, override it for booleans, and call it
from the three aggregate containers. The existing emitted-byte test is useful
but insufficient for a sensitive serialization PR. Add load-after-save round
trips for false/true values in a struct, fixed array, and dynamic array, then
verify a real package/save made by the engine can be reopened. Do not combine
this with Deus Ex support or save-system refactoring.

## Not upstream candidates

### Desktop WASD and inversion migration

The desktop work is coherent for this fork but is preference and migration
policy, not a correctness repair. It recognizes classic or empty movement
layouts (including the older `W=Fire` Surreal profile), writes WASD, forces
non-inverted look, and persists the result to `SE-User.ini`.

That heuristic can reinterpret a legitimate custom `W=Fire` layout, overwrite
an explicitly inverted legacy profile, and changes defaults across every UE1
game. Upstream has not adopted equivalent behavior. Keep the helper, native
persistence tests, and browser OPFS tests in the product branch. If desired,
ask the maintainer about an opt-in launcher preset; do not present the current
three-commit migration as a small bug-fix PR.

### Owner-game browser audio gates

`b570e7f0` adds bounded WebAudio buffer observations and launch/map assertions
to `web/smoke_test_owner_game.py`. It changes no engine behavior and depends on
the fork's complete browser launcher, audio bridge, and Playwright owner-data
harness. Current upstream has none of those prerequisites. Retain it as local
release evidence and never include game data or machine paths. It can be
reconsidered only after upstream accepts a browser platform and its testing
convention.

The same dependency rule excludes current WasmFS persistence, browser relative
mouse, renderer-selection, and WebXR seams from near-term upstream proposals.
They should not be used to turn a small correctness PR into a platform series.

## Already upstream; do not duplicate

The freshly fetched upstream already contains these generic fixes from other
work: `eba1e7c0` (StringToName token), `eb035dd9` (additional HP token),
`a618595e` (dynamic-array expansion on index access), and `5ef80b5e`
(globalconfig array property loading). New PRs for those changes would be
duplicates.

## Gate before any submission

For the selected candidate, create a new `pr/*` branch from `c2599d51`, replay
only the minimal files above, and inspect every line. Record the exact
before/after reproduction, focused build/test command, real-game manual result,
risks, and non-goals. The user must be able to explain the mechanism and
tradeoffs. Do not push, open a PR, or contact upstream until the user explicitly
requests that external action.
