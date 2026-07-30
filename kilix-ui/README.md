# kilix-ui

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

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
make
make test
make sanitize
```

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

## License

MIT.
