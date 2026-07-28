# Deus Ex reconstruction ledger

Date: 2026-07-28

This ledger reconstructs Deus Ex work onto the unified repository lineage. It
does not authorize merging the `demo/deus-ex` history: that branch was created
from the Farantir-derived `first-native-vr` repository at `c5f80d19`.

## Target and sources

- Target base: `integration/unified-engine` at `0f2e29c9`.
- Reconstruction branch: `feature/deus-ex-reconstruction`.
- Canonical upstream: `dpjudas/SurrealEngine` `master` at `2d47264c`.
- Local evidence: `evidence/demo-deus-ex` at `2e328f70` plus the preserved
  uncommitted Paul-conversation patch and QA runs.
- Unified/upstream merge base: `891082d9` (`Fix speech missing for some
  missions`). Unified is missing 24 later upstream commits as ancestors.

Disposition terms:

- **accepted**: reconstructed on this branch and subject to build/runtime gates;
- **review**: contains potentially useful production behavior requiring manual
  decomposition or adaptation;
- **evidence-only**: validation or diagnostic intent should be preserved, but
  the old implementation should not be copied into production;
- **do not replay**: merge/history content is tracked through its real source
  instead.

## Accepted canonical upstream commits

| Upstream | Reconstruction | Disposition |
| --- | --- | --- |
| `c2599d51` Implement `UBorderWindow` | `7599bae9` | accepted |
| `b0a1e098` increase Deus Ex conversation speech volume | `716b0810` | accepted |
| `e1e446e6` scope talk-slot gain to Deus Ex | `ce57386e` | accepted |
| `1b0c77ce` implement more of `UListWindow` | `746d9fe5` | accepted |
| `d623144c` dispatch list-selection events | `e97e88a1` | accepted |
| `b8f43572` fix the GC root intrusive list | `06fcb94e` | accepted; required by the reconstructed save-info GC root |
| `2b2694b3` implement reflected-property marking | `0407fb8b`, `35b5aad7` | accepted and corrected to dereference object-property storage and retain returned mark-list heads |
| `5bd65f1d` fix GC traversal/sweep | `e89700a7` | accepted |
| `d8eb2151` restore max-exclusive `Object.Rand` | `98778fb9` | accepted as gameplay correctness |
| `f964b195` travel non-Actor `FlagBase` objects | `03581f71` | accepted; required for Deus Ex flag state across maps |
| `0747cbc2` remove redundant object check | `e37e1eeb` | accepted with its GC/object dependency batch |
| `265e820e` register `GC` in `Extension` | `f4e17c7c` | accepted; matches the shipped Deus Ex class package |
| `20003131` experimental blend animation | `d39d6219`, `8e5600f5` | accepted and adapted: removed diagnostic logging, validated frame counts, and restored a real negative tween interval |

The upstream audio and tokenizer tests pass at this checkpoint, and the native
`SurrealEngine` target builds in `RelWithDebInfo`.

## Accepted local correction

| Evidence | Reconstruction | Disposition |
| --- | --- | --- |
| `54d15f6f`, `1299284e`, and the preserved Paul choice patch | `fc63e945` | accepted as one focused raw-release button correction for inventory-item, inventory-action, and conversation-choice buttons |

## Accepted reconstructed batches

| Source evidence | Reconstruction | Disposition |
| --- | --- | --- |
| `282c53e3`, `d2a45e4e`, `dae94552` | `6956d6ad` | accepted list parsing, selection/focus, sorting, movement, sizing, and pre-draw hit testing; unrelated large-commit content excluded |
| `df4def7e` | `c88239a9` | accepted inline `ScriptArray` wrapper correction with a regression test; crash-report changes excluded |
| `0f27d040` | `4f6b4dc4` | accepted dedicated Deus Ex save-info package ownership, adapted with a GC root; fatal-output changes excluded |
| `969c27d1` | `e16209d8` | accepted dump-location user/player state |
| `ff52f116` | `fa4a5a97` | accepted keyboard reset, adapted to release only keyboard/mouse composition contributors |
| `393ac849` | `30e31b03` | accepted shipped-script arity for readable text natives; fixture/audit code excluded |
| `393ac849`, `4d34645a`, `e4d3f4c1` | `05db8654` | accepted preferred-size flags, wrapped tile measurement, and Deus Ex reader width constraint |
| `0ee4d99b` | `e4d86070` | accepted Deus Ex-scoped Escape dismissal for the top modal; fixture/audit code excluded |
| upstream `2d47264c` | `10a81ef4` | accepted clip-window preferred size/child access and edit-window drawing; unrelated debug-render changes excluded |
| `282c53e3` | `647a1e8f` | accepted configurable/instant speech volume on top of the canonical Deus Ex talk gain |
| `282c53e3` | `d0a6f7c8` | accepted map/save directory enumeration and QuickSave naming with null checks |
| `282c53e3` | `245fbcdd` | accepted flag expiration and type-correct expiration lookup |
| `282c53e3` | `4852b28f` | accepted Deus Ex `loadgame` travel, signed/QuickSave slots, SaveInfo map lookup, startup load fallback, and saved-player possession |
| `282c53e3` | `280e4684` | accepted actor AI event callbacks, perception/reachability natives, path-node iterator, corrected `CanSee`, safe save deletion, optional save descriptions, and string-to-flag handling |
| `282c53e3` | `2e6dc435` | accepted child visibility, viewport fallback/state, edit controls, scale controls, root mouse/snapshot state, and computer-terminal controls |
| upstream GC batch plus reconstruction review | `35b5aad7` | accepted GC regression coverage and fixes to object/struct property marking |

## `demo/deus-ex` first-parent inventory

| Commit | Subject | Initial disposition |
| --- | --- | --- |
| `282c53e3` | Improve Deus Ex support | decomposed into accepted save/load, AI, animation, flag, audio, directory, and window batches; its older text parser is superseded by unified's tokenizer and tests |
| `235b82fc` | Merge upstream fixes | do not replay |
| `dd289c5f` | Fix upstream replay plan formatting | evidence-only |
| `9aad0d9f` | Add Deus Ex inventory audit | evidence-only |
| `a38fef81` | Add Deus Ex UI music audits | evidence-only |
| `0580a3d0` | Add Deus Ex look-at audit | evidence-only |
| `d2a45e4e` | Fix list window row selection | accepted in `6956d6ad` |
| `1673529a` | Add Deus Ex nearby item audit | evidence-only |
| `e732aa94` | Add Deus Ex movement input audit | evidence-only |
| `78a5976c` | Add bounded Deus Ex go-to | evidence-only; adapt to unified automation if still useful |
| `9692f0d3` | Improve Deus Ex navigation audits | evidence-only |
| `978c8987` | Add Deus Ex interaction audit | evidence-only |
| `fe0da00d` | Fix Deus Ex use interaction | evidence-only; preserves required `FrobTarget` setup for automation |
| `72a422db` | Add Deus Ex travel audit | evidence-only |
| `6e71113c` | Document Deus Ex intro test | evidence-only |
| `93e0555f` | Add Deus Ex conversation audio audit | evidence-only |
| `4f23a55e` | Document Deus Ex conversation boundary | evidence-only |
| `647fe1d1` | Add Deus Ex nearby pawn audit | evidence-only |
| `faa7ed48` | Document Deus Ex HQ navigation | evidence-only |
| `2dae3cc5` | Improve Deus Ex waypoint following | evidence-only; review route logic before adapting |
| `393ac849` | Fix Deus Ex readable UI | production native/layout portions accepted in `30e31b03` and `05db8654`; audit code remains evidence-only |
| `d65e609d` | Test Deus Ex books | evidence-only |
| `dae94552` | Fix list row hit testing | accepted in `6956d6ad` |
| `df4def7e` | Fix native dynamic script arrays | production correction accepted in `c88239a9`; crash-report diagnostics excluded |
| `3947f0c7` | Add Deus Ex Load Game UI audit | evidence-only |
| `0f27d040` | Fix Deus Ex save metadata serialization | accepted in `4f6b4dc4`; diagnostics excluded |
| `451c83f4` | Test Deus Ex Load Game UI flow | evidence-only |
| `1e657a3c` | Merge upstream changes | do not replay |
| `969c27d1` | Handle dump location login state | accepted in `e16209d8` |
| `ff52f116` | Reset input after player travel | accepted in `fa4a5a97` with unified input composition |
| `77016091` | Stabilize Deus Ex pickup fixture | evidence-only |
| `0ee4d99b` | Fix Deus Ex readable windows | production modal-close portion accepted in `e4d86070`; fixture/audit code excluded |
| `ceda75e7` | Add unattended game launch mode | evidence-only; replace with unified headless/automation facilities |
| `4d34645a` | Constrain Deus Ex readable width | accepted in `05db8654`, scoped to Deus Ex reader classes |
| `e4d3f4c1` | Measure wrapped tile heights | accepted in `05db8654` |
| `ba3cb36e` | Document Deus Ex UI validation | evidence-only |
| `bd4794cd` | Audit Deus Ex inventory grid | evidence-only |
| `68c7868a` | Exercise Deus Ex inventory clicks | evidence-only |
| `54d15f6f` | Fix Deus Ex inventory selection | accepted in `fc63e945` |
| `1299284e` | Exercise Deus Ex inventory actions | production portion accepted in `fc63e945`; remainder evidence-only |
| `2e328f70` | Close Deus Ex inventory fixture | evidence-only |
| uncommitted | Fix Paul dock conversation choice | accepted in `fc63e945` |

## Upstream commits after unified's merge base

Directly relevant commits are listed above. Every remaining upstream commit is
explicitly dispositioned here:

| Upstream | Initial relevance |
| --- | --- |
| `b8f43572`, `5bd65f1d`, `2b2694b3`, `0747cbc2`, `265e820e` | accepted in the GC/object dependency batch above |
| `20003131` | accepted and hardened in the blend-animation batch above |
| `d8eb2151`, `f964b195` | accepted above; `fb2d799c` is debugger-only metadata and not part of Deus Ex runtime reconstruction |
| `d4661608` | not Deus Ex-specific; explicitly targets Unreal Tournament 469e struct-member loading and is deferred to the general upstream-integration track |
| `4823c4e8`, `31ec0013`, `b397ad5c`, `21c6d6c0` | external music-player support; not currently Deus Ex-specific |
| `a8ffc9d1`, `737b2ef6` | Unreal 227/native operators; not currently Deus Ex-specific |
| `ea8fad80`, `ffc6e3af` | source split and build fix; do not replay solely for Deus Ex, but use new file locations when reconstructing on a later upstream base |
| `2d47264c` | relevant clip/edit window portions accepted in `10a81ef4`; debug rendering excluded |

The legacy audit, waypoint, unattended-launch, and fixture commits remain
evidence-only because unified deliberately removed the ad-hoc production
automation commands. Their scenario intent is retained in the preserved QA
manifests and should be re-expressed through unified's external/headless test
interfaces rather than reintroducing game-specific command handlers.

## Validation gates

For each accepted batch:

1. build the native `SurrealEngine` target;
2. run focused unit tests, then all registered native tests at stable
   checkpoints;
3. run the exact Deus Ex demo fixture without modifying game data;
4. run the corresponding installed GOTY retail scenario;
5. preserve logs and a manifest under `SurrealEngine/qa/runs`;
6. confirm UT99/Unreal Gold desktop behavior for generic window, input,
   serialization, audio, and travel changes.
