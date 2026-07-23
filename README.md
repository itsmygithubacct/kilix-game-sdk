# kilix-ui

`kilix-ui` is the reusable, game-rule-free interface layer for Kilix games.
It builds on `kilix-top-down-engine` and provides:

- wrapping focus navigation with disabled-item skipping and paging;
- styled list menus with visible-range tracking;
- fallback or atlas-backed nine-slice panels;
- dialogue boxes with optional portraits and prompts;
- bounded meters and compact input-prompt rows.
- allocation-free RPG party, inventory, command, target, and shop composites.

Games own every label, semantic action, selection consequence, portrait
choice, statistic, and visual theme. The library only turns those inputs into
consistent navigation and drawing. All draw calls borrow their strings and
images and perform no allocation.

The RPG composites accept plain caller-owned view records. They do not mutate
inventory, spend currency, apply damage, choose targets, advance turns, or
interpret status names. This keeps genre rules in each game while eliminating
duplicated panel layout and focus presentation.

## Build and verify

```sh
git submodule update --init --recursive
make
make test
make sanitize
```

The recursive checkout pins `kilix-top-down-engine` and its raster dependency.
Set `KILIX_TOP_DOWN_DIR` only when deliberately sharing another checked-out
renderer. The reusable Make fragment forwards that renderer and its raster
root into the recursive UI build and tracks every public renderer header, so
the UI cannot silently compile against its nested fallback checkout.
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
layout. Standalone UI tests link the renderer's pinned rasterizer directly.

## License

MIT.
