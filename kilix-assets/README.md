# kilix-assets

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

`kilix-assets` is the renderer-independent runtime asset layer for Kilix
games. It centralizes the work that was otherwise repeated in each game:

- safe source/install/environment asset-root resolution;
- bounded, CRC-checked PNG decoding to straight-alpha RGBA8;
- headerless raw-RGBA loading when an asset compiler emits exact dimensions;
- version-1 Kilix graphics-manifest parsing and structural validation;
- owned image caching, atlas cells, arbitrary image regions, and tick-based
  animation clips.

The decoder supports non-interlaced grayscale, grayscale-alpha, RGB, and RGBA
PNG files at eight bits per channel, plus indexed PNGs at 1, 2, 4, or 8 bits.
Palette alpha and grayscale/RGB `tRNS` transparency are expanded to
straight-alpha RGBA8. CRCs, chunk types and ordering, palette indices, zlib
stream completion, dimensions, and allocation budgets are validated. Other
bit depths and interlacing fail explicitly.

## Build and verify

```sh
make
make test
make sanitize
```

Validate a complete game pack with the same runtime decoder and manifest
rules:

```sh
./build/kilix-assets-check path/to/manifest.json path/to/game-root
```

Runtime dependencies are C11 and zlib. The library has no renderer, terminal,
gameplay, or content-installer dependency.

## Runtime ownership

Images and manifests must be zero-initialized before their first load and
cleared once when finished. Loads are transactional: failure leaves the
caller's existing object unchanged. A cache must be initialized once and
cleared once; it owns every returned image, and image pointers remain stable
until that clear. Cache identity is the exact path and format request, and its
byte budget counts decoded pixels rather than keys or index metadata. Cached
files are not automatically revalidated or reloaded.

Regions borrow image or cache storage. A renderer can map their `pixels`,
`width`, `height`, and `stride` directly into its image-view type. Callers must
serialize mutation of the same image, manifest, or cache; independent objects
have no shared mutable library state.

Asset paths receive lexical portability checks. The resolver searches an
optional environment root, source root, then installed root and returns only
an existing regular file. Roots are trusted configuration: lexical checks do
not turn a root containing symlinks into a filesystem sandbox. File loading is
bounded, rejects non-regular inputs such as FIFOs and directories, and refuses
a file whose identity, size, or modification metadata changes during the
read.

## Manifest version 1

The game keeps semantic identifiers—such as which atlas row is a party member
or which bitmap is a battle scene. The parser requires `schema_version`,
`game`, `atlases`, and `bitmaps`; validates UTF-8, JSON whitespace, dimensions,
grids, paths, identifiers, and duplicate runtime fields/IDs; and ignores valid
unknown authoring metadata.

Bitmap records use either the canonical `png`, `width`, and `height` fields or
the production-compatible `path` plus a complete 1×1 (or otherwise internally
consistent) `grid` object. Mixing the two representations is rejected rather
than resolved by field order.

The checker loads every referenced image through the same cache and decoder,
then verifies declared dimensions, atlas grids, and required transparency.

## License

MIT.
