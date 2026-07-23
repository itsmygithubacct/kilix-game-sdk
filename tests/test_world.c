#include "kilix_world.h"
#include "kilix_world_top_down.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL_COUNT 25u
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

int main(void)
{
    if (!test_navigation_and_visibility() || !test_world_records() ||
        !test_top_down_adapter())
        return EXIT_FAILURE;
    (void)puts(
        "PASS kilix-world navigation visibility regions portals top-down");
    return EXIT_SUCCESS;
}
