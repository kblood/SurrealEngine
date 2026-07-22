# Property serialization extraction

This branch reconstructs one generic engine correction discovered while
validating Deus Ex. Its read-only source is commit `27644b500208d1032bb65da7fc3832fe6f84d72e`
on the preserved `deus-ex-support` branch.

## Included source mapping

- `UProperty::SaveStructMemberValue` mirrors the existing untagged load path and
  delegates to `SaveValue` by default.
- `UBoolProperty::SaveStructMemberValue` writes the byte representation used by
  an untagged aggregate payload. A tagged boolean still stores its value in the
  property header and therefore keeps an empty `SaveValue` payload.
- Fixed arrays, dynamic arrays, and structs dispatch their elements through the
  untagged save path.
- The source test was renamed from `DXPropertySerializationTests` to
  `PropertySerializationTests` and expanded to cover all three aggregate call
  sites. It contains no game-specific fixtures or data.

## Deliberately excluded

The following changes share the original source commit but are separate review
units and are not present here:

- source name/import/export table preservation in `PackageWriter`;
- expanded `ObjectStream` error diagnostics;
- save-slot deletion and package-file management;
- root-window snapshot generation and thumbnails;
- list-window selection, focus, paging, and drawing behavior;
- every Deus Ex save, directory, menu, and validation change.

## Dependencies

The branch starts at upstream `origin/master` commit
`891082d9f05a0f7b9ffcb4f65c9653f36fce5613`. It has no dependency on the game
support registry, the cardinal-axis movement fix, or any Deus Ex module. Those
branches may be integrated in any order.

The remaining source-table preservation logic should become a separate generic
package-writer slice with a round-trip test before it is proposed upstream.
