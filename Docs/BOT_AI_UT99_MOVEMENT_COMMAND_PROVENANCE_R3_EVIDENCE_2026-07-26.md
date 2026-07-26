# UT99 movement-command provenance r3 evidence — 2026-07-26

## Scope

This note classifies the strict failure from the default-off, read-only
movement-command provenance observer. It concerns the UT436 retail Deck16-II
run at seed `104729`, 16 skill-3 bots, and 7,200 fixed ticks:

`D:\SurrealEngineQA\2026-07-26\ut436-deck16-movement-command-provenance-s104729-r3\runs\000000-command-provenance-dm-deck16-game-botpack-deathmatc-s104729-r0-d119cfd15af0`

The run has an attested enabled native path-commit observer and enabled
movement-command provenance observer. `Analyze-MovementCommandProvenance.py`
correctly stops at the first unsupported terminal instead of treating a
partial association as causal evidence.

## Verdict

`pri:15` is a genuine same-life command-supersession boundary, not observer
record loss, token clearing, or an unobserved native `MoveTo`/`MoveToward`
issue.

The final participant counters retain `50` movement-command observations and
`0` observer overflows. All three terminal PainTimer residence deaths retain a
nonzero entry command token, caller class, and caller function; their
provenance is deliberately marked inexact because later native movement
commands replaced the entry command before death.

| Life | Residence death tick | Entry token | Entry command tick / target | Later same-life tokens before death | Residence command changes | Exact at death |
| --- | ---: | ---: | --- | --- | ---: | --- |
| 1 | 297 | 1 | 3 / `PathNode122` | 2 at 84 (`PathNode122`); 3 at 248 (`PlayerStart16`) | 2 | no |
| 2 | 1103 | 4 | 847 / `PathNode122` | 5–10, ticks 902–1069; includes `MoveTo` and `PlayerStart16` reissues | 9 | no |
| 6 | 5728 | 3 | 5486 / targetless `MoveTo` | 4–9, ticks 5569–5661; mixes `Hunting` and `Roaming` | 10 | no |

For the first rejected death, the terminal record preserves life `1`, entry
token `1`, and caller `Botpack.TFemale1Bot.Roaming`; the raw record stream also
contains that token. The active command had subsequently advanced to token
`3`, so the observer's exact condition intentionally fails. The other two
PainTimer deaths exhibit the same pattern. No life boundary occurred between
the entry and terminal for any of these episodes.

## Interpretation

The current witness proves that a command was present at residence entry, but
does not prove which of the later in-water command transitions owned continued
exposure or removed a safe exit. Retaining the original entry token as an
"exact" terminal owner after it has been superseded would convert temporal
context into a causal claim. The fail-closed analyzer must continue to reject
that claim.

This evidence does not authorize a water steer, route veto, target override,
or any other movement behavior change.

## Next observer boundary

The next permitted slice is a default-off, bounded **hazard-residence command
transition ledger**. While a positive-DPS residence is active, it should append
an observer-only record for each native `MoveTo`/`MoveToward` issue containing:

- residence episode and life identifiers;
- prior and new movement-command tokens, issue tick, kind, caller, target,
  route head, and last native path commit;
- whether the command was issued before entry, is the entry command, or
  superseded a command during residence; and
- a bounded overflow/integrity outcome that makes the episode unanalyzable.

At death, the observer may report the ordered transition chain and classify it
as `entry_command_survived`, `superseded_in_residence`, or `unavailable`.
Only the first class may support an exact entry-to-terminal join. The other two
remain explicit non-causal evidence. The ledger must not change command state,
acceleration, route cache, latent state, or bot policy.
