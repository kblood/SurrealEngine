# Deus Ex text module extraction

This branch is the first explicitly Deus Ex-owned implementation stacked on
`pr/game-support-registry` at `ff175b0aa9da81d68331933f59140920146dc106`.
The registry already confines the Deus Ex text native registration to the
`DeusEx` game descriptor; this slice adds no game-name conditionals to the
engine loop, renderer, input system, or platform layer.

## Source mapping

The read-only source line is `deus-ex-support`. This extraction reconstructs
the text changes from:

- `3ef3a35ecd2a8a5675da256b295d82d2ee0fbb5d`, which identified and corrected
  exact-end matching, color output, email tagging, and alignment parsing in the
  original inline parser;
- `44f04e20de83037258716564ab8f2b3ffb1c4876`, which replaced that parser with
  the complete tested tokenizer, connected the UObject wrappers, and completed
  239-character `ExtString` paging.

The reconstructed module lives under `GameSupport/DeusEx`. Its tokenizer has no
UObject, package, renderer, platform, or commercial-data dependency. The
existing `UDXTextParser` and `UDXExtString` classes are thin adapters that copy
token results to UnrealScript-visible properties.

Compared with the preserved implementation, the extraction also returns the
payload of a label such as `<L=Start>` through `GetName`; the focused test now
covers the token-name contract as well as the label token ID.

## Ownership boundary

The separation is architectural rather than semantic:

- tokenization, metadata rules, block consumption, substitutions, and paging
  are all specific to the Deus Ex text format and belong to the Deus Ex module;
- keeping the parser pure and dependency-free is a reusable engine pattern,
  but none of these token rules should become a generic UE1 capability;
- the native wrapper remains registered through the game-support descriptor,
  so UT99 and Unreal do not select the behavior.

The source files remain part of `SurrealCommon` for now because the engine is
still linked as one static runtime. Their directory and registration ownership
allow a later per-game library split without changing parser call sites.

## Scope exclusions

This branch intentionally excludes save packages, menus and list widgets,
snapshots, AI perception, actor movement, window activation, conversation
reconstruction, and all game-data fixtures. It does not redistribute or load
the original text DLL.

## Automated coverage

`DeusExTextTokenizerTests` covers all 30 token IDs, exact-end text, three
alignment modes, RGB values, case-insensitive tags, player substitutions,
comment and goal block consumption, escaped brackets, graphic/font/label names,
file metadata, optional and extra email fields, empty metadata, and paging at
239-character boundaries. The test uses synthetic strings only.

Interactive validation with stock books, datacubes, email terminals, document
links, and multi-page speech remains a release-validation task rather than an
upstream unit-test dependency.
