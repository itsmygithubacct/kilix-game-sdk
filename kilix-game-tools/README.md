# kilix-game-tools

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

`kilix-game-tools` centralizes the Python validation and deterministic archive
primitives shared by Kilix games. It is an authoring/build dependency, never a
runtime dependency.

The package provides:

- generated WAV checksum and CC0/public-domain source-ledger validation;
- clean-room graphics manifest, atlas grid, PNG, PPM, checksum, and safe-path
  validation;
- game catalog checks through the authoritative `kilix-content` package;
- safe release-entry collection; and
- byte-reproducible, streaming tar+gzip and ZIP writers with normalized
  metadata and atomic output replacement.

Campaign schemas, gameplay code generation, release inventories, version
numbers, platform policy, and game-specific negative tests remain in each
game.

## Build and verify

```sh
make test
make release-gate
```

The project uses only Python’s standard library. Python 3.10 or newer is
required at runtime. The release gate additionally uses `coverage.py` and the
standard `pip`/setuptools build tooling available in the SDK build image. It
runs the unit and CLI suites, enforces at least 90% package coverage, builds
and imports a wheel from a clean staged source tree, and runs stable archive
and PPM workloads.

## Command line

Run from a checkout without installing:

```sh
PYTHONPATH=src python3 -m kilix_game_tools validate-audio \
  path/to/manifest.json path/to/source-provenance.json

PYTHONPATH=src python3 -m kilix_game_tools validate-graphics \
  path/to/assets/graphics/manifest.json

PYTHONPATH=src python3 -m kilix_game_tools --version
```

Catalog validation additionally needs the pinned `kilix-content/src`
directory on `PYTHONPATH`.

Games normally pin the SDK and invoke this component with:

```make
KILIX_GAME_SDK_DIR ?= third_party/kilix-game-sdk
KILIX_GAME_TOOLS_PYTHONPATH := \
	$(abspath $(KILIX_GAME_SDK_DIR)/kilix-game-tools/src):\
	$(abspath third_party/kilix-content/src)
```

## Library use

Game-specific packaging scripts can import `ArchiveEntry`, `collect_entry`,
`write_tar_gz`, and `write_zip`. The helpers reject unsafe names, symlinks,
duplicate destinations, and unsupported modes. The game still supplies the
complete reviewed file inventory. Inputs are streamed from regular-file
descriptors, and each writer flushes a uniquely named same-directory temporary
file before replacing the requested output. Archive bytes are reproducible for
the same file inventory, content, destination names, modes, Python/zlib
version, and helper version.

## Validation boundaries

- JSON inputs are UTF-8 objects no larger than 16 MiB. Duplicate members,
  non-finite numbers, and excessive nesting are rejected.
- Logical paths must be canonical portable relative paths: no empty, dot, or
  parent segments, backslashes, colons, or ASCII control characters.
  Filesystem inputs must be regular files rather than symlinks.
- PNG inputs and decoded rasters are each limited to 512 MiB. Validation
  streams every chunk and checks the signature, IHDR and palette rules, chunk
  order and CRCs, zlib termination, decoded scanline size, and filter bytes.
- P6 PPM headers are limited to 64 KiB and decoded payloads to 512 MiB. The
  parser reads only the header and verifies the regular file’s exact payload
  size.
- Audio artifact paths and their logical source paths must be unique and
  canonical. Source ledgers require empty `missing` and `invalid` lists plus a
  CC0/public-domain collection backed by an HTTPS page or a safe owning
  `provenance.json` reference.

## License

MIT.
