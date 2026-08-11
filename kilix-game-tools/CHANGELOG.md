# Changelog

## 0.3.0

- Add the `contact-sheet` command: composite generated art into one grid image
  so a set can be judged as a set. Consistency across a batch — one light
  direction, one scale, one treatment — is the property a per-file checksum
  cannot see and a reviewer cannot hold in their head across forty tabs.
- Box-average downscale rather than point sampling, so a reviewer judges the
  art instead of the aliasing; aspect preserved and centred, so a squashed
  plate is never reviewed as one; byte-identical output, so a sheet can be
  committed as a review record and a later one diffed against it.
- Accept 8- and 16-bit PNG here, unlike `validate-graphics`. Several image
  generators emit 16-bit, which runtimes refuse at load: that is art which
  exists and is not yet loadable, and it is exactly what a reviewer needs to
  see.

## 0.2.0

- Stream archive inputs and PPM validation instead of loading complete files.
- Write tar and ZIP output through unique, flushed temporary files before an
  atomic replacement.
- Validate complete PNG structure, checksums, compressed raster size, scanline
  filters, palette rules, and chunk ordering with explicit resource bounds.
- Reject duplicate JSON members, non-finite values, oversized manifests,
  non-canonical paths, duplicate audio artifacts, and malformed provenance.
- Add comprehensive boundary, CLI, install, coverage, and benchmark gates.

## 0.1.0

- Initial shared audio, graphics, catalog, and deterministic archive helpers.
