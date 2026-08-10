# kilix-world

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

`kilix-world` is a projection-independent C11 spatial core for Kilix games.
It provides immutable map records and allocation-free queries while leaving
rendering, game rules, mutable actor state, and content schemas to each game.

The initial API includes:

- bounded Cartesian grids with game-owned walkability, movement-cost, and
  opacity callbacks;
- checked cell/index conversion and deterministic cardinal neighbors;
- minimum-cost A* paths and cost-bounded reachable cells using caller-owned
  scratch buffers;
- Bresenham line-of-sight queries;
- prioritized rectangular regions;
- stable nearby interaction selection; and
- reciprocal cross-map portal validation.

The optional `libkilix-world-top-down.a` adapter converts cells to inset
logical rectangles, maps logical points back to cells, and converts paths to
cell-center points. It remains renderer-independent; callers can pass the
results to `kilix-top-down-engine` or another drawing backend.

The core has no renderer, terminal, audio, save, JSON, scripting, or operating
system dependency. Both `kilix-top-down-engine` and
`kilix-isometric-engine` can project its logical cells without making this
library depend on either renderer.

## Build and verify

```sh
make test
make sanitize
make test-clang
```

## Runtime ownership

Maps, region/portal/object tables, callbacks, search arrays, and output arrays
are all caller-owned. A dynamic game overlay can participate through the
walkability, movement-cost, and opacity callbacks without mutating compiled
map data.

Search memory is explicit. The search object and its five aligned arrays must
be mutually disjoint, and each array must contain the bound capacity:

```c
kilix_world_search_bind(
    &search, heap, positions, distances, previous, closed, CELL_COUNT);

kilix_world_find_path(
    &map.grid, actor, destination, &search,
    path, PATH_CAPACITY, &path_count, &movement_cost);
```

Path and reachable outputs must also be disjoint from the search workspace.
When an output array is too small, the query returns
`KILIX_WORLD_NO_SPACE`, reports the required count, and does not write a
partial array. Path queries also report the total cost when requested. Other
failures leave result arrays and scalar outputs unchanged. A path cost of
`UINT32_MAX` is valid; a route that cannot be represented reports
`KILIX_WORLD_OVERFLOW` rather than appearing disconnected.

Callbacks may reflect a dynamic overlay, but their answers must stay stable
for one query. The implementation may avoid repeated callback calls for cells
whose state it has already established. Searches mutate their bound scratch
arrays and therefore require external synchronization when a workspace is
shared. Independent grids and workspaces share no library state.

Line of sight retains the established direction-preserving Bresenham tie
behavior: a tie-sensitive diagonal ray may visit different cells when its
endpoints are reversed. This is gameplay-visible and is preserved for
compatibility. The origin never blocks itself; the caller chooses whether an
opaque goal blocks.

Bulk top-down conversions validate the full input before publishing any
element. Invalid cells and non-finite projected coordinates leave the output
and count unchanged. Input, output, and count storage must be disjoint.

No query allocates or performs I/O.

## Determinism and validation

Paths use minimum accumulated directed movement cost and deterministic
row-major tie breaking. Reachable cells are ordered by cost, then row-major
index, and the search does not evaluate steps beyond the requested cost
frontier. Catalog validation requires valid grids and record arrays, unique
map/region/object/portal IDs, in-bounds records, and reciprocal portal links.
Cycles and self-reciprocal portals remain valid.

## Scope boundary

The library does not define tile meanings, diagonal movement, character
occupancy, combat range, cover, quests, dialogue, encounters, random
generation, or draw ordering. Games compose those policies from the spatial
facts returned here.

Shared-library SONAME/version-link policy, pkg-config metadata, and uninstall
behavior are intentionally SDK-wide packaging decisions rather than local
world-model policy.

## License

MIT.
