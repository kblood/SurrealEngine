# WebXR authoritative muzzle-origin implementation plan

## Status and safety boundary

This document defines the remaining authoritative firing-origin work for WebXR.
It is intentionally separate from the implemented controller-relative weapon
viewmodel transform. The viewmodel seam is presentation-only; it must not become
gameplay authority until each stock weapon has deterministic fixtures, calibrated
package-qualified muzzle metadata, obstruction handling, and physical-headset
validation.

The stock-path audit used the locally installed retail GOG `BotPack.u` with
SHA-1 `B1365300C9B4111D30159F64E57628257FFFD172`. Mod packages and other package
revisions must fail closed unless they are qualified independently.

Implementation status on 2026-07-22:

- commit `0decaf01` adds the portable origin algebra, exact stock hitscan
  classifier, exception-safe LIFO firing context, VM post-result observer,
  transactional mutable-argument hook, diagnostics, and pure self-tests;
- commit `22c31e7a` invokes the real loaded retail
  `Botpack.ShockRifle.TraceFire(0)` path twice from the packed-input browser
  fixture and proves exact context/observer/sink/restoration counter deltas;
- the immutable calibration table remains empty and
  `WebXRAuthoritativeFireProductionEnabled` remains false, so production
  endpoint translation is deliberately zero; and
- `Actor.Trace`, projectile `Actor.Spawn`, obstruction clamping, package-hash
  qualification, calibrated weapon rows, and physical alignment remain open.

## Rejected shortcuts

Do not move `Pawn.Location`, globally rewrite `Weapon.FireOffset`, or replace
every `CalcDrawOffset` result.

The sibling-worktree candidate returned `worldMuzzle - Owner.Location` from
`CalcDrawOffset`. Stock callers subsequently add a rotated `FireOffset`, so the
actual firing origin becomes `worldMuzzle + rotated FireOffset`. Enforcer also
temporarily scales its `FireOffset` to 0.35. A generic `CalcDrawOffset` hook
therefore double-offsets shots, cannot cover Sniper, and can capture unrelated
cosmetic or damage-time calls.

## Required architecture

Keep the existing re-entrant exact-weapon call classifier and direction-only
`ViewRotation` scope. Add an exception-safe LIFO authoritative-fire context with:

- the exact local pawn and current weapon;
- package, class, and qualified firing-path policy;
- calibrated desired controller muzzle position;
- observed stock central origin and final sink origin;
- requested, applied, rejected, and restoration diagnostics.

Add two narrow VM facilities:

1. A post-result observer for exact `CalcDrawOffset` calls while a validated
   authoritative-fire context is active.
2. A mutable-argument hook immediately before exact authoritative sinks.

Only translate these sinks:

- `Pawn.TraceShot`: translate `StartTrace` and `EndTrace` by the same delta.
- `Actor.Trace`: translate both endpoints by the same delta.
- `Actor.Spawn`: translate location only when the receiver is the active weapon
  and the class is the expected projectile for the qualified path.

Preserve direction, spread, autoaim/toss rotation, projectile orbital/ring
offsets, charge behavior, and out-parameter references. Never affect remote
pawns, bots, unqualified mods, non-finite poses, or calls outside the exact
current-weapon fire context.

## Stock GOTY path matrix

| Weapon/path | Stock construction | Required WebXR handling |
|---|---|---|
| Enforcer primary, automatic alt, dual | Full XYZ; `FireOffset *= 0.35` around inherited trace fire | Use the runtime-scaled construction and prove nested LIFO restoration |
| Minigun2 primary/alt | YZ only | Translate the final trace endpoints from the observed YZ origin |
| SniperRifle primary | Eye position; no `CalcDrawOffset` or `FireOffset` | Mandatory `TraceShot` sink translation |
| ShockRifle primary | YZ trace | Translate trace endpoints |
| ShockRifle alt | Full-XYZ projectile | Translate only the qualified projectile spawn |
| SuperShockRifle | YZ trace | Reject later cosmetic `CalcDrawOffset` calls |
| PulseGun primary | Full XYZ plus rotating Y/Z orbit | Translate the central origin and preserve the orbit |
| PulseGun alt | Inherited projectile launch | Standard qualified projectile translation |
| Ripper primary/alt | Full XYZ | Standard qualified projectile translation |
| Flak primary | Central origin plus 6-8 projectiles | Apply one central delta and preserve every chunk offset |
| Flak alt | Full-XYZ slug | Ignore the preceding `WeaponLight` spawn |
| Eightball primary/alt | Central origin plus up to six ring/random rockets or grenades | Translate the center; preserve all relative offsets |
| Eightball `CheckTarget` | Target acquisition only | Never classify as a fired shot or haptic outcome |
| BioRifle primary/charged alt | Full XYZ | Standard qualified projectile translation |
| BioRifle `ShootLoad.Timer` | Two direct projectile spawns | Cover explicitly or prove unreachable in the shipped path |
| ImpactHammer charged tick | Direct native `Actor.Trace` | Translate endpoints; count a firing outcome only for a real trace/attack |
| ImpactHammer release/alt | YZ/full-XYZ trace variants | Translate the correct trace path independently |
| Chainsaw automatic/`Slash` notify | YZ `TraceFire` and full-XYZ `TraceShot` | Qualify both paths without treating animation alone as a hit |
| Translocator `ThrowTarget` | Axes are populated after origin calculation; effectively Calc-only | Preserve exact scoped rotation restoration; no invented FireOffset compensation |
| Redeemer initial/guided launch | Inherited full-XYZ projectile | Translate the initial launch only |
| GuidedWarShell tick | Continuously reads guider rotation | Explicitly exclude; steering/camera is a separate milestone |

## Staged implementation

1. **Implemented, fail-closed:** add pure origin-policy and
   endpoint-translation helpers with algebraic tests.
2. **Implemented:** add the result observer and mutable-argument hook to `Frame`, retaining
   exception-safe LIFO cleanup.
3. **Implemented:** add the exact local-current-weapon authoritative context
   and diagnostics.
4. **Implemented at the exact `Pawn.TraceShot` seam, production disabled:**
   classify Enforcer, Minigun2, SniperRifle, ShockRifle,
   SuperShockRifle, and Chainsaw.
5. Add direct `Actor.Trace` support for ImpactHammer.
6. Add single-projectile paths: Shock alt, Ripper, BioRifle, Translocator, and
   Redeemer initial launch.
7. Add PulseGun orbit and the Flak/Eightball multi-projectile paths.
8. Expose requested-versus-actual origin diagnostics and epsilon validation.
9. Add immutable package-and-class-qualified muzzle calibration. Keep the
   current zero-offset visual fallback non-authoritative.
10. Clamp the desired muzzle against level geometry so a tracked hand through a
    wall cannot fire through cover.
11. Design guided Redeemer steering/camera behavior as a separate milestone.
12. Enable a production path only after automation and physical Quest/Virtual
    Desktop calibration pass for every enabled weapon policy.

The loaded ShockRifle fixture suppresses/restores its directly controlled
noise, flash, effect/spawn, damage, ammo, haptic, and weapon state and verifies
that the equipped weapon and actor count survive. It intentionally calls stock
script, so the diagnostic still advances global `FRand` twice. A pathological
ray intersecting a live actor can receive `TakeDamage(0)`, and an intersected
live `ShockProj` could run its special hit path; the clean Deck fixture hit
neither case. This is a test-only call with no startup or production caller.

## Required fixtures and acceptance gates

- XYZ, YZ, and no-offset algebra, including a negative test proving the sibling
  double-offset failure.
- Enforcer 0.35 scaling and nested dual-Enforcer restoration.
- Exact loaded-package classifier and policy matrix; reject package-name and
  mod-class collisions.
- Sniper no-Calc handling.
- Shock trace/projectile split and cosmetic-Calc rejection.
- Pulse orbit, Flak cluster, and Eightball ring relative-position invariance.
- ImpactHammer path separation and Chainsaw `Slash` notify coverage.
- Translocator zero-axis behavior and byte-identical rotation restoration.
- Redeemer initial launch with explicit guided-flight exclusion.
- Mutable out-parameter identity and exact `End - Start` preservation.
- Tracking loss, non-finite pose, remote pawn, bot, exception-unwind, and stale
  current-weapon rejection.
- Runtime desired versus actual `TraceShot`, `Trace`, and `Spawn` origins within
  a documented epsilon.
- Near-wall obstruction tests preventing hand-through-wall firing.
- No duplicate haptics from nested paths and no per-tick hammer pulse without a
  real attack outcome.
- Physical alignment of barrel, muzzle flash, trace/projectile origin, and
  obstruction behavior on Quest 3 through the supported presentation route.

## Remaining inputs

The current controller-relative visual schema is deliberately empty and falls
back to the grip pose. It supplies no authoritative barrel location. Before a
gameplay origin can ship, the project still needs package-qualified per-weapon
muzzle calibration, an agreed wall-clamping policy, special/guided-weapon UX,
and physical headset measurements. Brave versus another Chromium frontend does
not alter this engine architecture; browser/runtime availability is handled by
the separate WebXR launch-readiness diagnostics.
