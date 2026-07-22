# Deus Ex compatibility roadmap

The goal is a reviewable series of upstream-quality commits, not a claim of full
support after reaching one map. This roadmap is updated as validation reveals
new blockers.

## Completed slices

- Reproducible GOG 1112fm baseline and reference provenance.
- Complete Deus Ex text token parsing and `ExtString` speech paging coverage.
- First AI perception slice: hearing, sight, visibility, motion, thresholds,
  detectability, occlusion, and original no-op smell behavior.
- Correct launcher/direct-map validation process.
- Silent, non-activating, per-process-log validation profile.
- QuickSave and numbered save creation plus direct QuickLoad/LoadGame startup.
- Save Game description input and existing-slot list population.
- Existing-slot selection, menu-driven overwrite and deletion, and serialized
  save-thumbnail previews.
- Stable source package indices and nested boolean serialization for reloadable
  map overwrites.
- Proprietary-data-free regression coverage for nested boolean serialization.
- Headless symbolization of background validation crashes.
- Normal title-menu entry into Training and mission-state-machine startup.
- Cardinal-axis actor movement for walking, swimming, and flying.
- Highlight and frob Training's first mover through the stock right-click path.
- Training reception-room view control, decoration frobbing, nanokey pickup,
  and stair traversal.
- Training upper-door unlock/open/cross, a fresh QuickSave reload at the
  reception checkpoint, and reconstruction of transient conversation bindings.

## Current work

1. Add missing generic array and save-metadata tests that do not require
   proprietary packages.
2. Continue through Training and trace each blocking interaction, AI,
   navigation, animation, conversation, or mission-script native in order.
3. Validate scripted travel from Training into the campaign and between early
   campaign maps, preserving inventory, flags, mission state, and save/load.

## Later compatibility slices

- Text consumers: books, DataCubes, email, bulletin links, notes, goals, and
  multi-page speech.
- AI fidelity: sampled light visibility, smooth peripheral falloff, alarm and
  callback events, path reachability, random destination selection, and combat.
- Animation and presentation: blend animation, head turning, landing sounds,
  and AVI/intro transitions.
- UI breadth: Load Game, overwrite/delete dialogs, settings, key customization,
  computers, conversations, goals/notes/images, and scrolling lists.
- Campaign state: flags, mission scripts, cross-map travel, inventory transfer,
  conversations, goals, endings, and save compatibility at representative early,
  middle, and late checkpoints.

## Pull-request gates

Do not propose the upstream pull request until:

- changes are split into coherent commits with no proprietary inputs or test
  artifacts;
- Release builds and focused CTests pass from a clean configuration;
- title, new game, Training, Liberty Island, scripted travel, QuickSave,
  numbered save, load, overwrite, and delete are re-tested;
- silent/no-focus behavior remains opt-in and does not regress normal launches;
- broad engine changes such as dynamic-array access have focused regression
  tests and are separated from Deus Ex-specific behavior where practical;
- known gaps and the exact validated scope are documented without overstating
  campaign completeness.
