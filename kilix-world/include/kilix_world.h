#ifndef KILIX_WORLD_H
#define KILIX_WORLD_H

/*
 * Projection-independent grid, navigation, visibility, region, portal, and
 * interaction queries. All maps and workspaces are caller-owned.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILIX_WORLD_VERSION_MAJOR 0
#define KILIX_WORLD_VERSION_MINOR 3
#define KILIX_WORLD_VERSION_PATCH 0
#define KILIX_WORLD_NO_INDEX SIZE_MAX

typedef enum kilix_world_result {
    KILIX_WORLD_OK = 0,
    KILIX_WORLD_INVALID_ARGUMENT = 1,
    KILIX_WORLD_OUT_OF_BOUNDS = 2,
    KILIX_WORLD_BLOCKED = 3,
    KILIX_WORLD_NOT_FOUND = 4,
    KILIX_WORLD_NO_SPACE = 5,
    KILIX_WORLD_OVERFLOW = 6,
    KILIX_WORLD_INVALID_MAP = 7
} kilix_world_result;

typedef struct kilix_world_cell {
    int32_t x;
    int32_t y;
} kilix_world_cell;

typedef bool (*kilix_world_walkable_fn)(void *context,
                                        kilix_world_cell cell);
typedef uint16_t (*kilix_world_move_cost_fn)(void *context,
                                             kilix_world_cell from,
                                             kilix_world_cell to);
typedef bool (*kilix_world_opaque_fn)(void *context,
                                      kilix_world_cell cell);

typedef struct kilix_world_grid {
    int32_t width;
    int32_t height;
    void *context;
    kilix_world_walkable_fn walkable;
    kilix_world_move_cost_fn move_cost;
    kilix_world_opaque_fn opaque;
} kilix_world_grid;

/*
 * Grid callbacks are borrowed and may be omitted. They and their context
 * must remain alive, and their answers must remain stable, for the duration
 * of a query. A movement cost of zero makes that directed step unavailable.
 */

kilix_world_result kilix_world_grid_init(
    kilix_world_grid *grid, int32_t width, int32_t height, void *context,
    kilix_world_walkable_fn walkable,
    kilix_world_move_cost_fn move_cost,
    kilix_world_opaque_fn opaque);
bool kilix_world_in_bounds(const kilix_world_grid *grid,
                           kilix_world_cell cell);
kilix_world_result kilix_world_cell_index(
    const kilix_world_grid *grid, kilix_world_cell cell, size_t *index);
kilix_world_result kilix_world_index_cell(
    const kilix_world_grid *grid, size_t index, kilix_world_cell *cell);
bool kilix_world_cell_walkable(const kilix_world_grid *grid,
                               kilix_world_cell cell);
uint16_t kilix_world_cell_move_cost(const kilix_world_grid *grid,
                                    kilix_world_cell from,
                                    kilix_world_cell to);
bool kilix_world_cell_opaque(const kilix_world_grid *grid,
                             kilix_world_cell cell);
size_t kilix_world_neighbors4(const kilix_world_grid *grid,
                              kilix_world_cell cell,
                              kilix_world_cell neighbors[4]);

typedef struct kilix_world_search {
    uint32_t *heap;
    size_t *heap_positions;
    uint32_t *distance;
    size_t *previous;
    uint8_t *closed;
    size_t cell_capacity;
    size_t heap_size;
} kilix_world_search;

/*
 * Bind five aligned, mutually disjoint arrays and a disjoint search object.
 * Each array has cell_capacity elements of its declared type; capacity must
 * be in 1..UINT32_MAX. Failed binding leaves the search object unchanged.
 */
kilix_world_result kilix_world_search_bind(
    kilix_world_search *search, uint32_t *heap, size_t *heap_positions,
    uint32_t *distance, size_t *previous, uint8_t *closed,
    size_t cell_capacity);

/*
 * Find a deterministic minimum-cost cardinal path. Start and goal must both
 * be walkable. The returned path includes start and goal. Path storage,
 * path_count, and optional total_cost must be mutually disjoint and disjoint
 * from the search object and arrays. On NO_SPACE, path_count reports the
 * required cell count, total_cost is published when requested, and no path
 * cells are written. Every other failure leaves all three outputs unchanged.
 * A representable cost of UINT32_MAX is valid; a reachable frontier beyond
 * that range reports OVERFLOW when no representable route reaches the goal.
 */
kilix_world_result kilix_world_find_path(
    const kilix_world_grid *grid, kilix_world_cell start,
    kilix_world_cell goal, kilix_world_search *search,
    kilix_world_cell *path, size_t path_capacity, size_t *path_count,
    uint32_t *total_cost);

/*
 * Enumerate cells whose minimum cardinal movement cost is <= max_cost. The
 * start cell must be walkable. Results are ordered by cost, then row-major
 * cell index. Cell storage and cell_count must be disjoint from each other
 * and from the search object and arrays. On NO_SPACE, cell_count reports the
 * required count and no cells are written. Every other failure leaves both
 * outputs unchanged.
 */
kilix_world_result kilix_world_reachable(
    const kilix_world_grid *grid, kilix_world_cell start,
    uint32_t max_cost, kilix_world_search *search,
    kilix_world_cell *cells, size_t cell_capacity, size_t *cell_count);

/*
 * Direction-preserving Bresenham visibility. Tie-sensitive diagonal rays may
 * visit different cells when their endpoints are reversed; this established
 * behavior is retained for gameplay compatibility. The origin never blocks
 * its own ray. The goal blocks only when requested. Failure leaves visible
 * unchanged.
 */
kilix_world_result kilix_world_line_of_sight(
    const kilix_world_grid *grid, kilix_world_cell from,
    kilix_world_cell to, bool opaque_goal_blocks, bool *visible);

typedef struct kilix_world_region {
    uint32_t id;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t priority;
} kilix_world_region;

typedef struct kilix_world_portal {
    uint32_t id;
    kilix_world_cell cell;
    uint32_t target_map;
    uint32_t target_portal;
} kilix_world_portal;

typedef struct kilix_world_object {
    uint32_t id;
    kilix_world_cell cell;
    uint32_t interaction_mask;
    int32_t priority;
} kilix_world_object;

typedef struct kilix_world_map {
    uint32_t id;
    kilix_world_grid grid;
    const kilix_world_region *regions;
    size_t region_count;
    const kilix_world_portal *portals;
    size_t portal_count;
    const kilix_world_object *objects;
    size_t object_count;
} kilix_world_map;

/* Map records and their borrowed arrays must remain immutable during queries. */

const kilix_world_region *kilix_world_region_at(
    const kilix_world_map *map, kilix_world_cell cell);
const kilix_world_portal *kilix_world_portal_at(
    const kilix_world_map *map, kilix_world_cell cell);
const kilix_world_object *kilix_world_interaction_at(
    const kilix_world_map *map, kilix_world_cell origin,
    uint32_t interaction_mask, uint32_t maximum_distance);

typedef struct kilix_world_catalog {
    const kilix_world_map *maps;
    size_t map_count;
} kilix_world_catalog;

const kilix_world_map *kilix_world_find_map(
    const kilix_world_catalog *catalog, uint32_t map_id);
const kilix_world_portal *kilix_world_find_portal(
    const kilix_world_map *map, uint32_t portal_id);
kilix_world_result kilix_world_catalog_validate(
    const kilix_world_catalog *catalog);

const char *kilix_world_result_name(kilix_world_result result);

#ifdef __cplusplus
}
#endif

#endif
