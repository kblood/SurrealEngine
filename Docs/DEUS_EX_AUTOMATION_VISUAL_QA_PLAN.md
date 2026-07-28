# Deus Ex automation and visual QA plan

Date: 2026-07-24

## Objective

Advance Deus Ex support through small runtime corrections, deterministic player
automation, and local screenshot analysis without making model output or
commercial game data part of the engine's correctness boundary.

The product integration branch is the working authority. The preserved
`deus-ex-support` and bot-parity histories are read-only evidence; useful work
must be reconstructed in narrow slices rather than merged wholesale.

## Current baseline

- Deus Ex 1002f demo detection, local browser import, package scanning, text
  tokenization, cardinal movement, property serialization, and pure AI
  perception formulas have automated coverage.
- The demo remains local-import-only. Bounded owner-data runs now verify map
  login, deterministic observation, `wait`, a nearby `walk_to_point`, exact
  actor walking, switch interaction, and routed TechGoggles and Ammo10mm
  acquisitions across two training-map obstacle topologies. They do not
  establish broader gameplay support.
- Live hearing now delegates through the Deus Ex detectable-actor gate to the
  tested pure formula. Sight has bounded scalar, direction, and BSP/blocking-
  mover LOS integration for calls that explicitly disable unresolved light
  sampling. Default sight, light sampling, smell, and the remaining perception
  slices are still incomplete. A tick-zero owner-data diagnostic now proves
  the scalar path and a fully blocked three-trace LOS path without claiming
  the missing slices.
- The experimental bot policies run in shadow mode. They do not control live
  bots and do not yet observe items, armor, or reliable route progress.
- Earlier Training work used background window messages. It proved ordinary
  movement and the stock right-click frob path, but it is not a deterministic
  engine-owned scenario runner.

## Implementation checkpoint

The automation foundations are now present in the working unified tree:

- live Deus Ex hearing delegates through the detectable-actor gate to the
  tested pure formula;
- the live sight slice delegates detectable targets through eye-relative
  distance, a separately tested optional FOV/aspect/cylinder direction gate,
  the pure apparent-size/threshold formula, and a bounded primary/top/bottom
  LOS sequence. Callers must explicitly disable unresolved light sampling, so
  default calls continue to fail closed;
- the automation protocol validates bounded commands, canonical observation
  snapshots with stable per-run actor identities, lifecycle results, digests,
  and telemetry; and
- the first player movement controller produces deterministic forward/turn
  intents, arrival, deadline, and stuck outcomes, then publishes them through
  conventional UE1 axes with independent synthetic controls. Large heading
  errors turn in place, and the automation-only turn scale of 4096 produces a
  bounded 172.8 degrees/second in the stock UE1 walking formula without
  changing desktop or XR bindings.

An opt-in `player-automation` headless driver now connects the controllers to
the actual viewport pawn in Deus Ex. It supports bounded `walk_to_point`,
`walk_to_actor`, `wait`, `acquire_item`, and `interact` configurations, uses
fixed-step simulation, rejects non-Deus Ex launches, fails if the player
identity changes, and writes a manifest, canonical initial observation, JSONL
lifecycle telemetry, terminal observation, and terminal result. It applies and
releases only its own composed synthetic controls. An optional
`--automation-abort-at-tick=N` schedule adds a
second exact `abort` command and digest to the manifest, validates its one-tick
lease, and cancels only the named active command.

For merged-pickup characterization, `acquire_item` accepts one optional
`--automation-followup-target-identity`. This is not a general command queue:
it is limited to one distinct second target, inherits the primary exact class
and arrival radius, and cannot be combined with a scheduled abort. The second
ordinary `acquire_item` command is materialized only after the first succeeds,
using the current tick and a fresh observation revision. The driver releases
its synthetic controls, resets all per-command navigation state, records a
full `followup-observation.json`, writes the exact materialized command and
digest, and preserves both terminal results in a versioned sequence summary.

The same driver exposes a diagnostic `--automation-action=sight_probe` run. It
reuses the exact identity/class selector fields but remains an internal
one-tick `wait` command rather than expanding the gameplay command protocol.
After map login it resolves directly from the complete stable-ID table, writes
a one-target observation without navigation/reachability queries, and calls
`AICanSee` four times at tick 0 with visibility fixed to 1: scalar-only,
direction-gated, primary LOS, and cylinder-fallback LOS. Light sampling is
disabled in all four calls. It then completes without advancing the level or
applying synthetic input. Its v2 run manifest binds the selector and fixed call
contract; `sight-probe.json` uses `surreal-deus-ex-sight-probe-v2` and records
the live observer/target inputs, primary/top/bottom endpoints and outcomes,
trace counts, scalar/direction/LOS results, collision contract, authority
boundary, and a canonical digest referenced by telemetry. `FastTrace` changes
collision bookkeeping even though it does not mutate gameplay properties, so
the manifest and artifact disclose that distinction explicitly.

`walk_to_actor` resolves one exact, reachable actor from observation revision
1 and fixes the movement destination to that snapshot location. It does not
silently chase a moving actor. The live actor identity must remain valid for the
command lifetime, progress telemetry retains the target identity, and success
means only that the configured arrival envelope was reached; it does not imply
interaction or state change.

Actor actions bind one exact observation selector, approach through ordinary
axes, wait for that exact actor to become the live Deus Ex `FrobTarget`, send
one right-mouse event through the stock `ParseRightClick` binding, and require
an ownership, tombstone/inventory, target-state, or correlated event-receiver
transition. Headless execution explicitly runs the pawn's stock
`HighlightCenterObject` script at the observation boundary because the normal
HUD/render path is absent; target selection is still performed by Deus Ex and
the driver never assigns `FrobTarget`. A bounded aim controller aligns the
camera through ordinary turn/look axes before interaction. Event evidence is
snapshotted only when the stock input is requested and is limited to actors
whose `Tag` exactly matches the target's non-empty `Event`; movement smaller
than 0.001 units is ignored as numerical jitter. They never call
`Touch`, change ownership directly, teleport the pawn, or accept arbitrary
console commands. The pure action controller covers single-attempt interaction,
stale observations, acquisition proof, target and event-receiver proof,
unrelated-motion rejection, sub-threshold jitter, missing-target failure,
wait, and abort.

Resource-aware observation v2 adds one bounded, allowlisted inventory resource
to each target when present: Deus Ex `AmmoAmount` as `ammo_amount`, or
`NumCopies` as `num_copies`, each capped at 1,000,000. This closes the controller
gap where a second stock pickup is destroyed after merging into an already-owned
stack and the owned actor count does not increase. Acquisition now accepts that
case only when the exact target has a tombstone and the aggregate resource for
owned actors of the exact same class and resource kind increases by exactly
the target's recorded amount. Resource-bearing tombstones cannot fall back to
same-class actor count; that legacy proof is reserved for targets without a
recorded resource. Data-free controller tests reject replacement actors with
the wrong amount, substituted kind, or missing resource even when owned class
count rises, and accept exact `ammo_amount` transitions from 20 to 30 and from
0 to 10. Historical visual-QA observation v1 evidence remains accepted; new
engine observations use v2.
Live abort covers wait-only, movement-only, and combined actor-action plus
movement ownership. It records the original command as `cancelled`, records the
abort command as `succeeded`, and releases automation input before termination.

Route research found that the tested Training Combat and TrainingFinal pickups
have no complete usable path from the current `FindPathToward` implementation.
A pure deterministic reach-spec planner now validates bounded graphs of at most
4096 nodes and 16384 links, filters pruned and undersized-clearance edges, uses
stable identity tie-breaking, and rejects malformed, ambiguous, or disconnected
requests. The live adapter prefers an inventory item's exact `myMarker` /
`InventorySpot`, then evaluates a bounded set of nearby generic endpoints. A
second selector can use up to 64 directly reachable, unvisited world actors
that make target progress, with deterministic score and identity tie-breaking.

Earlier bounded failures in TrainingFinal and TrainingCombat identified four
separate gaps rather than one generic pathfinding failure: overly broad
vertical waypoint rejection, vertically stacked candidates, movers whose
pre-transition location becomes the passage, and non-standable actor markers
whose horizontal position is still useful route progress. Each correction is
bounded and retains stable identity tie-breaking.

The obstacle interaction is proven both as its own bounded command and as a
sub-state of acquisition. Starting from the same TrainingFinal login, ordinary
movement reaches the closest legal position around `Switch1`, ordinary look
axes align it, the stock selector binds the exact switch, and one stock
right-click at tick 248 starts `DeusExMover0` (tag `FirstStealthDoor`) moving.
The correlated mover transition completes the standalone command at tick 249.

The acquisition adapter snapshots that mover's pre-transition location and
uses it as one bounded, ordinary-input passage segment after the door moves.
This avoids aiming directly through the newly opened corner. Spatial fallback
now rejects implausibly steep candidates while allowing bounded gentle climbs,
and it prefers a lower waypoint when vertically stacked actors share the same
arrival footprint. After the passage, the route reaches `Light8`, then obtains
a direct target path. Final actor staging derives a conservative radius from
the pawn eye height and 90 percent of Deus Ex's stock `MaxFrobDistance`; exact
stock target selection and an ownership transition still decide success.

The resulting TechGoggles command presses Switch1 at tick 248, traverses the
vacated passage, presses the exact goggles at tick 470, and succeeds at tick
471 when their owner becomes the exact viewport player. This proves one stock
pickup path, not general inventory, every obstacle topology, or campaign-wide
navigation.

TrainingCombat provides a second topology. The route first approaches
`DeusExMover26`; when movement blocks while the exact stock `DataLinkPlay`
actor is in `WaitForSpeech`, the driver advances ordinary world ticks for at
most 600 ticks and never changes the speech actor, mover, trigger, or flag.
Speech clears at tick 610. The retained mover-waypoint baseline then observes
the two `combatentry` movers transition, and a tight ordinary-input segment
crosses the selected mover's pre-transition location from ticks 762 through
864. A ceiling-height `Light2` marker is accepted only after the player reaches
its horizontal footprint inside a bounded 128-unit vertical envelope; routing
then continues through `Plant1` to the exact item. The stock right-click occurs
at tick 1430 and Ammo10mm ownership transfers at tick 1431. An abort scheduled
during the DataLink wait cancels the acquisition exactly at tick 300, proving
that the recovery does not swallow command cancellation.

The earlier apparent navigation stall also exposed a headless lifecycle defect.
The first footstep sound dereferenced an uninitialized audio backend because
headless drivers run synchronously before presentation-device setup. Minidump
resolution identified access violation `0xC0000005` at `USound::GetSound`,
`USound.cpp:66`. Headless setup now installs the existing no-op
`NullAudioDevice`; the identical owner-data path crosses the former crash tick,
reaches tick 250, and accepts its scheduled abort. Desktop setup still opens
the normal audio backend.

An owned-data smoke run can be invoked in a scratch build with arguments of
this form (coordinates must come from a fixture manifest):

```text
SurrealEngine.exe --autoplay --headless-driver=player-automation \
  --automation-action=walk_to_point \
  --automation-url=<map> --automation-target-x=<x> \
  --automation-target-y=<y> --automation-target-z=<z> \
  --automation-output=<dated-qa-run-directory> <Deus-Ex-folder>
```

An actor walk replaces the three coordinates with
`--automation-action=walk_to_actor`, `--automation-target-identity=<id>`, and
`--automation-target-class=<class>`. The selector must come from the saved
initial observation and must be marked reachable there.

A bounded same-class acquisition pair adds
`--automation-followup-target-identity=<second-id>` to an exact
`--automation-action=acquire_item` run. The second identity must also come from
the saved initial observation; its class and arrival radius are inherited.
This mode permits exactly two commands, shares one seed/fixed timestep/global
tick lease, and rejects scheduled abort configuration.

A bounded sight diagnostic uses `--automation-action=sight_probe` with the
same exact identity/class fields and `--automation-ticks=1`. It does not accept
coordinates, arrival/wait fields, or a scheduled abort. It changes no gameplay
properties and advances no level tick, but LOS calls do change collision check
bookkeeping. A positive scalar result exercises the detectable/apparent-size
path; direction and LOS results may legitimately be zero for an off-axis or
occluded initial target.

Optional bounds are `--automation-arrival-radius`, `--automation-seed`,
`--automation-ticks`, `--automation-fixed-delta`, and
`--automation-abort-at-tick`. The abort tick must be nonzero and earlier than
the run limit. The output directory is not source-controlled, and the command
does not capture or redistribute game content.

The local visual-QA sidecar is also implemented under `tools/visual-qa`. It
accepts only a loopback Ollama endpoint, verifies immutable capture hashes and
relative paths, resolves the exact installed model digest twice around the
capability check, requires advertised vision support, and uses schema-
constrained, temperature-zero chat requests without tools. Reports retain
prompt/schema/profile/manifest/image hashes and the raw Ollama response while
declaring `controls_live_player=false`.

Capture-manifest v2 prevents declared cross-run screenshot/observation
substitution before positive shadow evaluation, but the declaration alone does
not prove that a screenshot came from the named engine process. Its exact
session binding records the
session ID, source revision, production-binary hash, and configuration hash;
each capture binds its command ID and canonical observation hash, revision,
and matching tick. Replay-fixture v2 repeats that grounding, and generation,
replay, and labelled evaluation reject substituted session, command, binary,
configuration, observation hash/revision/tick, duplicate keys, or extra v2
fields. Fresh opt-in native automation now adds engine-owned D3D11/Vulkan
readback, a published-last runtime receipt, and an offline binder that validates
the completed terminal command boundary before materializing v2.

The first pre-effect slice is implemented for an exact standalone `interact`.
After ordinary movement and aiming are released, but before
`interaction_attempt` telemetry or the stock right-mouse press, the engine
renders the viewport and canonical observation. Receipt v2 binds stable
process-lifetime synthetic-input counters across render, zero prior synthetic
interaction presses, and the exact `visual_capture_published` telemetry
sequence. The binder requires that event to be immediately followed at the
same tick/revision by the exact target's `interaction_attempt`, then by a
successful terminal result. It emits capture-manifest v3 with hashes of the
receipt, complete events ledger, summary, and binder revision. The sidecar
revalidates those artifacts and admits a positive proposal only when its action
and target exactly equal that runtime binding; the derived candidate remains
non-dispatchable. Historical v1 and v2 evidence remains usable for advisory
review and `none` shadow cases, but cannot authorize a fresh positive proposal.
This is trusted-local-process evidence rather than signed attestation.

The exact actor-walk slice now uses the existing tick-zero `pre_action`
capture. Receipt v2 proves zero synthetic requests before and during render.
Before the adjacent tick-one `command_progress` event is emitted, the runtime
requires the process counter to increase by exactly three requests from the
ordinary forward, strafe, and turn adapter, with no interaction press. The
binder and sidecar require that exact event at tick 1/revision 2, the exact
reachable target, and a successful terminal result. Generic `pre_action` is
restricted to `walk_to_actor`; acquisition cannot borrow this weaker proof.

The acquisition slice now has a distinct `pre_pickup` barrier. It is reached
only when the top-level exact target requests its final stock interaction, so an
earlier route-opening switch press is permitted but an earlier attempt against
the same pickup is rejected. Receipt v3/barrier v2 records stable process
counters across render. The binder requires the adjacent exact-target
`interaction_attempt`, then an immediately adjacent successful result and
canonical effect observation proving the same TechGoggles instance changed
from unowned `Pickup`, `num_copies=1`, interactable/acquirable to exact-player
owned `Idle2`, the same resource amount, and non-interactable/non-acquirable.
Manifest v4/grounding v3 propagate those hashes through direct positive shadow
generation, replay-fixture/result v4/v3, and evaluation dataset/result v3;
every candidate remains non-dispatchable.

Advisory mode remains the default. An opt-in shadow mode accepts a hash-bound
engine observation plus caller-supplied action and exact-target allowlists. Its
strict local validator rejects unknown targets, capability mismatches,
disallowed actions, and out-of-bound waits. A valid suggestion is converted to
a locally derived candidate with `dispatch_authorized=false`; the sidecar has
no command-dispatch path. The model cannot supply raw coordinates, target
classes, arrival radii, input streams, attack, console, shell, or UI actions.

Thirty-six Python tests across the sidecar/replay, capture binder, and
labelled-evaluation layers
cover path traversal, image substitution, remote endpoints, capture/model
identity substitution, unknown or duplicate assertions, missing findings,
non-finite confidence, shadow target binding, action capabilities, action
allowlists, wait bounds, replay provenance and candidate substitution, curated
proposal mismatch, dual-label/adjudication substitution, complete attempt
accounting, semantic repeatability, deterministic scoring, same-session
grounding substitution, legacy-v1 `none` compatibility, positive-proposal v3
action/target enforcement, rejection of positive v2 declarations, runtime
receipt/input-barrier/config/terminal-ledger binding, exact walk first-effect
adjacency, replay-fixture v3 authority derivation, evaluation-dataset/result v2,
exact acquisition effect binding, replay-fixture/result v4/v3, and evaluation
dataset/result v3 propagation,
artifact-specific size limits, and non-dispatchability. The sidecar also preserves
bounded HTTP error bodies, which exposed an Ollama grammar failure; the
generation schema was simplified while strict post-response validation
remains unchanged.

`qwen3.5:9b` and `qwen3.6:27b` first completed validated advisory inference on
the same immutable existing WebGL2 gameplay smoke frame under Ollama 0.32.3.
They later analyzed the same immutable 2560x1440 Deus Ex TrainingFinal startup
capture, SHA-256
`30762a430ac22f830ae8a681f135bfbb6d123af55d10e6c8a5144f90ba85c8bd`.
Both returned schema-valid passes for coherent scene rendering, absence of a
blocking error, and HUD legibility with `controls_live_player=false`; measured
model time was about 11.5 s and 22.5 s respectively. This is a pipeline and
single-frame smoke result, not a human-labelled model-quality evaluation or
authorization for model-assisted demo control.

Both installed Qwen models also completed the opt-in shadow request against
the hash-bound TrainingFinal observation for exact reachable `Light155`, with
only `walk_to_actor` and `wait` allowed. Both conservatively proposed `none`.
Their schema-valid reports produced no shadow candidate and retained
`dispatch_authorized=false` and `controls_live_player=false`.

The first curated replay slice is implemented in `shadow_replay.py`. A
versioned fixture binds the exact screenshot hash, raw observation hash,
allowlisted actions and targets, wait bound, and one or more acceptable
proposal triples. Replay revalidates the report policy and proposal, derives
the candidate again locally, and rejects substituted provenance or candidates.
It emits a deterministic pass/fail evaluation with no Ollama, engine, or
dispatch access. This evaluates a proposal against an oracle; it does not yet
execute a candidate in a deterministic engine fixture.

The native `ShadowReplayAdapter` is the implemented non-dispatchable boundary. It
projects a curated candidate into the canonical automation command structure
at the exact observation tick, but has no driver, CLI, or JSON-loading hook.
It rejects candidates that claim dispatch authority, stale observations,
unsupported kinds, caller-selected radii, waits over 600 ticks, missing class
bindings, invalid leases, and targets that fail the existing class,
reachability, interactability, or acquirability resolver. Native tests cover
all four shadow action kinds and pin a canonical command digest.

A test-only deterministic `ShadowReplayFixture` now executes that projection
through the production movement controller, movement input adapter, input
composition, and interaction action controller. Its bounded kinematic world
advances only from the composed conventional axes. For acquisition and
interaction, it changes the next observation only after the controller emits
its single stock-interaction request; the production controller still requires
the resulting exact proof transition before success. Explicit test-only modes
now reproduce both owner-data-observed Deus Ex shapes: the pickup target becomes
a tombstone while the sole exact-class/exact-resource owned stack increases by
the target's recorded amount (synthetic 6 to 12), or a separate receiver whose
`Tag` exactly matches the target's non-empty `Event` moves by more than the
0.001-unit threshold while the target stays unchanged. A tombstone without the
resource delta, unrelated receiver motion, and correlated 0.0005-unit jitter
all time out. Missing, ambiguous, cross-command, overflow, and unknown-mode
configurations fail before telemetry; abort on the transition tick leaves the
world unmutated. Repeated transition telemetry, results, and final observations
are byte-identical, and every terminal path releases composed input. The
fixture never calls `Touch`, mutates a controller, loads model JSON, or connects
to a live viewport. This closes deterministic controller-fixture replay, but
not live-engine or owner-data replay dispatch.

A strict test-only loader now accepts validated
`surreal-visual-qa-shadow-replay-result-v1` text only when its exact fixture,
capture, fixture/observation/report hashes, model tag, and model digest match a
trusted binding. It rejects oversized, malformed, comment-bearing,
duplicate-key, over-nested, missing-field, extra-field, non-canonical,
provenance-substituted, unsupported-action, inconsistent-candidate, and every
authorization-claiming input. Valid persisted Qwen 3.5 and Qwen 3.6 `none`
shapes return a distinct no-action status. A synthetic accepted positive walk
candidate passes through the existing adapter and deterministic fixture twice
with byte-identical telemetry/results and released input. The loader is absent
from production source lists, CLI parsing, file I/O, Ollama, and the live
viewport, so this closes strict offline artifact-to-fixture replay only.

A pure offline `shadow_eval.py` scorer and a failure-preserving attempt receipt
are now implemented. Fresh reports and receipts bind the exact generator-script
hash. A labelled dataset binds capture/image, profile, observation, replay
fixture, two distinct human labels, and final adjudication; the final canonical
proposals must exactly equal the existing fixture oracle and use one action
family. Evaluation-dataset v2 requires a positive case to use
replay-fixture v3 and capture-manifest v3. Replay reloads the exact hashed
manifest and transitively revalidates the receipt, telemetry, terminal result,
and current audited binder before deriving grounding v2; it does not trust
copied fixture grounding. Evaluation-result v2 records this authority per case
and attempt. Historical dataset/fixture v1/v2 metrics remain readable but do
not admit fresh positive generation. The run ledger requires every
case/model/repeat slot. Deterministic
metrics cover schema validity, exact oracle accuracy, five-action confusion,
precision/recall/F1, unsafe commission, over-abstention, target/wait exactness,
semantic repeatability, pairwise agreement, and integer latency percentiles.
The scorer has no HTTP, engine, adapter, driver, or dispatch path.

Evaluation dataset/result v3 applies the same strict pipeline to acquisition:
it requires replay-fixture v4 and capture-manifest v4, carries
`pre_effect_bound`, and records both the pre-effect binding and effect
observation hashes per case and attempt.

The next visual-QA slice is the actual two-reviewer corpus:
three canonical,
confusable, and adversarial cases for each of `none`, `walk_to_actor`,
`acquire_item`, `interact`, and `wait`, followed by repeated Qwen runs. The
current single owner-data `none` case is only a provenance/repeatability
baseline. Explicitly armed live demo control remains a later, separately gated
step.

## Owner-data evidence checkpoint

Current source authority is `integration/unified-engine` at
`0f2e29c982beb926f1c00a5d71d140f06a5d2099` with an intentionally dirty
working tree. The current validated RelWithDebInfo scratch executable SHA-256 is
`eda4ce4fb1750779fa0ece78ee17ff11aafeb46308629df9827e8e094346084f`.

- `00_TrainingFinal` engine-owned presented wait capture:
  `deus-ex-trainingfinal-presented-wait-capture-v2-a` and `-b` use D3D11
  synchronous render-device readback at terminal tick 1 / observation revision
  2. The 2560x1440 PNG and canonical observation are byte-identical across the
  pair with SHA-256
  `2821348a0820129ff30cb639ab440a5400ebba73d388463bcdea2f27d8f53ee8`
  and
  `033d2b33e7880701b85a8c35f5366f4a851de27633431c540a4f46a43d024c67`.
  Human inspection confirms the TrainingFinal COVERT corridor and HUD with no
  desktop contamination. Each run has a published-last runtime receipt, a
  binder-materialized v2 capture manifest, and a `surreal-qa-run-v1` record.
  No Ollama inference was performed, and this proves only the terminal `wait`
  visual-grounding path, not a positive action or control path.

- `00_TrainingFinal` pre-stock Switch1 capture:
  `deus-ex-trainingfinal-switch1-pre-stock-capture-v1-a` and `-b` use the exact
  target `actor:DeusEx.Switch1:Switch1#0`, radius 16, seed 104729, fixed delta
  0.02, and D3D11 readback. Both publish the barrier at tick 248 / observation
  revision 249 as telemetry sequence 253 with zero synthetic interaction
  presses before and after render. Sequence 254 is the exact stock interaction
  attempt at the same boundary; the correlated `FirstStealthDoor` mover
  transition succeeds at tick 249 / revision 250. The 2560x1440 PNG SHA-256
  `a69349e32d8d3d3aa3e0c18e8b3b4f89ef0cbe1844125cf7adfb6670bb5cb3f3`,
  capture observation SHA-256
  `af9a5a6c1e80a74c983cb8faf949b7281188d07e48bf9c2956ee7bd74e75e48b`,
  terminal observation SHA-256
  `00df18f3052d1233f7f9ed71c0911fbfdc98a9745ededd5e60b4e7793861b831`,
  and summary SHA-256
  `6c89f58a6ec0bfc1eeae1b00be15e9c5961b5c67bd06f148e742c21b3f9ae282`
  are byte-identical across the pair. Human inspection confirms the highlighted
  wall switch, HUD/datalink, and no desktop contamination. The binder emits
  manifest v3 for each run. No Ollama inference was performed and no dispatch
  path was enabled.

- `00_TrainingFinal` pre-action Light155 walk capture:
  `deus-ex-trainingfinal-light155-pre-action-capture-v1-a` and `-b` capture the
  exact reachable `actor:Engine.Light:Light155#0` at tick 0/revision 1 with
  synthetic request counters 0-to-0 and zero interaction presses. Barrier
  sequence 4 is immediately followed by the counter-checked ordinary-axis
  progress event at tick 1/revision 2. Both runs retain the established success
  at tick 53/revision 54. Their 2560x1440 PNG SHA-256
  `2821348a0820129ff30cb639ab440a5400ebba73d388463bcdea2f27d8f53ee8`,
  capture observation SHA-256
  `2cfcad0de852a89cc1d0270b9fce0094b8a0a1b4213ee03bda0956db03596b5b`,
  final observation SHA-256
  `e20d9f574e7169d768f1cff25e1a95766adcac560e6966f6e671326a18f693a4`,
  and summary SHA-256
  `a77749f0ec54e216bae8a7eb85128f729a60e4770bb6da64dc8232586ef7c8a1`
  are byte-identical across the pair. Human inspection confirms the COVERT
  corridor and HUD with no desktop contamination. Each binder output is
  manifest v3. No model inference or dispatch was performed.

- `00_TrainingFinal` pre-pickup TechGoggles capture:
  `deus-ex-trainingfinal-techgoggles-pre-pickup-capture-v1-a` and `-b`
  capture exact target `actor:DeusEx.TechGoggles:TechGoggles0#0` at tick
  470/revision 471 after one permitted earlier Switch1 press. Counter values
  remain 1366-to-1366 and interaction presses 1-to-1 across render. Barrier
  sequence 478 is immediately followed by the exact stock pickup attempt at
  sequence 479 and ownership-transfer success at tick 471/revision 472,
  sequence 480. The pre-effect target is unowned `Pickup`, interactable and
  acquirable with `num_copies=1`; the final observation binds the same identity
  as exact-player owned `Idle2`, non-interactable/non-acquirable with the same
  resource. The pair has byte-identical 2560x1440 PNG SHA-256
  `b47b4db3471c44e85ccb609619334474473391a47fe98fb5488aa4fe08ea7e2c`,
  capture observation SHA-256
  `13f4544b48d12f9d28a15175f377dfdffabf8827cfda1c2ed505ad677b364b3e`,
  final observation SHA-256
  `e6be0f39d6d0a0368a937c1900364eb56233b730f6fd948bc5dff22cdb485863`,
  and summary SHA-256
  `1164fcf80624860bf2f69d1719980aa5372781863d6d75671de4fc3ee86d54cf`.
  Human inspection confirms the centered goggles under stock targeting brackets,
  HUD/datalink, and no desktop contamination. Each binder output is manifest
  v4. No model inference or dispatch was performed.

- `00_Training` nearby walk: target `(-1050, 825.843933, -65.103493)`,
  radius 16, seed 104729, fixed delta 1/60. It succeeds at tick 39 from
  `(-1149.243530, 825.843933, -65.103493)` and ends at
  `(-1061.713135, 825.843933, -84.000008)`.
- `00_TrainingFinal` exact actor walk: reachable snapshot target
  `actor:Engine.Light:Light155#0`, radius 40, seed 104729, fixed delta 0.02.
  Runs `deus-ex-trainingfinal-walk-to-light155-a` and `-b` both succeed at tick
  53 and have byte-identical event, result, initial-observation, and
  final-observation artifacts. Event SHA-256 is
  `4efb1cbf54fbf0c7734897ab16bc63f0846063da45f6ef3584ac5d150a87db30`.
  The final adapter-enabled binary repeats the tick-53 success in
  `deus-ex-trainingfinal-walk-to-light155-shadow-replay-adapter-final`; all
  four artifacts remain byte-identical to the earlier pair.
  The partial-sight binary repeats it again in
  `deus-ex-trainingfinal-walk-to-light155-sight-direction-final` with the same
  four hashes.
  The sight-probe binary repeats it in
  `deus-ex-trainingfinal-walk-to-light155-sight-probe-final`; its event,
  result, initial-observation, and final-observation files are again
  byte-identical.
  The final LOS binary repeats the exact `0.02`-second contract twice in
  `deus-ex-trainingfinal-walk-to-light155-los-v2-fixed002-final-a` and `-b`.
  Both still succeed at tick 53 and all four artifacts are byte-identical to
  the earlier baseline. A separate 1/60-second diagnostic succeeds at tick 63;
  its different tick count is cadence evidence, not an LOS regression.
  The current resource-aware binary repeats the `0.02` contract in
  `deus-ex-trainingfinal-walk-to-light155-resource-v2-final-a` and `-b`.
  Tick 53, event SHA-256
  `4efb1cbf54fbf0c7734897ab16bc63f0846063da45f6ef3584ac5d150a87db30`,
  and summary bytes remain unchanged; the pair's schema-v2 initial/final
  observation SHA-256 values are
  `2cfcad0de852a89cc1d0270b9fce0094b8a0a1b4213ee03bda0956db03596b5b`
  and `e20d9f574e7169d768f1cff25e1a95766adcac560e6966f6e671326a18f693a4`.
  A scheduled abort cancels the active actor walk at tick 10 and records the
  abort as succeeded after synthetic input release. Selecting unreachable
  `Light2` is rejected at tick 0 from the initial observation.
- `00_Training` wait: five ticks, seed 104729, fixed delta 1/60. It succeeds
  exactly at tick 5; repeated event, result, and observation files are
  byte-identical.
- Scheduled live abort is proven for a Training wait at tick 3 and active
  Training movement at tick 5. TrainingFinal `acquire_item` is cancelled at
  tick 4 after both its action and movement controllers are active. Each run
  emits a distinct accepted/succeeded abort lifecycle and a cancelled original
  result. Repeating the combined acquisition abort produces byte-identical
  event, observation, and summary files.
- Initial Training observations contain 247 bounded targets, no duplicate
  identities, and a maximum identity length of 71 bytes. Initial and repeated
  snapshot SHA-256 is
  `3b0491d379ac05318597f91f138c8e33f628ee1e2fd1d9cef5a4ce429cd5ac34`.
- Earlier `00_TrainingCombat` Ammo10mm and `00_TrainingFinal` TechGoggles
  probes preserve the progression from target binding and route diagnostics to
  clean bounded failures. They remain useful negative evidence for the former
  headless-audio crash, absolute vertical filter, unopened corner,
  mover-arrival envelope, overhead marker, and collision-only pickup envelope.
- `00_TrainingFinal` exact `Switch1` interaction is proven in
  `deus-ex-trainingfinal-switch1-stock-success-epsilon`: arrival at tick 228,
  stock exact-target right-click at tick 248, and success at tick 249 after the
  correlated `DeusExMover0` begins moving. The switch remains in `Active`; the
  proof is the event receiver tagged `FirstStealthDoor`, not proximity or an
  assumed successful pulse.
  The final adapter-enabled binary repeats this result in
  `deus-ex-trainingfinal-switch1-shadow-replay-adapter-final`; its event,
  result, initial observation, and final observation are byte-identical to the
  earlier run.
  The partial-sight binary repeats it in
  `deus-ex-trainingfinal-switch1-sight-direction-final`, again byte-identical.
  The sight-probe binary repeats it in
  `deus-ex-trainingfinal-switch1-sight-probe-final`, preserving the same
  four hashes and tick-249 success.
  The final LOS binary repeats the exact `0.02`-second contract twice in
  `deus-ex-trainingfinal-switch1-los-v2-fixed002-final-a` and `-b`; both retain
  the tick-249 success and the same four artifact hashes.
  The current resource-aware binary repeats it in
  `deus-ex-trainingfinal-switch1-resource-v2-final-a` and `-b`. Tick 249,
  event SHA-256
  `6d4f45466af617dee141c575c85304d86b4d8273a05aeffb9e1b633c96dcef38`,
  and summary bytes remain unchanged; schema-v2 initial/final observation
  SHA-256 values are
  `2cfcad0de852a89cc1d0270b9fce0094b8a0a1b4213ee03bda0956db03596b5b`
  and `00df18f3052d1233f7f9ed71c0911fbfdc98a9745ededd5e60b4e7793861b831`.
- `00_TrainingFinal` now has owner-data evidence for the supported scalar,
  direction, and LOS subset. Runs
  `deus-ex-trainingfinal-soldier0-sight-los-v2-final-a` and `-b`
  resolve exact target `actor:DeusEx.Soldier:Soldier0#0` at tick 0. The target
  is live and `bDetectable=true`; collision radius/height are 20/43, the player
  FOV is 75 degrees, minimum angular size is `0.000019039`, and the scalar result
  is `0.083278768`. The direction-gated result is `0.0` because the initial view
  is outside that FOV. The primary target-pawn endpoint uses its eye height and
  is blocked; cylinder fallback then tests the full-height top and bottom, both
  blocked. Non-cylinder LOS consumes one trace, cylinder LOS consumes three,
  and both results are `0.0`. Events, initial/final observation, probe artifact,
  and summary are byte-identical across runs. The v2 probe-artifact SHA-256 is
  `fff326a0e41305cf4e005d0580060ad10355aed86f0c9ae2d4bf22ad1793484c`;
  the manifest differs only by its required output-directory provenance.
- `00_TrainingFinal` exact TechGoggles acquisition is proven twice with
  resource-aware observation v2 in
  `deus-ex-trainingfinal-techgoggles-resource-v2-final-a` and `-b`.
  Events, result, initial observation, and final observation are byte-identical
  across runs. Both click the switch at tick 248, click the goggles at tick
  470, and succeed at tick 471 with exact ownership transfer. The target records
  `num_copies=1` before and after transfer. Event SHA-256 remains
  `4f9722038e9eb27648782dc166d10719a5aacf3c6125c2310905cc859c85261c`;
  v2 initial/final observation SHA-256 values are
  `2cfcad0de852a89cc1d0270b9fce0094b8a0a1b4213ee03bda0956db03596b5b`
  and `e6be0f39d6d0a0368a937c1900364eb56233b730f6fd948bc5dff22cdb485863`.
- `00_TrainingCombat` exact Ammo10mm acquisition is proven twice with
  resource-aware observation v2 in
  `deus-ex-trainingcombat-ammo10mm-resource-v2-final-a` and `-b`. Both succeed
  at tick 1431 and record `ammo_amount=6` on the target before and after its
  first ownership transfer. Events, result, and both observations are
  byte-identical across the pair. Event SHA-256 remains
  `1c5296de4a2d4836d78815dc349be64db4ec35ab208ce9315396be2f607143f2`;
  v2 initial/final observation SHA-256 values are
  `4147117e61185f2398a6a919d36314ef66ebbce84da185654c604b537de3b134`
  and `3ea853c6c2c3b9d3afed857aacdba97a3fa73f88b30d9c25427130ff397f3226`.
  These live runs validate resource capture and preserve the original route and
  ownership proof.
- The same-session stock merge is now proven in repeated runs
  `deus-ex-trainingcombat-ammo10mm-sequence-v1-a` and `-b`. Both use seed
  104729, fixed delta 0.02, a 4000-tick global lease, and exact Ammo10mm0 then
  Ammo10mm1 selectors. Command 1 succeeds at tick 1431 by ownership transfer;
  the fresh follow-up observation records the sole owned Ammo10mm actor at
  `ammo_amount=6`. Command 2 succeeds at tick 1558 only after Ammo10mm1 becomes
  a tombstone and Ammo10mm0 increases exactly from 6 to 12. Ammo10mm2-4 remain
  unchanged and unowned. Telemetry records exactly one stock right-click per
  target, at ticks 1430 and 1557. Events, initial/follow-up/final observations,
  materialized follow-up command, terminal summary, and sequence summary are
  byte-identical across the pair. Their SHA-256 values are respectively
  `9f649f95d0b215ce88c0b9866af3e9ba759f4a29ad3dbb186d2bc7454873b66d`,
  `4147117e61185f2398a6a919d36314ef66ebbce84da185654c604b537de3b134`,
  `3ea853c6c2c3b9d3afed857aacdba97a3fa73f88b30d9c25427130ff397f3226`,
  `2511f0ad08738153c88e656843ed02710c302de98ab4ba341185bf5ca69a2fee`,
  `323bc8670ed11d7378dd51bed74fd09491eff1f122e8070a60218480da9bb8b1`,
  `5db87b190572774ee6550de4f32297dc9074dec84ad766ec457216179ff1cc18`,
  and `ca46e2126a944168d3bb6564f7c14f852e8d83c06dc98c6adb7e97e7053dea79`.
  Each directory has a separate `surreal-qa-run-v1` record binding the dirty
  source HEAD, RelWithDebInfo executable hash, owner-data boundary, parameters,
  result, and artifact hashes; automation manifests differ only where their
  output-directory provenance requires it.
- After closing the resource-bearing class-count fallback, the rebuilt binary
  repeats that same sequence in
  `deus-ex-trainingcombat-ammo10mm-resource-proof-order-final-a` and `-b`.
  Both retain ownership success at tick 1431 and exact tombstone/resource
  success at tick 1558. Their events, initial/follow-up/final observations,
  materialized command, terminal result, and sequence summary are byte-identical
  across the pair and retain every behavioral SHA-256 listed above. Each new
  `surreal-qa-run-v1` record binds executable SHA-256
  `23f5c2b1d1445626ce63f0810c24db7bc24521d0667af27e30aa5e567b866538`;
  only the output-directory-bearing automation manifests differ.
- Nineteen post-change focused regression groups pass in the current scratch build:
  input composition, deterministic runtime, headless driver, bot benchmark
  protocol, automation protocol, shadow replay adapter, shadow replay fixture,
  strict shadow replay result loader, labelled shadow evaluation, interaction
  action controller, player automation run config, Deus Ex sight probe schema, player
  movement controller, reach-spec route planner, bot benchmark telemetry, and
  visual-QA sidecar, capture packager/binder, plus Deus Ex perception and actor
  movement. The current
  binary also retains the exact `0.02`-second actor walk pass at tick 53 and the
  standalone Switch1 pass at tick 249 in repeated, byte-identical artifacts.
  Earlier XR groups remain separately recorded evidence.
- A manifest-backed Deus Ex TrainingFinal startup capture was human-gated for
  content and desktop isolation, then analyzed by both installed Qwen models.
  The reports are advisory, hash-bound, tool-free, and do not control the player.
  The same capture and initial observation were then used for a bounded shadow
  request permitting only `walk_to_actor` to exact `Light155` or a wait of at
  most 60 ticks. Both models chose `none`, so neither report contains a
  candidate. Report SHA-256 is
  `6f4f69282b92cefc3f32ca476edb2e06e9aad37ad80e6cbbddff434b81cb5b6b`
  for `qwen3.5:9b` and
  `df95adead920cb7b6ef23cd33df1c1788e21504126555eda5d931146c8484191`
  for `qwen3.6:27b`.
  The curated `none` replay fixture SHA-256 is
  `593103ac92517085808077c42266ce5d0c2ea088ba77e5f16e815a1aa024fbd0`.
  Both reports pass that oracle, with byte-stable replay-result SHA-256 values
  `6df894919570d0e98b217be67bdeb8e9d0d735432b3c9ffd08fb54e6a8aa1620`
  and `2bc6430f2ebde7d437130ee21b1933aca05b9aa8e4291107fe0a4020ee22e7cd`
  respectively.
- A fresh provenance baseline under
  `deus-ex-shadow-eval-provenance-baseline-v1` reruns the same immutable
  human-curated `none` case twice per model with the current generator SHA-256
  `0a58e5073712f20ca30bb0a65bc345c4cdcaf48f44797321b42e6cbe4207c539`.
  All four receipts were written, all four reports were schema-valid, and all
  four replay results passed with the same canonical `none` triple. Qwen 3.5
  inference elapsed times were 13.079 and 3.265 seconds; Qwen 3.6 times were
  24.065 and 6.933 seconds, retaining cold/load effects as evidence rather than
  treating them as comparable warm distributions. The manifest SHA-256 is
  `ee4478be77dae5f52962112a43205066ae60941565629bdd5dc64952c846fd15`;
  the exact generation-time script is preserved beside the inputs under its
  recorded hash.
  This is one negative case, not positive-action accuracy or control
  qualification.

## Execution order

### 1. Narrow Deus Ex runtime slices

Connect hearing first because its inputs and formula are bounded and do not
depend on traces or approximate lighting. Preserve the detectable-actor gate,
optional volume/radius defaults, vertical attenuation, and hearing threshold.

Keep motion visibility, smell, light sampling, and field-of-view falloff as
separate slices. The base sight/direction/LOS slice now has positive owner-data
proof for its scalar path plus negative direction and fully blocked LOS results
with recorded geometry and trace order. Pure callback tests cover all positive
and short-circuit LOS branches without commercial data. Each remaining slice
still needs focused synthetic tests and an owner-data before/after scenario
before a gameplay-support claim.

### 2. Bounded automation contract

Define a versioned command and result protocol with stable command IDs,
observation revisions, issue/deadline ticks, explicit terminal reasons, and a
small action set:

- `walk_to_point`
- `walk_to_actor`
- `acquire_item`
- `interact`
- `wait`
- `abort`

Targets must come from an engine-produced bounded observation snapshot. Reject
ambiguous, stale, missing, disallowed, or unreachable targets. Do not accept
arbitrary console commands or model-supplied raw input streams.

### 3. Player movement and interaction adapters

Drive the viewport player through ordinary composed input with a dedicated
synthetic source. Always release synthetic controls on success, failure,
timeout, cancellation, map travel, or shutdown so keyboard, mouse, and XR
contributors remain intact.

Use engine path queries and progress telemetry for planning, but do not
teleport actors. A separately owned pawn adapter may use latent movement only
when the scenario explicitly owns that pawn; it must reject stock
UnrealScript-controlled bots.

For acquisition, navigate into normal touch range or use the selected game's
ordinary interaction semantic. Deus Ex should use its stock highlight/frob
path. Success is an inventory, owner, resource, or world-state transition, not
mere proximity. Never call `Touch` directly.

### 4. Deterministic evidence

Add pure validation, target-resolution, timeout/cancellation, input-release,
serialization, and adversarial-bound tests first. Follow with controlled
native fixtures for reachable, blocked, stuck, cancelled, and multi-node
movement, then a deterministic pickup fixture.

Every command emits bounded lifecycle telemetry for acceptance, target
resolution, route selection, progress, collision/stuck recovery, interaction
attempts, state deltas, and terminal outcome. Repeated runs with the same
binary, content, seed, and configuration should produce the same event digest.

Owner-data Deus Ex runs belong under a dated, manifest-backed QA directory.
Commercial packages, saves, screenshots, and videos stay out of Git.

### 5. Local visual QA

Capture remains engine/test-owned. Each screenshot record includes tick or
simulation time, camera/view identity, dimensions, action correlation, and an
image hash. Raw captures remain immutable; crops or annotations are separate
hashed derivatives.

An offline sidecar under `tools/visual-qa` will read the run manifest and a
versioned assertion profile, verify hashes and model vision capability, and
request schema-constrained findings from local Ollama. Store the exact model
tag and digest, Ollama version, prompt/schema hashes, inference settings, raw
response, validated findings, timings, and later human adjudication beside the
run.

Initial evaluation should compare a small Qwen 3.5 model, Qwen3-VL 8B, and a
Qwen 3.6 escalation model on human-labelled fixtures. Selection is based on
schema validity, false positives and negatives, repeat stability, region/OCR
accuracy, and latency rather than parameter count.

Visual findings are advisory. They may mark a run as needing review, but a
model verdict is never the sole pass criterion. Screenshot text is untrusted
input, the analyzer has no tools, and the unauthenticated Ollama API remains
loopback-only.

### 6. Control ladder

Model-assisted demo work progresses through:

1. off;
2. advisory report (implemented);
3. shadow action proposals with `controls_live_player=false` (implemented,
   non-dispatchable);
4. replay against curated fixtures (offline proposal-oracle replay, strict
   provenance-bound result loading, native non-dispatchable command projection,
   and deterministic controller-fixture execution implemented; live-engine
   replay remains pending); and
5. explicitly armed, bounded, one-action-at-a-time demo control.

Any armed mode must use only validated engine target IDs and the bounded
command grammar, enforce leases and timeouts, record accepted and rejected
actions, and provide an immediate abort. Attack, arbitrary UI actions, shell
access, and console execution are out of scope for the initial controller.

## Milestones

1. Live Deus Ex hearing integration and focused regression gates.
2. Pure automation protocol, target snapshots, and telemetry tests.
3. Synthetic-input `walk_to` with controlled reachable/blocked fixtures.
4. Stock-behavior `acquire_item` and Deus Ex `interact` validation.
5. Manifest-backed screenshot capture and offline structured visual review.
6. Human-labelled model evaluation and shadow demo suggestions.
7. Repeatability matrix and scoped Deus Ex Training report.

Completion means the evidence states exactly which demo/Training behaviors are
proven. It does not imply full Deus Ex campaign, AI, save, XR, or upstream-PR
readiness.
