# Changelog

## 0.4.0 — 2026-08-11

- Add `kilix_ui_draw_calendar`, a 7-column week grid with per-day mark
  bits, filler cells and an optional focus cursor. It performs no date
  arithmetic: which cell is today, which cells are filler, and what a
  mark means stay with the caller. The face is an `sr_font_id` on the new
  calendar struct rather than on `kilix_ui_style`, which stays a frozen
  ten-field layout.
- Add `kilix_ui_draw_meter_text` for caller-supplied bar text, and
  reimplement `kilix_ui_draw_meter` as that call with its `"%.0f/%.0f"`
  string, so existing meters render byte-identically.
- Add `kilix_ui_list_hit` and `kilix_ui_calendar_hit`, pure inverses of
  the corresponding draw layouts, so mouse hits agree with the pixels
  instead of each consumer re-deriving row geometry.

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
