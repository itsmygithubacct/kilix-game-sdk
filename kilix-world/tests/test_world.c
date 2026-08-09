#include "kilix_world.h"
#include "kilix_world_top_down.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL_COUNT 25u
#define MODEL_SIDE 8u
#define MODEL_CELL_COUNT (MODEL_SIDE * MODEL_SIDE)
#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                     \
        (void)fprintf(stderr, "%s:%d: check failed: %s\n",               \
                      __FILE__, __LINE__, #condition);                      \
        return false;                                                       \
    }                                                                       \
} while (false)

typedef struct fixture {
    const char *rows[5];
} fixture;

static bool walkable(void *context, kilix_world_cell cell)
{
    fixture *world = context;
    return world->rows[cell.y][cell.x] != '#';
}

static uint16_t move_cost(void *context, kilix_world_cell from,
                          kilix_world_cell to)
{
    fixture *world = context;
    (void)from;
    return world->rows[to.y][to.x] == 'm' ? UINT16_C(3) : UINT16_C(1);
}

static bool opaque(void *context, kilix_world_cell cell)
{
    fixture *world = context;
    return world->rows[cell.y][cell.x] == '#';
}

static bool bind_search(kilix_world_search *search, uint32_t heap[CELL_COUNT],
                        size_t positions[CELL_COUNT],
                        uint32_t distance[CELL_COUNT],
                        size_t previous[CELL_COUNT],
                        uint8_t closed[CELL_COUNT])
{
    return kilix_world_search_bind(search, heap, positions, distance,
                                   previous, closed, CELL_COUNT) ==
           KILIX_WORLD_OK;
}

static bool test_navigation_and_visibility(void)
{
    fixture world = {{
        ".....",
        ".###.",
        "...m.",
        ".###.",
        "....."
    }};
    fixture tie_blocked = {{
        ".....",
        ".#...",
        ".....",
        ".....",
        "....."
    }};
    fixture alternate_tie_blocked = {{
        ".#...",
        ".....",
        ".....",
        ".....",
        "....."
    }};
    kilix_world_grid grid;
    kilix_world_search search;
    uint32_t heap[CELL_COUNT];
    size_t positions[CELL_COUNT];
    uint32_t distance[CELL_COUNT];
    size_t previous[CELL_COUNT];
    uint8_t closed[CELL_COUNT];
    kilix_world_cell path[CELL_COUNT];
    kilix_world_cell reachable[CELL_COUNT];
    size_t path_count = 0u;
    size_t reachable_count = 0u;
    uint32_t total_cost = 0u;
    bool visible;

    CHECK(kilix_world_grid_init(&grid, 5, 5, &world, walkable,
                                move_cost, opaque) == KILIX_WORLD_OK);
    CHECK(bind_search(&search, heap, positions, distance, previous, closed));
    CHECK(kilix_world_find_path(
        &grid, (kilix_world_cell){0, 2}, (kilix_world_cell){4, 2},
        &search, path, CELL_COUNT, &path_count, &total_cost) ==
        KILIX_WORLD_OK);
    CHECK(path_count == 5u && total_cost == 6u);
    CHECK(path[0].x == 0 && path[0].y == 2);
    CHECK(path[path_count - 1u].x == 4 && path[path_count - 1u].y == 2);
    CHECK(kilix_world_find_path(
        &grid, (kilix_world_cell){0, 2}, (kilix_world_cell){4, 2},
        &search, path, 2u, &path_count, NULL) == KILIX_WORLD_NO_SPACE);
    CHECK(path_count == 5u);
    CHECK(kilix_world_reachable(
        &grid, (kilix_world_cell){0, 0}, 2u, &search,
        reachable, CELL_COUNT, &reachable_count) == KILIX_WORLD_OK);
    CHECK(reachable_count == 5u);
    reachable[0] = (kilix_world_cell){91, 92};
    reachable_count = 93u;
    CHECK(kilix_world_reachable(
        &grid, (kilix_world_cell){0, 0}, 2u, &search,
        reachable, 1u, &reachable_count) == KILIX_WORLD_NO_SPACE);
    CHECK(reachable_count == 5u &&
          reachable[0].x == 91 && reachable[0].y == 92);
    CHECK(kilix_world_find_path(
        &grid, (kilix_world_cell){1, 1}, (kilix_world_cell){0, 1},
        &search, path, CELL_COUNT, &path_count, &total_cost) ==
        KILIX_WORLD_BLOCKED);
    CHECK(kilix_world_reachable(
        &grid, (kilix_world_cell){1, 1}, 2u, &search,
        reachable, CELL_COUNT, &reachable_count) == KILIX_WORLD_BLOCKED);
    CHECK(kilix_world_cell_move_cost(
        &grid, (kilix_world_cell){1, 1},
        (kilix_world_cell){0, 1}) == 0u);
    CHECK(kilix_world_line_of_sight(
        &grid, (kilix_world_cell){0, 1}, (kilix_world_cell){4, 1},
        false, &visible) == KILIX_WORLD_OK && !visible);
    CHECK(kilix_world_line_of_sight(
        &grid, (kilix_world_cell){0, 0}, (kilix_world_cell){4, 0},
        false, &visible) == KILIX_WORLD_OK && visible);
    CHECK(kilix_world_grid_init(&grid, 5, 5, &tie_blocked, walkable,
                                move_cost, opaque) == KILIX_WORLD_OK);
    CHECK(kilix_world_line_of_sight(
        &grid, (kilix_world_cell){0, 0}, (kilix_world_cell){2, 1},
        false, &visible) == KILIX_WORLD_OK && !visible);
    CHECK(kilix_world_grid_init(
        &grid, 5, 5, &alternate_tie_blocked, walkable,
        move_cost, opaque) == KILIX_WORLD_OK);
    CHECK(kilix_world_line_of_sight(
        &grid, (kilix_world_cell){0, 0}, (kilix_world_cell){2, 1},
        false, &visible) == KILIX_WORLD_OK && visible);
    return true;
}

static bool test_world_records(void)
{
    fixture world = {{".....", ".....", ".....", ".....", "....."}};
    kilix_world_map maps[2] = {0};
    static const kilix_world_region regions[] = {
        {1u, 0, 0, 5, 5, 1}, {2u, 1, 1, 2, 2, 4}
    };
    static const kilix_world_portal first_portals[] = {
        {10u, {4, 2}, 2u, 20u}
    };
    static const kilix_world_portal second_portals[] = {
        {20u, {0, 2}, 1u, 10u}
    };
    static const kilix_world_object objects[] = {
        {30u, {2, 2}, UINT32_C(1), 1},
        {31u, {2, 1}, UINT32_C(1), 5}
    };
    kilix_world_catalog catalog = {maps, 2u};
    const kilix_world_region *region;
    const kilix_world_object *object;

    maps[0].id = 1u;
    maps[0].regions = regions;
    maps[0].region_count = 2u;
    maps[0].portals = first_portals;
    maps[0].portal_count = 1u;
    maps[0].objects = objects;
    maps[0].object_count = 2u;
    maps[1].id = 2u;
    maps[1].portals = second_portals;
    maps[1].portal_count = 1u;
    CHECK(kilix_world_grid_init(&maps[0].grid, 5, 5, &world, walkable,
                                NULL, opaque) == KILIX_WORLD_OK);
    CHECK(kilix_world_grid_init(&maps[1].grid, 5, 5, &world, walkable,
                                NULL, opaque) == KILIX_WORLD_OK);
    CHECK(kilix_world_catalog_validate(&catalog) == KILIX_WORLD_OK);
    region = kilix_world_region_at(&maps[0], (kilix_world_cell){1, 1});
    CHECK(region && region->id == 2u);
    object = kilix_world_interaction_at(
        &maps[0], (kilix_world_cell){1, 1}, UINT32_C(1), 2u);
    CHECK(object && object->id == 31u);
    CHECK(kilix_world_portal_at(
              &maps[0], (kilix_world_cell){4, 2})->target_map == 2u);
    maps[1].id = 3u;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    CHECK(strcmp(kilix_world_result_name(KILIX_WORLD_NOT_FOUND),
                 "route not found") == 0);
    return true;
}

static bool test_invalid_records_are_safe(void)
{
    fixture world = {{".....", ".....", ".....", ".....", "....."}};
    kilix_world_region invalid_region = {
        1u, INT32_MIN, 0, 1, 1, 0
    };
    kilix_world_object invalid_object = {
        2u, {INT32_MIN, INT32_MIN}, UINT32_C(1), 0
    };
    kilix_world_portal invalid_portal = {
        3u, {INT32_MIN, INT32_MIN}, 1u, 3u
    };
    kilix_world_map map = {0};

    map.id = 1u;
    map.regions = &invalid_region;
    map.region_count = 1u;
    map.objects = &invalid_object;
    map.object_count = 1u;
    map.portals = &invalid_portal;
    map.portal_count = 1u;
    CHECK(kilix_world_grid_init(&map.grid, 5, 5, &world, walkable,
                                NULL, opaque) == KILIX_WORLD_OK);
    CHECK(kilix_world_region_at(
              &map, (kilix_world_cell){0, 0}) == NULL);
    CHECK(kilix_world_interaction_at(
              &map, (kilix_world_cell){0, 0}, UINT32_C(1),
              UINT32_MAX) == NULL);
    CHECK(kilix_world_portal_at(
              &map, (kilix_world_cell){0, 0}) == NULL);
    CHECK(kilix_world_portal_at(
              &map, (kilix_world_cell){-1, 0}) == NULL);
    return true;
}

static bool test_top_down_adapter(void)
{
    fixture world = {{".....", ".....", ".....", ".....", "....."}};
    kilix_world_grid grid;
    kilix_world_td_layout layout;
    kilix_world_td_rect rect;
    kilix_world_td_rect rects[3];
    kilix_world_td_point points[3];
    kilix_world_cell cell;
    static const kilix_world_cell path[3] = {
        {0, 0}, {1, 0}, {1, 1}
    };
    size_t count = 0u;

    CHECK(kilix_world_grid_init(&grid, 5, 5, &world, walkable,
                                NULL, opaque) == KILIX_WORLD_OK);
    CHECK(kilix_world_td_layout_init(&layout, 8.0f, 12.0f,
                                     16.0f, 20.0f));
    CHECK(kilix_world_td_cell_rect(
        &grid, &layout, (kilix_world_cell){2, 3}, 2.0f, &rect) ==
        KILIX_WORLD_OK);
    CHECK(rect.x == 42.0f && rect.y == 74.0f);
    CHECK(rect.width == 12.0f && rect.height == 16.0f);
    CHECK(kilix_world_td_point_cell(
        &grid, &layout, 43.0f, 75.0f, &cell) == KILIX_WORLD_OK);
    CHECK(cell.x == 2 && cell.y == 3);
    CHECK(kilix_world_td_point_cell(
        &grid, &layout, 7.0f, 12.0f, &cell) ==
        KILIX_WORLD_OUT_OF_BOUNDS);
    CHECK(kilix_world_td_cell_rects(
        &grid, &layout, path, 3u, 1.0f, rects, 2u, &count) ==
        KILIX_WORLD_NO_SPACE && count == 3u);
    CHECK(kilix_world_td_cell_rects(
        &grid, &layout, path, 3u, 1.0f, rects, 3u, &count) ==
        KILIX_WORLD_OK && count == 3u);
    CHECK(kilix_world_td_path_points(
        &grid, &layout, path, 3u, points, 3u, &count) ==
        KILIX_WORLD_OK && count == 3u);
    CHECK(points[0].x == 16.0f && points[0].y == 22.0f);
    CHECK(points[2].x == 32.0f && points[2].y == 42.0f);
    return true;
}

static bool test_grid_and_workspace_contracts(void)
{
    kilix_world_grid grid = {7, 9, NULL, NULL, NULL, NULL};
    kilix_world_grid saved_grid = grid;
    kilix_world_search search;
    kilix_world_search saved_search;
    uint32_t heap[8] = {0u};
    size_t positions[8] = {0u};
    uint32_t distance[8] = {0u};
    size_t previous[8] = {0u};
    uint8_t closed[8] = {0u};
    kilix_world_cell path[8] = {{91, 92}};
    kilix_world_cell cell = {93, 94};
    kilix_world_cell neighbors[4];
    size_t index = 95u;
    size_t path_count = 96u;

    CHECK(kilix_world_grid_init(
              &grid, 0, 2, NULL, NULL, NULL, NULL) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(memcmp(&grid, &saved_grid, sizeof(grid)) == 0);
    CHECK(kilix_world_grid_init(
              &grid, 3, 2, NULL, NULL, NULL, NULL) == KILIX_WORLD_OK);
    CHECK(kilix_world_in_bounds(&grid, (kilix_world_cell){2, 1}));
    CHECK(!kilix_world_in_bounds(&grid, (kilix_world_cell){3, 1}));
    CHECK(kilix_world_cell_index(
              &grid, (kilix_world_cell){2, 1}, &index) == KILIX_WORLD_OK);
    CHECK(index == 5u);
    CHECK(kilix_world_cell_index(
              &grid, (kilix_world_cell){3, 1}, &index) ==
          KILIX_WORLD_OUT_OF_BOUNDS && index == 5u);
    CHECK(kilix_world_index_cell(&grid, 5u, &cell) == KILIX_WORLD_OK);
    CHECK(cell.x == 2 && cell.y == 1);
    CHECK(kilix_world_index_cell(&grid, 6u, &cell) ==
          KILIX_WORLD_OUT_OF_BOUNDS && cell.x == 2 && cell.y == 1);
    CHECK(kilix_world_neighbors4(
              &grid, (kilix_world_cell){1, 0}, neighbors) == 3u);
    CHECK(neighbors[0].x == 0 && neighbors[0].y == 0);
    CHECK(neighbors[1].x == 2 && neighbors[1].y == 0);
    CHECK(neighbors[2].x == 1 && neighbors[2].y == 1);

    CHECK(kilix_world_search_bind(
              &search, heap, positions, distance, previous, closed, 8u) ==
          KILIX_WORLD_OK);
    saved_search = search;
    CHECK(kilix_world_search_bind(
              &search, heap, positions, heap, previous, closed, 8u) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(memcmp(&search, &saved_search, sizeof(search)) == 0);
    CHECK(kilix_world_search_bind(
              &search, (uint32_t *)(void *)&search, positions,
              distance, previous, closed, 1u) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(memcmp(&search, &saved_search, sizeof(search)) == 0);
#if SIZE_MAX > UINT32_MAX
    CHECK(kilix_world_search_bind(
              &search, heap, positions, distance, previous, closed,
              (size_t)UINT32_MAX + 1u) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(memcmp(&search, &saved_search, sizeof(search)) == 0);
#endif

    search.distance = heap;
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){2, 1}, &search,
              path, 8u, &path_count, NULL) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(path_count == 96u && path[0].x == 91 && path[0].y == 92);
    search = saved_search;
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){2, 1}, &search,
              (kilix_world_cell *)(void *)previous, 3u,
              &path_count, NULL) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(path_count == 96u);
    return true;
}

static uint16_t maximum_move_cost(void *context, kilix_world_cell from,
                                  kilix_world_cell to)
{
    (void)context;
    (void)from;
    (void)to;
    return UINT16_MAX;
}

static bool test_distance_boundaries_with_buffers(
    size_t capacity, uint32_t *heap, size_t *positions,
    uint32_t *distance, size_t *previous, uint8_t *closed)
{
    kilix_world_grid grid;
    kilix_world_search search;
    size_t path_count = 0u;
    uint32_t total_cost = 0u;
    kilix_world_result result;

    CHECK(kilix_world_search_bind(
              &search, heap, positions, distance, previous, closed,
              capacity) == KILIX_WORLD_OK);
    CHECK(kilix_world_grid_init(
              &grid, 65538, 1, NULL, NULL, maximum_move_cost, NULL) ==
          KILIX_WORLD_OK);
    result = kilix_world_find_path(
        &grid, (kilix_world_cell){0, 0},
        (kilix_world_cell){65537, 0}, &search,
        NULL, 0u, &path_count, &total_cost);
    CHECK(result == KILIX_WORLD_NO_SPACE);
    CHECK(path_count == 65538u && total_cost == UINT32_MAX);

    CHECK(kilix_world_grid_init(
              &grid, 65539, 1, NULL, NULL, maximum_move_cost, NULL) ==
          KILIX_WORLD_OK);
    path_count = 77u;
    total_cost = 78u;
    result = kilix_world_find_path(
        &grid, (kilix_world_cell){0, 0},
        (kilix_world_cell){65538, 0}, &search,
        NULL, 0u, &path_count, &total_cost);
    CHECK(result == KILIX_WORLD_OVERFLOW);
    CHECK(path_count == 77u && total_cost == 78u);
    return true;
}

static bool test_distance_boundaries(void)
{
    const size_t capacity = 65539u;
    uint32_t *heap = malloc(capacity * sizeof(*heap));
    size_t *positions = malloc(capacity * sizeof(*positions));
    uint32_t *distance = malloc(capacity * sizeof(*distance));
    size_t *previous = malloc(capacity * sizeof(*previous));
    uint8_t *closed = malloc(capacity * sizeof(*closed));
    bool passed = false;

    if (heap && positions && distance && previous && closed)
        passed = test_distance_boundaries_with_buffers(
            capacity, heap, positions, distance, previous, closed);
    else
        (void)fprintf(stderr, "%s:%d: allocation failed\n",
                      __FILE__, __LINE__);
    free(closed);
    free(previous);
    free(distance);
    free(positions);
    free(heap);
    return passed;
}

typedef struct model_world {
    size_t width;
    size_t height;
    uint8_t blocked[MODEL_CELL_COUNT];
    uint16_t costs[MODEL_CELL_COUNT];
} model_world;

static size_t model_cell_index(const model_world *world,
                               kilix_world_cell cell)
{
    return (size_t)cell.y * world->width + (size_t)cell.x;
}

static bool model_walkable(void *context, kilix_world_cell cell)
{
    const model_world *world = context;
    return world->blocked[model_cell_index(world, cell)] == 0u;
}

static uint16_t model_move_cost(void *context, kilix_world_cell from,
                                kilix_world_cell to)
{
    const model_world *world = context;
    (void)from;
    return world->costs[model_cell_index(world, to)];
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void reference_distances(const model_world *world, size_t start,
                                uint32_t distances[MODEL_CELL_COUNT])
{
    uint8_t visited[MODEL_CELL_COUNT] = {0u};
    size_t count = world->width * world->height;
    size_t iteration;
    for (iteration = 0u; iteration < count; ++iteration)
        distances[iteration] = UINT32_MAX;
    distances[start] = 0u;
    for (iteration = 0u; iteration < count; ++iteration) {
        size_t selected = SIZE_MAX;
        size_t index;
        size_t x;
        size_t y;
        static const int32_t offsets[4][2] = {
            {0, -1}, {-1, 0}, {1, 0}, {0, 1}
        };
        for (index = 0u; index < count; ++index)
            if (!visited[index] && distances[index] != UINT32_MAX &&
                (selected == SIZE_MAX ||
                 distances[index] < distances[selected] ||
                 (distances[index] == distances[selected] &&
                  index < selected)))
                selected = index;
        if (selected == SIZE_MAX) break;
        visited[selected] = 1u;
        x = selected % world->width;
        y = selected / world->width;
        for (index = 0u; index < 4u; ++index) {
            int32_t nx = (int32_t)x + offsets[index][0];
            int32_t ny = (int32_t)y + offsets[index][1];
            size_t neighbor;
            uint32_t candidate;
            if (nx < 0 || ny < 0 || (size_t)nx >= world->width ||
                (size_t)ny >= world->height)
                continue;
            neighbor = (size_t)ny * world->width + (size_t)nx;
            if (visited[neighbor] || world->blocked[neighbor]) continue;
            candidate = distances[selected] + world->costs[neighbor];
            if (candidate < distances[neighbor])
                distances[neighbor] = candidate;
        }
    }
}

static bool test_reference_navigation(void)
{
    uint32_t random_state = UINT32_C(0x91e10da5);
    size_t case_index;
    for (case_index = 0u; case_index < 10000u; ++case_index) {
        model_world world = {0};
        kilix_world_grid grid;
        kilix_world_search search;
        uint32_t heap[MODEL_CELL_COUNT];
        size_t positions[MODEL_CELL_COUNT];
        uint32_t distance[MODEL_CELL_COUNT];
        size_t previous[MODEL_CELL_COUNT];
        uint8_t closed[MODEL_CELL_COUNT];
        kilix_world_cell path[MODEL_CELL_COUNT];
        kilix_world_cell cells[MODEL_CELL_COUNT];
        uint32_t expected[MODEL_CELL_COUNT];
        size_t count;
        size_t start_index;
        size_t goal_index;
        size_t path_count = 0u;
        uint32_t total_cost = 0u;
        kilix_world_result result;
        size_t index;

        world.width = 2u + next_random(&random_state) % 7u;
        world.height = 2u + next_random(&random_state) % 7u;
        count = world.width * world.height;
        for (index = 0u; index < count; ++index) {
            world.blocked[index] =
                (uint8_t)(next_random(&random_state) % 5u == 0u);
            world.costs[index] =
                (uint16_t)(1u + next_random(&random_state) % 9u);
        }
        start_index = next_random(&random_state) % count;
        goal_index = next_random(&random_state) % count;
        world.blocked[start_index] = 0u;
        world.blocked[goal_index] = 0u;
        reference_distances(&world, start_index, expected);
        CHECK(kilix_world_grid_init(
                  &grid, (int32_t)world.width, (int32_t)world.height,
                  &world, model_walkable, model_move_cost, NULL) ==
              KILIX_WORLD_OK);
        CHECK(kilix_world_search_bind(
                  &search, heap, positions, distance, previous, closed,
                  MODEL_CELL_COUNT) == KILIX_WORLD_OK);
        result = kilix_world_find_path(
            &grid,
            (kilix_world_cell){
                (int32_t)(start_index % world.width),
                (int32_t)(start_index / world.width)
            },
            (kilix_world_cell){
                (int32_t)(goal_index % world.width),
                (int32_t)(goal_index / world.width)
            },
            &search, path, MODEL_CELL_COUNT, &path_count, &total_cost);
        if (expected[goal_index] == UINT32_MAX) {
            CHECK(result == KILIX_WORLD_NOT_FOUND);
        } else {
            uint32_t path_cost = 0u;
            CHECK(result == KILIX_WORLD_OK);
            CHECK(path_count >= 1u && path_count <= count);
            CHECK(model_cell_index(&world, path[0]) == start_index);
            CHECK(model_cell_index(&world, path[path_count - 1u]) ==
                  goal_index);
            for (index = 1u; index < path_count; ++index) {
                int32_t dx = path[index].x - path[index - 1u].x;
                int32_t dy = path[index].y - path[index - 1u].y;
                CHECK((dx == 0 && (dy == -1 || dy == 1)) ||
                      (dy == 0 && (dx == -1 || dx == 1)));
                CHECK(model_walkable(&world, path[index]));
                path_cost += model_move_cost(
                    &world, path[index - 1u], path[index]);
            }
            CHECK(path_cost == expected[goal_index]);
            CHECK(total_cost == expected[goal_index]);
        }
        if ((case_index & 3u) == 0u) {
            uint32_t maximum = next_random(&random_state) % 20u;
            size_t cell_count = 0u;
            size_t expected_count = 0u;
            uint32_t cost;
            result = kilix_world_reachable(
                &grid,
                (kilix_world_cell){
                    (int32_t)(start_index % world.width),
                    (int32_t)(start_index / world.width)
                },
                maximum, &search, cells, MODEL_CELL_COUNT, &cell_count);
            CHECK(result == KILIX_WORLD_OK);
            for (cost = 0u; cost <= maximum; ++cost)
                for (index = 0u; index < count; ++index)
                    if (expected[index] == cost) {
                        CHECK(expected_count < cell_count);
                        CHECK(model_cell_index(
                                  &world, cells[expected_count]) == index);
                        ++expected_count;
                    }
            CHECK(cell_count == expected_count);
        }
    }
    return true;
}

static bool model_opaque(void *context, kilix_world_cell cell)
{
    const model_world *world = context;
    return world->blocked[model_cell_index(world, cell)] != 0u;
}

static bool model_visible(const model_world *world, kilix_world_cell from,
                          kilix_world_cell to, bool opaque_goal_blocks)
{
    int32_t x = from.x;
    int32_t y = from.y;
    int64_t dx = from.x < to.x ?
        (int64_t)to.x - (int64_t)from.x :
        (int64_t)from.x - (int64_t)to.x;
    int64_t dy = from.y < to.y ?
        (int64_t)to.y - (int64_t)from.y :
        (int64_t)from.y - (int64_t)to.y;
    int32_t sx = from.x < to.x ? 1 : -1;
    int32_t sy = from.y < to.y ? 1 : -1;
    int64_t error = dx - dy;

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
        if (world->blocked[model_cell_index(world, current)] != 0u)
            return false;
    }
    return true;
}

static bool test_visibility_compatibility(void)
{
    model_world world = {8u, 8u, {0u}, {0u}};
    kilix_world_grid grid;
    uint32_t random_state = UINT32_C(0x243f6a88);
    size_t case_index;
    bool visible = false;

    CHECK(kilix_world_grid_init(
              &grid, 8, 8, &world, NULL, NULL, model_opaque) ==
          KILIX_WORLD_OK);
    for (case_index = 0u; case_index < 5000u; ++case_index) {
        size_t index;
        kilix_world_cell from;
        kilix_world_cell to;
        bool forward;
        bool reverse;
        bool goal_blocks = (next_random(&random_state) & 1u) != 0u;
        for (index = 0u; index < MODEL_CELL_COUNT; ++index)
            world.blocked[index] =
                (uint8_t)(next_random(&random_state) % 7u == 0u);
        from = (kilix_world_cell){
            (int32_t)(next_random(&random_state) % 8u),
            (int32_t)(next_random(&random_state) % 8u)
        };
        to = (kilix_world_cell){
            (int32_t)(next_random(&random_state) % 8u),
            (int32_t)(next_random(&random_state) % 8u)
        };
        CHECK(kilix_world_line_of_sight(
                  &grid, from, to, goal_blocks, &forward) == KILIX_WORLD_OK);
        CHECK(kilix_world_line_of_sight(
                  &grid, to, from, goal_blocks, &reverse) == KILIX_WORLD_OK);
        CHECK(forward == model_visible(&world, from, to, goal_blocks));
        CHECK(reverse == model_visible(&world, to, from, goal_blocks));
    }
    (void)memset(world.blocked, 0, sizeof(world.blocked));
    world.blocked[9u] = 1u;
    CHECK(kilix_world_line_of_sight(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){2, 1}, false, &visible) == KILIX_WORLD_OK &&
          !visible);
    CHECK(kilix_world_line_of_sight(
              &grid, (kilix_world_cell){2, 1},
              (kilix_world_cell){0, 0}, false, &visible) == KILIX_WORLD_OK &&
          visible);
    (void)memset(world.blocked, 0, sizeof(world.blocked));
    world.blocked[1u] = 1u;
    CHECK(kilix_world_line_of_sight(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}, false, &visible) == KILIX_WORLD_OK &&
          visible);
    CHECK(kilix_world_line_of_sight(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}, true, &visible) == KILIX_WORLD_OK &&
          !visible);
    visible = true;
    CHECK(kilix_world_line_of_sight(
              &grid, (kilix_world_cell){-1, 0},
              (kilix_world_cell){1, 0}, false, &visible) ==
          KILIX_WORLD_OUT_OF_BOUNDS && visible);
    return true;
}

static bool test_catalog_scaling_boundaries_with_buffers(
    kilix_world_region *regions, kilix_world_map *maps,
    kilix_world_portal *left, kilix_world_portal *right)
{
    kilix_world_catalog catalog;
    kilix_world_map portal_maps[2] = {0};
    size_t index;

    for (index = 0u; index < 257u; ++index) {
        regions[index] = (kilix_world_region){
            (uint32_t)(index + 1u), 0, 0, 1, 1, 0
        };
        maps[index] = (kilix_world_map){0};
        maps[index].id = (uint32_t)(index + 1u);
        CHECK(kilix_world_grid_init(
                  &maps[index].grid, 1, 1, NULL, NULL, NULL, NULL) ==
              KILIX_WORLD_OK);
    }
    maps[0].regions = regions;
    maps[0].region_count = 256u;
    catalog = (kilix_world_catalog){maps, 1u};
    CHECK(kilix_world_catalog_validate(&catalog) == KILIX_WORLD_OK);
    regions[255].id = regions[0].id;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    regions[255].id = 256u;
    maps[0].region_count = 257u;
    CHECK(kilix_world_catalog_validate(&catalog) == KILIX_WORLD_OK);
    regions[256].id = regions[0].id;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    regions[256].id = 257u;

    catalog = (kilix_world_catalog){maps, 257u};
    CHECK(kilix_world_catalog_validate(&catalog) == KILIX_WORLD_OK);
    maps[256].id = maps[0].id;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    maps[256].id = 257u;
    maps[0].regions = NULL;
    maps[0].region_count = 0u;

    CHECK(kilix_world_grid_init(
              &portal_maps[0].grid, 129, 1, NULL, NULL, NULL, NULL) ==
          KILIX_WORLD_OK);
    CHECK(kilix_world_grid_init(
              &portal_maps[1].grid, 129, 1, NULL, NULL, NULL, NULL) ==
          KILIX_WORLD_OK);
    portal_maps[0].id = 1u;
    portal_maps[1].id = 2u;
    portal_maps[0].portals = left;
    portal_maps[1].portals = right;
    for (index = 0u; index < 129u; ++index) {
        left[index] = (kilix_world_portal){
            (uint32_t)(1000u + index), {(int32_t)index, 0},
            2u, (uint32_t)(2000u + index)
        };
        right[index] = (kilix_world_portal){
            (uint32_t)(2000u + index), {(int32_t)index, 0},
            1u, (uint32_t)(1000u + index)
        };
    }
    catalog = (kilix_world_catalog){portal_maps, 2u};
    portal_maps[0].portal_count = 128u;
    portal_maps[1].portal_count = 128u;
    CHECK(kilix_world_catalog_validate(&catalog) == KILIX_WORLD_OK);
    right[127].target_portal = 7u;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    right[127].target_portal = left[127].id;
    portal_maps[0].portal_count = 129u;
    portal_maps[1].portal_count = 129u;
    CHECK(kilix_world_catalog_validate(&catalog) == KILIX_WORLD_OK);

    return true;
}

static bool test_catalog_scaling_boundaries(void)
{
    kilix_world_region *regions = malloc(257u * sizeof(*regions));
    kilix_world_map *maps = malloc(257u * sizeof(*maps));
    kilix_world_portal *left = malloc(129u * sizeof(*left));
    kilix_world_portal *right = malloc(129u * sizeof(*right));
    bool passed = false;

    if (regions && maps && left && right)
        passed = test_catalog_scaling_boundaries_with_buffers(
            regions, maps, left, right);
    else
        (void)fprintf(stderr, "%s:%d: allocation failed\n",
                      __FILE__, __LINE__);
    free(right);
    free(left);
    free(maps);
    free(regions);
    return passed;
}

static bool test_top_down_transactions(void)
{
    kilix_world_grid grid;
    kilix_world_grid invalid_grid = {0};
    kilix_world_td_layout layout;
    kilix_world_td_layout huge_layout;
    kilix_world_cell cells[2] = {{0, 0}, {2, 0}};
    kilix_world_td_rect rects[2] = {
        {91.0f, 92.0f, 93.0f, 94.0f},
        {95.0f, 96.0f, 97.0f, 98.0f}
    };
    kilix_world_td_rect saved_rects[2];
    kilix_world_td_point point = {81.0f, 82.0f};
    size_t count = 83u;
    union {
        kilix_world_cell cells[2];
        kilix_world_td_point points[2];
    } alias = {{{0, 0}, {1, 0}}};

    CHECK(kilix_world_grid_init(
              &grid, 2, 1, NULL, NULL, NULL, NULL) == KILIX_WORLD_OK);
    CHECK(kilix_world_td_layout_init(
              &layout, 0.0f, 0.0f, 8.0f, 8.0f));
    (void)memcpy(saved_rects, rects, sizeof(rects));
    CHECK(kilix_world_td_cell_rects(
              &grid, &layout, cells, 2u, 0.0f,
              rects, 2u, &count) == KILIX_WORLD_OUT_OF_BOUNDS);
    CHECK(count == 83u &&
          memcmp(rects, saved_rects, sizeof(rects)) == 0);
    count = 84u;
    CHECK(kilix_world_td_cell_rects(
              &grid, &layout, cells, 2u, 0.0f,
              rects, 1u, &count) == KILIX_WORLD_NO_SPACE);
    CHECK(count == 2u &&
          memcmp(rects, saved_rects, sizeof(rects)) == 0);

    CHECK(kilix_world_td_layout_init(
              &huge_layout, FLT_MAX, 0.0f, FLT_MAX, 1.0f));
    count = 85u;
    CHECK(kilix_world_td_path_points(
              &grid, &huge_layout, cells, 1u,
              &point, 1u, &count) == KILIX_WORLD_OVERFLOW);
    CHECK(count == 85u && point.x == 81.0f && point.y == 82.0f);
    count = 86u;
    CHECK(kilix_world_td_path_points(
              &invalid_grid, &layout, NULL, 0u,
              NULL, 0u, &count) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(count == 86u);
    count = 87u;
    CHECK(kilix_world_td_path_points(
              &grid, &layout, alias.cells, 2u,
              alias.points, 2u, &count) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(count == 87u);
    return true;
}

static bool test_public_error_contracts(void)
{
    fixture open = {{
        ".....", ".....", ".....", ".....", "....."
    }};
    fixture divided = {{
        ".....", "#####", ".....", "#####", "....."
    }};
    kilix_world_grid grid;
    kilix_world_grid invalid_grid = {0};
    kilix_world_search search;
    uint32_t heap[CELL_COUNT];
    size_t positions[CELL_COUNT];
    uint32_t distance[CELL_COUNT];
    size_t previous[CELL_COUNT];
    uint8_t closed[CELL_COUNT];
    kilix_world_cell path[CELL_COUNT] = {{71, 72}};
    kilix_world_cell cell = {73, 74};
    kilix_world_cell neighbors[4];
    size_t index = 75u;
    size_t count = 76u;
    uint32_t cost = 77u;
    bool visible = true;
    kilix_world_td_layout layout;
    kilix_world_td_layout huge_layout;
    kilix_world_td_rect rect = {78.0f, 79.0f, 80.0f, 81.0f};
    kilix_world_td_point point = {82.0f, 83.0f};
    kilix_world_region regions[3] = {
        {1u, -1, 0, 1, 1, 99},
        {2u, 0, 0, 5, 5, 1},
        {3u, 0, 0, 5, 5, 2}
    };
    kilix_world_portal portals[2] = {
        {1u, {-1, 0}, 1u, 1u},
        {2u, {1, 1}, 1u, 2u}
    };
    kilix_world_object objects[3] = {
        {1u, {-1, 0}, UINT32_C(1), 99},
        {2u, {1, 0}, UINT32_C(1), 1},
        {3u, {0, 1}, UINT32_C(1), 4}
    };
    kilix_world_map map = {0};
    kilix_world_catalog catalog = {&map, 1u};
    size_t result_index;
    static const char *const result_names[] = {
        "ok", "invalid argument", "cell out of bounds",
        "destination blocked", "route not found",
        "output or scratch buffer too small", "world arithmetic overflow",
        "invalid world map"
    };

    CHECK(kilix_world_cell_index(NULL, (kilix_world_cell){0, 0}, &index) ==
          KILIX_WORLD_INVALID_ARGUMENT && index == 75u);
    CHECK(kilix_world_cell_index(&invalid_grid,
                                 (kilix_world_cell){0, 0}, NULL) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_index_cell(NULL, 0u, &cell) ==
          KILIX_WORLD_INVALID_ARGUMENT && cell.x == 73 && cell.y == 74);
    CHECK(kilix_world_index_cell(&invalid_grid, 0u, NULL) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(!kilix_world_cell_walkable(NULL, (kilix_world_cell){0, 0}));
    CHECK(kilix_world_cell_opaque(NULL, (kilix_world_cell){0, 0}));
    CHECK(kilix_world_neighbors4(
              NULL, (kilix_world_cell){0, 0}, neighbors) == 0u);
    CHECK(kilix_world_neighbors4(
              &invalid_grid, (kilix_world_cell){0, 0}, NULL) == 0u);

    CHECK(kilix_world_grid_init(
              &grid, 5, 5, &open, walkable, move_cost, opaque) ==
          KILIX_WORLD_OK);
    CHECK(kilix_world_cell_walkable(&grid, (kilix_world_cell){0, 0}));
    CHECK(kilix_world_cell_move_cost(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}) == 1u);
    CHECK(!kilix_world_cell_opaque(&grid, (kilix_world_cell){0, 0}));
    CHECK(kilix_world_search_bind(
              &search, NULL, positions, distance, previous, closed,
              CELL_COUNT) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_search_bind(
              &search, heap, positions, distance, previous, closed, 0u) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(bind_search(
              &search, heap, positions, distance, previous, closed));
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}, &search,
              path, CELL_COUNT, NULL, &cost) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}, &search,
              NULL, 1u, &count, &cost) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){-1, 0},
              (kilix_world_cell){1, 0}, &search,
              path, CELL_COUNT, &count, &cost) ==
          KILIX_WORLD_OUT_OF_BOUNDS);
    CHECK(count == 76u && cost == 77u && path[0].x == 71);
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){5, 0}, &search,
              path, CELL_COUNT, &count, &cost) ==
          KILIX_WORLD_OUT_OF_BOUNDS);
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}, &search,
              path, CELL_COUNT, positions, &cost) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_reachable(
              &grid, (kilix_world_cell){0, 0}, 1u, &search,
              path, CELL_COUNT, NULL) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_reachable(
              &grid, (kilix_world_cell){0, 0}, 1u, &search,
              NULL, 1u, &count) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_reachable(
              &grid, (kilix_world_cell){5, 0}, 1u, &search,
              path, CELL_COUNT, &count) == KILIX_WORLD_OUT_OF_BOUNDS);

    CHECK(kilix_world_grid_init(
              &grid, 5, 5, &divided, walkable, NULL, opaque) ==
          KILIX_WORLD_OK);
    count = 76u;
    cost = 77u;
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){0, 2}, &search,
              path, CELL_COUNT, &count, &cost) == KILIX_WORLD_NOT_FOUND);
    CHECK(count == 76u && cost == 77u);
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 1},
              (kilix_world_cell){0, 2}, &search,
              path, CELL_COUNT, &count, &cost) == KILIX_WORLD_BLOCKED);
    CHECK(kilix_world_find_path(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){0, 1}, &search,
              path, CELL_COUNT, &count, &cost) == KILIX_WORLD_BLOCKED);
    CHECK(kilix_world_reachable(
              &grid, (kilix_world_cell){0, 1}, 1u, &search,
              path, CELL_COUNT, &count) == KILIX_WORLD_BLOCKED);
    CHECK(kilix_world_line_of_sight(
              &grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}, false, NULL) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_line_of_sight(
              &invalid_grid, (kilix_world_cell){0, 0},
              (kilix_world_cell){1, 0}, false, &visible) ==
          KILIX_WORLD_INVALID_ARGUMENT && visible);

    CHECK(kilix_world_grid_init(
              &map.grid, 5, 5, &open, walkable, NULL, opaque) ==
          KILIX_WORLD_OK);
    map.id = 1u;
    map.regions = regions;
    map.region_count = 3u;
    map.portals = portals;
    map.portal_count = 2u;
    map.objects = objects;
    map.object_count = 3u;
    CHECK(kilix_world_region_at(
              &map, (kilix_world_cell){1, 1})->id == 3u);
    CHECK(kilix_world_portal_at(
              &map, (kilix_world_cell){1, 1})->id == 2u);
    CHECK(kilix_world_interaction_at(
              &map, (kilix_world_cell){0, 0}, UINT32_C(1), 1u)->id == 3u);
    CHECK(kilix_world_region_at(NULL, (kilix_world_cell){0, 0}) == NULL);
    CHECK(kilix_world_portal_at(NULL, (kilix_world_cell){0, 0}) == NULL);
    CHECK(kilix_world_interaction_at(
              NULL, (kilix_world_cell){0, 0}, 1u, 1u) == NULL);
    CHECK(kilix_world_find_map(NULL, 1u) == NULL);
    CHECK(kilix_world_find_map(&catalog, 99u) == NULL);
    CHECK(kilix_world_find_portal(NULL, 1u) == NULL);
    CHECK(kilix_world_find_portal(&map, 99u) == NULL);
    CHECK(kilix_world_catalog_validate(NULL) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    {
        kilix_world_catalog empty_catalog = {NULL, 0u};
        CHECK(kilix_world_catalog_validate(&empty_catalog) ==
              KILIX_WORLD_INVALID_ARGUMENT);
    }
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    map.regions = NULL;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    map.region_count = 0u;
    map.objects = NULL;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);
    map.object_count = 0u;
    map.portals = NULL;
    CHECK(kilix_world_catalog_validate(&catalog) ==
          KILIX_WORLD_INVALID_MAP);

    CHECK(kilix_world_td_layout_init(
              &layout, 0.0f, 0.0f, 8.0f, 8.0f));
    CHECK(!kilix_world_td_layout_init(
              &layout, NAN, 0.0f, 8.0f, 8.0f));
    CHECK(kilix_world_td_cell_rect(
              &invalid_grid, &layout, (kilix_world_cell){0, 0},
              0.0f, &rect) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_td_cell_rect(
              &grid, &layout, (kilix_world_cell){0, 0},
              4.0f, &rect) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_td_cell_rect(
              &grid, &layout, (kilix_world_cell){5, 0},
              0.0f, &rect) == KILIX_WORLD_OUT_OF_BOUNDS);
    CHECK(kilix_world_td_layout_init(
              &huge_layout, FLT_MAX, 0.0f, FLT_MAX, 1.0f));
    CHECK(kilix_world_td_cell_rect(
              &grid, &huge_layout, (kilix_world_cell){1, 0},
              0.0f, &rect) == KILIX_WORLD_OVERFLOW);
    CHECK(kilix_world_td_point_cell(
              &grid, &layout, NAN, 0.0f, &cell) ==
          KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(kilix_world_td_point_cell(
              &grid, &layout, FLT_MAX, 0.0f, &cell) ==
          KILIX_WORLD_OUT_OF_BOUNDS);
    CHECK(kilix_world_td_point_cell(
              &grid, &layout, -1.0f, 0.0f, &cell) ==
          KILIX_WORLD_OUT_OF_BOUNDS);
    count = 84u;
    CHECK(kilix_world_td_cell_rects(
              &grid, &layout, path, SIZE_MAX, 0.0f,
              NULL, 0u, &count) == KILIX_WORLD_INVALID_ARGUMENT);
    CHECK(count == 84u);
    CHECK(kilix_world_td_path_points(
              &grid, &layout, path, 2u,
              &point, 1u, &count) == KILIX_WORLD_NO_SPACE);
    CHECK(count == 2u && point.x == 82.0f && point.y == 83.0f);

    for (result_index = 0u;
         result_index < sizeof(result_names) / sizeof(result_names[0]);
         ++result_index)
        CHECK(strcmp(
                  kilix_world_result_name((kilix_world_result)result_index),
                  result_names[result_index]) == 0);
    CHECK(strcmp(
              kilix_world_result_name((kilix_world_result)99),
              "unknown world result") == 0);
    return true;
}

int main(void)
{
    bool passed = true;
#define RUN_TEST(test) do {                                                  \
    bool current = test();                                                   \
    (void)printf("%s %s\n", current ? "PASS" : "FAIL", #test);        \
    passed = current && passed;                                              \
} while (false)
    RUN_TEST(test_navigation_and_visibility);
    RUN_TEST(test_world_records);
    RUN_TEST(test_invalid_records_are_safe);
    RUN_TEST(test_top_down_adapter);
    RUN_TEST(test_grid_and_workspace_contracts);
    RUN_TEST(test_distance_boundaries);
    RUN_TEST(test_reference_navigation);
    RUN_TEST(test_visibility_compatibility);
    RUN_TEST(test_catalog_scaling_boundaries);
    RUN_TEST(test_top_down_transactions);
    RUN_TEST(test_public_error_contracts);
#undef RUN_TEST
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
