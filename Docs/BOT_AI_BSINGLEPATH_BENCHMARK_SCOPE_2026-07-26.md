# `bSinglePath` benchmark-scope audit

Date: 2026-07-26
Scope: stock UT436 and Unreal Gold 226b bot matches only; static review of the
retail script exports and retained controlled-map evidence. No game was
launched and no UCC tool was used.

## Conclusion

`bSinglePath` is **not exercised by the standard bot navigation path in the
controlled benchmark matches**. The UT436 Botpack and Unreal Gold deathmatch
bot classes both leave the optional argument unset, which selects its default
`false` value. Therefore the current omission/limited handling of that
parameter cannot explain the observed Deck16-II or DeathFan bot stalls, wall
contacts, or hazard deaths.

The omission can still matter to normal UE1 gameplay outside this benchmark
scope: Unreal Gold's `ScriptedPawn` explicitly passes `true` for low-intelligence
creatures and in a few scripted movement states. Its exact retail native
semantics remain unproven, so this is a separate parity item, not an authorized
bot-navigation change.

## Native contract and current engine status

Both retained retail exports declare the same `Pawn` native interface:

```unrealscript
native(518) final function Actor FindPathTo(vector aPoint,
    optional bool bSinglePath, optional bool bClearPaths);
native(517) final function Actor FindPathToward(actor anActor,
    optional bool bSinglePath, optional bool bClearPaths);
```

The current bridge maps an omitted script optional to `false` and forwards it:
`SurrealEngine/Native/NPawn.cpp:118-132`. `UPawn::FindPathTo` forwards to
`FindPathToward` (`UActor.cpp:5368-5371`), but the terminal navigation-point
branch of `UPawn::FindPathToward` does not read `singlePath`
(`UActor.cpp:5421-5439`). Thus true and false currently take the same route
search path in this worktree.

That is a real parameter-loss/parity risk, but not proof of an effect. The
public UnrealScript declaration does not define what "single path" means, and
there is no retail oracle result for a true/false differential. Do not infer a
tie-break, search-width, or route-cache rule from the name alone.

## UT436 controlled bot path

Evidence source: the exact UT436 package export retained at
`SurrealEngine/qa/runs/2026-07-26/script-export-hitwall-certificate/ut436/`.

- `Engine/Classes/Pawn.uc:268-271` declares the optional parameter.
- `Botpack/Classes/Bot.uc:4` declares `class Bot expands Pawn`.
- `Bot.uc:1321-1341`, `FindBestPathToward`, calls
  `FindPathToward(desired,,bClearPaths)`. The empty second slot deliberately
  leaves `bSinglePath` at the native default; only `bClearPaths` is supplied.
- A full export search finds no Botpack call that supplies `true` as the second
  argument to `FindPathToward` or `FindPathTo`.

The retained 16-bot `DM-Deck16][` stock benchmark summary records only
`Botpack.TMale1Bot`, `TMale2Bot`, `TFemale1Bot`, and `TFemale2Bot` participants
(`qa/runs/2026-07-26/deck16-pick-target-warn-observer-v1/`). They inherit the
Botpack bot path above. The exact map catalog has 1,989 actors, 251 navigation
points, and no `ScriptedPawn`/creature actor
(`script-export-hitwall-certificate/ut436/DM-Deck16][.json`).

**Result:** `bSinglePath=true` has no caller in the bot roster or map actor
population of the controlled UT436 Deck16-II benchmark.

## Unreal Gold 226b controlled bot path

Evidence source: the exact Unreal 226b package export retained at
`SurrealEngine/qa/runs/2026-07-26/script-export-hitwall-certificate/unreal226b/`.

- `Engine/Classes/Pawn.uc:260-263` declares the same optional parameter.
- `UnrealShare/Classes/Bots.uc:4` declares `class Bots extends Pawn`, rather
  than extending `ScriptedPawn`.
- `Bots.uc:887-902`, `FindBestPathToward`, calls `FindPathToward(desired)` with
  no second argument.
- `HumanBot -> Bots`, `MaleBot/FemaleBot -> HumanBot`, and
  `MaleThreeBot/FemaleOneBot` inherit that route path. The 16-bot DmDeathFan
  summary also includes `UnrealI.MaleOneBot`, `MaleTwoBot`, `FemaleTwoBot`, and
  `SkaarjPlayerBot`; the retained source hierarchy places each under
  `UnrealShare.Bots`, not `ScriptedPawn`.

The DmDeathFan map catalog records 683 actors, 116 navigation points, and zero
`ScriptedPawn`/creature actors
(`script-export-hitwall-certificate/unreal226b/DmDeathFan.json`). The current
16-bot controlled run is complete and contains only the bot subclasses above
(`qa/runs/2026-07-26/deathfan-targetless-timeout-closeout-v1/summary.json`).

**Result:** the standard Unreal Gold deathmatch bot route path also uses the
default false mode; DmDeathFan does not add a map-owned `ScriptedPawn` caller.

## Where `true` is actually live

The same exact Unreal Gold export demonstrates that the parameter is not dead
in the broader game:

- `UnrealShare/Classes/ScriptedPawn.uc:576-584` calls
  `FindPathToward(desired, true)` only when
  `Intelligence <= BRAINS_Reptile`; otherwise it uses the default mode.
- `ScriptedPawn.uc:2933-2945` and `2957-2969` call
  `FindPathTo(desiredDest, true)` after a failed `PointReachable` check in
  scripted movement states.

Those are creature/NPC paths. They are neither inherited nor invoked by
`UnrealShare.Bots`, and the two catalogued benchmark maps contain none of those
actors. A single-player map with a low-intelligence `ScriptedPawn`, or a mod
that directly supplies the argument, can therefore plausibly observe a
difference once retail semantics are established.

## Disposition

| Question | Answer |
| --- | --- |
| Is true mode used by the standard UT436 benchmark bots? | No. |
| Is true mode used by the standard Unreal 226b deathmatch bots? | No. |
| Do Deck16-II or DmDeathFan add an NPC caller? | No, according to retained map catalogs. |
| Could ignoring the parameter affect ordinary UE1 gameplay? | Yes, for `ScriptedPawn` low-intelligence/scripted movement and third-party callers. |
| Does it justify a current bot fix or explain the active bot failures? | No. |

If this parity item is resumed, first build a small two-start-anchor fixture
and run the *same* `FindPathToward(goal, false)` and `true` calls against a
retail executable. Record returned first hop, all script-visible `RouteCache`
entries, and outcome when the nearest reachable start cannot complete the
route. Only then decide whether the parameter limits starts, search branches,
or route output. Keep that experiment separate from the Deck16/DeathFan bot
quality matrix.
