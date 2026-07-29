# BOT AI UT99-first continuation plan — 2026-07-26

This is a planning document. It adds no runtime behavior, relaxes no gate,
renames no metric, and opens no held-out map. It sequences the next work on
Unreal Tournament 436 bot quality, retains Unreal Gold 226b as a compatibility
lane, judges the proposed self-splash provenance observer, and states the
conditions under which work stops.

It supersedes nothing in `Docs/BOT_AI_QUALITY_EXECUTION_PLAN.md` or
`Docs/BOT_AI_CONTINUATION_PLAN_OPUS5.md`; those remain the authoritative
iteration history and the authoritative fail-closed rule set. Where this plan
reorders work, it says so explicitly and gives the reason.

## Standing judgment

The branch is **not release-ready and not merge-ready as a bot behavior
change**. Nothing below changes that, and nothing below may be described as a
behavior improvement until a candidate passes measured multi-map evidence
against a frozen stock anchor.

The recent UT99 tranche — iteration 164 plus the five 2026-07-26 evidence notes
— is high-quality observation work that has now produced four consecutive
non-authorizations. That is a correct outcome, but it is also a signal: the
water lane's evidence question has been asked three times in three forms
(command ledger, causal slice, route/trajectory) and returned "heterogeneous,
no common cause" each time. This plan therefore changes the question rather
than adding a fourth variant of it, and it puts one concrete shared-engine
fidelity defect in front of the policy work.

## 1. Verified facts

These are directly attested by the cited artifacts, analyzers, or source in
this worktree. They are not inferences.

### 1.1 The UT99 anchor and its outcome partition

The current best UT99 anchor is UT436 `DM-Deck16][?Game=Botpack.DeathMatchPlus`,
seed `104729`, 16 stock Botpack bots at external skill 3, 7,200 fixed ticks
(120 simulated seconds), initial-layout fingerprint
`sha256:3082c76e18e6122d8b13b4c65b1568938f3f12d300d6e19c576c592352bf76a4`.
Matrix and structural quality analysis pass.

Its exact totals are: damage taken `6895`; damage from other participants
`5175`; damage from nonparticipants/environment `1632`; self-attributed damage
`88`; deaths `49`; direct enemy kills `35`; direct self kills `0`; unassisted
environmental deaths `11`.

### 1.2 Where the deaths actually are

The hazard-residence lane sealed 16 integrity-valid episodes with zero record
overflow: 15 `PainTimer` harmful-water death terminals and one run-end censor.
Entry-zone identity is concentrated — `134:SlimeZone0` 12, `137:SlimeZone2` 2,
`136:SlimeZone1` 1 — but the causal evidence is not:

- 14 of 15 deaths transition `falling` to `falling`, with no static support, no
  same-tick `MayFall`, and no walking `HitWall` boundary. The one exception is
  a single `walking` to `falling` `SlimeZone2` entry with a `MayFall` boundary
  at tick 883 and no `HitWall`.
- Entry commands are 13 `MoveToward` and two `MoveTo`, across Roaming (9),
  Hunting (3), Charging/Retreating (3) and one mixed state. Same-life command
  lineages range from one to nineteen commands.
- After entry there are 55 further native command replacements, distributed
  0/1/2/3/4/6/18 across episodes. Entry callers are Roaming (10), Hunting (4),
  Retreating (1), Charging (1); entry targets include PathNodes, Jumpboots,
  UDamage, ShieldBelts, Enforcer, LiftExit, PlayerStart, and targetless
  `MoveTo`.
- At entry, 13 episodes retain a route head and three have an exact native
  `RouteCache` clear commit — the missing heads are recorded absence, not
  missing data. Retained heads are heterogeneous (`PathNode122`,
  `InventorySpot152`, `LiftExit6`, and several singletons). 15 of 16 retain a
  concrete command target, all joined to their entry command and native
  path-commit sequence by the fail-closed analyzer.

`pri:15` is the only near-repeat — lives 1 and 2 share an early
`PathNode122` → `PlayerStart16` pattern — and it does not recur across
independent bots. Iteration 164 separately established that all three `pri:15`
terminal residences had a valid entry command that was **superseded** by later
same-life native `MoveTo`/`MoveToward` issues before death, with 50 exact
command observations and zero overflow. The inexact joins are real
supersession, not a retention defect.

### 1.3 The combat-safety observers

- `CanFireAtEnemy` observer (`4503b6d7`), same anchor: 231 exact calls, 0
  overflows, 0 integrity failures, 125 stock `true`, 106 shadow non-enemy
  blocker indications, and **zero** calls that were both stock-`true` and
  shadow-blocked. One hit-class disagreement (`TFemale2Carcass -> enforcer`),
  not on an authorized shot. Actual/shadow hit-presence disagreement is zero.
- Self-damage: four positive observations totalling the `88` self-attributed
  damage. All four occur in `PHYS_Falling`; three are in `FallingState`
  (Ichthys at ticks 1136 and 2918, Tamerlane at 1657) and one in `RangedAttack`
  (Alys at 6561). `direct_self_kills` is `0`.

### 1.4 Fixture and harness state

- UT99 supports unattended bot-only deterministic matches through
  `--autoplay` and `--headless-driver=bot-benchmark`; the controller suppresses
  automatic bots, logs in a spectator viewport, creates the exact requested
  roster, and fails if the realized roster differs. No UCC, UnrealEd, or player
  input is involved. This is the approved integration-test path.
- Commit `d9a0550b` adds `matrix.map_start_layouts` to
  `Tools/BotBenchmark/Run-BotBenchmarkMatrix.py`, keyed to exact map URLs, bound
  into run and pair identity, failing before engine launch on unknown,
  malformed, duplicate, or mismatched entries. Existing `seeds` and global
  `start_layouts` manifests remain compatible. 30 focused runner tests and 239
  full BotBenchmark tests pass.
- Deck16 and Pressure have **different** layout fingerprints at seed `104729`.
  A single global layout declaration cannot truthfully qualify both maps.
- Map evidence status: Deck16 complete 16-bot/7,200-tick captures; Pressure one
  complete 16-bot/7,200-tick capture at seed `104729`; Morbias a 16-bot
  1,200-tick smoke; Morpheus and Fractal static headless catalog/load evidence
  only; Phobos has no local load, catalog, or bot-match evidence at all.
- `Tools/BotBenchmark/QualificationCampaigns/UT436-Unreal226b-qualification-campaign-v1.json`
  declares exactly three fail-closed `unrepresented_release_requirements`:
  `role_swapped_participant_policy_coverage`, `avoidable_suicide_rate`, and
  `ai_frame_p95_ms`. Pressure's observation-scope p95 of 3.242 ms is not the
  in-engine metric and is not a substitute for it.

### 1.5 Engine facts relevant to the splash lane

Read directly from this worktree:

- `SurrealEngine/VM/Iterator.cpp:369-388` — `VisibleCollidingActorsIterator`
  builds its candidate set from
  `engine->Level->Collision.CollidingActors(Location, Radius)` and filters only
  on null, `bHidden`, and class. **It performs no visibility trace of any
  kind.** The native is registered as `Actor` index 312 in
  `SurrealEngine/Native/NActor.cpp:93-95`, with a separate 219-era
  four-argument form.
- The hidden-actor filter is `(IgnoreHidden || !actor->bHidden())`
  (`Iterator.cpp:380`), i.e. passing `bIgnoreHidden = true` *admits* hidden
  actors. That is at least suspicious against the parameter's name and needs an
  independent retail determination before it is touched.
- `CollisionSystem::TraceAnyHit(from, to, tracingActor, traceActors, traceWorld, visibilityOnly)`
  already exists (`SurrealEngine/Collision/TopLevel/CollisionSystem.h:32`) and
  backs `NActor::FastTrace`. A visibility-filtered iterator is implementable
  with existing, tested collision primitives.
- Retail `Actor.HurtRadius` is UnrealScript (`final function`), so it is
  VM-dispatched and hookable, and there is exactly one implementation in the
  loaded package set. `bHurtEntry` is already exposed at
  `SurrealEngine/UObject/UActor.h:796`.
- `VMCallHookRegistry` (`SurrealEngine/VM/CallHooks.h`) supports ordered
  `Enter` hooks returning cleanup closures, plus `ObserveResult` on normal
  return, with per-hook rollback and isolated observer exceptions. Nested
  invocation-scoped observation is existing infrastructure, not new work.
- Self-damage attribution already exists and is exact:
  `SurrealEngine/BotBenchmark/BotBenchmarkDriver.cpp:1829-1865` counts a
  canonical `TakeDamage` health loss into `damage_taken_from_self_exact` when
  the instigator identity equals the victim identity, and into
  `damage_taken_from_nonparticipants_exact` when the instigator is not a
  tracked participant.

### 1.6 Worktree hygiene

The tree currently carries two uncommitted modifications:
`Docs/BOT_AI_OPUS5_REVIEW_2026-07-25.md` and
`SurrealEngine/UObject/PawnInventoryReachability.cpp`. Build-identity
attestation (iteration 162) records dirty-tree state. No A/B capture in this
plan may be produced from a dirty tree.

## 2. Hypotheses, explicitly not facts

Each is stated with the observation that motivates it and the test that would
falsify it. None may be cited as a cause until its test runs.

| ID | Hypothesis | Motivating observation | Falsification test |
| --- | --- | --- | --- |
| H1 | The four self-damage events are splash from the bot's own projectile, reaching the instigator through `Actor.HurtRadius`. | All four are in `PHYS_Falling`; `Bot.uc` `FireWhileFalling` and `LongFall` fire while falling. | The self-splash observer's reconciliation partition (§3.2, C3). A self-damage event with no enclosing `HurtRadius` scope falsifies H1 for that event. |
| H2 | Surreal's occlusion-free `VisibleCollidingActors` inflates splash damage — both self and inter-participant — relative to retail UT99. | `Iterator.cpp:369-388` has no trace; the native's name implies a visibility filter. | A retail UT436 oracle case in which an occluded pawn inside `DamageRadius` takes zero damage while a clear pawn at equal distance takes damage. If retail also damages through the wall, H2 is false. |
| H3 | The `bIgnoreHidden` polarity in `Iterator.cpp:380` is inverted relative to retail. | The condition admits hidden actors precisely when the flag is set. | The same retail oracle with a collidable `bHidden` victim, run at both flag values. |
| H4 | Splash inflation is large enough to distort measured bot combat and survival outcomes on Deck16. | 1,632 of 6,895 damage is nonparticipant/environmental and is unpartitioned by mechanism. | Paired A/B on the frozen anchor after the S2 correction. Unchanged death partition, kills, and damage totals falsify H4 and make the correction fidelity-only. |
| H5 | The `SlimeZone0` concentration (12 of 15 deaths) reflects a route or geometry attractor with a controllable pre-fall decision. | The spatial cluster is strong and repeatable. | Pressure plus a second Deck16 seed. No comparable concentration kills the zone-specific form; only the map-local form would survive. |
| H6 | The `pri:15` `PathNode122` → `PlayerStart16` pattern is a reusable causal signature. | Two of that bot's lives share it. | A second independent bot, seed, or map reproducing the same pair. Absent that, H6 stays dead — and iteration 164's supersession result already argues against it. |
| H7 | A `CanFireAtEnemy` blocker veto could improve safety. | 106 shadow blocker indications. | Already falsified on this anchor: zero coincide with a stock `true`. H7 requires a new attested record meeting all four reopen conditions of the CanFireAtEnemy note. |

Note the asymmetry between H2 and H1. H2 is a shared UE1 correctness question
answerable by a retail oracle and pure fixtures, independent of bot policy. H1
is a bot-policy question whose entire observed magnitude is 88 of 6,895 damage
(1.28%) and 0 of 49 deaths. That asymmetry drives the ordering in §4.

## 3. Judgment: the self-splash provenance observer design

**Verdict: structurally sound and correctly refuses causal claims, but it is
misprioritized as a standalone lead slice and is under-specified in four ways.
Accept it with the corrections below, and land it riding the occlusion work
rather than ahead of it.**

### 3.1 What the design gets right

- Binding to `Engine.Actor.HurtRadius` is the correct seam. It is a `final`
  function, so exactly one implementation exists in the loaded package set and
  reflection cannot be ambiguous — a materially better binding situation than
  `SetEnemy` or `HitWall`, both of which needed state-override handling.
- Requiring exactly five arguments and no return value, and failing closed on
  contract, type, non-finite, nested-lifecycle, or overflow violations, matches
  the pattern proven in this tree by the `SetEnemy`, `PickTarget`, `WarnTarget`,
  and `CanFireAtEnemy` observers.
- Joining **only** a nested canonical `TakeDamage` inside the `HurtRadius`
  invocation scope, rather than correlating by time, is the right discipline.
  Iteration 69's `intact_command_but_no_action_lead` and iteration 114's
  `command_replaced` are precisely what happens when a lane accepts temporal
  association; this design avoids that class of error by construction.
- Recording the contemporaneous weapon class as descriptive context and never
  as causal proof is correct and must stay that way.
- Declining to add a splash guard, modify `HurtRadius`, or veto `FireWeapon` is
  correct. Four observations, zero self-kills, and no identified damage class
  support none of them.

### 3.2 Required corrections

**C1 — Scope by instigator, not by receiver.** `HurtRadius` executes on the
projectile or explosion actor, which is never a benchmark participant. A
"benchmark-participant-only" scope resolved from the hook instance observes
nothing. The observer must resolve the source actor's `Instigator` at entry and
scope on that identity. `Instigator` being `None`, destroyed, or a
non-participant must be an explicit recorded outcome
(`instigator_unavailable`, `instigator_not_participant`), never a silent skip —
otherwise a zero splash count is indistinguishable from a broken binding.

**C2 — Use a bounded stack, not a single active record.** `bHurtEntry` is a
per-actor re-entry guard; it prevents recursion of the *same* actor only. A
`TakeDamage` inside one `HurtRadius` can trigger a chain explosion whose own
`HurtRadius` nests inside the first. The observer needs a bounded active-scope
stack with innermost-scope attribution, an explicit `ambiguous_enclosing_scope`
integrity outcome, and a depth bound whose exhaustion is an integrity failure
rather than a dropped record. The `VMCallHook` `Enter`/cleanup contract supports
this; the pickup-touch depth counter at `BotBenchmarkDriver.cpp:1814-1827` is
the existing precedent for the bookkeeping and its fail-closed cleanup
assertion.

**C3 — Reconcile against `damage_taken_from_self_exact`. This is the single
most important addition.** As designed the observer counts splash events, so it
cannot distinguish "no self-splash occurred" from "self-splash occurred through
a path this observer does not see". UT99 produces instigator-equals-victim
`TakeDamage` from more than one path, and the four observed events have not
been shown to be splash at all — that is H1, still untested. The observer must
emit an exact partition of every positive self-attributed damage event in the
run into:

- `explained_by_hurtradius_scope` — the canonical `TakeDamage` was nested in an
  observed `HurtRadius` scope whose source instigator equals the victim;
- `unexplained_self_damage` — a positive self-attributed health delta with no
  enclosing observed `HurtRadius` scope;
- `unavailable` — observer disabled, contract rejected, overflowed, or
  integrity-failed for that event.

The analyzer must require that the three buckets sum exactly to the count of
positive self-damage events reconciled from `damage_taken_from_self_exact`, and
must reject the artifact otherwise. Without this, a negative result is not
evidence. With it, a run reporting four `unexplained` events is a genuine
falsification of H1 that immediately redirects the lane — to falling damage,
direct projectile `Touch`, or telefrag — instead of consuming a second capture.

**C4 — Carry the retail-visibility counterfactual per victim.** Given §1.5,
every splash victim record must additionally carry a read-only,
applied-nowhere result of a retail-equivalent visibility trace from
`HitLocation` to the victim, using the existing
`CollisionSystem::TraceAnyHit(..., visibilityOnly)`. This changes no damage and
no behavior. It makes the same capture answer H2's magnitude question — how
many splash victims would an occlusion-filtered iterator have excluded? — at
near-zero cost, and it prevents the provenance numbers from being read as a
retail bot-safety quantity when they are currently a Surreal-specific one.
Record both victim sets; never substitute one for the other.

### 3.3 Additional record fields

Beyond the fields the research note lists, require: the source actor's
`Instigator` life ID and participant identity; `bHurtEntry` state at entry, so a
retail re-entry rejection is visible rather than inferred; the enclosing scope
depth; the per-victim retail-visibility verdict from C4; and the victim's exact
benchmark participant identity, so the C3 reconciliation is a join rather than a
name match. The five-argument contract must be re-derived by reflection from the
owner install at runtime, not trusted from the research note's line reference.

### 3.4 Expected value, stated in advance

On the anchor the entire observable population is four events, 88 damage
(1.28% of damage taken), and zero deaths. **This observer cannot move any
release gate, and no result from it may be presented as a bot-quality
improvement.** Its legitimate purposes are (a) testing H1 and closing or
redirecting the self-damage lane cheaply, and (b) carrying the C4
counterfactual for the occlusion work. Those are worth one bounded slice. They
are not worth sequencing ahead of the 15 harmful-water deaths or the three
fail-closed campaign metrics, and the research note's implicit ordering is
changed accordingly.

## 4. Ordered implementation, test, and capture plan

Each step is independently reviewable, independently revertible, and carries
its own stop condition. S0–S5 are the UT99-first lane; W1–W3 are the
independent measurement lane and may run concurrently because they touch no
gameplay path.

### S0 — Freeze the stock anchor (no code change)

1. Commit or revert the two uncommitted worktree modifications. No capture
   below may come from a dirty tree.
2. Re-run the Deck16 anchor of §1.1 twice from the same attested Release
   binary. Record executable SHA-256, git state, layout fingerprint, and the
   full outcome partition of §1.1–1.2.
3. Record the same for Unreal Gold 226b `DmDeathFan` at its established seed
   and native difficulty 3, as the compatibility control.

**Closes when** both repetitions are exactly equivalent per
`Compare-BotBenchmarkRuns.py`, ignoring only the audited output directory and
the host-varying `ai_frame_timing` field, in both games.

**Stop condition.** If the repetitions are not exactly equivalent, everything
below stops. A non-deterministic anchor invalidates every A/B in this plan, and
that defect becomes the highest-priority work item.

### S1 — Retail `VisibleCollidingActors` occlusion oracle (UT99 first)

Reuse the isolated retail-oracle harness pattern already proven for
`MinHitWall` under `Tools/BotBenchmark/RetailHitWallOracle/`: disposable
runtime below the QA directory, `System`-only copy with asset junctions, local
package compile behind the existing bounded compile guard, bounded dedicated
server child, and full before/after SHA-256 inventories of the installed retail
root.

Cases, each with two repetitions and pinned geometry:

1. **Clear control.** Instigator, an unobstructed line, and a victim pawn at a
   fixed distance strictly inside `DamageRadius`. Expect damage.
2. **Occluded case.** Same distance, with a solid static blocker between
   `HitLocation` and the victim. Retail's verdict here decides H2.
3. **Hidden-actor case.** A collidable `bHidden` victim, run at both
   `bIgnoreHidden` values, deciding H3.
4. **Negative control.** Victim outside `DamageRadius`. Expect no damage in
   every configuration.

**Closes when** all four cases have two matching repetitions, byte-identical
installed inventories, and an unambiguous verdict for H2 and H3.

**Stop conditions.**

- If retail damages the occluded victim, H2 is false. Close it, record the
  result, remove the occlusion correction from this plan, and keep only C4's
  counterfactual field as descriptive telemetry.
- If the oracle cannot produce a repeatable pinned contact — the exact failure
  mode that blocked the `MinHitWall` quarter-step batches — record the empirical
  result as an unresolved bracket and do **not** change the iterator. An
  unproven operand blocks a runtime correction; that rule is unchanged.

### S2 — Shared iterator fidelity correction (only if S1 confirms H2)

Scope is deliberately minimal: `VisibleCollidingActorsIterator` gains the
visibility filter its name and the retail verdict require, and the
`bIgnoreHidden` polarity is set to whatever S1 case 3 proved. Nothing else. No
`HurtRadius` change, no damage-model change, no bot policy, no new slide or
callback path.

1. Pure fixtures first: clear line, fully occluded, grazing/edge-on, blocker
   behind the victim, hidden victim at both flag values, zero radius, victim at
   the exact origin, and self-as-victim. Assert the selected set exactly, not
   just its size.
2. Preserve the 219-era four-argument registration path explicitly, with its
   own fixture rather than an assumption of equivalence.
3. Paired A/B against the S0 anchor, UT99 first: full death partition, kills,
   damage totals and sources, `hit_wall_events_exact`, hazard entries and
   exposure, movement-intent stall metrics, and union navigation coverage.
4. Then the Unreal Gold 226b compatibility A/B on `DmDeathFan` and `DmDeck16`.
5. Then Morpheus (§6), because this change acts on airborne explosions and
   Morpheus is the low-gravity stress case.

**Closes when** UT Deck16, UT Morpheus, and both Unreal anchors each have
deterministic repeats and a recorded delta panel.

**Stop conditions.**

- Any non-determinism introduced by the change: revert immediately and prove
  rollback by matching executable and event-stream SHA-256, not by similar
  aggregates.
- A survival, hazard, combat, wall, stall, or coverage regression on any single
  anchor blocks promotion even if the aggregate improves. The standing rule
  that an aggregate gain may not conceal a per-map safety regression applies
  without exception.
- If the correction is behavior-neutral everywhere, that is a *success* for a
  fidelity slice: it becomes a merge candidate on correctness grounds under
  §7(A), and H4 is falsified.

### S3 — Self-splash provenance observer, default-off (§3 as corrected)

Implement with corrections C1–C4 and the fields of §3.3. Default-off, carried
through the command line, run identity, manifest, summary, matrix provenance,
analyzer, and `Compare-BotBenchmarkRuns.py`, exactly as every prior observer.

Pure fixtures: matched instigator-equals-victim join; non-matching victim
rejected; zero or negative health delta rejected; an unrelated nested
`TakeDamage` inside the same `HurtRadius` scope rejected; nested chain
explosion attributed to the innermost scope; non-finite argument; overflow;
unbalanced nested-stack exit; `Instigator` absent; and a package set whose
`HurtRadius` signature does not match, which must disable the observer with an
explicit reason rather than pass on zero records.

**Closes when** observer-on/off runs on the S0 anchor in both games are
byte-identical after excluding only `config_id`, the declared observer
envelope, and the declared `_exact` counters; two candidate repetitions per
configuration are exactly equivalent; and the C3 reconciliation partition sums
exactly on every run.

**Stop conditions.**

- Any observer-induced gameplay difference: remove, do not explain.
- Any record overflow, integrity failure, or reconciliation mismatch: the
  artifact is rejected, not annotated.
- If the contract validator rejects `Engine.Actor.HurtRadius` in either game,
  the observer disables itself with a stable reason and the run reports zero
  coverage — never partial data.

### S4 — One capture, two questions

Run the S0 anchor once with S3 enabled on the pre-S2 binary and once on the
post-S2 binary, in both games. This yields, from the same seed and layout: the
C3 self-damage partition, deciding H1; and the C4 occluded-victim
counterfactual measured against the actual post-correction victim set,
quantifying H2 and H4.

**Stop conditions.**

- If C3 reports `unexplained_self_damage` for the majority of self-damage
  events, H1 is false. Close the splash lane, record the result, and redirect
  to whichever mechanism the unexplained events indicate. Do **not** widen the
  observer's seam to manufacture coverage.
- If C4 shows that occlusion filtering excludes no victim on any anchor, H4 is
  false and the correction is fidelity-only. Say so plainly.

### S5 — Only now: is there a combat-safety behavior candidate?

A candidate may be *proposed* only if S4 produced, on UT99, a repeated class of
self-splash events sharing one instigator state, one damage class, and a
pre-fire decision point with a demonstrated safe alternative — and only if that
class is large enough to matter. Given `direct_self_kills == 0` on the anchor,
the honest prior is that no such candidate exists, and "no candidate" is the
expected and acceptable outcome of this step.

The four reopen conditions in the `CanFireAtEnemy` note remain binding for any
fire-authorization change, and none is currently satisfied.

### W1–W3 — Independent measurement lane (concurrent, no gameplay path)

These are the three fail-closed campaign requirements of §1.4. They gate the
release regardless of which behavior lane eventually succeeds, and none depends
on S0–S5.

- **W1 — in-engine `ai_frame_p95_ms`.** Cheapest of the three and a hard
  requirement. Instrument only bot-relevant per-frame work; emit p50/p95/p99/max
  and sample count with per-run bot-count context; exclude the values from
  byte-equivalence by an audited ignore rather than by weakening the comparator.
  Pressure's 3.242 ms observation-scope figure is explicitly not this metric and
  must not be reported as it. Iteration 137's finding that shadow
  observation/policy dominates the aggregate p95 (2.088 ms of 2.564 ms) is the
  first thing this metric should confirm or refute.
- **W2 — `avoidable_suicide_rate`.** The pure classifier contract exists
  (iteration 84) and the pre-commit witness plumbing exists (iterations 85–86
  and 89). The blocking element is the *safe alternative at commitment*, which
  §1.2's 14-of-15 already-falling entries make hard to establish. Reporting the
  metric as unavailable with its exact evidence requirement, and leaving the
  gate failing, remains a successful outcome for this workstream.
- **W3 — `role_swapped_participant_policy_coverage`.** Blocked on
  per-participant policy binding by immutable roster index (continuation-plan
  workstream 7). Shadow-only binding first; a bound run must be
  artifact-equivalent to an unbound run before anything else.

### The water lane, explicitly

The three UT99 water observers — command ledger, causal slice, route/trajectory
— have each closed with no authorization and have collectively established that
the terminal cluster is spatial, not causal. A fourth variant of the same
question is not authorized by this plan.

The lane changes to one question with a hard budget: **at the last tick each of
the 15 victims was on proven static support, did a collision-clear, non-pain,
target-progress-preserving alternative exist?** That is exactly the evidence W2
needs, so it is not a separate workstream — it becomes W2's UT99 positive
control set. If W2 cannot label those 15 deaths without guessing, the water lane
is parked, not widened, and the `SlimeZone0` concentration is recorded as an
unexplained map-local cluster pending H5's Pressure test.

### Addendum — physics-fidelity reframing (2026-07-26, same-day review)

A same-day review of this plan against its own cited evidence changes the
water lane's priority, without contradicting any decision above.

**Route/reachability is now cleared as a mechanism, not merely deprioritized.**
`BOT_AI_CROSS_GAME_REACHSPEC_CAPABILITY_EVIDENCE_2026-07-26.md` and
`BOT_AI_REACHSPEC_NATIVE_COMMIT_CAPABILITY_EVIDENCE_2026-07-26.md` show zero
capability-incompatible committed edges on this exact anchor, and
`BOT_AI_DIRECT_REACH_DIFFERENTIAL_2026-07-26.md` shows zero hazardous or
non-hazard deaths linked to any `ActorReachable`/direct-reach terminal. The
confirmed reach-flag and `bSinglePath` omissions in
`BOT_AI_UE1_MOVEMENT_PATH_DIFFERENTIAL_2026-07-26.md` remain real source gaps
but did not fire on this anchor. No further route-graph or direct-reach
observation is authorized for the water lane; that sub-question is closed.

**The unresolved differential that fits the actual death shape is
`MinHitWall`/falling-collision dispatch, not routing.** Fourteen of fifteen
water deaths are `falling`-to-`falling` transitions with no static support and
heterogeneous entry commands and states (§1.2) — the shared element is not a
decision, it is whatever governs the pawn's trajectory while already airborne.
`BOT_AI_UE1_MOVEMENT_PATH_DIFFERENTIAL_2026-07-26.md` already establishes that
Surreal dispatches the walking `HitWall` script callback on a vertical-normal
band, where retail dispatches it on an explicit `MinHitWall` dot-product
threshold — a confirmed source-level difference, still unproven against a
retail oracle. `Docs/BOT_AI_QUALITY_EXECUTION_PLAN.md`'s prior falling-physics
correction (iteration 51) is direct precedent that this class of change has
large, map-dependent effects (Morpheus wall contacts 330 to 2,283, seam
detections 3 to 433, while Deck16/DeathFan stayed neutral-to-good) — physics
divergence of this kind is plausible and precedented in this codebase, unlike
a routing-graph divergence, which has now been checked and found absent here.

**Reprioritization.** The water lane's next authorized action is not a fourth
bot-decision/route observer. It is resuming the `MinHitWall` retail-oracle
bisection that `BOT_AI_QUALITY_EXECUTION_PLAN.md` already scoped and left
blocked — not falsified, blocked: the finer quarter-step batches never ran
because the disposable retail UCC compiler faulted during driver-cache
startup, before any package compiled or server case executed. The already-
completed half-step brackets (UT436 `[-395000, -394500]`, Unreal Gold
`[-396500, -396000]`) remain valid. This is promoted ahead of W2's positive-
control labeling exercise: a falling-collision-dispatch fidelity result can
change what W2 is even labeling, so it should run first, using the existing
bounded compile guard (`compile-attempt-1.json`, fail-closed on any faulted or
incomplete compile) rather than a new fixture design.

This does not authorize a runtime `MinHitWall` correction. It authorizes
resuming the oracle measurement under the same fail-closed rules as every
other step in this plan: two matching repetitions, byte-identical installed
inventories, and an identified operand before any dispatch predicate changes.

## 5. Fail-closed stop conditions

These are absolute. Each stops the affected work rather than downgrading a
claim.

1. **Dirty or unattested tree.** No capture, A/B, or comparison from a tree
   whose git state and executable SHA-256 are not recorded.
2. **Non-deterministic repeat.** Two same-configuration repetitions that are
   not exactly equivalent, ignoring only audited fields, invalidate the case.
   Same-seed repeats remain determinism checks, never independent samples.
3. **Missing, null, or overflowed metric.** Fails the gate. A metric is added by
   implementing honest telemetry — never by renaming a proxy, never by relaxing
   an analyzer, never by editing a gate file in
   `Tools/BotBenchmark/QualificationCampaigns/`.
4. **Observer neutrality violation.** Any observer that changes the gameplay
   stream is removed, not explained. Neutrality is proven by two-repetition byte
   equivalence plus baseline/candidate equivalence ignoring only the newly added
   `_exact` counters, `_diagnostics` payloads, declared envelope, and
   `output_directory`.
5. **Reconciliation mismatch.** Any exact counter group that fails to partition
   its population — C3's self-damage partition, the splash victim sets, the
   death attribution partition — rejects the artifact.
6. **Unproven operand.** A retail oracle that yields only an empirical bracket
   does not authorize a runtime predicate change. This blocked `MinHitWall`; it
   blocks the iterator correction identically if S1 is inconclusive.
7. **Per-map regression.** A regression on any single map or seed blocks
   promotion regardless of aggregate improvement.
8. **Zero opportunity set.** A candidate with zero activations in a game
   supplies non-perturbation evidence only, never cross-game qualification.
   Iterations 94, 118, and 160 are the precedents.
9. **Temporal association presented as causation.** Any claim joining two
   observations without an exact same-life, same-command, same-scope link is
   rejected. Unknown stays unknown; `direct_self_kills == 0` is not evidence of
   no self-combat damage, and legacy scoreboard attribution is not causal
   evidence.
10. **Cross-map layout reuse.** Comparing against a layout fingerprint observed
    on a different map invalidates the run.
11. **Held-out map opened before tuning freeze.** Immediate stop. `DM-Fractal`,
    `DM-Phobos`, `DmElsinore`, and `DmRadikus` stay closed.
12. **Default-off flag becoming default-on** without full cross-game
    qualification. Every existing experiment flag stays default-off.
13. **Upstream contact.** No PR, external post, or maintainer contact without an
    explicit request from the user, per `CLAUDE.md`. Preparing and auditing a
    candidate does not grant that permission.

## 6. When Pressure, Morpheus, and Unreal Gold belong

**Deck16-II** stays the diagnosis and tuning anchor. It holds the only complete
causal instrumentation set and its layout fingerprint is frozen. Every candidate
is born here. Freeze three observed stock layout fingerprints before any
candidate comparison.

**`DM-Pressure`** is the first UT99 cross-map qualification case. It enters when
a candidate has a completed, deterministic Deck16 A/B — not before, because a
candidate without a Deck16 delta panel has nothing for Pressure to confirm or
contradict. It already has one complete 16-bot/7,200-tick capture at seed
`104729`, making it the cheapest honest second map, and it requires its own
`matrix.map_start_layouts` entry and its own frozen fingerprints. Its first use
for any candidate doubles as the test of H5 — whether hazard deaths concentrate
the same way off Deck16. Pressure is a tuning map, not held-out.

**`DM-Morpheus`** is the low-gravity stress lane and is conditionally required,
not routinely required. It needs a 16-bot/7,200-tick smoke before it can carry
any comparison. It becomes mandatory for any candidate touching falling physics,
air control, projectile flight, splash geometry, or wall/seam handling — which
includes the S2 iterator correction, because low gravity lengthens airborne
explosion opportunities. The precedent is decisive: iteration 51's retail
falling correction was neutral-to-good on Deck and DeathFan but took Morpheus
walls from 330 to 2,283 and seam detections from 3 to 433. A candidate in this
class that skips Morpheus has not been tested.

**`DM-Fractal`** is screened separately whenever it is opened, because warp
zones are a distinct traversal case; it is currently held-out. **`DM-Phobos`**
has no local load, catalog, or match evidence and must not appear in any
qualification until a bounded headless catalog and a 16-bot smoke pass.
**`DM-Morbias][`** remains a cheap, historically clean non-regression check
(iteration 140) but proves nothing about hazards.

**Unreal Gold 226b is the compatibility lane, not a second tuning lane.** Its
obligations are asymmetric with UT99's and should be stated that way:

- Every change to a **shared UE1 contract** must pass an Unreal A/B. Both items
  in the S-lane qualify: `VisibleCollidingActors` is shared native behavior and
  `Actor.HurtRadius` is shared retail script. `DmDeathFan` is the hazard-bearing
  anchor; `DmDeck16` is the quiet control.
- Unreal must demonstrate **either safe activation or proven inertness**, not
  improvement. A zero-opportunity Unreal run is a valid non-regression result
  and an invalid qualification result; it can never substitute for cross-game
  evidence of a behavior candidate.
- Adapter separation is unchanged: `UnrealShare.DeathMatchGame`,
  `UnrealShare.Bots`, `UnrealSpectator`, skills 0–3 with `ReSetSkill`, and the
  exact automatic-roster contract, routed through
  `SurrealEngine/BotBenchmark/BotBenchmarkGameProfile.cpp` and
  `BotControlledMatch.cpp` rather than through UT assumptions.
- Held-out Unreal maps stay closed.

A UT99-only scope decision is permissible for an adapter-scoped change, but only
when stated explicitly, justified by evidence, and gated in the game profile —
never by a game-name predicate inside shared native code. Iteration 155 already
rejected that shape.

## 7. Merge-readiness criteria

Merges stay focused, per `CLAUDE.md` and the execution plan's merge strategy.
Three classes, with distinct bars.

### (A) Shared engine fidelity correction — e.g. the S2 iterator change

1. The defect is confirmed to still exist on current `dpjudas/master`, not only
   on this branch.
2. A concise reproduction exists, with observable before/after behavior.
3. Retail evidence identifies the correct semantics — S1's oracle, two
   repetitions per case, byte-identical installed inventories. An empirical
   bracket without an identified operand does not qualify.
4. Pure fixtures cover the positive case, the negative case, the boundary, and
   the invalid-geometry case, and assert the exact selected set.
5. Paired deterministic A/B on UT436 Deck16, UT436 Morpheus (this change is in
   the conditional class), and Unreal Gold `DmDeathFan` and `DmDeck16`, with no
   regression in the death partition, hazard entries, hazard exposure, combat
   metrics, wall contacts, stall metrics, or navigation coverage on **any**
   single map.
6. Every changed line understood and explainable, with its assumptions stated;
   risks, preserved behavior, and non-goals documented.
7. The PR carries only this correction plus its tests — no planning documents,
   no telemetry slices, no unrelated formatting or generated history.

Behavior-neutrality across all four anchors is an acceptable and expected
outcome for (A). Such a change merges on correctness grounds and must be
described as a fidelity correction, never as a bot improvement.

### (B) Telemetry / observer slice — e.g. S3

1. Default-off, carried through command line, run identity, manifest, summary,
   matrix provenance, analyzer, and `Compare-BotBenchmarkRuns.py`.
2. Observer-on/off byte equivalence in both games, plus two-repetition
   equivalence per configuration.
3. Complete, monotonic counter group with exact reconciliation and explicit
   overflow accounting; analyzer schema version bumped; fail-closed on partial
   groups.
4. Positive coverage in at least one anchor per game, or an explicit recorded
   zero-coverage result — never a silent zero.
5. No claim of behavior improvement, in the commit message or the document.
6. The slice closes as an iteration entry in
   `Docs/BOT_AI_QUALITY_EXECUTION_PLAN.md` with an explicit decision of reject,
   revise, experimental, release-candidate, or merge-ready.

### (C) Bot behavior candidate

All of (A)'s understanding gates, plus:

1. A causally attributable witness: same-life, same-command, same-scope, with a
   certified safe alternative at the decision point. Temporal association is not
   sufficient.
2. A deterministic activating fixture reproducing the exact decision, plus its
   disjoint negative cases.
3. A non-zero opportunity set in both games, **or** an explicit, justified,
   adapter-scoped decision recorded in the game profile.
4. Multi-map, multi-seed paired evidence versus the frozen stock anchor: UT436
   Deck16 plus Pressure at minimum; plus Morpheus if the candidate is in the
   falling/air/collision class; plus the Unreal compatibility pair.
5. Non-inferiority on the full panel — `kills_exact`, `match_score_delta`,
   `damage_dealt_to_other_participants_exact`, damage efficiency, deaths and
   their exact partition, `hazard_entries`, `hazard_exposure_seconds`,
   `hit_wall_events_exact`, movement-intent stall metrics, navigation coverage,
   and `confirmed_pickups_exact` — with no per-map safety regression.
6. The campaign's fail-closed requirements satisfied or explicitly still
   failing, with the gate left failing rather than relaxed.
7. Held-out maps opened only after tuning parameters freeze, and then once.

**No candidate currently meets (C), and no work in this plan is expected to
produce one. Do not describe any result here as a bot-quality improvement until
a candidate has passed measured multi-map evidence.**

## 8. Explicitly not authorized by this plan

- Any `CanFireAtEnemy` override, fire veto, or trace-collision-flag change.
- Any splash guard, `HurtRadius` modification, or `FireWeapon` veto.
- Any water-egress steering, acceleration overlay, planner-handoff promotion,
  route pin, target override, reachspec veto, or `ActorReachable` return-value
  change.
- Any `MinHitWall` dispatch predicate change; the native operand remains
  unidentified and the retail brackets are empirical only.
- Any `PickTarget` predicate behavior change; iteration 159 removed it on
  cross-game evidence and that decision stands.
- Enabling any existing default-off experiment flag by default.
- Opening `DM-Fractal`, `DM-Phobos`, `DmElsinore`, or `DmRadikus`.
- Any upstream PR, external post, or maintainer contact.

## 9. Relationship to the standing workstreams

This plan does not reorder `Docs/BOT_AI_CONTINUATION_PLAN_OPUS5.md` except in
one respect, stated plainly: the self-splash provenance observer is demoted
from lead slice to a bounded rider on the occlusion work, on the grounds of
§3.4's measured 1.28%-of-damage, zero-deaths ceiling. The `HitWall` corner
fixture, the targetless `MoveTo` timeout closure, the `SetEnemy` observer
integration, recovery-clearance timing, in-engine `ai_frame_p95_ms`, and causal
avoidable-suicide classification all remain as sequenced there. The splash
observer's records are a future input to the avoidable-suicide partition, never
a shortcut around it.
