# ReachSpec native-commit capability evidence

Date: 2026-07-26  
Scope: read-only stock-map evidence for `DM-Deck16][` (UT436) and
`DmDeathFan` (Unreal Gold 226b). No route-selection code, benchmark settings,
or UCC execution was used for this pass.

## Verdict

There are **zero observed capability-incompatible native committed edges** in
the two selected stock benchmark runs, under the documented UE1 bot profile:
`walk`, `jump`, `swim`, `open_doors`, and `special` true; `fly` false; and
`bIsPlayer` true. The live post-spawn capability witnesses independently
confirm all of those movement/door/special bits except `bIsPlayer`; neither
catalog nor committed edge contains `R_PLAYERONLY`, so that unobserved bit
does not affect this result.

This is a negative result for these two fixtures, not evidence that the
current reach-flag omissions are safe in general. In particular, the telemetry
does not sample mutable capabilities such as `bCanJump` at each commit.

| Map/run | Native committed paths | Committed edge occurrences | Distinct ReachSpecs | Unsupported occurrences |
| --- | ---: | ---: | ---: | ---: |
| UT436 `DM-Deck16][` | 1,706 | 417 | 102 | 0 |
| Unreal 226b `DmDeathFan` | 89 | 136 | 12 | 0 |
| Total | 1,795 | 553 | 114 map-local | 0 |

## Exact data and capability join

The SDK mapping used for the join is `R_WALK=1`, `R_FLY=2`, `R_SWIM=4`,
`R_JUMP=8`, `R_DOOR=16`, `R_SPECIAL=32`, and `R_PLAYERONLY=64`.
`APawn::calcMoveFlags()` derives the corresponding mask from the Pawn
capabilities. The standard bot mask is therefore `125` (`1 + 4 + 8 + 16 +
32 + 64`), with `R_FLY` absent.

The catalogs and runs were:

| Fixture | Validated catalog | Map package SHA-1 | Stock run | Capability witness |
| --- | --- | --- | --- | --- |
| UT436 Deck16 | `qa/runs/2026-07-26/script-export-hitwall-certificate/ut436/DM-Deck16][.json` | `d157dfe26490180660a0f207607d4743190b980b` | `qa/runs/2026-07-26/stock-anchor-refresh-v1/ut436-deck16` | `TMale1Bot0`, `TMale2Bot0` |
| Unreal 226b DeathFan | `qa/runs/2026-07-26/map-catalog-v4-deathfan/DmDeathFan.json` | `b8f5d776d0516a5afa62f1d305601a2d8f99da57` | `qa/runs/2026-07-26/inventory-marker-reachability-ab-v2/unreal226b-deathfan-stock` | `MaleThreeBot0`, `MaleTwoBot0` |

Both run manifests enable the read-only native path-commit observer. The UT
run has all navigation intervention/live-policy switches off. The Unreal run's
inventory and swim-egress switches are observers (`*_live_enabled: false`),
and its directory is the retained `stock` arm of the A/B capture.

`Analyze-NativePathCommits.py` validated every emitted edge index against the
matching catalog's directed endpoints, dimensions, raw reach flags, unknown
bits, and pruned state. It also rejected record overflow. The validated
telemetry counts were:

| Fixture | Cache clears | Truncated commits | Edge flag mask | Occurrences | Distinct specs | Representative committed edge |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| Deck16 | 631 | 1 | `1` (`walk`) | 357 | 86 | `#120 PathNode9 -> PathNode8` |
| Deck16 | 631 | 1 | `9` (`walk+jump`) | 44 | 11 | `#416 PathNode50 -> LiftExit6` |
| Deck16 | 631 | 1 | `32` (`special`) | 16 | 5 | `#1173 LiftExit0 -> LiftCenter0` |
| DeathFan | 41 | 0 | `1` (`walk`) | 68 | 6 | `#1111 LiftExit4 -> InventorySpot36` |
| DeathFan | 41 | 0 | `32` (`special`) | 68 | 6 | `#1163 LiftCenter1 -> LiftExit4` |

Every listed bit is present in both documented bot profiles and their
post-spawn witnesses. No committed edge has `R_FLY`, `R_SWIM`, `R_DOOR`,
`R_PLAYERONLY`, an unknown bit, or a rejected/pruned catalog record. Thus the
exact unsupported-edge counts are zero for both the 553 edge occurrences and
the 114 map-local ReachSpec identities represented by them.

## What the trace can and cannot prove

For these emitted native path-cache commits, the data has enough edge identity
to prove the static capability result: `reachspec_index` is present for every
edge and is checked against its catalog record and adjacent committed nodes.
The result is stronger than the earlier route-cache first-hop correlation,
which could be ambiguous when multiple ReachSpecs shared endpoints.

It does **not** prove all of the following:

- The bot still had every mutable capability at the exact selection tick.
  `bCanJump` can be changed by Bot script states after the post-spawn witness;
  a per-commit capability snapshot is needed to rule that case out for the 44
  `walk+jump` Deck16 edges.
- The committed path was physically completed. The record is the bounded
  native cache commit at `pre_special_cache_commit`; later collision, movers,
  dynamic traversal state, and script ownership remain separate evidence.
- No unrecorded native selection occurred. This pass proves the observer's
  complete bounded commit records (with zero overflow), not a universal claim
  about all possible game maps or all runtime paths.

Accordingly, this evidence does not justify wiring the inert ReachSpec model
into selection. The next safe differential, if this question remains useful,
is a read-only per-commit snapshot of `bCanWalk`, `bCanJump`, `bCanSwim`,
`bCanFly`, `bCanOpenDoors`, `bCanDoSpecial`, and `bIsPlayer`, joined to this
same exact ReachSpec index.
