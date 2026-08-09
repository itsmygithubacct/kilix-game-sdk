/*
 * test_boundaries.c — input validation, failure paths, and storage contracts
 * for the core archive.
 *
 * The existing suites cover behaviour on well-formed input. These cases pin
 * what each public entry point does at the edge of its documented domain:
 * that extreme integer inputs are refused rather than wrapped, that outputs
 * are left untouched on failure, and that a structure describing more storage
 * than it binds is rejected instead of indexed.
 */
#include <stdio.h>
#include <string.h>

#include "kilix_tactics_map.h"
#include "kilix_tactics_nav.h"
#include "kilix_tactics_projection.h"
#include "kilix_tactics_render.h"
#include "kilix_tactics_sight.h"
#include "kilix_tactics_types.h"

static unsigned long g_checks;
static unsigned long g_failures;

static void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        printf("    FAIL %s\n", what);
    }
}

static void section(const char *name)
{
    printf("%s\n", name);
}

/* ------------------------------------------------------------ directions */

static void test_direction_domain(void)
{
    int32_t extremes[] = {INT32_MIN, INT32_MIN + 1, -3, -1, 0, 1, 3,
                          INT32_MAX - 1, INT32_MAX};
    size_t i;
    size_t j;

    section("direction helpers over the whole int32 domain");
    for (i = 0u; i < sizeof(extremes) / sizeof(extremes[0]); ++i) {
        for (j = 0u; j < sizeof(extremes) / sizeof(extremes[0]); ++j) {
            kt_direction dir = kt_direction_from_delta(extremes[i], extremes[j]);

            check((unsigned)dir < (unsigned)KT_DIR_COUNT,
                  "kt_direction_from_delta returns a valid octant");
        }
    }
    /* The wedge answers for representable inputs are unchanged. */
    check(kt_direction_from_delta(0, -1) == KT_DIR_N, "north");
    check(kt_direction_from_delta(1, -1) == KT_DIR_NE, "north-east");
    check(kt_direction_from_delta(1, 0) == KT_DIR_E, "east");
    check(kt_direction_from_delta(1, 1) == KT_DIR_SE, "south-east");
    check(kt_direction_from_delta(0, 1) == KT_DIR_S, "south");
    check(kt_direction_from_delta(-1, 1) == KT_DIR_SW, "south-west");
    check(kt_direction_from_delta(-1, 0) == KT_DIR_W, "west");
    check(kt_direction_from_delta(-1, -1) == KT_DIR_NW, "north-west");
    check(kt_direction_from_delta(0, 0) == KT_DIR_N, "zero delta reports N");

    /* The documented turn range is 0..4 for every input, in range or not. */
    for (i = 0u; i < 64u; ++i) {
        for (j = 0u; j < 64u; ++j) {
            uint32_t turns = kt_direction_turn_distance((kt_direction)i,
                                                        (kt_direction)j);

            check(turns <= 4u, "kt_direction_turn_distance stays within 0..4");
        }
    }
    check(kt_direction_turn_distance(KT_DIR_N, KT_DIR_S) == 4u, "N to S is 4");
    check(kt_direction_turn_distance(KT_DIR_N, KT_DIR_NW) == 1u, "N to NW is 1");
}

/* ------------------------------------------------------------ projection */

static void test_projection_extents(void)
{
    kt_projection projection;

    section("projection extents are bounded at init");
    check(kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW) == KT_OK,
          "the shipped parameter set is accepted");
    check(kt_projection_init(&projection, KT_PROJECTION_MAX_EXTENT,
                             KT_PROJECTION_MAX_EXTENT, KT_PROJECTION_MAX_EXTENT,
                             KT_ROTATE_CCW) == KT_OK,
          "the documented maximum is accepted");
    check(kt_projection_init(&projection, KT_PROJECTION_MAX_EXTENT + 2, 16, 24,
                             KT_ROTATE_CCW) == KT_ERR_RANGE,
          "an oversized tile width is refused");
    check(kt_projection_init(&projection, 32, KT_PROJECTION_MAX_EXTENT + 2, 24,
                             KT_ROTATE_CCW) == KT_ERR_RANGE,
          "an oversized tile height is refused");
    check(kt_projection_init(&projection, 32, 16, KT_PROJECTION_MAX_EXTENT + 1,
                             KT_ROTATE_CCW) == KT_ERR_RANGE,
          "an oversized level step is refused");
    check(kt_projection_init(&projection, 2147483646, 2147483646, 0,
                             KT_ROTATE_CCW) == KT_ERR_RANGE,
          "an extent near INT32_MAX is refused");
    check(kt_projection_init(&projection, 33, 16, 24, KT_ROTATE_CCW) ==
              KT_ERR_RANGE,
          "an odd tile width is still refused");
}

static void test_projection_range(void)
{
    kt_cell cells[4];
    kt_map map;
    kt_projection projection;
    kt_camera camera;
    kt_screen_point out;
    kt_screen_point untouched;
    int32_t out_x;
    int32_t out_y;

    section("transforms refuse results they cannot represent");
    (void)kt_map_init(&map, 2, 2, 1, cells, 4u);
    (void)kt_map_validate(&map);
    (void)kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW);
    kt_camera_init(&camera);

    /* A camera origin at the limit cannot host a non-zero projected cell. */
    camera.origin_x = INT32_MAX;
    camera.origin_y = INT32_MAX;
    untouched.x = 12345;
    untouched.y = 54321;
    out = untouched;
    check(kt_project(&map, &projection, &camera, kt_cell_point_make(1, 1, 0),
                     &out) == KT_ERR_RANGE,
          "kt_project reports an unrepresentable screen point");
    check(out.x == untouched.x && out.y == untouched.y,
          "kt_project leaves the output untouched on failure");

    kt_camera_init(&camera);
    camera.zoom_percent = 65535u;
    out = untouched;
    check(kt_project_subcell(&projection, &camera, INT32_MAX, INT32_MIN,
                             INT32_MAX, &out) == KT_ERR_RANGE,
          "kt_project_subcell reports an unrepresentable screen point");
    check(out.x == untouched.x && out.y == untouched.y,
          "kt_project_subcell leaves the output untouched on failure");

    /* The map-free rotation accepts any position it can reflect, and reports
     * the ones it cannot rather than wrapping them. */
    kt_camera_init(&camera);
    camera.rotation = 2u;
    out_x = 999;
    out_y = 888;
    check(kt_rotate_extent(&projection, &camera, 40, 40, INT32_MIN, INT32_MIN,
                           &out_x, &out_y) == KT_ERR_RANGE,
          "kt_rotate_extent reports an unrepresentable reflection");
    check(out_x == 999 && out_y == 888,
          "kt_rotate_extent leaves outputs untouched on failure");
    check(kt_rotate_extent(&projection, &camera, 40, 40, 39, 39, &out_x,
                           &out_y) == KT_OK && out_x == 0 && out_y == 0,
          "kt_rotate_extent still reflects an in-extent position");
}

static void test_projection_forged_state(void)
{
    kt_cell cells[4];
    kt_map map;
    kt_projection projection;
    kt_camera camera;
    kt_screen_point out;
    kt_cell_point picked;

    section("a projection restored with an out-of-range extent is refused");
    (void)kt_map_init(&map, 2, 2, 1, cells, 4u);
    (void)kt_map_validate(&map);
    (void)kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW);
    kt_camera_init(&camera);
    /* A saved view written straight into the struct, bypassing init. */
    projection.tile_width = 2147483646;
    projection.tile_height = 2147483646;
    check(kt_project(&map, &projection, &camera, kt_cell_point_make(0, 0, 0),
                     &out) == KT_ERR_STATE,
          "kt_project refuses an out-of-range projection");
    check(kt_pick_cell(&map, &projection, &camera, (kt_screen_point){0, 0},
                       &picked) != KT_OK,
          "kt_pick_cell refuses an out-of-range projection");
}

/* ------------------------------------------------------------------- map */

static void test_map_storage_contract(void)
{
    kt_cell one;
    kt_cell cells[4];
    kt_map map;
    kt_map good;

    section("map lookups honour the storage actually bound");
    memset(&one, 0, sizeof(one));
    map.width = 2;
    map.height = 2;
    map.levels = 1;
    map.cells = &one;
    map.cell_capacity = 1u;
    map.elevation_span = 0;

    check(kt_map_validate(&map) == KT_ERR_CAPACITY,
          "kt_map_validate names the shortfall");
    check(kt_map_cell_count(&map) == 0u,
          "kt_map_cell_count reports nothing addressable");
    check(!kt_map_contains(&map, kt_cell_point_make(1, 1, 0)),
          "kt_map_contains refuses a cell beyond the bound storage");
    check(kt_map_index(&map, kt_cell_point_make(1, 1, 0)) == SIZE_MAX,
          "kt_map_index refuses a cell beyond the bound storage");
    check(kt_map_cell_const(&map, kt_cell_point_make(1, 1, 0)) == NULL,
          "kt_map_cell_const refuses a cell beyond the bound storage");
    check(kt_map_cell_blocks(&map, kt_cell_point_make(1, 1, 0),
                             KT_CHANNEL_MOVE),
          "an unaddressable cell blocks, matching the off-map rule");
    check(kt_map_wall_blocks(&map, kt_cell_point_make(1, 1, 0), KT_WALL_WEST,
                             KT_CHANNEL_MOVE),
          "an unaddressable boundary blocks, matching the off-map rule");

    /* An extent past the documented maxima is refused the same way. */
    map.width = KT_MAP_MAX_SPAN + 1;
    map.height = 1;
    map.levels = 1;
    map.cells = cells;
    map.cell_capacity = 4u;
    check(!kt_map_contains(&map, kt_cell_point_make(0, 0, 0)),
          "an over-span extent addresses nothing");
    check(kt_map_validate(&map) == KT_ERR_RANGE,
          "kt_map_validate names an over-span extent");

    /* A properly initialised map is unaffected. */
    check(kt_map_init(&good, 2, 2, 1, cells, 4u) == KT_OK, "init succeeds");
    check(kt_map_cell_count(&good) == 4u, "count is the full extent");
    check(kt_map_contains(&good, kt_cell_point_make(1, 1, 0)),
          "every declared cell is addressable");
    check(kt_map_index(&good, kt_cell_point_make(1, 1, 0)) == 3u,
          "row-major index is unchanged");
}

/* ------------------------------------------------------------------- nav */

static uint32_t uniform_step(void *user, kt_cell_point from, kt_direction dir,
                             kt_cell_point *dest)
{
    (void)user; (void)from; (void)dir; (void)dest;
    return 1u;
}

static void test_nav_workspace_contract(void)
{
    kt_cell cells[64];
    kt_map map;
    kt_nav_node nodes[64];
    uint32_t heap[64];
    uint32_t heap_pos[64];
    kt_nav_workspace workspace;
    kt_nav_hooks hooks;
    kt_path path;
    size_t reached = 12345u;
    size_t i;

    section("navigation refuses a workspace whose storage is not bound");
    (void)kt_map_init(&map, 8, 8, 1, cells, 64u);
    for (i = 0u; i < 64u; ++i) { cells[i].move_cost = 1u; }
    (void)kt_map_validate(&map);
    kt_nav_hooks_init(&hooks);
    hooks.step_cost = uniform_step;

    memset(&workspace, 0, sizeof(workspace));
    workspace.node_capacity = 64u;
    workspace.heap = heap;
    workspace.heap_capacity = 64u;
    workspace.heap_pos = heap_pos;
    workspace.heap_pos_capacity = 64u;
    check(kt_nav_reachable(&map, &workspace, &hooks,
                           kt_cell_point_make(0, 0, 0), 8u, &reached) ==
              KT_ERR_ARGUMENT,
          "an unbound node array is refused");
    check(!kt_nav_was_reached(&map, &workspace, kt_cell_point_make(0, 0, 0)),
          "and the same workspace reports nothing reached");

    workspace.nodes = nodes;
    workspace.heap = NULL;
    check(kt_nav_reachable(&map, &workspace, &hooks,
                           kt_cell_point_make(0, 0, 0), 8u, &reached) ==
              KT_ERR_ARGUMENT,
          "an unbound heap is refused");

    section("the documented storage contract routes");
    check(kt_nav_workspace_init_indexed(&workspace, nodes, 64u, heap, 64u,
                                        heap_pos, 64u) == KT_OK,
          "the indexed workspace initialises");
    check(kt_nav_find_path(&map, &workspace, &hooks,
                           kt_cell_point_make(0, 0, 0),
                           kt_cell_point_make(7, 7, 0), 1000u, &path) == KT_OK,
          "a route is found on open terrain");
    check(path.count == 7u, "the diagonal route is seven steps");

    /*
     * kt_nav_workspace_init() leaves the position index unset, and both
     * orderings require it. The shortfall must be named, not answered as a
     * missing route -- that is the failure docs/integration.md described
     * incorrectly before this review.
     */
    check(kt_nav_workspace_init(&workspace, nodes, 64u, heap, 64u) == KT_OK,
          "the convenience form initialises");
    check(kt_nav_find_path(&map, &workspace, &hooks,
                           kt_cell_point_make(0, 0, 0),
                           kt_cell_point_make(7, 7, 0), 1000u, &path) ==
              KT_ERR_CAPACITY,
          "a workspace without a position index reports the shortfall");
}

static void test_nav_heuristic_saturates(void)
{
    kt_cell cells[64];
    kt_map map;
    kt_nav_node nodes[64];
    uint32_t heap[64];
    uint32_t heap_pos[64];
    kt_nav_workspace workspace;
    kt_nav_hooks hooks;
    kt_path path;
    size_t i;

    section("the default heuristic saturates rather than wrapping");
    (void)kt_map_init(&map, 8, 8, 1, cells, 64u);
    for (i = 0u; i < 64u; ++i) { cells[i].move_cost = 1u; }
    (void)kt_map_validate(&map);
    (void)kt_nav_workspace_init_indexed(&workspace, nodes, 64u, heap, 64u,
                                        heap_pos, 64u);
    kt_nav_hooks_init(&hooks);
    hooks.step_cost = uniform_step;
    hooks.min_step_cost = 1073741824u;

    check(kt_nav_find_path(&map, &workspace, &hooks,
                           kt_cell_point_make(0, 0, 0),
                           kt_cell_point_make(7, 7, 0), UINT32_MAX, &path) ==
              KT_OK,
          "a route is still found with an enormous declared minimum");
    check(path.total_cost == 7u, "and it is the optimal seven-step route");
}

/* ------------------------------------------------------------ draw queue */

static void test_queue_storage_contract(void)
{
    kt_draw_item item;
    kt_draw_item items[4];
    kt_draw_queue queue;
    uint64_t empty_hash;

    section("the draw queue reads only the storage it owns");
    memset(&item, 0, sizeof(item));
    queue.items = &item;
    queue.capacity = 1u;
    queue.count = 2u;
    queue.sequence = 2u;
    queue.sorted = false;

    check(kt_draw_queue_sort(&queue) == KT_ERR_STATE,
          "sorting past the bound storage is refused");

    queue.sorted = true;
    (void)kt_draw_queue_init(&queue, items, 4u);
    empty_hash = kt_draw_queue_hash(&queue);
    queue.count = 8u;
    check(kt_draw_queue_hash(&queue) != 0u,
          "hashing past the bound storage still returns");
    queue.count = 0u;
    check(kt_draw_queue_hash(&queue) == empty_hash,
          "and an empty queue hashes to the seed");
}

static void test_queue_sort_orders(void)
{
    kt_draw_item items[8];
    kt_draw_queue queue;
    size_t i;

    section("painter order is depth, then layer, then submission");
    (void)kt_draw_queue_init(&queue, items, 8u);
    (void)kt_draw_submit_keyed(&queue, 5, 1u, 100u, NULL);
    (void)kt_draw_submit_keyed(&queue, 5, 0u, 101u, NULL);
    (void)kt_draw_submit_keyed(&queue, 1, 3u, 102u, NULL);
    (void)kt_draw_submit_keyed(&queue, 5, 1u, 103u, NULL);
    (void)kt_draw_submit_keyed(&queue, -7, 0u, 104u, NULL);
    check(kt_draw_queue_sort(&queue) == KT_OK, "the queue sorts");
    check(queue.items[0].handle == 104u, "lowest depth first");
    check(queue.items[1].handle == 102u, "then the next depth");
    check(queue.items[2].handle == 101u, "then the lower layer within a depth");
    check(queue.items[3].handle == 100u, "then submission order within a layer");
    check(queue.items[4].handle == 103u, "and the later submission after it");

    section("an already ordered queue is unchanged");
    check(kt_draw_queue_sort(&queue) == KT_OK, "sorting again succeeds");
    check(queue.items[0].handle == 104u && queue.items[4].handle == 103u,
          "and leaves the order alone");

    section("a reversed queue is ordered correctly");
    (void)kt_draw_queue_init(&queue, items, 8u);
    for (i = 0u; i < 8u; ++i) {
        (void)kt_draw_submit_keyed(&queue, (int64_t)(8u - i), 0u,
                                   (uint32_t)i, NULL);
    }
    check(kt_draw_queue_sort(&queue) == KT_OK, "the reversed queue sorts");
    for (i = 1u; i < 8u; ++i) {
        check(queue.items[i - 1u].depth <= queue.items[i].depth,
              "the result is ordered by depth");
    }
}

/* ----------------------------------------------------------------- sight */

static void test_sight_capacity_reporting(void)
{
    static kt_cell cells[64 * 64];
    kt_map map;
    kt_cell_point out[4];
    size_t count = 12345u;

    section("a trace that overruns its buffer names the shortfall");
    (void)kt_map_init(&map, 64, 64, 1, cells, 64u * 64u);
    (void)kt_map_validate(&map);
    check(kt_sight_trace(&map, kt_cell_point_make(0, 0, 0),
                         kt_cell_point_make(63, 63, 0), out, 4u, &count) ==
              KT_ERR_CAPACITY,
          "kt_sight_trace reports the capacity shortfall");

    count = 12345u;
    check(kt_sight_trace(&map, kt_cell_point_make(0, 0, 0),
                         kt_cell_point_make(1, 0, 0), out, 4u, &count) == KT_OK,
          "a trace that fits succeeds");
    check(count == 0u, "an adjacent trace reports no intervening cells");
}

int main(void)
{
    test_direction_domain();
    test_projection_extents();
    test_projection_range();
    test_projection_forged_state();
    test_map_storage_contract();
    test_nav_workspace_contract();
    test_nav_heuristic_saturates();
    test_queue_storage_contract();
    test_queue_sort_orders();
    test_sight_capacity_reporting();

    printf("\n%lu checks, %lu failures\n", g_checks, g_failures);
    return g_failures == 0u ? 0 : 1;
}
