# Changelog

## 0.2.0

- Decode indexed PNGs at 1, 2, 4, and 8 bits and honor palette, grayscale,
  and RGB transparency while retaining straight-alpha RGBA8 output.
- Stream consecutive IDAT chunks directly into the bounded scanline buffer;
  validate PNG ordering, palette indices, zlib completion, and trailing data.
- Make regular-file reads descriptor-bound and reject changing or non-regular
  inputs without blocking on FIFOs.
- Parse strict UTF-8 and JSON escapes, reject duplicate runtime fields and
  identifiers, and accept both deployed version-1 bitmap record forms.
- Grow manifest storage geometrically and index cache entries by hash while
  preserving manifest order and returned-image pointer stability.
- Harden region/atlas validation, checker arithmetic and diagnostics,
  dependency tracking, caller build-flag composition, sanitizer isolation,
  and the unit/integration suite.

The 23-function public C surface and public structure layouts are unchanged
from 0.1.0. Stricter rejection of malformed inputs and the additional valid
PNG/manifest behavior motivate the minor-version change.

## 0.1.0

- Initial renderer-independent asset resolver, PNG/raw decoder, cache, atlas,
  animation clip, version-1 manifest parser, and pack checker.
