# kilix-game-tools

`kilix-game-tools` centralizes the Python validation and deterministic archive
primitives shared by Kilix games. It is an authoring/build dependency, never a
runtime dependency.

The package provides:

- generated WAV checksum and CC0/public-domain source-ledger validation;
- clean-room graphics manifest, atlas grid, PNG, PPM, checksum, and safe-path
  validation;
- game catalog checks through the authoritative `kilix-content` package;
- safe release-entry collection; and
- byte-reproducible tar+gzip and ZIP writers with normalized metadata.

Campaign schemas, gameplay code generation, release inventories, version
numbers, platform policy, and game-specific negative tests remain in each
game.

## Build and verify

```sh
make test
```

The project uses only Python’s standard library. Python 3.10 or newer is
required.

## Command line

Run from a checkout without installing:

```sh
PYTHONPATH=src python3 -m kilix_game_tools validate-audio \
  path/to/manifest.json path/to/source-provenance.json

PYTHONPATH=src python3 -m kilix_game_tools validate-graphics \
  path/to/assets/graphics/manifest.json
```

Catalog validation additionally needs the pinned `kilix-content/src`
directory on `PYTHONPATH`.

Games normally pin this repository under `third_party/kilix-game-tools` and
invoke it with:

```make
KILIX_GAME_TOOLS_PYTHONPATH := \
	$(abspath third_party/kilix-game-tools/src):\
	$(abspath third_party/kilix-content/src)
```

## Library use

Game-specific packaging scripts can import `ArchiveEntry`, `collect_entry`,
`write_tar_gz`, and `write_zip`. The helpers reject unsafe names, symlinks,
duplicate destinations, and unsupported modes. The game still supplies the
complete reviewed file inventory.

## License

MIT.
