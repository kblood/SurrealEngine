# UT99 Deck16 Vision Candidate Triage

Date: 2026-07-26  
Scope: interpretation of the first provenance-bound `Pawn.CanSee` benchmark
pair. This is not a release or merge decision.

## Controlled pair

`ut436-deck16-pawn-vision-attribution-preflight` ran a 16-bot,
all-skill-3, 7,200-tick `DM-Deck16][?Game=Botpack.DeathMatchPlus` pair at seed
`104729`. Both variants used the same executable, roster, observer, and
finite-command guard; the only behavior difference was the benchmark-gated
vision cone. The matrix runner assigned a shared pair ID and captured
provenance. Both runs and the combined fail-closed analysis passed.

| Metric | Stock | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Kills | 35 | 47 | +12 |
| Deaths | 49 | 57 | +8 |
| Suicides | 14 | 10 | -4 |
| Unassisted environmental deaths | 11 | 9 | -2 |
| Damage to participants | 5,175 | 6,393 | +1,218 |
| Hit-wall events | 5,629 | 2,493 | -3,136 |
| Movement-intent stuck events proxy | 24 | 26 | +2 |
| Union navigation nodes visited | 199 | 202 | +3 |

The movement guard recorded zero interventions in both runs.

## Death interpretation

The additional eight deaths are not an independent survival counter defect. In
this closed all-bot free-for-all, exact deaths partition as enemy kills plus
suicides:

```text
stock:     49 deaths = 35 enemy kills + 14 suicides
candidate: 57 deaths = 47 enemy kills + 10 suicides
```

The candidate increases shared combat tempo: it creates twelve more kills and
four fewer suicides, which mechanically yields eight more total deaths. Its
score delta rises from 21 to 37. This is a promising combat/safety tradeoff,
not evidence of a per-bot survival advantage; every participant receives the
same candidate behavior.

## Movement concern

The candidate has one unresolved recovery episode that warrants repeat
evidence. `Alarik` (`pri:9`) remains nearly stationary while `Walking` with a
latent `MoveToward` targeting `HealthVial6`, from ticks 5172 through 5472
(86.20–91.20 seconds), near `(2272.88, -645.15, -504)`. It has zero
acceleration and misses the five-second recovery deadline.

No `Pawn.CanSee` call occurs in that episode. The candidate changes only the
vision predicate, so this supports an indirect trajectory/path effect at most;
it does not justify changing navigation logic yet. Of the eight additional
exact stall detections, five are intentional stops and three replan within five
seconds, leaving the single missed-deadline episode as the actionable signal.

## Required next evidence

1. Complete the exact same-seed formal repeat and verify the episode.
2. Run a fresh paired Deck16 seed; inspect missed-deadline records rather than
   inferring causality from aggregates.
3. Do not change navigation unless the same target/geometry reproduces.
4. Preserve the frozen UT436 qualification requirement: six paired runs each
   on Deck16, Pressure, and Morpheus, with no intent-stuck regression, before
   any release decision.

Relevant artifacts:

- `qa/runs/2026-07-26/ut436-deck16-pawn-vision-attribution-preflight`
- `Tools/BotBenchmark/QualificationCampaigns/UT436-tuning-quality-gates-v1.json`

## Fresh-seed result and disposition

The required fresh pair completed at seed `271828`, with both structural
validations passing. It does **not** support advancing this vision candidate:

| Metric | Stock | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Kills | 33 | 33 | 0 |
| Deaths | 51 | 51 | 0 |
| Suicides | 18 | 18 | 0 |
| Unassisted environmental deaths | 12 | 12 | 0 |
| Hit-wall events | 2,464 | 9,535 | +7,071 |
| Movement-intent stuck events proxy | 17 | 22 | +5 |
| Movement-intent no-progress seconds | 184.67 | 162.68 | -21.98 |
| Longest movement-intent no-progress seconds | 28.43 | 13.37 | -15.07 |

The candidate has no combat or survival benefit in this seed, while exact
hit-wall contacts and the intent-stuck proxy regress sharply. The missed
five-second recovery count is tied at two, so the specific `HealthVial6`
episode does not generalize as a unique causal defect; the broader wall-contact
regression is sufficient to reject the current cone correction as a UT99
quality candidate.

Keep the implementation default-off as a tested diagnostic experiment. Do not
enable it for release or proceed to the UT99 map qualification matrix without a
new, separately evidenced candidate.

Additional artifact:

- `qa/runs/2026-07-26/ut436-deck16-pawn-vision-fresh-seed-s271828`
