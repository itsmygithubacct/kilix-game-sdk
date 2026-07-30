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

The decoder supports non-interlaced, 8-bit grayscale, grayscale-alpha, RGB,
and RGBA PNG files. Indexed and higher-bit-depth PNGs fail explicitly instead
of being decoded inconsistently. All decoding is transactional: a failed load
leaves the caller's active image unchanged.

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

The game keeps semantic identifiers—such as which atlas row is a party member
or which bitmap is a battle scene. `kilix-assets` owns file validation and
decoded-pixel lifetime. A renderer can consume `kilix_asset_region` by mapping
its `pixels`, `width`, `height`, and `stride` directly into its image-view type.

Manifests may contain provenance, generation, labels, and other authoring
metadata. The runtime parser deliberately ignores unknown metadata while
strictly validating the `schema_version`, `game`, `atlases`, and `bitmaps`
records it needs.

## License

MIT.
