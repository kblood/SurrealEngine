# Cross-game live ReachSpec capability evidence

Date: 2026-07-26

## Scope and result

Two complete, stock 16-bot, 7,200-tick anchor runs were executed with the
default-off native path-commit and ReachSpec-capability observers explicitly
enabled. Both runs have an engine-attested manifest and complete summary; both
the quality analyzer and the ReachSpec commit-capability analyzer passed.

This is **observer evidence only**. It confirms that the analyzer can join an
exact native route-cache commit to the matching catalog edge and realized live
capability witness in both supported games. It does not establish that a
ReachSpec, route-cache, or anchor caused a harmful move, and it authorizes no
bot behavior change.

## Attested lanes

| Lane | External QA run | Map / game | Capability result | Quality result |
| --- | --- | --- | --- | --- |
| UT436 | `C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-26\ut436-deck16-reachspec-capability-stock-s104729-7200-r3-attested-parity` | `DM-Deck16][?Game=Botpack.DeathMatchPlus` / Unreal Tournament 436 | qualified; 5,219 exact committed edges and 5,219 exact eligible edges | complete, exit 0, 7,200 ticks; no paired-comparison mismatches |
| Unreal 226b | `C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-26\unreal226b-deathfan-reachspec-capability-stock-s104729-7200-r1-attested-parity` | `DmDeathFan?Game=UnrealShare.DeathMatchGame` / Unreal Gold 226b | qualified; 823 exact committed edges and 823 exact eligible edges | complete, exit 0, 7,200 ticks; no paired-comparison mismatches |

For both lanes, `committed_edges_exact == eligible_edges_exact`; thus the
capability audit reported zero rejected/mismatched committed edges. The
observer configuration was explicit and consistent in each `manifest.json` and
`summary.json.config`:

```text
native_path_commit_observer_enabled=true
reachspec_capability_observer_enabled=true
```

## Durable evidence and provenance

Each run directory contains the following durable evidence, in addition to its
manifest-backed event stream:

- `manifest.json` and `summary.json` — completion state, configuration, and
  engine build attestation;
- `bot-realized-capabilities.json` — actual roster identity and capability
  witness used by the capability join;
- `route-execution.jsonl` — exact native committed-path and edge observations;
- `reachspec-capability.json` — successful catalog-backed capability audit;
- `quality-v2.json` (UT) or `quality.json` (Unreal) — successful quality
  analysis.

The two manifests attest the same executable identity:

```text
build identity: sha256:2E94166EA327BB28D4D3B7F1C93CDBCB15AC7AD5076E02E18EC7954DA39DED33
executable SHA-256: 086DC774EFC7D3223C8DD95E405B03DFFBAC0BB76309D6DCB19E25B8C153C9E8
source commit: a74af4dca81c853f2cfee4f62538ce6fb5a4ec45
source tree: 0970ea3913d509d4d1cc76164ed91ec217a8079e
source dirty: true
```

The attestation is material: it binds both evidence sets to the executable
which emitted them, rather than treating an externally supplied summary or
analysis report as sufficient. `source dirty: true` also means the runs are
evidence for that exact attested binary, not a clean-tree release claim.

The catalog bindings are `DM-Deck16][` / SHA-1
`d157dfe26490180660a0f207607d4743190b980b` and `DmDeathFan` / SHA-1
`b8f5d776d0516a5afa62f1d305601a2d8f99da57`, respectively.

## Decision boundary

Route filtering remains rejected for these anchors. The evidence shows that
the committed edges were compatible with the live participant capabilities; it
does not show that a committed route was the active harmful command when a
failure occurred. A route cache can be historical context after a direct
movement command or `SpecialHandling` redirection.

Consequently, do not add a ReachSpec capability filter, veto, route pin,
route-cache clear policy, or route penalty from these results. The standing
authorization boundary in `BOT_AI_ROUTE_EXECUTION_FINDINGS.md` and
`BOT_AI_OPUS5_REVIEW_2026-07-25.md` remains unchanged: a future candidate
needs a deterministic same-life causal witness tying the current anchor,
selected edge, active command, progress/stall window, and harmful outcome
together, followed by a counterfactual and cross-game qualification.

No engine or bot behavior change is authorized by this note.
