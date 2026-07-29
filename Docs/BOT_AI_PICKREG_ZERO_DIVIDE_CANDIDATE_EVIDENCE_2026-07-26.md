# PickRegDestination Zero-Divide Guard Evidence

Date: 2026-07-26  
Status: viable benchmark-gated candidate; not merge-ready or release-qualified.

## Mechanism

`7e359376` adds a default-off guard for one proven undefined bot-script
expression. It returns a zero direction only if all conditions hold:

1. the immediate UnrealScript caller is `PickRegDestination`;
2. the receiver is in `UnrealShare.Bots` or `Botpack.Bot` ancestry;
3. native vector/float divide receives exactly `(0,0,0) / 0`.

All other native divide operations—including nonzero/zero and unrelated
zero/zero calls—retain retail UE1 behavior. `0c00f3c5` binds and fail-closes
the bounded activation records in telemetry and the quality analyzer.

## Unreal Gold 226b evidence

The original DeathFan failure path is seed `104729`, 16 bots at skill 0,
7,200 ticks. The clean paired comparison keeps vision and the broad
finite-destination containment guard off; only this guard differs.

| Metric | Stock | Candidate | Candidate − stock |
| --- | ---: | ---: | ---: |
| Kills | 43 | 51 | +8 |
| Deaths | 109 | 108 | -1 |
| Suicides | 66 | 57 | -9 |
| Unassisted environmental deaths | 59 | 50 | -9 |
| Damage dealt | — | — | +531 |
| Hit-wall events | 1,404 | 1,484 | +80 |
| Movement-intent stuck events | 7 | 5 | -2 |

Two independently generated, provenance-bound matrices reproduce every listed
delta exactly:

- `qa/runs/2026-07-26/unreal226b-deathfan-pickreg-zero-divide-tuning-s104729-r1`
- `qa/runs/2026-07-26/unreal226b-deathfan-pickreg-zero-divide-tuning-s104729-r2`

The candidate records exactly two bounded activations on `MaleTwoBot0`, with
zero activation overflow. The former vision-stress reproduction also completes
with the broad containment guard off, zero vector non-finite observations, and
two narrow guard activations; it passes the repaired strict analyzer.

The +80 hit-wall contacts are an adverse signal that requires fresh
map/seed evidence. It must not be ignored because other safety/combat counters
improve.

## UT436 compatibility evidence

On `DM-Deck16][`, seed `104729`, 16 all-skill-3 bots, 7,200 ticks, with the
vision experiment disabled, stock and candidate are exactly tied on every
reported controlled metric:

| Metric | Stock | Candidate |
| --- | ---: | ---: |
| Kills / deaths / suicides | 35 / 49 / 14 | 35 / 49 / 14 |
| Unassisted environmental deaths | 11 | 11 |
| Hit-wall events | 5,629 | 5,629 |
| Movement-intent stuck events | 24 | 24 |
| Guard activations | — | 0 |

Both runs pass structural analysis in
`qa/runs/2026-07-26/ut436-deck16-pickreg-zero-divide-compatibility-s104729-r1`.

## Required next evidence

1. Fresh Unreal Gold DeathFan seed(s), including deterministic repeats.
2. Unreal Gold `DmHealPod` tuning evidence with the same pair controls.
3. UT436 map/seed compatibility (or positive activation if independently
   observed), without regression.
4. The frozen cross-game tuning and held-out matrix plus available quality
   gates. Current release gates remain deliberately fail-closed for
   unrepresented role-swapped, causal-avoidable-suicide, and 16-bot timing
   requirements.
