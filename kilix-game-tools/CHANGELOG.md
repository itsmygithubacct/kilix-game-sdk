# Changelog

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
