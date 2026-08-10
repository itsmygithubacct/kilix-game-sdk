# kilix-ui

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

`kilix-ui` is the reusable, game-rule-free interface layer for Kilix games.
It builds on `kilix-top-down-engine` and provides:

- wrapping focus navigation with disabled-item skipping and paging;
- styled list menus with visible-range tracking;
- fallback or atlas-backed nine-slice panels;
- dialogue boxes with optional portraits and prompts;
- bounded meters and compact input-prompt rows;
- allocation-free RPG party, inventory, command, target, and shop composites.

Games own every label, semantic action, selection consequence, portrait
choice, statistic, and visual theme. The library only turns those inputs into
consistent navigation and drawing. All draw calls borrow their strings and
images and perform no allocation. Content is intersected with the active
canvas clip, and each call restores the clip it received.

The RPG composites accept plain caller-owned view records. They do not mutate
inventory, spend currency, apply damage, choose targets, advance turns, or
interpret status names. This keeps genre rules in each game while eliminating
duplicated panel layout and focus presentation.

## Build and verify

```sh
make
make test
make sanitize
make test-clang
make benchmark
```

The ten native suites include exact primitive/composite frame hashes, a
128,000-step deterministic focus reference model, invalid geometry/view/style
transactions, nested clipping, small-skin fallback, bounded long-text and
offscreen-row equivalence, a C++ header check, randomized rendering safety,
and an allocation interposition check. The benchmark reports checksums beside
each timing so performance work cannot silently alter pixels or focus state.

The SDK supplies `kilix-top-down-engine` and game-kit’s raster dependency.
The reusable Make fragment forwards those shared roots into the UI build and
tracks every public renderer header, so the UI cannot silently compile against
a different renderer checkout.
Applications normally link in this order:

```text
game objects
libkilix-ui.a
libkilix-top-down-soft.a
libkilix-top-down-core.a
libkilix-game-kit.a
-lz -lpthread -lm
```

`kilix-game-kit` supplies the one `soft-raster` implementation in that common
layout. Standalone UI tests link that same SDK checkout directly.

## Drawing and ownership contract

Renderers must have an initialized canvas. Views require a finite positive
scale, and rectangles require positive dimensions with representable right
and bottom edges. Invalid contexts and geometry are no-ops. A valid nine-slice
that cannot fit its destination falls back to the theme panel and border.

A null style selects the default theme. Supplied styles are copied and never
mutated: padding is made nonnegative, row height is made positive, font scale
is clamped to 1–8, and panel alpha is made finite and clamped to 0–1.
Non-finite portrait alpha is a no-op; other portrait alpha is clamped to 0–1.

The caller owns every string, image, array, focus record, and style for the
duration of a call. Non-null counted arrays contain at least the stated number
of elements. Drawing visits only rows, dialogue lines, and glyphs that can
intersect the active clip, so hidden suffixes do not make frame time grow with
an unbounded menu or label. Visible pixels retain the established output.

## License

MIT.
