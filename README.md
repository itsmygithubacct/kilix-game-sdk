# kilix-world

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

Search memory is explicit:

```c
kilix_world_search_bind(
    &search, heap, positions, distances, previous, closed, CELL_COUNT);

kilix_world_find_path(
    &map.grid, actor, destination, &search,
    path, PATH_CAPACITY, &path_count, &movement_cost);
```

When an output array is too small, path and reachable queries return
`KILIX_WORLD_NO_SPACE` and report the required count. No query allocates.

## Scope boundary

The library does not define tile meanings, diagonal movement, character
occupancy, combat range, cover, quests, dialogue, encounters, random
generation, or draw ordering. Games compose those policies from the spatial
facts returned here.

## License

MIT.
