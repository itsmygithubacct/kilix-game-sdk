#include "kilix_world.h"

#include <limits.h>
#include <string.h>

static bool grid_dimensions_valid(const kilix_world_grid *grid,
                                  size_t *cell_count)
{
    size_t width;
    size_t height;
    if (!grid || grid->width <= 0 || grid->height <= 0) return false;
    width = (size_t)grid->width;
    height = (size_t)grid->height;
    if (width > SIZE_MAX / height) return false;
    if (cell_count) *cell_count = width * height;
    return true;
}

kilix_world_result kilix_world_grid_init(
    kilix_world_grid *grid, int32_t width, int32_t height, void *context,
    kilix_world_walkable_fn walkable,
    kilix_world_move_cost_fn move_cost,
    kilix_world_opaque_fn opaque)
{
    kilix_world_grid candidate;
    size_t ignored;
    if (!grid) return KILIX_WORLD_INVALID_ARGUMENT;
    candidate.width = width;
    candidate.height = height;
    candidate.context = context;
    candidate.walkable = walkable;
    candidate.move_cost = move_cost;
    candidate.opaque = opaque;
    if (!grid_dimensions_valid(&candidate, &ignored))
        return KILIX_WORLD_INVALID_ARGUMENT;
    *grid = candidate;
    return KILIX_WORLD_OK;
}

bool kilix_world_in_bounds(const kilix_world_grid *grid,
                           kilix_world_cell cell)
{
    return grid_dimensions_valid(grid, NULL) &&
           cell.x >= 0 && cell.x < grid->width &&
           cell.y >= 0 && cell.y < grid->height;
}

kilix_world_result kilix_world_cell_index(
    const kilix_world_grid *grid, kilix_world_cell cell, size_t *index)
{
    if (!index || !grid_dimensions_valid(grid, NULL))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!kilix_world_in_bounds(grid, cell)) return KILIX_WORLD_OUT_OF_BOUNDS;
    *index = (size_t)cell.y * (size_t)grid->width + (size_t)cell.x;
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_index_cell(
    const kilix_world_grid *grid, size_t index, kilix_world_cell *cell)
{
    size_t count;
    if (!cell || !grid_dimensions_valid(grid, &count))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (index >= count) return KILIX_WORLD_OUT_OF_BOUNDS;
    cell->x = (int32_t)(index % (size_t)grid->width);
    cell->y = (int32_t)(index / (size_t)grid->width);
    return KILIX_WORLD_OK;
}

bool kilix_world_cell_walkable(const kilix_world_grid *grid,
                               kilix_world_cell cell)
{
    return kilix_world_in_bounds(grid, cell) &&
           (!grid->walkable || grid->walkable(grid->context, cell));
}

uint16_t kilix_world_cell_move_cost(const kilix_world_grid *grid,
                                    kilix_world_cell from,
                                    kilix_world_cell to)
{
    if (!kilix_world_in_bounds(grid, from) ||
        !kilix_world_cell_walkable(grid, to)) return 0u;
    return grid->move_cost ? grid->move_cost(grid->context, from, to) :
           UINT16_C(1);
}

bool kilix_world_cell_opaque(const kilix_world_grid *grid,
                             kilix_world_cell cell)
{
    return !kilix_world_in_bounds(grid, cell) ||
           (grid->opaque && grid->opaque(grid->context, cell));
}

size_t kilix_world_neighbors4(const kilix_world_grid *grid,
                              kilix_world_cell cell,
                              kilix_world_cell neighbors[4])
{
    static const int32_t offsets[4][2] = {
        {0, -1}, {-1, 0}, {1, 0}, {0, 1}
    };
    size_t count = 0u;
    size_t index;
    if (!neighbors || !kilix_world_in_bounds(grid, cell)) return 0u;
    for (index = 0u; index < 4u; ++index) {
        kilix_world_cell candidate = {
            cell.x + offsets[index][0], cell.y + offsets[index][1]
        };
        if (kilix_world_in_bounds(grid, candidate))
            neighbors[count++] = candidate;
    }
    return count;
}

kilix_world_result kilix_world_search_bind(
    kilix_world_search *search, uint32_t *heap, size_t *heap_positions,
    uint32_t *distance, size_t *previous, uint8_t *closed,
    size_t cell_capacity)
{
    if (!search || cell_capacity == 0u || !heap || !heap_positions ||
        !distance || !previous || !closed)
        return KILIX_WORLD_INVALID_ARGUMENT;
    search->heap = heap;
    search->heap_positions = heap_positions;
    search->distance = distance;
    search->previous = previous;
    search->closed = closed;
    search->cell_capacity = cell_capacity;
    search->heap_size = 0u;
    return KILIX_WORLD_OK;
}

static void reset_search(kilix_world_search *search, size_t count)
{
    size_t index;
    search->heap_size = 0u;
    for (index = 0u; index < count; ++index) {
        search->heap_positions[index] = KILIX_WORLD_NO_INDEX;
        search->distance[index] = UINT32_MAX;
        search->previous[index] = KILIX_WORLD_NO_INDEX;
        search->closed[index] = 0u;
    }
}

static uint32_t manhattan(const kilix_world_grid *grid, size_t index,
                          size_t goal)
{
    kilix_world_cell left;
    kilix_world_cell right;
    uint32_t dx;
    uint32_t dy;
    (void)kilix_world_index_cell(grid, index, &left);
    (void)kilix_world_index_cell(grid, goal, &right);
    dx = (uint32_t)(left.x > right.x ? left.x - right.x :
                    right.x - left.x);
    dy = (uint32_t)(left.y > right.y ? left.y - right.y :
                    right.y - left.y);
    return dx > UINT32_MAX - dy ? UINT32_MAX : dx + dy;
}

static uint32_t priority(const kilix_world_grid *grid,
                         const kilix_world_search *search, size_t index,
                         size_t goal)
{
    uint32_t distance = search->distance[index];
    uint32_t heuristic = goal == KILIX_WORLD_NO_INDEX ?
                         0u : manhattan(grid, index, goal);
    return distance > UINT32_MAX - heuristic ?
           UINT32_MAX : distance + heuristic;
}

static bool heap_before(const kilix_world_grid *grid,
                        const kilix_world_search *search, size_t left,
                        size_t right, size_t goal)
{
    uint32_t left_priority = priority(grid, search, left, goal);
    uint32_t right_priority = priority(grid, search, right, goal);
    return left_priority < right_priority ||
           (left_priority == right_priority &&
            (search->distance[left] < search->distance[right] ||
             (search->distance[left] == search->distance[right] &&
              left < right)));
}

static void heap_swap(kilix_world_search *search, size_t left, size_t right)
{
    uint32_t temporary = search->heap[left];
    search->heap[left] = search->heap[right];
    search->heap[right] = temporary;
    search->heap_positions[search->heap[left]] = left;
    search->heap_positions[search->heap[right]] = right;
}

static void heap_up(const kilix_world_grid *grid, kilix_world_search *search,
                    size_t position, size_t goal)
{
    while (position > 0u) {
        size_t parent = (position - 1u) / 2u;
        if (!heap_before(grid, search, search->heap[position],
                         search->heap[parent], goal)) break;
        heap_swap(search, position, parent);
        position = parent;
    }
}

static void heap_down(const kilix_world_grid *grid,
                      kilix_world_search *search, size_t position,
                      size_t goal)
{
    for (;;) {
        size_t left = position * 2u + 1u;
        size_t right = left + 1u;
        size_t selected = position;
        if (left < search->heap_size &&
            heap_before(grid, search, search->heap[left],
                        search->heap[selected], goal))
            selected = left;
        if (right < search->heap_size &&
            heap_before(grid, search, search->heap[right],
                        search->heap[selected], goal))
            selected = right;
        if (selected == position) return;
        heap_swap(search, position, selected);
        position = selected;
    }
}

static bool heap_push_or_update(const kilix_world_grid *grid,
                                kilix_world_search *search, size_t index,
                                size_t goal)
{
    size_t position = search->heap_positions[index];
    if (position != KILIX_WORLD_NO_INDEX) {
        heap_up(grid, search, position, goal);
        return true;
    }
    if (search->heap_size >= search->cell_capacity) return false;
    position = search->heap_size++;
    search->heap[position] = (uint32_t)index;
    search->heap_positions[index] = position;
    heap_up(grid, search, position, goal);
    return true;
}

static size_t heap_pop(const kilix_world_grid *grid,
                       kilix_world_search *search, size_t goal)
{
    size_t result = search->heap[0];
    --search->heap_size;
    search->heap_positions[result] = KILIX_WORLD_NO_INDEX;
    if (search->heap_size != 0u) {
        search->heap[0] = search->heap[search->heap_size];
        search->heap_positions[search->heap[0]] = 0u;
        heap_down(grid, search, 0u, goal);
    }
    return result;
}

static kilix_world_result prepare_search(
    const kilix_world_grid *grid, kilix_world_cell start,
    kilix_world_search *search, size_t *count, size_t *start_index)
{
    if (!search || !count || !start_index ||
        !grid_dimensions_valid(grid, count) || *count > UINT32_MAX ||
        search->cell_capacity < *count || !search->heap ||
        !search->heap_positions || !search->distance ||
        !search->previous || !search->closed)
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (kilix_world_cell_index(grid, start, start_index) != KILIX_WORLD_OK)
        return KILIX_WORLD_OUT_OF_BOUNDS;
    reset_search(search, *count);
    search->distance[*start_index] = 0u;
    if (!heap_push_or_update(grid, search, *start_index,
                             KILIX_WORLD_NO_INDEX))
        return KILIX_WORLD_NO_SPACE;
    return KILIX_WORLD_OK;
}

static kilix_world_result relax_neighbors(
    const kilix_world_grid *grid, kilix_world_search *search,
    size_t current_index, size_t goal)
{
    kilix_world_cell current;
    kilix_world_cell neighbors[4];
    size_t neighbor_count;
    size_t neighbor_position;
    (void)kilix_world_index_cell(grid, current_index, &current);
    neighbor_count = kilix_world_neighbors4(grid, current, neighbors);
    for (neighbor_position = 0u; neighbor_position < neighbor_count;
         ++neighbor_position) {
        kilix_world_cell neighbor = neighbors[neighbor_position];
        size_t neighbor_index;
        uint16_t cost = kilix_world_cell_move_cost(grid, current, neighbor);
        uint32_t candidate;
        if (cost == 0u) continue;
        (void)kilix_world_cell_index(grid, neighbor, &neighbor_index);
        if (search->closed[neighbor_index]) continue;
        if (search->distance[current_index] >
            UINT32_MAX - (uint32_t)cost) continue;
        candidate = search->distance[current_index] + (uint32_t)cost;
        if (candidate >= search->distance[neighbor_index]) continue;
        search->distance[neighbor_index] = candidate;
        search->previous[neighbor_index] = current_index;
        if (!heap_push_or_update(grid, search, neighbor_index, goal))
            return KILIX_WORLD_NO_SPACE;
    }
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_find_path(
    const kilix_world_grid *grid, kilix_world_cell start,
    kilix_world_cell goal, kilix_world_search *search,
    kilix_world_cell *path, size_t path_capacity, size_t *path_count,
    uint32_t *total_cost)
{
    size_t count;
    size_t start_index;
    size_t goal_index;
    size_t cursor;
    size_t required = 0u;
    kilix_world_result result;
    if (!path_count || (path_capacity != 0u && !path))
        return KILIX_WORLD_INVALID_ARGUMENT;
    *path_count = 0u;
    result = prepare_search(grid, start, search, &count, &start_index);
    if (result != KILIX_WORLD_OK) return result;
    if (kilix_world_cell_index(grid, goal, &goal_index) != KILIX_WORLD_OK)
        return KILIX_WORLD_OUT_OF_BOUNDS;
    if (goal_index != start_index && !kilix_world_cell_walkable(grid, goal))
        return KILIX_WORLD_BLOCKED;
    search->heap_positions[start_index] = 0u;
    heap_up(grid, search, 0u, goal_index);
    while (search->heap_size != 0u) {
        size_t current = heap_pop(grid, search, goal_index);
        if (current == goal_index) break;
        search->closed[current] = 1u;
        result = relax_neighbors(grid, search, current, goal_index);
        if (result != KILIX_WORLD_OK) return result;
    }
    if (search->distance[goal_index] == UINT32_MAX)
        return KILIX_WORLD_NOT_FOUND;
    cursor = goal_index;
    for (;;) {
        ++required;
        if (cursor == start_index) break;
        if (search->previous[cursor] == KILIX_WORLD_NO_INDEX)
            return KILIX_WORLD_INVALID_MAP;
        cursor = search->previous[cursor];
        if (required > count) return KILIX_WORLD_INVALID_MAP;
    }
    *path_count = required;
    if (total_cost) *total_cost = search->distance[goal_index];
    if (path_capacity < required) return KILIX_WORLD_NO_SPACE;
    cursor = goal_index;
    while (required != 0u) {
        --required;
        (void)kilix_world_index_cell(grid, cursor, &path[required]);
        if (cursor == start_index) break;
        cursor = search->previous[cursor];
    }
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_reachable(
    const kilix_world_grid *grid, kilix_world_cell start,
    uint32_t max_cost, kilix_world_search *search,
    kilix_world_cell *cells, size_t cell_capacity, size_t *cell_count)
{
    size_t count;
    size_t start_index;
    size_t required = 0u;
    kilix_world_result result;
    if (!cell_count || (cell_capacity != 0u && !cells))
        return KILIX_WORLD_INVALID_ARGUMENT;
    *cell_count = 0u;
    result = prepare_search(grid, start, search, &count, &start_index);
    if (result != KILIX_WORLD_OK) return result;
    while (search->heap_size != 0u) {
        size_t current = heap_pop(grid, search, KILIX_WORLD_NO_INDEX);
        if (search->distance[current] > max_cost) break;
        search->closed[current] = 1u;
        if (required < cell_capacity)
            (void)kilix_world_index_cell(grid, current, &cells[required]);
        ++required;
        result = relax_neighbors(grid, search, current,
                                 KILIX_WORLD_NO_INDEX);
        if (result != KILIX_WORLD_OK) return result;
    }
    *cell_count = required;
    return required > cell_capacity ? KILIX_WORLD_NO_SPACE :
           KILIX_WORLD_OK;
}

kilix_world_result kilix_world_line_of_sight(
    const kilix_world_grid *grid, kilix_world_cell from,
    kilix_world_cell to, bool opaque_goal_blocks, bool *visible)
{
    int32_t x;
    int32_t y;
    int64_t dx;
    int64_t dy;
    int32_t sx;
    int32_t sy;
    int64_t error;
    if (!visible || !grid_dimensions_valid(grid, NULL))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!kilix_world_in_bounds(grid, from) ||
        !kilix_world_in_bounds(grid, to))
        return KILIX_WORLD_OUT_OF_BOUNDS;
    *visible = true;
    x = from.x;
    y = from.y;
    dx = from.x < to.x ? to.x - from.x : from.x - to.x;
    dy = from.y < to.y ? to.y - from.y : from.y - to.y;
    sx = from.x < to.x ? 1 : -1;
    sy = from.y < to.y ? 1 : -1;
    error = dx - dy;
    while (x != to.x || y != to.y) {
        int64_t doubled = error * 2;
        kilix_world_cell current;
        if (doubled >= -dy) {
            error -= dy;
            x += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y += sy;
        }
        current.x = x;
        current.y = y;
        if (current.x == to.x && current.y == to.y &&
            !opaque_goal_blocks)
            break;
        if (kilix_world_cell_opaque(grid, current)) {
            *visible = false;
            break;
        }
    }
    return KILIX_WORLD_OK;
}

static uint32_t cell_distance(kilix_world_cell left, kilix_world_cell right)
{
    uint32_t dx = (uint32_t)(left.x > right.x ? left.x - right.x :
                             right.x - left.x);
    uint32_t dy = (uint32_t)(left.y > right.y ? left.y - right.y :
                             right.y - left.y);
    return dx > UINT32_MAX - dy ? UINT32_MAX : dx + dy;
}

const kilix_world_region *kilix_world_region_at(
    const kilix_world_map *map, kilix_world_cell cell)
{
    const kilix_world_region *selected = NULL;
    size_t index;
    if (!map || (map->region_count != 0u && !map->regions) ||
        !kilix_world_in_bounds(&map->grid, cell)) return NULL;
    for (index = 0u; index < map->region_count; ++index) {
        const kilix_world_region *region = &map->regions[index];
        if (region->width <= 0 || region->height <= 0 ||
            cell.x < region->x || cell.y < region->y ||
            cell.x - region->x >= region->width ||
            cell.y - region->y >= region->height)
            continue;
        if (!selected || region->priority > selected->priority)
            selected = region;
    }
    return selected;
}

const kilix_world_portal *kilix_world_portal_at(
    const kilix_world_map *map, kilix_world_cell cell)
{
    size_t index;
    if (!map || (map->portal_count != 0u && !map->portals)) return NULL;
    for (index = 0u; index < map->portal_count; ++index)
        if (map->portals[index].cell.x == cell.x &&
            map->portals[index].cell.y == cell.y)
            return &map->portals[index];
    return NULL;
}

const kilix_world_object *kilix_world_interaction_at(
    const kilix_world_map *map, kilix_world_cell origin,
    uint32_t interaction_mask, uint32_t maximum_distance)
{
    const kilix_world_object *selected = NULL;
    uint32_t selected_distance = UINT32_MAX;
    size_t index;
    if (!map || interaction_mask == 0u ||
        (map->object_count != 0u && !map->objects) ||
        !kilix_world_in_bounds(&map->grid, origin)) return NULL;
    for (index = 0u; index < map->object_count; ++index) {
        const kilix_world_object *object = &map->objects[index];
        uint32_t distance;
        if ((object->interaction_mask & interaction_mask) == 0u) continue;
        distance = cell_distance(origin, object->cell);
        if (distance > maximum_distance) continue;
        if (!selected || distance < selected_distance ||
            (distance == selected_distance &&
             object->priority > selected->priority)) {
            selected = object;
            selected_distance = distance;
        }
    }
    return selected;
}

const kilix_world_map *kilix_world_find_map(
    const kilix_world_catalog *catalog, uint32_t map_id)
{
    size_t index;
    if (!catalog || (catalog->map_count != 0u && !catalog->maps))
        return NULL;
    for (index = 0u; index < catalog->map_count; ++index)
        if (catalog->maps[index].id == map_id) return &catalog->maps[index];
    return NULL;
}

const kilix_world_portal *kilix_world_find_portal(
    const kilix_world_map *map, uint32_t portal_id)
{
    size_t index;
    if (!map || (map->portal_count != 0u && !map->portals)) return NULL;
    for (index = 0u; index < map->portal_count; ++index)
        if (map->portals[index].id == portal_id)
            return &map->portals[index];
    return NULL;
}

kilix_world_result kilix_world_catalog_validate(
    const kilix_world_catalog *catalog)
{
    size_t map_index;
    if (!catalog || !catalog->maps || catalog->map_count == 0u)
        return KILIX_WORLD_INVALID_ARGUMENT;
    for (map_index = 0u; map_index < catalog->map_count; ++map_index) {
        const kilix_world_map *map = &catalog->maps[map_index];
        size_t previous_map;
        size_t index;
        if (!grid_dimensions_valid(&map->grid, NULL) ||
            (map->region_count != 0u && !map->regions) ||
            (map->portal_count != 0u && !map->portals) ||
            (map->object_count != 0u && !map->objects))
            return KILIX_WORLD_INVALID_MAP;
        for (previous_map = 0u; previous_map < map_index; ++previous_map)
            if (catalog->maps[previous_map].id == map->id)
                return KILIX_WORLD_INVALID_MAP;
        for (index = 0u; index < map->region_count; ++index) {
            const kilix_world_region *region = &map->regions[index];
            size_t previous;
            if (region->width <= 0 || region->height <= 0 ||
                region->x < 0 || region->y < 0 ||
                region->x > map->grid.width - region->width ||
                region->y > map->grid.height - region->height)
                return KILIX_WORLD_INVALID_MAP;
            for (previous = 0u; previous < index; ++previous)
                if (map->regions[previous].id == region->id)
                    return KILIX_WORLD_INVALID_MAP;
        }
        for (index = 0u; index < map->object_count; ++index) {
            size_t previous;
            if (!kilix_world_in_bounds(&map->grid, map->objects[index].cell))
                return KILIX_WORLD_INVALID_MAP;
            for (previous = 0u; previous < index; ++previous)
                if (map->objects[previous].id == map->objects[index].id)
                    return KILIX_WORLD_INVALID_MAP;
        }
        for (index = 0u; index < map->portal_count; ++index) {
            const kilix_world_portal *portal = &map->portals[index];
            const kilix_world_map *target_map;
            const kilix_world_portal *target_portal;
            size_t previous;
            if (!kilix_world_in_bounds(&map->grid, portal->cell))
                return KILIX_WORLD_INVALID_MAP;
            for (previous = 0u; previous < index; ++previous)
                if (map->portals[previous].id == portal->id)
                    return KILIX_WORLD_INVALID_MAP;
            target_map = kilix_world_find_map(catalog, portal->target_map);
            target_portal = kilix_world_find_portal(
                target_map, portal->target_portal);
            if (!target_map || !target_portal ||
                target_portal->target_map != map->id ||
                target_portal->target_portal != portal->id)
                return KILIX_WORLD_INVALID_MAP;
        }
    }
    return KILIX_WORLD_OK;
}

const char *kilix_world_result_name(kilix_world_result result)
{
    switch (result) {
    case KILIX_WORLD_OK: return "ok";
    case KILIX_WORLD_INVALID_ARGUMENT: return "invalid argument";
    case KILIX_WORLD_OUT_OF_BOUNDS: return "cell out of bounds";
    case KILIX_WORLD_BLOCKED: return "destination blocked";
    case KILIX_WORLD_NOT_FOUND: return "route not found";
    case KILIX_WORLD_NO_SPACE: return "output or scratch buffer too small";
    case KILIX_WORLD_OVERFLOW: return "world arithmetic overflow";
    case KILIX_WORLD_INVALID_MAP: return "invalid world map";
    default: return "unknown world result";
    }
}
