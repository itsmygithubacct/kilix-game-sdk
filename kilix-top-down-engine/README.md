# kilix-top-down-engine

`kilix-top-down-engine` is a reusable C11 orthographic renderer for Kilix
games. It provides deterministic logical viewport fitting, frame-derived
camera shake, an owned software framebuffer, world-space primitives, and
straight-alpha RGBA pixel-art drawing. It does not own game rules or terminal
presentation.

The project produces two static archives:

- `libkilix-top-down-core.a`: dependency-free view transforms and camera shake.
- `libkilix-top-down-soft.a`: framebuffer and drawing support over a
  caller-linked `soft-raster` implementation.

Chumrunner, Kilix Fantasy, Pleb-Bound, and other games can share this renderer
while keeping their own maps, actors, combat, UI, and visual style.

## Build and verify

```sh
git submodule update --init --recursive
make
make test
make sanitize
make test-deps
make test-integration
make test-headers
make test-install
make test-clang
make benchmark
make release-gate
```

The runtime needs a C11 compiler, Make, `ar`, and the pinned `soft-raster`
checkout. Drawing is terminal-independent and performs no allocation after a
renderer has been initialized or resized.

The headless example writes a deterministic PPM:

```sh
./build/headless-viewer build/top-down.ppm
```

## Use from a Kilix game

A game normally checks out both shared projects directly:

```text
third_party/
  kilix-game-kit/
  kilix-top-down-engine/
```

Include game-kit first. Its `SOFT_RASTER_DIR` then causes the top-down adapter
to compile against the same pinned rasterizer that game-kit archives, avoiding
duplicate implementations:

```make
KILIX_GAME_KIT_DIR ?= third_party/kilix-game-kit
KILIX_GAME_KIT_ROOT := $(abspath $(KILIX_GAME_KIT_DIR))
include $(KILIX_GAME_KIT_DIR)/mk/game-kit.mk

KILIX_TOP_DOWN_DIR ?= third_party/kilix-top-down-engine
include $(KILIX_TOP_DOWN_DIR)/mk/kilix-top-down.mk

CPPFLAGS += $(KILIX_GAME_KIT_CPPFLAGS) $(KILIX_TD_CPPFLAGS)

game: $(GAME_OBJECTS) $(KILIX_TD_LIBS) $(KILIX_GAME_KIT_LIB)
	$(CC) -o $@ $(GAME_OBJECTS) $(KILIX_TD_LIBS) \
		$(KILIX_GAME_KIT_LIB) $(KILIX_GAME_KIT_LDLIBS)
```

The core fit API deliberately accepts separate fit and alignment rectangles.
For example, a HUD can be excluded while choosing scale, while a small bottom
gutter remains available during vertical centering. `KI_TD_SCALE_PIXEL_ART`
prefers an integer scale at normal sizes and becomes fractional below a chosen
threshold. A minimum scale can preserve readable pixels by cropping on very
small displays.

The soft adapter accepts straight RGBA8 data—the format emitted by
`kilix-assets`. Alpha values below 8 are transparent, and nearest resizing plus
quarter-turn rotation preserve pixel-art edges. Borrowed atlas subregions,
nine-slice panels, row-major tile batches, and caller-buffered stable sprite
layer ordering all render without allocating. The adapter intentionally does
not reinterpret these bytes as `soft-raster`'s premultiplied canvas format.

The view API also provides checked screen-to-logical inversion and clipped
visible-cell bounds. Games can use those helpers with `kilix-world` for
pointer picking and tile culling while keeping navigation and map semantics
outside the renderer.

See [integration.md](docs/integration.md) for lifecycle, resize, draw order,
and ownership guidance.

## Ownership boundary

The engine owns:

- canvas and packed RGBA framebuffer lifetime;
- logical-to-screen fitting, scaling, origin, and presentation offsets;
- deterministic camera-shake offsets;
- transformed rectangles, circles, ellipses, and lines;
- straight-alpha logical, screen-space, resized, rotated, and integer-scale
  sprite paths;
- borrowed atlas subregions, nine-slice drawing, tile batches, and stable
  caller-buffered sprite ordering.

The game owns:

- simulation state and random number generation;
- tile meanings, collision, rooms, quests, actors, and draw-pass ordering;
- art catalogs, animation policy, semantic layer assignment, maps, dialogue,
  and screens;
- Kitty terminal lifecycle, `kitty-input`, audio, and saves.

That boundary keeps presentation deterministic without forcing different
games into one data model.

## Install

```sh
make DESTDIR="$staging" PREFIX=/opt/kilix install
```

Install `soft-raster` beneath the same prefix and use:

```sh
pkg-config --cflags --libs --static kilix-top-down-engine
```

## License

MIT.
