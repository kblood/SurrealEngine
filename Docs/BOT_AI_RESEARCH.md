# FPS bot AI research

This document records implementation references for improving Unreal Engine 1
bots in Surreal Engine. It is a design and provenance index, not permission to
copy code without a file-level license review.

## Conclusions

The useful FPS bot projects converge on a layered design:

1. a subjective perception and memory model;
2. graph navigation plus predictive path following and stuck recovery;
3. an interruptible decision layer;
4. a human-constrained aim and fire controller;
5. local team coordination; and
6. deterministic scenarios, telemetry, and repeated match analysis.

Surreal should retain `Botpack.Bot` as the stock compatibility brain while it
restores the native services that the scripts expect. Experimental enhanced
bots can then compete against that baseline without silently changing stock
behavior.

## Primary references

### Unreal Tournament and Unreal-family systems

- [Exported UT99 469b UnrealScript](https://github.com/Slipyx/UT99) is the
  closest readable authority for `Botpack.Bot` states and decisions.
- [Steven Polge's Unreal Tournament AI guide](https://unrealarchive.org/unreal-tournament/documents/reference/unrealed/unreal-tournament-ai/index.html)
  describes NavigationPoints, reachspecs, route construction, map authoring,
  and bot debugging. These authored map semantics must remain authoritative.
- [Pogamut 3 and GameBots2004](https://github.com/kefik/Pogamut3) demonstrate
  an external observation/action protocol, agent memory, navigation helpers,
  and automated UT2004 matches. The GPL code is a reference or separate-process
  tool, not a zlib-compatible donor.
- [UT GameBots](https://gamebots.sourceforge.net/) is an earlier example of
  exposing perceptions and actions to external programs over a network.

### Counter-Strike and Half-Life bots

- [YaPB](https://github.com/yapb/yapb) is the strongest practical reference.
  Its main repository is MIT licensed and descends from PODBot. Relevant ideas
  include a priority/desire task stack, waypoint A*, path smoothing, visibility
  caches, learned team danger, remembered enemies, hearing, spring/damper aim,
  and data-driven difficulty. The GPL `ext/linkage` submodule must not be
  treated as MIT code.
- [ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS) contains a readable
  reverse-engineered CSBot implementation. It is not an official Valve source
  release. Its explicit states, subjective game state, recognition queue,
  uncertain sound localization, encounter points, hiding spots, and orthogonal
  skill/aggression/teamwork profiles are particularly relevant. Copying must be
  limited to code covered by the project's
  [MIT license transition](https://github.com/rehlds/ReGameDLL_CS/blob/master/LICENSE-TRANSITION.md).
- [PODBot MM](https://github.com/APGRoboCop/podbot_mm) is a useful historical
  comparison for the same task/desire lineage, but it is GPL-3.0.

No verified official public release of Valve's Counter-Strike or Condition
Zero CSBot source was found. ReGameDLL is legitimate reverse engineering, not
an official source publication.

### Quake and arena FPS bots

- [Quake III Arena BotLib and AAS](https://github.com/id-Software/Quake-III-Arena)
  provide the most complete reference for reachability compilation, travel
  times, movement prediction, avoid-reach memory, alternative routes, goal
  stacks, and fuzzy item/weapon weighting. The GPL license requires clean-room
  reimplementation for Surreal.
- [Cube 2: Sauerbraten](https://github.com/lsalzman/sauerbraten) and
  [Red Eclipse](https://github.com/redeclipse/base) are permissively licensed,
  compact arena-FPS references for waypoint navigation, target selection,
  skill parameters, and local steering.

### Component and navigation libraries

- [Valve NextBot](https://github.com/ValveSoftware/source-sdk-2013/tree/master/src/game/server/NextBot)
  cleanly separates intention, behavior, vision, known-entity memory, body,
  locomotion, and path following. Its suspend/resume action lifecycle and
  explicit stuck/failure events are excellent architecture references. The
  [Source SDK license](https://github.com/ValveSoftware/source-sdk-2013/blob/master/LICENSE)
  does not permit copying it into Surreal.
- [Recast/Detour](https://github.com/recastnavigation/recastnavigation) is zlib
  licensed and directly reusable. Detour's corridor repair, local avoidance,
  and off-mesh concepts could supplement UE1 navigation, but must not replace
  authored reachspec behavior.
- [BehaviorTree.CPP](https://github.com/BehaviorTree/BehaviorTree.CPP) and
  [GPGOAP](https://github.com/stolk/GPGOAP) are permissive implementations of
  alternative decision systems. Both are lower priority because Botpack already
  supplies a state machine and Surreal's immediate deficits are native services.

### Evaluation systems

- [ViZDoom](https://github.com/Farama-Foundation/ViZDoom) demonstrates seeded
  reset/step episodes, structured observations, off-screen execution, scenario
  definitions, and recordings.
- Pogamut demonstrates repeatable Unreal-family tournaments.
- The bot fork at preserved commit `e853cf6b` already contains structural trace
  validation, controlled fixtures, match matrices, combat/navigation metrics,
  role swapping, bootstrap intervals, and adjacent-skill analysis. Unified
  should extract compatible pieces instead of inventing an unrelated protocol.

## License use table

| Source | License posture for Surreal | Intended use |
| --- | --- | --- |
| YaPB main repository | MIT; audit submodules | algorithms or independently adapted code |
| ReGameDLL_CS current MIT-covered files | MIT; audit transition/history | algorithms or independently adapted code |
| Recast/Detour | zlib | direct optional component or adapted algorithms |
| Cube 2 / Red Eclipse | zlib-style | compact implementation donors |
| GPGOAP | Apache-2.0 | later experiment only |
| BehaviorTree.CPP | MIT | lifecycle/tracing ideas; integration unlikely |
| Quake III | GPL-2.0-or-later | architecture study and clean-room implementation |
| PODBot / Pogamut | GPL | study or separate process |
| RCBot2 | AGPL | study or separate process |
| Valve NextBot | Valve Source SDK terms | architecture study only |
| OldUnreal patch source | Epic/OldUnreal terms | compatibility evidence only |

For every incorporated source, record the exact repository URL, commit, file,
license, copied/adapted status, and reviewer in a provenance manifest before
merging it into the product branch.
