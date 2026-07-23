# kilix-ui

`kilix-ui` is the reusable, game-rule-free interface layer for Kilix games.
It builds on `kilix-top-down-engine` and provides:

- wrapping focus navigation with disabled-item skipping and paging;
- styled list menus with visible-range tracking;
- fallback or atlas-backed nine-slice panels;
- dialogue boxes with optional portraits and prompts;
- bounded meters and compact input-prompt rows.

Games own every label, semantic action, selection consequence, portrait
choice, statistic, and visual theme. The library only turns those inputs into
consistent navigation and drawing. All draw calls borrow their strings and
images and perform no allocation.

## Build and verify

```sh
git submodule update --init --recursive
make
make test
make sanitize
```

The recursive checkout pins `kilix-top-down-engine` and its raster dependency.
Set `KILIX_TOP_DOWN_DIR` only when deliberately sharing another checked-out
renderer. Applications normally link in this order:

```text
game objects
libkilix-ui.a
libkilix-top-down-soft.a
libkilix-top-down-core.a
libkilix-game-kit.a
-lz -lpthread -lm
```

`kilix-game-kit` supplies the one `soft-raster` implementation in that common
layout. Standalone UI tests link the renderer's pinned rasterizer directly.

## License

MIT.
