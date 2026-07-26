# UT99 self-damage provenance research

## Evidence

The attested Deck16 clearance-observer match is also the current best exact
self-damage sample:

- Artifact:
  `D:\SurrealEngineQA\2026-07-26\ut436-deck16-canfire-observer-s104729-r1\runs\000000-canfire-observer-dm-deck16-game-botpack-deathmatc-s104729-r0-192080e2fdb2`
- UT436 `DM-Deck16][`, seed `104729`, 16 stock skill-3 bots, 7,200 fixed
  ticks / 120 simulated seconds.
- Matrix and structural analysis passed.
- Exact match totals: damage taken `6895`; self-attributed damage `88`;
  damage from other participants `5175`; from nonparticipants `1632`; deaths
  `49`; direct self kills `0`; direct enemy kills `35`; unassisted
  environmental deaths `11`.

The four positive self-damage observations are:

| Bot | Tick / seconds | Health delta | State / physics / latent action |
| --- | ---: | ---: | --- |
| `pri:5` Ichthys | 1136 / 18.933 | 12 | `FallingState` / Falling / Sleep |
| `pri:5` Ichthys | 2918 / 48.633 | 34 | `FallingState` / Falling / Sleep |
| `pri:8` Tamerlane | 1657 / 27.617 | 25 | `FallingState -> Retreating` / Falling / WaitForLanding |
| `pri:14` Alys | 6561 / 109.350 | 17 | `RangedAttack` / Falling / TurnToward |

This is enough to justify attribution instrumentation.  It is **not** enough
to infer a weapon, projectile, damage type, firing call, or a fire veto.
`direct_self_kills == 0` does not mean no self-combat damage, and legacy
scoreboard attribution is deliberately not used as causal evidence.

## Narrow recommended observer

Implement one default-off, UT99-only **self-splash provenance observer**.
It must remain observation-only and scope to benchmark participants.

The causal seam is retail `Engine.Actor.HurtRadius` (`Actor.uc:928`), with
the exact contract:

```uc
final function HurtRadius(float DamageAmount, float DamageRadius,
                          name DamageName, float Momentum, vector HitLocation)
```

`HurtRadius` runs on the projectile or explosion actor, reads its `Instigator`,
and makes nested `Victims.TakeDamage(... Instigator ... DamageName)` calls for
visible collision victims.  The existing benchmark hook already observes
canonical `Pawn.TakeDamage` and reliable realized health deltas.

The observer should:

1. Resolve exactly `Engine.Actor.HurtRadius`, require five arguments and no
   return value, and open an invocation-scoped record on entry.
2. Join only a nested canonical `TakeDamage` where victim identity equals the
   `HurtRadius` source actor's instigator identity and the health delta is
   positive.
3. Emit only that exact nested relation; do not guess across asynchronous or
   unrelated damage calls.
4. Fail closed on contract/type/non-finite violations, nested-stack lifecycle
   faults, or record overflow.

Required record fields include observer tick/time; HurtRadius and TakeDamage
invocation tokens; source actor identity/class; instigator and victim
identity/life IDs; damage name; requested damage/radius/momentum/hit location;
realized health delta; finite source, instigator, and victim positions; and
victim physics/state/latent action.  A contemporaneous weapon class may be
recorded as descriptive context, never as causal proof.

## No behavior candidate yet

Do not add a splash guard, modify `HurtRadius`, or veto `FireWeapon` now.  The
new observer intentionally classifies splash/explosion damage only; direct
impact and hitscan self damage remain unclassified rather than being guessed.

Relevant future script joins, only after repeated provenance evidence exists:

- `Bot.FireWeapon` (`Bot.uc:1388`) calls `Weapon.Fire` or `AltFire`.
- `FallingState.Timer` calls `CanFireAtEnemy` (`Bot.uc:6667`).
- `FireWhileFalling` calls `CanFireAtEnemy` then `FireWeapon`
  (`Bot.uc:6781-6782`).
- `LongFall` calls `CanFireAtEnemy` then `PlayRangedAttack`/`FireWeapon`
  (`Bot.uc:6810+`).

A future behavior candidate needs repeated observer-attested cases for one
projectile/damage class and pre-fire state, plus a fixed-seed and cross-map
comparison proving a safety gain without combat regression.
