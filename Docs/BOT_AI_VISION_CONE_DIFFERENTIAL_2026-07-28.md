# The bot vision cone, measured against retail UT99

Date: 2026-07-28

Method is the same one that settled the reachability question in
`BOT_AI_PAIN_LEDGE_LIVELOCK_AND_NAV_GRAPH_2026-07-27.md`: ask both engines the
identical native question and diff the answers. Both engines execute the same
UnrealScript, so any disagreement is a native difference by construction.

## The corpus

A `Botpack.TMale1` observer and a `Botpack.TMale1` target are spawned with
collision off. The observer is placed on every 16th navigation point of
DM-Deck16][, the target on every navigation point within 800 units. For each
pair the observer's `PeripheralVision` is swept over `0.7`, `0.0` and `-0.2`,
and its yaw over a full turn in 32 steps. Each sample records the cosine
between the observer's facing and the direction to the target, `CanSee` and
`LineOfSightTo`.

`0.7` is what `Bot.SetPeripheralVision` gives a novice, `-0.2` is what it gives
skill 3 — the skill the bot benchmark runs at (`Bot.uc:790-804`).

Retail runs it as `bProbeVisionCorpus` in the BotTelemetry mutator, we run it as
`--trace-corpus-vision`. Both walk `NavigationPointList` in the same order,
which matters because the observer stride selects by position in that list.

42624 retail rows against 42816 of ours; 42624 queries matched on both sides.
Both engines report the same probe: `r=17 h=39 sightradius=4100 periphdefault=0`.

Artifacts: `qa/runs/2026-07-28/vision-corpus/`.

## Headline

| | Pairs |
| --- | --- |
| `CanSee` both true | 8465 |
| `CanSee` both false | 27449 |
| **`CanSee` ours true, retail false** | **5673** |
| `CanSee` retail true, ours false | 1037 |
| agreement | 84.3% |

| | Pairs |
| --- | --- |
| `LineOfSightTo` agreement | 97.7% |
| retail true, ours false | 864 |
| ours true, retail false | 96 |

## 1. We switch the cone off entirely when PeripheralVision is not positive

`PawnVisionCone.cpp:24` returns `true` unconditionally when
`peripheralVision <= 0.0f`. That is not an optimisation, it is the whole test
being skipped: `dot >= negative` still rejects everything far enough behind.

Restricted to samples with line of sight and inside sight radius:

| PeripheralVision | retail accepted | ours accepted |
| --- | --- | --- |
| 0.7 | 2207 / 6592 (33%) | 1474 / 6368 (23%) |
| 0.0 | 3376 / 6592 (51%) | **6368 / 6368 (100%)** |
| −0.2 | 3897 / 6592 (59%) | **6368 / 6368 (100%)** |

At `0.0` and `-0.2` we accept every single sample, including cosine `-1.000` —
a target directly behind the pawn. Retail rejects 49% and 41% respectively.

**Skill 3 bots run at `PeripheralVision = -0.2`.** Our benchmark bots therefore
have 360 degree vision. Retail's do not.

## 2. Both engines key the cone off Rotation, not ViewRotation

Worth ruling out, because `Pawn` carries both and only `ViewRotation` is
documented as where the pawn is looking.

Re-running the corpus with the sweep turning `ViewRotation` while leaving
`Rotation` fixed (`ProbeVisionRotMode=1`, `--trace-corpus-vision-rotation-mode=1`)
makes retail's acceptance rate go flat — 32%, 50% and 61% across every cosine
bucket, independent of the swept angle:

```
retail, ViewRotation swept, periph 0.7
  cos [-1000) 32.1%  [-750) 31.7%  [-500) 34.4%  [-250) 31.7%
      [    0) 31.3%  [ 250) 34.2%  [ 500) 31.7%  [ 750) 32.1%
```

A flat response means the answer ignores the thing being swept. Ours goes flat
in the same way. So neither engine's cone reads `ViewRotation`, and this is not
the source of the disagreement.

For reference, our engine only assigns `ViewRotation = Rotation` for
`bIsPlayer() && Role() >= ROLE_AutonomousProxy` (`UActor.cpp:6155`); nothing
maintains it otherwise.

## 3. Ours is a hard step, retail's cone widens as the target gets closer

Ours is exactly the code: at `PeripheralVision = 0.7`, nothing below cosine
0.700 is accepted and everything at or above it is, with the boundary landing
between 699 and 700. Retail is not a step.

Acceptance by cosine, split by separation, retail, line of sight only:

```
periph  0.7
  near  <300    cos[-1000]  0.0%  [ -600]  2.7%  [ -200] 70.6%  [  200] 88.8%  [  600] 76.0%
  mid 300-500   cos[-1000]  0.0%  [ -600]  0.0%  [ -200] 10.5%  [  200] 73.9%  [  600] 80.3%
  far  >=500    cos[-1000]  0.0%  [ -600]  0.0%  [ -200]  0.0%  [  200] 14.3%  [  600] 82.4%
```

At 500 units and beyond retail behaves close to `dot >= PeripheralVision`: the
boundary sits near cosine 0.7 and agreement with that rule reaches 85%. Inside
300 units retail accepts targets at cosine −0.2, more than 100 degrees off axis.
Agreement of the plain cone rule with retail, by separation:

| separation | samples | agreement of `dot >= PeripheralVision` | retail accepted |
| --- | --- | --- | --- |
| 0–100 | 288 | 68.4% | 75.3% |
| 100–200 | 1728 | 63.2% | 74.3% |
| 200–300 | 2688 | 59.6% | 67.3% |
| 300–400 | 3552 | 70.6% | 54.5% |
| 400–500 | 3552 | 75.4% | 43.7% |
| 500–600 | 2688 | 80.0% | 38.3% |
| 600–700 | 2304 | 82.0% | 32.6% |
| 700–800 | 2976 | 85.2% | 30.2% |

The effective angular threshold falls as separation grows, consistent with a
fixed positional slack rather than a pure angular test — roughly
`dot(forward, delta) >= PeripheralVision * |delta| - K` with K on the order of
200 units. That is a shape, not a confirmed formula.

Ruled out as the cause: measuring the cone from the eye position rather than the
actor origin. Sweeping an assumed eye offset from 0 to 45 units changes
agreement by less than 0.15 percentage points (74.09% down to 73.97%), so the
vertical reference point is not what produces the ramp.

## 4. SeePlayer is never dispatched, in either engine

Separate from the cone, and probably larger. `EventName::SeePlayer` and
`EventName::EnemyNotVisible` are declared in `ScriptCall.h` and never passed to
`CallEvent` anywhere. `Pawn.SightCounter`, which retail uses to pace the
periodic visibility scan, has a property offset and an accessor
(`UActor.h:2540`) and is never read or written.

`Bot.uc` overrides `SeePlayer` in its base class and in the `Acquisition`
(`3859`), `Roaming` (`4079`) and `StakeOut` (`6695`) states, and that is its
primary passive route to noticing a new enemy while moving around. In both this
fork and current `dpjudas/master` that route does not exist: bots can only
acquire through `HearNoise`, which is wired (`UActor.cpp:3995`), and through the
explicit `CanSee` / `LineOfSightTo` calls already inside a state's logic.

## What this does not yet establish

- The exact closed form of retail's test. The navigation point corpus confounds
  angle with separation, height difference and occlusion. Pinning the formula
  needs a controlled sweep: one observer, one target, fixed separations, open
  space, fine angular steps.
- Whether `Visibility` matters. `UT_Invisibility.uc:70` sets
  `Pawn(Owner).Visibility = 10`, and neither `CanSee` nor `LineOfSightTo` in
  `UActor.cpp` ever reads it, so the invisibility powerup plausibly does nothing
  to bot vision here. The corpus logs the column but never varies it.
- The 864 samples where retail has line of sight and we do not. `LineOfSightTo`
  has no cone, so that is a trace difference and is untouched by any of the
  above.
- No behavioural benchmark has been run against a corrected cone. Nothing here
  has been changed in the engine yet.
