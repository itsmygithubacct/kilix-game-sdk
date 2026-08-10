# Changelog

## 0.3.0 — 2026-08-09

- Intersect list, dialogue, and composite content with the caller's active
  clip and restore that clip after every draw.
- Reject invalid renderers, non-positive/non-finite views, overflowing
  rectangles, and non-finite portrait alpha without drawing or invoking
  undefined numeric conversions.
- Normalize caller styles locally, clamp alpha/font scale, and keep caller
  records unchanged.
- Fall back to the colored panel when a valid nine-slice cannot fit its
  destination.
- Bound row, dialogue-line, formatted-string, prompt, and glyph work to visible
  output while preserving ordinary frame checksums exactly.
- Preserve caller build/link flags, track test dependencies, isolate Clang and
  sanitizer output, make standalone SDK-layout tests work without overrides,
  and add C++ and benchmark targets.
- Expand verification to ten continuing suites with exact golden hashes,
  randomized focus/reference and rendering checks, malformed-input
  transactions, nested clipping, long-text equivalence, and all-API allocation
  interposition.
