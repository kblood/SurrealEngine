# UT99 `Pawn.CanSee` Witness Provenance

Date: 2026-07-26  
Scope: deterministic UT436 `DM-Deck16][` observation evidence only. This is
not a merge or quality qualification decision.

## Configuration

Both runs used `DM-Deck16][?Game=Botpack.DeathMatchPlus`, seed `104729`, 16
bots at difficulty 3, 7,200 fixed ticks, and the finite-movement guard enabled.
The read-only pawn-vision observer was enabled in both runs. The only relevant
configuration difference was `pawn_vision_cone_enabled`:

| Variant | Vision cone | Observer | Completion / validation |
| --- | ---: | ---: | --- |
| `ut436-deck16-cansee-witness-stock-s104729-skill3-7200-r1` | off | on | complete / passed |
| `ut436-deck16-cansee-witness-candidate-s104729-skill3-7200-r1` | on | on | complete / passed |

The exact finite-movement guard intervention count was zero in both runs, so it
did not alter this A/B evidence.

## Provenance result

The observer recorded the immediate UnrealScript caller of the native
`Pawn.CanSee` call. Every observation came from a real `Botpack` bot state
function; no synthetic probe or native wrapper was recorded as the caller.

| Caller class / function | Stock calls | Candidate calls | Candidate divergences |
| --- | ---: | ---: | ---: |
| `Botpack.TMale1Bot` / `Killed` | 57 | 55 | 26 |
| `Botpack.TMale1Bot` / `Hunting` | 32 | 13 | 2 |
| `Botpack.TMale2Bot` / `Killed` | 38 | 80 | 46 |
| `Botpack.TMale2Bot` / `Hunting` | 62 | 40 | 14 |
| `Botpack.TFemale1Bot` / `Killed` | 7 | 21 | 6 |
| `Botpack.TFemale1Bot` / `Hunting` | 27 | 33 | 5 |
| `Botpack.TFemale2Bot` / `Killed` | 33 | 55 | 37 |
| `Botpack.TFemale2Bot` / `Hunting` | 107 | 33 | 0 |
| **Total** | **363** | **410** | **136** |

`Killed` and `Hunting` are therefore the exercised UT Botpack paths for this
candidate. The class names are the concrete spawned bot classes; a state
function may be inherited, so this table is caller provenance rather than a
claim that each concrete class owns a separate script body.

## Cone-result evidence

All 410 candidate records selected the corrected cone; all 363 stock records
selected the legacy cone. The observer still computes both outcomes for each
valid call, which makes disagreement observable without changing the stock
result.

| Candidate legacy cone | Candidate corrected cone | Returned visible | Calls |
| --- | --- | --- | ---: |
| true | false | false | 109 |
| false | true | false | 14 |
| false | true | true | 13 |
| **Different outcomes** |  |  | **136** |

The 136 candidate disagreements were distributed across `Killed` (115) and
`Hunting` (21), and targeted the four concrete bot classes: `TMale1Bot` (44),
`TMale2Bot` (42), `TFemale2Bot` (28), and `TFemale1Bot` (22). All records had
`integrity_valid=true`; the counter reported zero observer overflows and zero
integrity failures. The stock run also had zero overflows/failures and 114
counterfactual legacy/corrected disagreements, but selected the legacy result
for every call.

This proves the corrected branch was exercised by intended UT bot-script
decision paths and changed returned visibility on 13 witnessed calls. It does
not prove that a particular subsequent kill, death, or route choice was caused
by an individual call: once the cone is enabled, the two deterministic matches
take different state trajectories and their calls cannot be paired one-for-one.

## Supporting aggregate context

The same one-seed run is promising but not sufficient to qualify quality:

| Metric | Stock | Candidate | Difference |
| --- | ---: | ---: | ---: |
| Kills | 35 | 47 | +12 |
| Deaths | 49 | 57 | +8 |
| Suicides | 14 | 10 | -4 |
| Unassisted environmental deaths | 11 | 9 | -2 |
| Damage dealt to participants | 5,175 | 6,393 | +1,218 |
| Hit-wall events | 5,629 | 2,493 | -3,136 |
| Move-stall detections | 10 | 18 | +8 |
| Union navigation nodes visited | 199 | 202 | +3 |

The score changes must be repeated with the same configuration and evaluated on
additional UT99 seeds/maps before describing the candidate as an improvement.

## Reproduction / extraction commands

The compact `quality-v2.json` files verify completion, counters, configuration,
and fail-closed analysis. Caller provenance was extracted from the event stream
without parsing unrelated records:

```powershell
$rx = [regex]'"pawn_can_see_records":\[(?<records>.*?)\]'
foreach ($line in [System.IO.File]::ReadLines((Join-Path $run 'events.jsonl'))) {
    if ($line.IndexOf('"pawn_can_see_records":[{',
        [StringComparison]::Ordinal) -lt 0) { continue }
    foreach ($match in $rx.Matches($line)) {
        $records = ('{"r":[' + $match.Groups['records'].Value + ']}') |
            ConvertFrom-Json -Depth 16
        # group $records.r by caller_class/caller_function and cone fields
    }
}
```

Observed files:

- `C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-26\ut436-deck16-cansee-witness-stock-s104729-skill3-7200-r1`
- `C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-26\ut436-deck16-cansee-witness-candidate-s104729-skill3-7200-r1`
