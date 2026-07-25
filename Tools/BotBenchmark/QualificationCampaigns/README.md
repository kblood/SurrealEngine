# Bot qualification campaign authority

`UT436-Unreal226b-qualification-campaign-v1.json` fixes the four required
owner-data matrices, their frozen URLs, baseline/candidate IDs, 16-bot roster,
three pinned start layouts, and two repetitions per layout. It is an authority
for constructing owner-local release manifests; it intentionally contains no
commercial-game root or executable path.

Each matrix must use release provenance and its named gate file:

```powershell
python .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.py `
  --manifest .\owner-local-release-matrix.json `
  --output C:\Devstuff\QuestGames\SurrealEngine\qa\runs\YYYY-MM-DD\bot-qualification `
  --quality-gates .\Tools\BotBenchmark\QualificationCampaigns\UT436-tuning-quality-gates-v1.json
```

`--quality-gates` validates the checked-in configuration before launching a
match, implies structural analysis, writes `quality-gate-result.json`, makes a
failed gate fail the matrix, and hashes the gate input in `provenance.json`.
The gate result is therefore attributable to both the exact matrix input and
the exact checked-in policy.

The current campaign is deliberately not passable. Its gate files require the
four metrics recorded under `unrepresented_release_requirements`; current
telemetry does not emit them. This prevents exact death partitions, wall-time,
or separate-executable A/B comparisons from being misrepresented as causal
avoidable-suicide evidence, role-swapped participant control, recovery-time
evidence, or 16-bot in-engine p95 timing. Add those authoritative metrics and
their tests before changing the gate files; do not relax or remove them to
obtain a passing result.

Held-out matrices are sequenced after their matching tuning matrix passes. A
campaign pass requires every matrix result and every named gate result to pass;
the runner invokes one matrix at a time so game roots and binaries never enter
the checked-in campaign authority.
