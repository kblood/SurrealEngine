# Local visual QA sidecar

`visual_qa.py` performs advisory analysis of immutable, hashed screenshots from
a Surreal QA run. It accepts only a loopback Ollama endpoint, verifies the
selected model exists and advertises `vision`, sends no tools, and records the
model tag/digest, Ollama version, prompt/schema/image hashes, raw response, and
validated findings. Advisory mode is the default. Every report declares
`controls_live_player=false`.

Historical advisory runs use `surreal-visual-qa-capture-manifest-v1` with 1–64
relative image paths. Each capture requires `id`, `path`, `sha256`, `tick`,
`camera`, `width`, `height`, and optional `command_id`. Relative paths cannot
escape the manifest directory and each image is limited to 25 MiB.

Positive shadow proposals require a runtime-bound
`surreal-visual-qa-capture-manifest-v3` or v4. Their exact session record binds a
session ID, source revision and dirty state, production-binary hash, and
configuration hash.
Each capture additionally binds a command ID and a relative canonical
observation by hash, revision, and tick; the capture and observation ticks must
match. The v3 binding also hashes the engine-owned receipt, complete telemetry
ledger, and terminal summary, and identifies the exact barrier event, immediately
following stock-action event, action kind, and target. Observations are limited
to 8 MiB and cannot escape the manifest directory. Duplicate JSON keys and
extra fields are rejected. Manifest v4 additionally binds the adjacent final
pickup attempt and exact post-click ownership/resource observation. Legacy v1
and declared v2 shadow input remain
readable for `none` proposals, but cannot produce a positive proposal.

A v2 manifest prevents declared artifact substitution, but does not by itself
prove that an image came from the engine process or precedes an action. Fresh
native automation can opt in to a same-process D3D11 or Vulkan readback with
the capture arguments and an explicit `--automation-capture-phase`. The
historical `post_simulation` phase publishes receipt v1. Three positive phases are
currently validated. `pre_stock_interaction` captures immediately after movement
and aiming are released, and before telemetry or synthetic right-click for the
stock frob, the engine renders the viewport, writes the PNG and canonical
observation, and publishes receipt v2 last. Its input barrier records stable
process-lifetime synthetic-input counters and zero prior interaction presses.
`pre_action` captures an exact reachable `walk_to_actor` target at tick zero
with zero synthetic requests; the immediately following telemetry boundary at
tick one is accepted only after the runtime applies exactly the ordinary
forward, strafe, and turn axes and no interaction press. `pre_pickup` permits
earlier route interactions but captures the exact unowned, acquirable final
target before its stock right-click. Receipt v3 records stable counters
(including any earlier switch press); the adjacent action must target that item
and the next event/observation must prove ownership transfer with the same
resource amount.
WebGPU and null rendering are intentionally unsupported because they cannot
provide this synchronous presented-frame proof.

After the process exits, materialize the strict consumer manifest only after
the binder verifies the receipt, bounded files, hashes, PNG dimensions,
automation configuration, complete telemetry ledger, and matching terminal
command result:

```text
python tools/visual-qa/bind_automation_capture.py \
  --receipt <automation-run>/visual-capture/capture-receipt.json \
  --binary <build>/SurrealEngine.exe
```

The receipt is an engine-owned provenance record under the trusted local
binary/process assumption, not a signed attestation. Receipt v1 materializes
manifest v2 for advisory/`none` use. A validated receipt v2 materializes
manifest v3 and may admit only the exact receipt-bound `interact` or
`walk_to_actor` proposal. It still creates a non-dispatchable candidate.
Validated receipt v3 materializes manifest v4 and may admit only its exact
`acquire_item` target after revalidating the adjacent ownership effect. It also
creates only a non-dispatchable candidate.

The assertion profile uses `surreal-visual-qa-assertion-profile-v1` with a
bounded `profile_id`, `prompt`, and 1–64 `{id, description}` assertions.

Example:

```text
python tools/visual-qa/visual_qa.py \
  --run-manifest <qa-run>/capture-manifest.json \
  --profile <qa-run>/assertions.json \
  --model qwen3.5:9b --output <qa-run>/visual-qa/qwen3.5-9b/report.json
```

An opt-in shadow mode may request one non-dispatchable suggestion from the
model. The engine observation, permitted action kinds, exact target identities,
and wait bound are all supplied by the caller and validated locally. For
example:

```text
python tools/visual-qa/visual_qa.py \
  --run-manifest <qa-run>/capture-manifest.json \
  --profile <qa-run>/assertions.json \
  --model qwen3.5:9b --output <qa-run>/visual-qa/qwen3.5-9b/report.json \
  --shadow-observation <qa-run>/observation.json \
  --shadow-action walk_to_actor --shadow-action wait \
  --shadow-target-identity <exact-observation-target-id> \
  --shadow-max-wait-ticks 60
```

Shadow actions are limited to `walk_to_actor`, `acquire_item`, `interact`, and
`wait`. Targeted proposals must select an allowlisted target whose observation
capability matches the action. Unknown targets, capability mismatches,
out-of-bound waits, and other action kinds are rejected. A valid proposal is
translated to a locally derived `surreal-automation-shadow-candidate-v1`
record with `dispatch_authorized=false`; the sidecar has no command dispatch
path and never controls the player.

## Curated shadow replay

`shadow_replay.py` evaluates an existing shadow report against a curated,
hash-bound acceptance oracle without contacting Ollama or the engine. The
historical `surreal-visual-qa-shadow-replay-fixture-v1` binds one capture hash,
one raw observation hash, the exact action/target policy, and 1–64 acceptable
`{action, target_identity, wait_ticks}` proposals. The v2 fixture adds the
exact session, command, binary, configuration, source-revision, and observation
grounding from capture-manifest v2. Replay requires the report result and
provenance to repeat that grounding exactly. Replay-fixture v3 carries the
SHA-256 of the original capture-manifest v3 instead of copied grounding. Replay
reloads that manifest, revalidates its receipt/events/summary/binder chain, and
derives grounding v2 itself. Replay-fixture v4 similarly reloads
capture-manifest v4 and derives grounding v3 with the pre-effect binding and
effect-observation hashes.

```text
python tools/visual-qa/shadow_replay.py \
  --fixture <qa-run>/shadow-replay/fixture.json \
  --capture-manifest <automation-run>/capture-manifest.json \
  --observation <automation-run>/observation.json \
  --report <qa-run>/visual-qa/model/report.json \
  --output <qa-run>/shadow-replay/model-result.json
```

Replay revalidates the observation policy and proposal, verifies report,
capture, observation, and model provenance, and independently derives the
candidate. Substituted candidates or provenance are rejected. A valid but
uncurated proposal produces a deterministic `failed` replay result. A matching
proposal produces `passed`; neither verdict dispatches anything. Legacy results
use `surreal-visual-qa-shadow-replay-result-v1`. Fixture v3 produces result v2,
which records the exact manifest/binding hashes and `pre_action_bound` authority
and fixture v4 produces result v3 with `pre_effect_bound` authority. Both newer
results are deliberately rejected by the older native v1 loader. Every result repeats
`dispatch_authorized=false` and `controls_live_player=false`. Exit status is 0
for a match, 1 for a valid mismatch, and 2 for invalid evidence.

Fresh model runs can also write a bounded attempt receipt so validation errors,
timeouts, or transport failures remain visible instead of disappearing from an
evaluation denominator:

```text
python tools/visual-qa/visual_qa.py <normal arguments> \
  --receipt <attempt>/receipt.json \
  --attempt-id <case:model:repeat> --attempt-repeat 1
```

Successful reports and receipts pin the SHA-256 of the exact `visual_qa.py`
generator. A failed attempt writes a receipt with a bounded error category and
no report hash. All receipt authority flags remain false.

## Labelled shadow evaluation

`shadow_eval.py` is a pure offline scorer. It makes no HTTP requests, imports no
engine/adapter/driver code, and consumes only saved, hash-bound artifacts. Its dataset schema
requires each case to bind its capture manifest/image, assertion profile,
observation, replay fixture, two distinct human-label records, and an
adjudication record. The adjudicated canonical proposals must exactly equal the
fixture oracle and must all share one action family.

Evaluation-dataset v2 carries replay-fixture v3 and capture-manifest v3 through
the same validator. Every positive case requires `pre_action_bound` authority;
evaluation-result v2 records the manifest and pre-action-binding hashes per
case and attempt. Dataset/result v3 carries fixture/manifest v4 and requires
`pre_effect_bound` authority plus exact effect-observation hashes. Historical
v1/v2 fixtures remain deterministic offline
metrics evidence, but cannot become fresh positive generation authority.

The run ledger declares every case/model/repeat slot. Missing or duplicate slots
are rejected, while a valid failure receipt is counted as an invalid attempt.
The deterministic result reports schema validity, exact-oracle accuracy,
five-action confusion and precision/recall/F1, unsafe commission,
over-abstention, target/wait exactness, semantic repeatability, pairwise
agreement, and integer latency percentiles. It never emits a native-loadable
candidate or grants dispatch authority.

```text
python tools/visual-qa/shadow_eval.py \
  --dataset <qa-run>/dataset.json --run <qa-run>/evaluation-run.json \
  --output <qa-run>/evaluation-result.json
```

The engine-side `Automation/ShadowReplayAdapter` can independently project a
validated fixture candidate into the canonical command structure for native
tests. It requires `dispatch_authorized=false`, fixes action radii locally,
reuses the engine observation/target validators, and has no runtime driver or
JSON-loading integration. It is a validation boundary, not a control path.

`Tests/Fixtures/ShadowReplayFixture` adds deterministic execution around that
projection using the production movement, interaction, input-adapter, and
telemetry contracts. Its synthetic world applies only composed axis output and
supplies an explicitly selected fixture transition only after one interaction
request. In addition to direct ownership and target-state proofs, it reproduces
the two owner-data-observed Deus Ex proof shapes: an exact pickup-target
tombstone plus an exact delta on the sole matching owned resource stack, and a
separate receiver movement whose `Tag` exactly matches the interacted target's
`Event`. Negative fixtures cover a missing resource delta, unrelated receiver
motion, sub-threshold receiver jitter, ambiguous bindings, abort before the
transition, and bounded timeout. Repeated positive evidence is byte-identical
and all terminal paths release synthetic input. This remains a data-free
controller test rather than a live-engine replay mode; it cannot load a model
report or dispatch into the engine.

The report may request human review but cannot establish an engine pass by
itself. Captured UI text is treated as untrusted data. Do not expose Ollama's
unauthenticated API outside loopback for this workflow.

The generation schema intentionally uses a broadly supported JSON Schema
subset. Every response is still checked locally for exact fields, capture and
assertion identities, assertion count, uniqueness, bounded text, valid status,
and finite confidence before a report is written.
