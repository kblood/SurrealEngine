# Positive-DPS Walking Preflight Veto v1

## Scope

This is an opt-in, benchmark-only movement experiment. It observes an autonomous
stock bot while walking, requires a confirmed static-world fall forecast into a
known, finite, positive-DPS pain zone, and is enabled only with
`--botbench-walking-preflight-positive-dps-veto=1`. The default is off.

The experiment uses the existing UE1 collision move primitive to attempt a
collision-checked return to the iteration origin before committing the falling
transition. On a successful return it clears velocity and acceleration and
forces a movement replan. It does not teleport, choose routes, aim, or fire.
One action is authorized per semantic episode/life; all other confirmed
observations are debounced. A failed return fails open.

`DamagePerSec` is now captured exactly from the forecast landing zone. Unknown,
non-finite, zero, and negative values cannot authorize the control.

## Telemetry

The benchmark records the option in its manifest, summary, and config identity.
It emits exact per-bot counters for eligibility, application, debounce, forced
replans, and rejected collision returns. The analyzer also validates the exact
landing DPS evidence and counter bounds.

## Qualification result: not promoted

The deterministic matrix was run on 2026-07-25 with 4 bots for 1,800 ticks and
two repetitions per case:

| Game/map | Seeds | Baseline | Enabled result |
| --- | --- | --- | --- |
| UT99 Deck16-II | 104729, 271828, 314159 | 8 deaths, 6 suicides, 16 hazard entries | 12 deaths, 6 suicides, 14 hazard entries; 36 applications |
| Unreal Gold DeathFan | 424242 | 16 deaths, 6 suicides, 14 hazard entries | Identical; zero eligible or applied controls |

For the originally implicated Deck16-II seed 104729, each baseline repetition
had one death and each enabled repetition had three. The enabled experiment is
therefore a deterministic regression and does not meet the action-effect or
non-regression gates. It remains default-off and is not release- or merge-ready.

Evidence is stored outside the source tree at
`qa/runs/2026-07-25/positive-dps-preflight-veto-v1/`.

## Legacy-first correction

The regression audit found that v1 returned before the stock pain-ledge guard
could update its recovery/replan state. The live branch now gives that guard
strict precedence: it records and restores first, and the experimental veto is
considered only when the guard declines. The same Deck16-II matrix then matched
the baseline exactly (8 deaths, 6 suicides, 16 hazard entries), with 122 exact
eligible confirmations but zero new applications. Unreal Gold DeathFan also
remained identical with zero eligible controls. This removes the demonstrated
regression, but it also means the experiment has no demonstrated benefit and
still fails the action-effect promotion gate.

## Next step

Do not tune this global veto by loosening its evidence gate. First causally
match the original fatal chain to a particular preflight episode and add
action-to-outcome evidence: an attempt identity, the legacy guard decision,
rollback fraction, ensuing replan, and later hazard entry or death. Positive
DPS alone is insufficient to prove a safe alternative. DeathFan remains a
separate falling/death attribution problem because this walking observer saw no
eligible events there.
