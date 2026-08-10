#include "kilix_world.h"

#include <limits.h>
#include <string.h>

#define KILIX_WORLD_ID_LINEAR_LIMIT 16u
#define KILIX_WORLD_ID_HASH_LIMIT 256u
#define KILIX_WORLD_ID_HASH_CAPACITY 512u

typedef struct byte_range {
    uintptr_t begin;
    uintptr_t end;
} byte_range;

static bool byte_range_init(byte_range *range, const void *pointer,
                            size_t count, size_t element_size,
                            size_t alignment)
{
    size_t byte_count;
    uintptr_t begin;
    if (!range || element_size == 0u || alignment == 0u) return false;
    if (count == 0u) {
        range->begin = 0u;
        range->end = 0u;
        return true;
    }
    if (!pointer || (uintptr_t)pointer % alignment != 0u ||
        count > SIZE_MAX / element_size)
        return false;
    byte_count = count * element_size;
    begin = (uintptr_t)pointer;
    if (byte_count > UINTPTR_MAX - begin) return false;
    range->begin = begin;
    range->end = begin + byte_count;
    return true;
}

static bool byte_ranges_overlap(byte_range left, byte_range right)
{
    return left.begin < right.end && right.begin < left.end;
}

static bool byte_ranges_disjoint(const byte_range *ranges, size_t count)
{
    size_t left;
    for (left = 0u; left < count; ++left) {
        size_t right;
        for (right = left + 1u; right < count; ++right)
            if (byte_ranges_overlap(ranges[left], ranges[right]))
                return false;
    }
    return true;
}

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

static bool cell_in_bounds_unchecked(const kilix_world_grid *grid,
                                     kilix_world_cell cell)
{
    return cell.x >= 0 && cell.x < grid->width &&
           cell.y >= 0 && cell.y < grid->height;
}

static size_t cell_index_unchecked(const kilix_world_grid *grid,
                                   kilix_world_cell cell)
{
    return (size_t)cell.y * (size_t)grid->width + (size_t)cell.x;
}

static kilix_world_cell index_cell_unchecked(const kilix_world_grid *grid,
                                             size_t index)
{
    kilix_world_cell cell = {
        (int32_t)(index % (size_t)grid->width),
        (int32_t)(index / (size_t)grid->width)
    };
    return cell;
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
           cell_in_bounds_unchecked(grid, cell);
}

kilix_world_result kilix_world_cell_index(
    const kilix_world_grid *grid, kilix_world_cell cell, size_t *index)
{
    if (!index || !grid_dimensions_valid(grid, NULL))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!cell_in_bounds_unchecked(grid, cell))
        return KILIX_WORLD_OUT_OF_BOUNDS;
    *index = cell_index_unchecked(grid, cell);
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_index_cell(
    const kilix_world_grid *grid, size_t index, kilix_world_cell *cell)
{
    size_t count;
    if (!cell || !grid_dimensions_valid(grid, &count))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (index >= count) return KILIX_WORLD_OUT_OF_BOUNDS;
    *cell = index_cell_unchecked(grid, index);
    return KILIX_WORLD_OK;
}

bool kilix_world_cell_walkable(const kilix_world_grid *grid,
                               kilix_world_cell cell)
{
    return grid_dimensions_valid(grid, NULL) &&
           cell_in_bounds_unchecked(grid, cell) &&
           (!grid->walkable || grid->walkable(grid->context, cell));
}

uint16_t kilix_world_cell_move_cost(const kilix_world_grid *grid,
                                    kilix_world_cell from,
                                    kilix_world_cell to)
{
    if (!grid_dimensions_valid(grid, NULL) ||
        !cell_in_bounds_unchecked(grid, from) ||
        !cell_in_bounds_unchecked(grid, to) ||
        (grid->walkable && !grid->walkable(grid->context, from)) ||
        (grid->walkable && !grid->walkable(grid->context, to)))
        return 0u;
    return grid->move_cost ? grid->move_cost(grid->context, from, to) :
           UINT16_C(1);
}

bool kilix_world_cell_opaque(const kilix_world_grid *grid,
                             kilix_world_cell cell)
{
    return !grid_dimensions_valid(grid, NULL) ||
           !cell_in_bounds_unchecked(grid, cell) ||
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
    if (!neighbors || !grid_dimensions_valid(grid, NULL) ||
        !cell_in_bounds_unchecked(grid, cell))
        return 0u;
    for (index = 0u; index < 4u; ++index) {
        kilix_world_cell candidate = {
            cell.x + offsets[index][0], cell.y + offsets[index][1]
        };
        if (cell_in_bounds_unchecked(grid, candidate))
            neighbors[count++] = candidate;
    }
    return count;
}

static bool search_storage_valid(const kilix_world_search *search,
                                 const void *owner)
{
    byte_range ranges[6];
    if (!search || !owner ||
        (uintptr_t)search % _Alignof(kilix_world_search) != 0u ||
        (uintptr_t)owner % _Alignof(kilix_world_search) != 0u ||
        search->cell_capacity == 0u ||
        search->cell_capacity > UINT32_MAX ||
        search->heap_size > search->cell_capacity ||
        !byte_range_init(&ranges[0], owner, 1u,
                         sizeof(kilix_world_search),
                         _Alignof(kilix_world_search)) ||
        !byte_range_init(&ranges[1], search->heap,
                         search->cell_capacity, sizeof(*search->heap),
                         _Alignof(uint32_t)) ||
        !byte_range_init(&ranges[2], search->heap_positions,
                         search->cell_capacity,
                         sizeof(*search->heap_positions),
                         _Alignof(size_t)) ||
        !byte_range_init(&ranges[3], search->distance,
                         search->cell_capacity,
                         sizeof(*search->distance),
                         _Alignof(uint32_t)) ||
        !byte_range_init(&ranges[4], search->previous,
                         search->cell_capacity,
                         sizeof(*search->previous),
                         _Alignof(size_t)) ||
        !byte_range_init(&ranges[5], search->closed,
                         search->cell_capacity,
                         sizeof(*search->closed), _Alignof(uint8_t)))
        return false;
    return byte_ranges_disjoint(ranges, 6u);
}

static bool search_output_valid(const kilix_world_search *search,
                                const void *output, size_t output_count,
                                size_t output_size, size_t output_alignment,
                                const void *count_output,
                                const void *cost_output)
{
    byte_range ranges[9];
    size_t range_count;
    size_t storage_index;
    if (!byte_range_init(&ranges[0], search, 1u,
                         sizeof(*search), _Alignof(kilix_world_search)) ||
        !byte_range_init(&ranges[1], search->heap,
                         search->cell_capacity, sizeof(*search->heap),
                         _Alignof(uint32_t)) ||
        !byte_range_init(&ranges[2], search->heap_positions,
                         search->cell_capacity,
                         sizeof(*search->heap_positions),
                         _Alignof(size_t)) ||
        !byte_range_init(&ranges[3], search->distance,
                         search->cell_capacity,
                         sizeof(*search->distance),
                         _Alignof(uint32_t)) ||
        !byte_range_init(&ranges[4], search->previous,
                         search->cell_capacity,
                         sizeof(*search->previous),
                         _Alignof(size_t)) ||
        !byte_range_init(&ranges[5], search->closed,
                         search->cell_capacity,
                         sizeof(*search->closed), _Alignof(uint8_t)) ||
        !byte_range_init(&ranges[6], output, output_count,
                         output_size, output_alignment) ||
        !byte_range_init(&ranges[7], count_output, 1u,
                         sizeof(size_t), _Alignof(size_t)))
        return false;
    range_count = 8u;
    if (cost_output) {
        if (!byte_range_init(&ranges[8], cost_output, 1u,
                             sizeof(uint32_t), _Alignof(uint32_t)))
            return false;
        range_count = 9u;
    }
    for (storage_index = 0u; storage_index < 6u; ++storage_index) {
        size_t output_index;
        for (output_index = 6u; output_index < range_count;
             ++output_index)
            if (byte_ranges_overlap(
                    ranges[storage_index], ranges[output_index]))
                return false;
    }
    return byte_ranges_disjoint(&ranges[6], range_count - 6u);
}

kilix_world_result kilix_world_search_bind(
    kilix_world_search *search, uint32_t *heap, size_t *heap_positions,
    uint32_t *distance, size_t *previous, uint8_t *closed,
    size_t cell_capacity)
{
    kilix_world_search candidate = {
        heap, heap_positions, distance, previous, closed,
        cell_capacity, 0u
    };
    if (!search || (uintptr_t)search % _Alignof(kilix_world_search) != 0u ||
        !search_storage_valid(&candidate, search))
        return KILIX_WORLD_INVALID_ARGUMENT;
    *search = candidate;
    return KILIX_WORLD_OK;
}

static void reset_search(kilix_world_search *search, size_t count)
{
    size_t index;
    search->heap_size = 0u;
    (void)memset(search->distance, UINT8_MAX,
                 count * sizeof(*search->distance));
    (void)memset(search->closed, 0, count * sizeof(*search->closed));
    for (index = 0u; index < count; ++index) {
        search->heap_positions[index] = KILIX_WORLD_NO_INDEX;
        search->previous[index] = KILIX_WORLD_NO_INDEX;
    }
}

typedef struct heap_order {
    size_t width;
    size_t goal;
    kilix_world_cell goal_cell;
} heap_order;

static uint32_t manhattan(size_t index, const heap_order *order)
{
    size_t x = index % order->width;
    size_t y = index / order->width;
    size_t goal_x = (size_t)order->goal_cell.x;
    size_t goal_y = (size_t)order->goal_cell.y;
    uint32_t dx;
    uint32_t dy;
    dx = (uint32_t)(x > goal_x ? x - goal_x : goal_x - x);
    dy = (uint32_t)(y > goal_y ? y - goal_y : goal_y - y);
    return dx > UINT32_MAX - dy ? UINT32_MAX : dx + dy;
}

static uint32_t priority(const kilix_world_search *search, size_t index,
                         const heap_order *order)
{
    uint32_t distance = search->distance[index];
    uint32_t heuristic = order->goal == KILIX_WORLD_NO_INDEX ?
                         0u : manhattan(index, order);
    return distance > UINT32_MAX - heuristic ?
           UINT32_MAX : distance + heuristic;
}

static bool heap_before(const kilix_world_search *search, size_t left,
                        size_t right, const heap_order *order)
{
    uint32_t left_priority = priority(search, left, order);
    uint32_t right_priority = priority(search, right, order);
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

static void heap_up(kilix_world_search *search, size_t position,
                    const heap_order *order)
{
    while (position > 0u) {
        size_t parent = (position - 1u) / 2u;
        if (!heap_before(search, search->heap[position],
                         search->heap[parent], order)) break;
        heap_swap(search, position, parent);
        position = parent;
    }
}

static void heap_down(kilix_world_search *search, size_t position,
                      const heap_order *order)
{
    for (;;) {
        size_t left = position * 2u + 1u;
        size_t right = left + 1u;
        size_t selected = position;
        if (left < search->heap_size &&
            heap_before(search, search->heap[left],
                        search->heap[selected], order))
            selected = left;
        if (right < search->heap_size &&
            heap_before(search, search->heap[right],
                        search->heap[selected], order))
            selected = right;
        if (selected == position) return;
        heap_swap(search, position, selected);
        position = selected;
    }
}

static bool heap_push_or_update(kilix_world_search *search, size_t index,
                                const heap_order *order)
{
    size_t position = search->heap_positions[index];
    if (position != KILIX_WORLD_NO_INDEX) {
        heap_up(search, position, order);
        return true;
    }
    if (search->heap_size >= search->cell_capacity) return false;
    position = search->heap_size++;
    search->heap[position] = (uint32_t)index;
    search->heap_positions[index] = position;
    heap_up(search, position, order);
    return true;
}

static size_t heap_pop(kilix_world_search *search,
                       const heap_order *order)
{
    size_t result = search->heap[0];
    --search->heap_size;
    search->heap_positions[result] = KILIX_WORLD_NO_INDEX;
    if (search->heap_size != 0u) {
        search->heap[0] = search->heap[search->heap_size];
        search->heap_positions[search->heap[0]] = 0u;
        heap_down(search, 0u, order);
    }
    return result;
}

static kilix_world_result search_preflight(
    const kilix_world_grid *grid, kilix_world_search *search, size_t *count)
{
    if (!count || !grid_dimensions_valid(grid, count) ||
        *count > UINT32_MAX || !search_storage_valid(search, search) ||
        search->cell_capacity < *count)
        return KILIX_WORLD_INVALID_ARGUMENT;
    return KILIX_WORLD_OK;
}

static bool start_search(kilix_world_search *search, size_t count,
                         size_t start_index, const heap_order *order)
{
    reset_search(search, count);
    search->distance[start_index] = 0u;
    return heap_push_or_update(search, start_index, order);
}

static kilix_world_result relax_neighbor(
    const kilix_world_grid *grid, kilix_world_search *search,
    kilix_world_cell current, size_t current_index,
    kilix_world_cell neighbor, size_t neighbor_index,
    const heap_order *order, uint32_t cost_limit, bool *overflowed)
{
    uint16_t cost;
    uint32_t candidate;
    if (search->closed[neighbor_index] ||
        (grid->walkable &&
         !grid->walkable(grid->context, neighbor)))
        return KILIX_WORLD_OK;
    cost = grid->move_cost ?
           grid->move_cost(grid->context, current, neighbor) :
           UINT16_C(1);
    if (cost == 0u) return KILIX_WORLD_OK;
    if (search->distance[current_index] >
        UINT32_MAX - (uint32_t)cost) {
        if (overflowed) *overflowed = true;
        return KILIX_WORLD_OK;
    }
    candidate = search->distance[current_index] + (uint32_t)cost;
    if (candidate > cost_limit ||
        (candidate >= search->distance[neighbor_index] &&
         !(search->distance[neighbor_index] == UINT32_MAX &&
           search->heap_positions[neighbor_index] == KILIX_WORLD_NO_INDEX)))
        return KILIX_WORLD_OK;
    search->distance[neighbor_index] = candidate;
    search->previous[neighbor_index] = current_index;
    if (!heap_push_or_update(search, neighbor_index, order))
        return KILIX_WORLD_NO_SPACE;
    return KILIX_WORLD_OK;
}

static kilix_world_result relax_neighbors(
    const kilix_world_grid *grid, kilix_world_search *search,
    size_t current_index, const heap_order *order,
    uint32_t cost_limit, bool *overflowed)
{
    kilix_world_cell current = index_cell_unchecked(grid, current_index);
    kilix_world_cell neighbor;
    kilix_world_result result;
    if (current.y > 0) {
        neighbor = (kilix_world_cell){current.x, current.y - 1};
        result = relax_neighbor(
            grid, search, current, current_index, neighbor,
            current_index - (size_t)grid->width, order,
            cost_limit, overflowed);
        if (result != KILIX_WORLD_OK) return result;
    }
    if (current.x > 0) {
        neighbor = (kilix_world_cell){current.x - 1, current.y};
        result = relax_neighbor(
            grid, search, current, current_index, neighbor,
            current_index - 1u, order, cost_limit, overflowed);
        if (result != KILIX_WORLD_OK) return result;
    }
    if (current.x + 1 < grid->width) {
        neighbor = (kilix_world_cell){current.x + 1, current.y};
        result = relax_neighbor(
            grid, search, current, current_index, neighbor,
            current_index + 1u, order, cost_limit, overflowed);
        if (result != KILIX_WORLD_OK) return result;
    }
    if (current.y + 1 < grid->height) {
        neighbor = (kilix_world_cell){current.x, current.y + 1};
        result = relax_neighbor(
            grid, search, current, current_index, neighbor,
            current_index + (size_t)grid->width, order,
            cost_limit, overflowed);
        if (result != KILIX_WORLD_OK) return result;
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
    uint32_t found_cost;
    heap_order order;
    bool found = false;
    bool overflowed = false;
    kilix_world_result result;
    if (!path_count || (path_capacity != 0u && !path))
        return KILIX_WORLD_INVALID_ARGUMENT;
    result = search_preflight(grid, search, &count);
    if (result != KILIX_WORLD_OK) return result;
    if (!search_output_valid(
            search, path, path_capacity, sizeof(*path),
            _Alignof(kilix_world_cell), path_count, total_cost))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!cell_in_bounds_unchecked(grid, start) ||
        !cell_in_bounds_unchecked(grid, goal))
        return KILIX_WORLD_OUT_OF_BOUNDS;
    start_index = cell_index_unchecked(grid, start);
    goal_index = cell_index_unchecked(grid, goal);
    if (grid->walkable && !grid->walkable(grid->context, start))
        return KILIX_WORLD_BLOCKED;
    if (goal_index != start_index && grid->walkable &&
        !grid->walkable(grid->context, goal))
        return KILIX_WORLD_BLOCKED;
    order = (heap_order){(size_t)grid->width, goal_index, goal};
    if (!start_search(search, count, start_index, &order))
        return KILIX_WORLD_NO_SPACE;
    while (search->heap_size != 0u) {
        size_t current = heap_pop(search, &order);
        if (current == goal_index) {
            found = true;
            break;
        }
        search->closed[current] = 1u;
        result = relax_neighbors(
            grid, search, current, &order, UINT32_MAX, &overflowed);
        if (result != KILIX_WORLD_OK) return result;
    }
    if (!found)
        return overflowed ? KILIX_WORLD_OVERFLOW : KILIX_WORLD_NOT_FOUND;
    found_cost = search->distance[goal_index];
    cursor = goal_index;
    for (;;) {
        ++required;
        if (cursor == start_index) break;
        if (required >= count ||
            search->previous[cursor] == KILIX_WORLD_NO_INDEX ||
            search->previous[cursor] >= count)
            return KILIX_WORLD_INVALID_MAP;
        cursor = search->previous[cursor];
    }
    if (path_capacity < required) {
        *path_count = required;
        if (total_cost) *total_cost = found_cost;
        return KILIX_WORLD_NO_SPACE;
    }
    cursor = goal_index;
    {
        size_t remaining = required;
        while (remaining != 0u) {
            --remaining;
            path[remaining] = index_cell_unchecked(grid, cursor);
            if (cursor == start_index) break;
            cursor = search->previous[cursor];
        }
    }
    *path_count = required;
    if (total_cost) *total_cost = found_cost;
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
    heap_order order;
    kilix_world_result result;
    if (!cell_count || (cell_capacity != 0u && !cells))
        return KILIX_WORLD_INVALID_ARGUMENT;
    result = search_preflight(grid, search, &count);
    if (result != KILIX_WORLD_OK) return result;
    if (!search_output_valid(
            search, cells, cell_capacity, sizeof(*cells),
            _Alignof(kilix_world_cell), cell_count, NULL))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!cell_in_bounds_unchecked(grid, start))
        return KILIX_WORLD_OUT_OF_BOUNDS;
    if (grid->walkable && !grid->walkable(grid->context, start))
        return KILIX_WORLD_BLOCKED;
    start_index = cell_index_unchecked(grid, start);
    order = (heap_order){
        (size_t)grid->width, KILIX_WORLD_NO_INDEX, {0, 0}
    };
    if (!start_search(search, count, start_index, &order))
        return KILIX_WORLD_NO_SPACE;
    while (search->heap_size != 0u) {
        size_t current = heap_pop(search, &order);
        search->heap[count - required - 1u] = (uint32_t)current;
        ++required;
        search->closed[current] = 1u;
        if (search->distance[current] < max_cost) {
            result = relax_neighbors(
                grid, search, current, &order, max_cost, NULL);
            if (result != KILIX_WORLD_OK) return result;
        }
    }
    if (cell_capacity < required) {
        *cell_count = required;
        return KILIX_WORLD_NO_SPACE;
    }
    {
        size_t index;
        for (index = 0u; index < required; ++index)
            cells[index] = index_cell_unchecked(
                grid, search->heap[count - index - 1u]);
    }
    *cell_count = required;
    return KILIX_WORLD_OK;
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
    bool next_visible = true;
    if (!visible || !grid_dimensions_valid(grid, NULL))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!cell_in_bounds_unchecked(grid, from) ||
        !cell_in_bounds_unchecked(grid, to))
        return KILIX_WORLD_OUT_OF_BOUNDS;
    x = from.x;
    y = from.y;
    dx = from.x < to.x ?
         (int64_t)to.x - (int64_t)from.x :
         (int64_t)from.x - (int64_t)to.x;
    dy = from.y < to.y ?
         (int64_t)to.y - (int64_t)from.y :
         (int64_t)from.y - (int64_t)to.y;
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
        current = (kilix_world_cell){x, y};
        if (x == to.x && y == to.y && !opaque_goal_blocks)
            break;
        if (grid->opaque && grid->opaque(grid->context, current)) {
            next_visible = false;
            break;
        }
    }
    *visible = next_visible;
    return KILIX_WORLD_OK;
}

static uint32_t cell_distance(kilix_world_cell left, kilix_world_cell right)
{
    int64_t signed_dx = (int64_t)left.x - (int64_t)right.x;
    int64_t signed_dy = (int64_t)left.y - (int64_t)right.y;
    uint64_t dx = signed_dx < 0 ? (uint64_t)-signed_dx :
                                  (uint64_t)signed_dx;
    uint64_t dy = signed_dy < 0 ? (uint64_t)-signed_dy :
                                  (uint64_t)signed_dy;
    return dx > UINT32_MAX || dy > UINT32_MAX - dx ?
           UINT32_MAX : (uint32_t)(dx + dy);
}

static bool record_array_valid(const void *records, size_t count,
                               size_t record_size, size_t alignment)
{
    byte_range ignored;
    return byte_range_init(
        &ignored, records, count, record_size, alignment);
}

static uint32_t record_id_at(const void *records, size_t record_size,
                             size_t index)
{
    uint32_t id;
    const unsigned char *record =
        (const unsigned char *)records + index * record_size;
    (void)memcpy(&id, record, sizeof(id));
    return id;
}

static uint32_t hash_id(uint32_t value)
{
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    return value ^ (value >> 16);
}

static bool record_ids_unique(const void *records, size_t count,
                              size_t record_size)
{
    size_t index;
    if (count <= KILIX_WORLD_ID_LINEAR_LIMIT ||
        count > KILIX_WORLD_ID_HASH_LIMIT) {
        for (index = 0u; index < count; ++index) {
            size_t previous;
            uint32_t id = record_id_at(records, record_size, index);
            for (previous = 0u; previous < index; ++previous)
                if (record_id_at(records, record_size, previous) == id)
                    return false;
        }
        return true;
    }
    {
        uint16_t slots[KILIX_WORLD_ID_HASH_CAPACITY] = {0u};
        for (index = 0u; index < count; ++index) {
            uint32_t id = record_id_at(records, record_size, index);
            size_t slot = (size_t)hash_id(id) &
                          (KILIX_WORLD_ID_HASH_CAPACITY - 1u);
            while (slots[slot] != 0u) {
                size_t previous = (size_t)slots[slot] - 1u;
                if (record_id_at(records, record_size, previous) == id)
                    return false;
                slot = (slot + 1u) &
                       (KILIX_WORLD_ID_HASH_CAPACITY - 1u);
            }
            slots[slot] = (uint16_t)(index + 1u);
        }
    }
    return true;
}

static bool region_valid_for_grid(const kilix_world_grid *grid,
                                  const kilix_world_region *region)
{
    int64_t right;
    int64_t bottom;
    if (!grid || !region || region->width <= 0 || region->height <= 0 ||
        region->x < 0 || region->y < 0)
        return false;
    right = (int64_t)region->x + (int64_t)region->width;
    bottom = (int64_t)region->y + (int64_t)region->height;
    return right <= (int64_t)grid->width &&
           bottom <= (int64_t)grid->height;
}

const kilix_world_region *kilix_world_region_at(
    const kilix_world_map *map, kilix_world_cell cell)
{
    const kilix_world_region *selected = NULL;
    size_t index;
    if (!map || !grid_dimensions_valid(&map->grid, NULL) ||
        !record_array_valid(map->regions, map->region_count,
                            sizeof(*map->regions),
                            _Alignof(kilix_world_region)) ||
        !cell_in_bounds_unchecked(&map->grid, cell))
        return NULL;
    for (index = 0u; index < map->region_count; ++index) {
        const kilix_world_region *region = &map->regions[index];
        int64_t right;
        int64_t bottom;
        if (!region_valid_for_grid(&map->grid, region))
            continue;
        right = (int64_t)region->x + (int64_t)region->width;
        bottom = (int64_t)region->y + (int64_t)region->height;
        if ((int64_t)cell.x < (int64_t)region->x ||
            (int64_t)cell.y < (int64_t)region->y ||
            (int64_t)cell.x >= right ||
            (int64_t)cell.y >= bottom)
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
    if (!map || !grid_dimensions_valid(&map->grid, NULL) ||
        !record_array_valid(map->portals, map->portal_count,
                            sizeof(*map->portals),
                            _Alignof(kilix_world_portal)) ||
        !cell_in_bounds_unchecked(&map->grid, cell))
        return NULL;
    for (index = 0u; index < map->portal_count; ++index)
        if (cell_in_bounds_unchecked(
                &map->grid, map->portals[index].cell) &&
            map->portals[index].cell.x == cell.x &&
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
        !grid_dimensions_valid(&map->grid, NULL) ||
        !record_array_valid(map->objects, map->object_count,
                            sizeof(*map->objects),
                            _Alignof(kilix_world_object)) ||
        !cell_in_bounds_unchecked(&map->grid, origin))
        return NULL;
    for (index = 0u; index < map->object_count; ++index) {
        const kilix_world_object *object = &map->objects[index];
        uint32_t distance;
        if (!cell_in_bounds_unchecked(&map->grid, object->cell) ||
            (object->interaction_mask & interaction_mask) == 0u)
            continue;
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
    if (!catalog ||
        !record_array_valid(catalog->maps, catalog->map_count,
                            sizeof(*catalog->maps),
                            _Alignof(kilix_world_map)))
        return NULL;
    for (index = 0u; index < catalog->map_count; ++index)
        if (catalog->maps[index].id == map_id) return &catalog->maps[index];
    return NULL;
}

const kilix_world_portal *kilix_world_find_portal(
    const kilix_world_map *map, uint32_t portal_id)
{
    size_t index;
    if (!map ||
        !record_array_valid(map->portals, map->portal_count,
                            sizeof(*map->portals),
                            _Alignof(kilix_world_portal)))
        return NULL;
    for (index = 0u; index < map->portal_count; ++index)
        if (map->portals[index].id == portal_id)
            return &map->portals[index];
    return NULL;
}

typedef struct portal_index_slot {
    size_t map_index_plus_one;
    size_t portal_index_plus_one;
} portal_index_slot;

static uint32_t hash_portal_key(uint32_t map_id, uint32_t portal_id)
{
    return hash_id(map_id ^ (hash_id(portal_id) + UINT32_C(0x9e3779b9)));
}

static bool portal_index_insert(
    portal_index_slot slots[KILIX_WORLD_ID_HASH_CAPACITY],
    const kilix_world_catalog *catalog, size_t map_index,
    size_t portal_index)
{
    const kilix_world_map *map = &catalog->maps[map_index];
    const kilix_world_portal *portal = &map->portals[portal_index];
    size_t slot = (size_t)hash_portal_key(map->id, portal->id) &
                  (KILIX_WORLD_ID_HASH_CAPACITY - 1u);
    while (slots[slot].map_index_plus_one != 0u) {
        const kilix_world_map *present_map =
            &catalog->maps[slots[slot].map_index_plus_one - 1u];
        const kilix_world_portal *present_portal =
            &present_map->portals[slots[slot].portal_index_plus_one - 1u];
        if (present_map->id == map->id && present_portal->id == portal->id)
            return false;
        slot = (slot + 1u) & (KILIX_WORLD_ID_HASH_CAPACITY - 1u);
    }
    slots[slot].map_index_plus_one = map_index + 1u;
    slots[slot].portal_index_plus_one = portal_index + 1u;
    return true;
}

static const kilix_world_portal *portal_index_find(
    const portal_index_slot slots[KILIX_WORLD_ID_HASH_CAPACITY],
    const kilix_world_catalog *catalog, uint32_t map_id,
    uint32_t portal_id)
{
    size_t slot = (size_t)hash_portal_key(map_id, portal_id) &
                  (KILIX_WORLD_ID_HASH_CAPACITY - 1u);
    while (slots[slot].map_index_plus_one != 0u) {
        const kilix_world_map *map =
            &catalog->maps[slots[slot].map_index_plus_one - 1u];
        const kilix_world_portal *portal =
            &map->portals[slots[slot].portal_index_plus_one - 1u];
        if (map->id == map_id && portal->id == portal_id)
            return portal;
        slot = (slot + 1u) & (KILIX_WORLD_ID_HASH_CAPACITY - 1u);
    }
    return NULL;
}

kilix_world_result kilix_world_catalog_validate(
    const kilix_world_catalog *catalog)
{
    portal_index_slot portal_slots[KILIX_WORLD_ID_HASH_CAPACITY];
    size_t portal_total = 0u;
    size_t map_index;
    bool use_portal_index = true;
    if (!catalog || catalog->map_count == 0u ||
        !record_array_valid(catalog->maps, catalog->map_count,
                            sizeof(*catalog->maps),
                            _Alignof(kilix_world_map)))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!record_ids_unique(
            catalog->maps, catalog->map_count, sizeof(*catalog->maps)))
        return KILIX_WORLD_INVALID_MAP;
    for (map_index = 0u; map_index < catalog->map_count; ++map_index) {
        const kilix_world_map *map = &catalog->maps[map_index];
        if (!grid_dimensions_valid(&map->grid, NULL) ||
            !record_array_valid(map->regions, map->region_count,
                                sizeof(*map->regions),
                                _Alignof(kilix_world_region)) ||
            !record_array_valid(map->portals, map->portal_count,
                                sizeof(*map->portals),
                                _Alignof(kilix_world_portal)) ||
            !record_array_valid(map->objects, map->object_count,
                                sizeof(*map->objects),
                                _Alignof(kilix_world_object)))
            return KILIX_WORLD_INVALID_MAP;
        if (use_portal_index) {
            if (map->portal_count >
                KILIX_WORLD_ID_HASH_LIMIT - portal_total)
                use_portal_index = false;
            else
                portal_total += map->portal_count;
        }
    }
    if (use_portal_index && portal_total != 0u)
        (void)memset(portal_slots, 0, sizeof(portal_slots));
    for (map_index = 0u; map_index < catalog->map_count; ++map_index) {
        const kilix_world_map *map = &catalog->maps[map_index];
        size_t index;
        if (!record_ids_unique(
                map->regions, map->region_count, sizeof(*map->regions)) ||
            !record_ids_unique(
                map->objects, map->object_count, sizeof(*map->objects)) ||
            !record_ids_unique(
                map->portals, map->portal_count, sizeof(*map->portals)))
            return KILIX_WORLD_INVALID_MAP;
        for (index = 0u; index < map->region_count; ++index) {
            const kilix_world_region *region = &map->regions[index];
            if (!region_valid_for_grid(&map->grid, region))
                return KILIX_WORLD_INVALID_MAP;
        }
        for (index = 0u; index < map->object_count; ++index) {
            if (!cell_in_bounds_unchecked(
                    &map->grid, map->objects[index].cell))
                return KILIX_WORLD_INVALID_MAP;
        }
        for (index = 0u; index < map->portal_count; ++index) {
            const kilix_world_portal *portal = &map->portals[index];
            if (!cell_in_bounds_unchecked(&map->grid, portal->cell) ||
                (use_portal_index &&
                 !portal_index_insert(
                     portal_slots, catalog, map_index, index)))
                return KILIX_WORLD_INVALID_MAP;
        }
    }
    for (map_index = 0u; map_index < catalog->map_count; ++map_index) {
        const kilix_world_map *map = &catalog->maps[map_index];
        size_t index;
        for (index = 0u; index < map->portal_count; ++index) {
            const kilix_world_portal *portal = &map->portals[index];
            const kilix_world_portal *target_portal;
            if (use_portal_index) {
                target_portal = portal_index_find(
                    portal_slots, catalog, portal->target_map,
                    portal->target_portal);
            } else {
                const kilix_world_map *target_map =
                    kilix_world_find_map(catalog, portal->target_map);
                target_portal = target_map ?
                    kilix_world_find_portal(
                        target_map, portal->target_portal) : NULL;
            }
            if (!target_portal ||
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
