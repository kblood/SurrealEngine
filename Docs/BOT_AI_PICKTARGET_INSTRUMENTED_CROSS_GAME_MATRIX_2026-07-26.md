# Instrumented PickTarget cross-game matrix — 2026-07-26

## Method

This matrix eliminates the historical-binary observer mismatch. The stock
comparison executable is branch `experiment/pick-target-stock-instrumented`
at `b9994f58`: it is the current observer/analyzer stack with only the native
`UPawn::PickTarget` predicate restored to stock. The fixed executable is the
current BOT AI branch. Both ran 16 bots, difficulty 3, seed `104729`, 7,200
fixed ticks at `0.0166667`, with PickTarget, WarnTarget/TryToDuck,
direct-reach-command, and observer-only hazard-egress telemetry enabled.
Every listed artifact completed and passed `Analyze-BotQuality.py`; no UCC was
used.

## Results

| Adapter/map | Stock K/D/S, damage | Fixed K/D/S, damage | Other exact observation | Decision |
| --- | --- | --- | --- | --- |
| UT436 `DM-Deck16][` | 37/54/17, 4950 | 44/53/9, 5844 | coverage 0.6773 → 0.7888; fixed returns 37 living targets while stock returns 0 and skips 1573; both have zero `TryToDuck` outcomes | Strong positive single-seed evidence. |
| Unreal Gold 226b `DmDeathFan` | 53/110/57, 6986 | 50/117/67, 6976 | environmental deaths 53 → 64; warnings 342 → 411 and duck outcomes 4 → 4 | Reject shared rollout: combat, survival, and damage all regress on this adapter. |

Artifacts:

- `qa/runs/2026-07-26/ut436-deck16-instrumented-stock-s104729-7200-r1/`
- `qa/runs/2026-07-26/ut436-deck16-instrumented-fixed-s104729-7200-r1/`
- `qa/runs/2026-07-26/unreal226b-deathfan-instrumented-stock-s104729-7200-r1/`
- `qa/runs/2026-07-26/unreal226b-deathfan-instrumented-fixed-s104729-7200-r1/`

## Boundary

This qualifies neither a universal fix nor a new safety write. It demonstrates
that the same native predicate has different aggregate consequences in the two
stock script ecosystems. A future candidate must either establish an exact,
game-qualified downstream cause and preserve the other adapter's stock
behavior, or achieve positive cross-game qualification on expanded seeds/maps.
