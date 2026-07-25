# Retail `MinHitWall` oracle findings

## Scope

This records controlled retail observations of walking `HitWall` notification
eligibility. It does not authorize a runtime change. The test package is copied
into a disposable game runtime by
`Tools/BotBenchmark/Run-RetailMinHitWallOracle.ps1`; the installed retail files
are SHA-256 inventoried before and after each run.

## Stable-contact fixture

The calibration uses one pinned glancing contact against a runtime-only round
`BlockAll`. Earlier runs allowed the latent `MoveTo` to continue after the first
contact, letting the pawn slide around the cylinder and changing the later
normal/velocity. Those later contacts are not a valid witness for the initial
threshold decision.

Both retail packages now emit `pinned_contact_latched` immediately after the
first probe `Bump`, with `MoveTimer = -1`. This does not change the current
collision's velocity, acceleration, collision, or state; it only makes the
latent move return before a subsequent walking step. The runner requires:

- exactly one selected start/contact and exactly one latch;
- bilateral Bump records with the expected dynamic blocker;
- every retained Bump trace normal and velocity to match the selected first
  contact within `0.00001` per component; and
- a `HitWall` callback, when present, immediately after the final retained
  probe Bump.

Any contact drift, missing latch, malformed sequence, or overflow invalidates
the run rather than being treated as a suppressed callback.

## Retail results, 2026-07-25

Each threshold was run twice with an identical pinned start, heading, blocker
placement, and first trace normal `(0.917526, -0.397677, 0)`.

| Game | Map | Suppressed twice | Dispatched twice | Resulting bracket |
| --- | --- | --- | --- | --- |
| UT436 | `DM-Deck16][` | -0.396000, -0.395000 | -0.394000 | (-0.395000, -0.394000] |
| Unreal Gold 226b | `DmMorbias` | -0.398000, -0.397000 | -0.396000 | (-0.397000, -0.396000] |

Authoritative artifacts are external QA evidence:

- `qa/retail/minhitwall-oracle-v3/ut436-pinned-latched-r1/pinned-boundary-summary.json`
- `qa/retail/minhitwall-oracle-v3/unreal226b-pinned-latched-r1/pinned-boundary-summary.json`

These results prove a repeatable retail notification boundary in both games.
They do **not** prove the equality comparator or the exact native operand: the
pre-callback Bump trace normal/velocity is only a witness, and the retail
boundary differs slightly from its reconstructed dot product. The current
SurrealEngine `TickWalking` vertical Z-band predicate must therefore remain
unchanged until the native corner fixture supplies an exact dispatch-time
operand and the full behavior-quality gate is passed.

## Next required evidence

1. Repair the native two-contact corner fixture so it invokes a real stock
   `HitWall` handler in both games and exposes the dispatch-time operand.
2. Add pure boundary cases on both sides of each retail bracket, including the
   equality boundary once its operand is known.
3. Only then try a default-off notification-only candidate, preserving the
   physical slide/retry path and qualifying paired full matches for survival,
   wall contacts, stuck episodes, and combat.
