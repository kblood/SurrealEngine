# Bot AI architecture

## Product goals

The first goal is maximum competent play, not artificial difficulty tiers or
human-looking mistakes. The engine should first produce bots that reliably
perceive, navigate, acquire equipment, fight, survive, and pursue objectives.
Difficulty can later constrain recognition, aim, planning budget, information
precision, and risk tolerance without weakening basic correctness.

Stock UT behavior remains a compatibility target. Enhanced policies are
explicit opt-in candidates until deterministic evidence shows what they improve
and what they regress.

## Layered model

### 1. Perception and subjective memory

Tactical code must not read omniscient world state. Each bot receives known
actors containing:

- stable actor identity and classification;
- last observed position and velocity;
- observation time and information source;
- confidence or positional uncertainty;
- visibility, hostility, damage, and team-report flags; and
- expiry appropriate to the information source.

The acquisition pipeline is potential target, FOV/range, line of sight,
recognition, threat selection, aim acquisition, then fire authorization.
Hearing produces an uncertain location rather than exact enemy coordinates.

### 2. Navigation and locomotion

UE1 NavigationPoints and reachspecs stay authoritative. A runtime cache may add
derived information such as visibility, traversal type, exposure, encounter
direction, recent failure, and bounded danger.

The path follower owns lookahead steering, arrival envelopes, doors, lifts,
ladders, jumps, drops, short movement prediction, temporary avoidance, and a
deterministic stuck ladder. The decision policy requests destinations; it does
not micromanage every movement tick.

### 3. Tactical queries

Policies consume bounded queries rather than walking all actors and navigation
nodes themselves:

- reachable item and enemy candidates;
- route distance, ETA, danger, and exposure;
- alternative routes;
- weapon suitability and ammunition state;
- local friend/enemy counts;
- cover or escape candidates; and
- objective state.

Every query must have deterministic ordering and useful diagnostic output.

### 4. Decision policies

The comparison starts with three policy families:

| Policy | Purpose |
| --- | --- |
| `stock-botpack` | Compatibility baseline using existing scripts and repaired native services |
| `utility-arena` | YaPB-like scored actions with an interruptible task stack |
| `tactical-state` | CSBot/NextBot-like explicit combat, hunt, investigate, resupply, retreat, and recovery states |

Policies use the same perception, navigation, tactical-query, aim, and command
interfaces. Policy identity and version are immutable benchmark inputs. A
variant must never be selected implicitly by build order or global mutable
state.

An action returns one of continue, succeed, fail, change, suspend, or resume.
Decision traces include the selected action, alternatives, scores/reasons, and
the observation revision on which the decision was based.

### 5. Aim and weapon control

The high-quality controller may use full information initially, but it must
still obey weapon mechanics and physically advance view rotation. It tracks
angular velocity and acceleration, chooses weapon-appropriate target points,
leads projectiles, controls bursts/recoil, avoids friendly fire, and reports
why it withheld a shot.

Later difficulty profiles add recognition delay, stable aim error, reduced
prediction, reaction latency, and lower tactical-query budgets. Random errors
must be seeded and reproducible.

### 6. Coordination

Begin with local coordination: shared sightings, leader/follower intent,
pickup/cover reservations, nearby strength estimates, and short-lived calls for
help. Do not introduce a central omniscient team planner until local behavior is
measured and insufficient.

## Runtime boundaries

Bot AI remains inert unless a game creates a bot or a benchmark explicitly
selects a policy. Desktop, XR, browser, VM, and renderer behavior are outside
the bot-policy boundary.

The current `BotBenchmark` driver and `DeterministicRuntime` remain the execution
foundation. Extensions should be small topics: richer observation, event
attribution, multi-bot roster control, policy selection, scenario fixtures,
analysis, then behavior changes.

The experimental integration branch may compare complete candidate stacks.
Any upstream proposal must be reconstructed as one understandable correctness
fix with a reproduction and focused test, following `CLAUDE.md`.
