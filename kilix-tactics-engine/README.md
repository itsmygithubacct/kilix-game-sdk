# kilix-tactics-engine

`kilix-tactics-engine` is a deterministic C11 library for isometric tactical
space: stacked cell grids, projection with rotation and cutaway, inverse
picking, pathfinding, line of sight, cover, and stable draw ordering. It is
shared by C-COM and Kilix Advanced Tactics the same way
`kilix-top-down-engine` is shared by Chumrunner, Kilix Fantasy, Legend of
Kilix, and Pleb-Bound.

It owns spatial mechanism. It does not own game rules.

The project produces two static archives:

- `libkilix-tactics-core.a` — standard-library-only types, map storage,
  projection, picking, navigation, sight, cover, and draw ordering.
- `libkilix-tactics-soft.a` — an adapter that draws a sorted queue into a
  caller-owned `sr_canvas`; it references but does not bundle `soft-raster`.

## Build and verify

```sh
make
make test
make sanitize
make test-clang
make test-headers
```

The runtime needs a C11 compiler, Make, and `ar`. The core archive has no
dependency beyond the standard library, so ordering and spatial logic are
testable headlessly. Only the soft adapter needs a `soft-raster` checkout,
supplied through `SOFT_RASTER_DIR`.

## Use from a Kilix game

Pin this repository under `third_party/` and include its Make fragment after
game-kit's, so game-kit's exported `SOFT_RASTER_DIR` selects the rasterizer
and the adapter never compiles a second copy:

```make
include third_party/kilix-game-kit/mk/game-kit.mk
include third_party/kilix-tactics-engine/mk/kilix-tactics.mk

CPPFLAGS += $(KILIX_GAME_KIT_CPPFLAGS) $(KILIX_TACTICS_CPPFLAGS)

game: $(GAME_OBJECTS) $(KILIX_TACTICS_LIBS) $(KILIX_GAME_KIT_LIB)
	$(CC) -o $@ $(GAME_OBJECTS) $(KILIX_TACTICS_LIBS) \
		$(KILIX_GAME_KIT_LIB) $(KILIX_GAME_KIT_LDLIBS)
```

## Coordinate and cell model

The grid is `width x height x levels`, with `levels >= 1`. One model serves
both a game with stacked floors and a game with a single elevated plane:

- C-COM instantiates `40 x 40 x 4`, uses all four wall and occupancy facts,
  and expresses height through the level index.
- Kilix Advanced Tactics instantiates `w x h x 1` and carries terrain height
  in each cell's elevation offset.

Projection is parameterised on the tile envelope and the vertical step:

```text
screen_x = origin_x + (x - y) * (tile_width  / 2)
screen_y = origin_y + (x + y) * (tile_height / 2) - height * level_step
```

C-COM initialises `(32, 16, 24)` and Kilix Advanced Tactics `(32, 16, 12)`.
Both games already share the identical 32x40 sprite cell over a 32x16 floor
diamond, so only the vertical step differs.

The two games number their quarter turns in opposite senses and both persist
that number, so the sense is explicit rather than assumed:
`KT_ROTATE_CCW` matches C-COM, `KT_ROTATE_CW` matches Kilix Advanced Tactics,
and the two are related by `r -> (4 - r) & 3`. World coordinates never
rotate; simulation, paths, sight, and saves stay in world space and only
presentation and its inverse use the remap.

## Ownership boundary

The engine owns:

- the stacked cell grid, its wall facts, and its cover facts;
- projection, inverse picking, rotation, zoom, and cutaway;
- the frozen direction-to-wall edge table and corner-cutting rules;
- A\* and the reachability flood, over a caller step-cost hook;
- the sight trace, within a level and across levels;
- directional cover reporting;
- stable painter draw ordering and the allocation-free blit path.

The game owns:

- time units, energy, and every cost that is not a pure step cost;
- what a part means — armour, destruction, doors, footsteps, light, smoke;
- fog of war and per-side visibility bookkeeping;
- unit occupancy, reaction fire, AI, targeting policy, and accuracy;
- art catalogues, animation policy, saves, content, and UI.

Concretely, the engine answers "is the west edge of this cell opaque", never
"can this soldier afford to walk here". Games express their rules through
`kt_nav_hooks.step_cost` and `kt_sight_hooks.veto`.

## Determinism

The engine consumes no random stream and holds no global mutable state. It
allocates nothing after a map, workspace, or queue has been initialised;
callers provide storage and the engine reports required capacities. Every
public entry point validates its arguments and returns a status.

Painter depth is total over distinct cells, so paint order does not depend on
the order a game submits its terrain. `kt_draw_queue_hash()` exposes an
order-sensitive hash of the sorted queue for golden tests that need to catch
draw-order drift without rendering anything.

## Frozen contracts

Two pieces are lifted verbatim from C-COM and must not drift, because that
game's rendering and pathfinding contracts depend on them:

- the projection, which at `(32, 16, 24)` reproduces
  `sx = (x - y) * 16; sy = (x + y) * 8 - z * 24` bit for bit;
- the direction-to-wall edge table, whose four-entry diagonal rows are
  exactly why corners cannot be cut.

`make test` asserts the first against both games' reference formulas across
their full grids, at every rotation and zoom.

See [docs/integration.md](docs/integration.md) for build wiring, storage and
heap sizing, the ownership seams, and how to reproduce an existing game's
behaviour bit-exactly when migrating onto the engine.

## License

MIT. See `LICENSE`.
