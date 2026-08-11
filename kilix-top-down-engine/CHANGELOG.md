# Changelog

## 0.3.3 - 2026-08-11

- Add `ki_td_soft_rgba_backdrop`, a full-canvas blit that samples per screen
  pixel instead of per logical cell, so a backdrop plate authored above the
  logical size keeps its detail and maps 1:1 at `scale = plate / logical`.
  `ki_td_soft_rgba_resized` samples once per logical cell, which is correct
  for sprites quantized to logical space and discards most of a large
  plate's columns when it fills the screen. Existing callers are unchanged.

## 0.3.2 - 2026-08-09

- Reject non-finite and unrepresentable transforms and geometry before they
  reach rasterizer float-to-integer conversions; clamp finite alpha above one.
- Preserve view and inverse-transform outputs transactionally on invalid input,
  and reject overlapping sprite-command and scratch buffers.
- Replace quadratic ordering for large sprite passes with an allocation-free
  O(n log n) path while retaining exact stable order and a linear sorted fast
  path.
- Cull screen-space images, logical sprites, resized sprites, and tile batches
  to the active clip without changing visible pixels.
- Isolate dependency builds from caller-selected build directories, compose
  caller flags safely, and repair recursive dependency-pin validation.
- Expand strict, sanitizer, randomized model, culling-equivalence, numeric
  boundary, and allocation-free test coverage.

## 0.1.0 - 2026-07-22

- Introduce a byte-stable orthographic renderer for Kilix games.
- Add configurable viewport fitting and deterministic camera shake.
- Add a `soft-raster` adapter for framebuffer lifecycle, primitives, and
  straight-alpha RGBA pixel art.
- Add color-modulated resized RGBA sprite blits.
- Add standalone, sanitizer, header, install, and game-kit integration gates.
