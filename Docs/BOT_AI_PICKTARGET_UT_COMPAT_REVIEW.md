# PickTarget UT436 compatibility review

Date: 2026-07-26  
Scope: read-only review of the `UPawn::PickTarget` correction (`e32755d4`),
installed UT436 and Unreal Gold 226b script packages, and current deterministic
match evidence. This review makes no source-code change.

## Conclusion

`UPawn::PickTarget` contained an inverted liveness predicate. The old code
skipped `Health() > 0` despite its adjacent `Skip dead pawns` comment; the
corrected predicate skips `Health() <= 0`. This is a real shared native defect,
not a UT-specific semantic difference. The controlled fixture proves that the
corrected function selects a visible living pawn in both UT436 and Unreal Gold.

It is nevertheless **not merge-ready as an unconditional BOT AI improvement**.
The current paired UT Deck16 evidence is a deterministic adverse outcome, while
the single Unreal Gold DeathFan pair improves. The likely UT mechanism is now
narrow and testable: the repair activates stock `ShockRifle.Timer` and
`SniperRifle.Timer` paths that set `bPointing` and dispatch `WarnTarget` to a
living pawn. `WarnTarget` can make the target duck/alter its behavior, which
can cascade into movement and environmental deaths on Deck16. That is an
inference from the exact script path and match counters, not yet a proven
per-episode causal attribution.

Do not add a game-name branch inside `UPawn::PickTarget`. Both games use the
same native contract: target a living, visible pawn. A UT-only dead-target
compatibility mode would preserve an engine bug and would be unsafe to call
retail parity. Keep the correction quarantined as a candidate until the
provenance experiment below qualifies it or rejects it.

## Native semantic evidence

`SurrealEngine/UObject/UActor.cpp` now rejects self and nonliving pawns:

```cpp
if (pawn == this || pawn->Health() <= 0)
    continue;
```

Upstream `origin/master` still has `pawn->Health() > 0`; its own preceding
comment says `Skip dead pawns or ourselves`. `NPawn::RegisterFunctions` binds
this routine as UE1 native 531 (`Pawn.PickTarget`). The fixed headless
`bot-pick-target-fixture` selected a visible living candidate on both adapters.
That establishes the function-level contract, but not broad match quality.

## Retail script call topology

The package review used the workspace's read-only UELib CLI against the
installed packages; it did not run UCC or write to either game installation.

| Game/package | SHA-1 | Native-531 callers found | Relevant stock behavior |
| --- | --- | --- | --- |
| UT436 `BotPack.u` | `b1365300c9b4111d30159f64e57628257fffd172` | `UT_Eightball.CheckTarget`, `ShockRifle.Timer`, `SniperRifle.Timer` | The Shock and Sniper timers call `Pawn.PickTarget`, set `bPointing`, and call `Pawn(targ).WarnTarget(...)` when a pawn is found. `UT_Eightball.CheckTarget` takes its native-query branch only when the owner is a `PlayerPawn`; stock benchmark bots derive from `Bot`, so that branch is not the likely bot effect. |
| Unreal Gold 226b `UnrealShare.u` | `2bd91ab92544dab22d353d3b64e47581090e8c14` | `Eightball.CheckTarget`, `ASMD.Timer` | `ASMD.Timer` is the corresponding timer path: it queries native 531, sets `bPointing`, and calls `WarnTarget`. `Eightball.CheckTarget` returns before the query for `bIsPlayer` owners, so it is not the stock-bot path. |

The class hierarchy also agrees across games: UT's benchmark classes such as
`TMale1Bot` derive through `MaleBotPlus` from `Bot`, and Unreal Gold's `Bots`
derives from `Pawn`. Neither difference justifies a different definition of a
valid target.

UT `Bot.WarnTarget` is not inert. In states that delegate to it, a living bot
with an enemy may pass the probability, flight-time, direction, and movement
checks and call `TryToDuck`. Acquisition and falling states suppress it, while
`TacticalMove` and `Retreating` may delegate to the base behavior. Therefore a
correctly returned target can change movement timing without changing the
targeting call's own parameters.

## Current match evidence

All figures below are 16-bot, 7,200-tick (120 s), deterministic observer runs.
The fixed UT repeat is byte-identical to its first fixed run. The stock runs
need a second exact repeat before any matrix-level verdict.

| Scenario | Kills | Deaths | Suicides | Environmental deaths | Damage dealt | Walls | Movement-intent no-progress (s) | PickTarget result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| UT Deck16, seed 271828, old predicate | 33 | 51 | 18 | 17 | 4606 | 2464 | 184.667 | 10 living LOS candidates, 0 living targets returned |
| UT Deck16, seed 271828, fixed predicate | 27 | 50 | 23 | 23 | 4287 | 3240 | 261.650 | 16 living LOS candidates, all 16 returned |
| Unreal DeathFan, seed 104729, old predicate | 50 | 113 | 63 | 59 | 6749 | 2963 | 122.533 | 30 living LOS candidates, 0 living targets returned |
| Unreal DeathFan, seed 104729, fixed predicate | 59 | 107 | 48 | 46 | 7504 | 6069 | 136.317 | 17 living LOS candidates, all 17 returned |

The UT result is harmful under the project gates: fewer kills, five additional
suicides/environmental deaths, less damage, more wall contacts, and materially
more movement-intent no-progress. It is enough to block promotion, but one
stock seed/map comparison does not establish that the semantic repair itself is
wrong or that it must be adapter-scoped.

## Precise next safe design and experiment

1. Leave the shared predicate semantically correct; do not introduce a
   UT/Unreal game-name condition or a geometry-only workaround.
2. Add a default-off, read-only native-531 provenance observer before another
   behavior change. Each record must include game/package hash, pawn identity,
   stock-autonomous-bot flag, active weapon class, VM class/state/function,
   `Enemy` identity before the call, selected identity, whether it equals
   `Enemy`, and the existing candidate/result counters. Bounded records,
   overflow counters, and fail-closed reconciliation are required.
3. Add a second read-only `WarnTarget` observer keyed to the selected target,
   recording dispatch, receiver VM state, and whether the receiver subsequently
   issues a duck/movement-state transition. It must not infer causality from a
   later score alone.
4. Run stock and fixed predicate variants twice each, first on UT Deck16 with
   seeds 271828 and 104729, then a held-out UT map, and Unreal DeathFan plus a
   held-out Unreal map. Keep 16 bots, fixed roster, 7,200 ticks, and the same
   build except for the predicate. Require exact repeat equivalence and no
   observer overflow/integrity failure.
5. Promote only if the paired matrix shows no safety regression and the new
   provenance shows that the target/warning path is behaving as intended. If
   the Deck16 regression repeats and is attributed to these two UT weapon
   timers, reject this candidate for the BOT AI branch. A later narrowly scoped
   weapon-script compatibility candidate would then need its own retail
   reproduction and fixture; it must not silently alter generic `PickTarget`.

This approach preserves the real native correction as a separately reviewable
engine candidate while refusing to claim a bot-quality win without causal,
cross-game evidence.
