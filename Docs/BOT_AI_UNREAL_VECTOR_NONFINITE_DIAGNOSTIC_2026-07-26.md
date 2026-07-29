# Unreal Gold Vector Non-Finite Diagnostic

Date: 2026-07-26  
Scope: read-only diagnostic evidence for the Unreal Gold 226b DeathFan
non-finite bot destination. This is not a behavior change or qualification
result.

## Instrumentation

Commit `89c62efb` adds the default-off benchmark switch
`--botbench-vector-nonfinite-observer=1`. It observes post-result
UnrealScript vector arithmetic for pawn callers, preserving every arithmetic
result and native semantic. It records bounded pawn identity, life/tick,
operation, finite/NaN/infinite classes, and the immediate UnrealScript caller.
The quality analyzer rejects a run with an observed non-finite operation, so a
diagnostic capture cannot be mistaken for quality evidence.

## Candidate diagnostic

The guarded, 5,100-tick Unreal Gold DeathFan diagnostic with the vision
experiment enabled completed only because the existing finite-destination
containment guard intervened at tick 5,081. The new observer identifies the
first operation for that pawn as:

| Field | Value |
| --- | --- |
| Pawn | `pri:4`, actor 211, `UnrealI.MaleOneBot` |
| Life / tick | 7 / 5081 |
| Script caller | `UnrealI.MaleOneBot.PickRegDestination` |
| Operation | `divide_vector_float` |
| Input classes | vector finite; scalar finite |
| Result class | all components `nan` |

For a finite vector divided by a finite scalar to yield all-NaN components,
the observed numeric form is necessarily a zero vector divided by zero. The
following operations only propagate the NaN; they are not the origin.

Other `PickRegDestination` calls also produce the same intermediate form for
`UnrealI.MaleTwoBot` at ticks 1891, 1895, 1949, and 2038. They do not all
become a sampled non-finite `Destination`.

## Exact owner-local source provenance

The no-UCC owner-local export of the installed Unreal Gold package contains
the unguarded expression in
`UnrealShare/Classes/Bots.uc:3750,3799`:

```unrealscript
enemyDist = VSize(Location - Enemy.Location);
enemydir = (Enemy.Location - Location)/enemyDist;
```

This explains the zero vector / zero scalar input when a bot and enemy occupy
the same location. The source was exported to the external QA artifact only:

`qa/runs/2026-07-26/script-export-hitwall-certificate/unreal226b/scripts/UnrealShare/Classes/Bots.uc`

## Matched stock control

With the vision experiment disabled but the vector observer and containment
guard enabled, the same 5,100-tick layout has 46 non-finite vector-operation
records from one affected pawn. The candidate diagnostic has 56 records from
two affected pawns, including the tick-5,081 `MaleOneBot` event. Neither
diagnostic has observer overflow or integrity failure.

Therefore the arithmetic edge case is present in stock Unreal Gold scripts;
the vision experiment changes reachability of one destination-corrupting
instance but has not been proven to cause the underlying script defect.

## Disposition

Do not change `NObject::Divide_VectorFloat` or global `Normal` semantics yet.
The UnrealScript operator intentionally permits division by zero, and this
diagnostic alone does not prove the retail native engine's expected result or
the correct downstream recovery behavior. The finite-destination guard is
containment evidence only and remains a strict analyzer rejection.

The next implementation decision requires an independently verified UE1
compatibility oracle for the operator/downstream movement boundary, followed
by a narrow fixture and guard-free Unreal benchmark evidence.
