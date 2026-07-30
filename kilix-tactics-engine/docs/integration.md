# Integration

How a game consumes `kilix-tactics-engine`, and where the boundary sits.

## Build wiring

Pin the engine under `third_party/` and include its Make fragment after
game-kit's, so game-kit's exported `SOFT_RASTER_DIR` selects the rasterizer and
the soft adapter never compiles a second copy:

```make
include third_party/kilix-game-kit/mk/game-kit.mk
include third_party/kilix-tactics-engine/mk/kilix-tactics.mk

CPPFLAGS += $(KILIX_GAME_KIT_CPPFLAGS) $(KILIX_TACTICS_CPPFLAGS)

game: $(GAME_OBJECTS) $(KILIX_TACTICS_LIBS) $(KILIX_GAME_KIT_LIB)
	$(CC) -o $@ $(GAME_OBJECTS) $(KILIX_TACTICS_LIBS) \
		$(KILIX_GAME_KIT_LIB) $(KILIX_GAME_KIT_LDLIBS)
```

A game that does not use game-kit can compile the seven sources directly; the
core archive needs nothing beyond the standard library, and only
`src/render_soft.c` needs `soft-raster` headers.

Two practical notes, both learned from real consumers:

- A static archive must follow the objects that reference it on the link line.
  Test binaries that link a translation unit using the engine need the archives
  too, listed last.
- If your build derives dependency files with `$(OBJECTS:.o=.d)`, filter to
  `%.d`. Archive paths pass through that substitution untouched and `make` will
  try to `-include` an `.a` as a makefile.

## The boundary

The engine owns spatial **mechanism**. The game owns **meaning**.

| Engine | Game |
| --- | --- |
| cell grid, projection, rotation, cutaway, picking | what a tile *is* |
| the direction-to-wall edge table | time units, energy, stamina |
| A\* and the reachability flood | stairs, climb, doors, occupancy |
| the sight trace | fog of war, smoke, lighting, reaction fire |
| directional cover facts | accuracy, damage, morale |
| painter ordering, the blit path | art catalogues, animation, saves, UI |

Rules reach the engine through four seams, never by being moved into it:

- `kt_nav_hooks.step_cost` — what one step costs, and whether it changes level.
- `kt_sight_hooks.veto` — a per-cell stop the engine does not model.
- `kt_edge_tests()` — the frozen table, so a game can apply its own predicate.
- `kt_ray_cells()` — pure ray geometry, for rules that *accumulate* along a ray.

If you find yourself wanting to add a game concept to `kt_cell`, check first
whether a hook already carries it. `wall_cost` and `opacity_height` exist as
opaque caller data precisely so the engine does not have to understand them.

## Storage

Every structure takes caller-supplied storage and allocates nothing after
initialisation. Capacities are reported, not assumed:

```c
kt_cell   cells[W * H * D];
kt_map    map;
kt_map_init(&map, W, H, D, cells, W * H * D);
/* ... write compiled facts ... */
kt_map_validate(&map);          /* also measures elevation_span */

kt_nav_node nodes[W * H * D];
uint32_t    heap[/* see below */];
```

Heap sizing depends on the ordering discipline:

- `KT_NAV_ORDER_SEQUENCE` (default) pushes a lazy duplicate per improving
  relaxation, so it needs `kt_nav_required_heap(map)` slots — one per
  predecessor per cell. Sizing it to the cell count overflows and surfaces as a
  spurious "no route".
- `KT_NAV_ORDER_DECREASE_KEY` keeps one entry per node and needs only one slot
  per cell, plus a position index bound through
  `kt_nav_workspace_init_indexed()`.

## Reproducing an existing game's behaviour

The hard-won lesson from migrating two shipped games: **equal cost is not equal
route**, and almost every ordering choice is observable.

- **Rotation sense.** Games number quarter turns in opposite directions and
  persist the number. Pass `KT_ROTATE_CCW` or `KT_ROTATE_CW`; do not normalise.
- **Zoom.** If your renderer scales the *viewport* rather than the projection,
  leave `zoom_percent` at 100 and keep viewport scaling game-side. That keeps
  the transform exactly the identity.
- **A\* key.** If yours is not an integer sum — a float32 key, say — return its
  raw bit pattern from `hooks.heuristic` and reassemble it in `hooks.priority`.
  For non-negative float32 the `uint32` bit pattern orders identically to the
  float, so the comparison stays bit-exact.
- **Heap discipline.** Because no closed node is reopened, the first
  predecessor to relax a cell owns its route permanently, so tie order selects
  among equal-cost routes. Measured on a real consumer: the default ordering
  changed the chosen route on roughly 14 queries in 1000, always at equal cost.
- **Settle order.** `kt_nav_reachable_collect()` reports cells as they settle.
  If your AI consumes the sequence, compare sequences, not sets.
- **Depth order.** `KT_DEPTH_DIAGONAL_MAJOR` and `KT_DEPTH_LEVEL_MAJOR` invert
  for overlapping cross-level sprites. Match your terrain pass.

## Migrate with a differential harness

Do not swap an implementation and rely on the result looking right. Keep the
old one, run both, and compare — under an environment variable so the harness
ships harmlessly:

```c
n = engine_version(...);
if (parity_mode()) {
    ref_n = reference_version(...);   /* retained, not deleted */
    compare(n, ref_n);                /* element by element */
}
return n;
```

Compare the *output*, not a summary of it. A cost-only or count-only comparison
misses exactly the failure that matters. In practice this harness found a
divergence that five seeds of testing had already declared clean, and it only
appeared once the sample was widened to a different scenario generator.

## Determinism

The engine consumes no random stream and holds no global mutable state. Given
identical inputs it produces identical output, including the order of every
sequence it reports. `kt_draw_queue_hash()` exists so a game can catch
draw-order drift without rendering anything.
