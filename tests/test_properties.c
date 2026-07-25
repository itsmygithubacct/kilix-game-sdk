/*
 * test_properties.c — property and differential tests.
 *
 * test_tactics.c checks the engine against each consuming game's frozen
 * formulas. This file checks the engine against ITSELF and against
 * independent reference implementations, on randomised inputs, so that the
 * primitives both migrations are about to depend on are known correct rather
 * than merely self-consistent.
 *
 * The strongest check here is A* optimality against a brute-force Dijkstra:
 * an inadmissible heuristic or a broken heap would silently return
 * longer-than-optimal routes, which in a game reads as "the pathfinder feels
 * wrong" rather than as a test failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kilix_tactics.h"
#include "kilix_tactics_nav.h"
#include "kilix_tactics_render.h"
#include "kilix_tactics_sight.h"

static int g_failures;
static int g_checks;
static const char *g_case;

static void fail(const char *what)
{
    ++g_failures;
    printf("  FAIL [%s] %s\n", g_case ? g_case : "-", what);
}

static void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        fail(what);
    }
}

/* SplitMix64: the seed expander both games already use. */
static uint64_t g_state;

static uint64_t next_u64(void)
{
    uint64_t z = (g_state += UINT64_C(0x9E3779B97F4A7C15));

    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static uint32_t next_below(uint32_t bound)
{
    return bound == 0u ? 0u : (uint32_t)(next_u64() % (uint64_t)bound);
}

/* ---- fixtures ---- */
enum { PW = 14, PH = 11, PD = 3 };
enum { PCELLS = PW * PH * PD };

static kt_cell g_cells[PCELLS];
static kt_nav_node g_nodes[PCELLS];
/* kt_nav_required_heap(): one queue slot per predecessor per cell. */
static uint32_t g_heap[PCELLS];
static uint32_t g_heap_pos[PCELLS];
static uint32_t g_heap_pos[PCELLS];

/* Reference Dijkstra state, deliberately a separate simple implementation. */
static uint64_t g_ref_cost[PCELLS];
static bool g_ref_done[PCELLS];

typedef struct step_ctx {
    const kt_map *map;
    bool allow_diagonal;
    bool level_links;
} step_ctx;

/*
 * The step-cost hook under test. Plain and total: a cell's own move_cost, the
 * frozen edge table for walls, and a 3/2 diagonal surcharge. Deliberately
 * contains no level changes unless level_links is set, so the reference
 * implementation can stay obviously correct.
 */
static uint32_t prop_step(void *user, kt_cell_point from, kt_direction dir,
                          kt_cell_point *dest)
{
    const step_ctx *ctx = (const step_ctx *)user;
    const kt_cell *cell;
    uint32_t cost;

    if (!ctx->allow_diagonal && KT_DIR_IS_DIAGONAL(dir)) {
        return KT_STEP_BLOCKED;
    }
    if (kt_edge_blocked(ctx->map, from, dir, KT_CHANNEL_MOVE)) {
        return KT_STEP_BLOCKED;
    }
    cell = kt_map_cell_const(ctx->map, *dest);
    if (cell == NULL || cell->move_cost == KT_MOVE_BLOCKED) {
        return KT_STEP_BLOCKED;
    }
    if (kt_map_cell_blocks(ctx->map, *dest, KT_CHANNEL_MOVE)) {
        return KT_STEP_BLOCKED;
    }
    cost = cell->move_cost;
    if (KT_DIR_IS_DIAGONAL(dir)) {
        cost = cost * 3u / 2u;
    }
    return cost;
}

/*
 * Reference shortest path: O(V^2) Dijkstra with no heap and no heuristic.
 * Slow and obviously correct, which is the point.
 */
static void reference_dijkstra(const kt_map *map, step_ctx *ctx,
                               kt_cell_point start, uint64_t max_cost)
{
    size_t count = kt_map_cell_count(map);
    size_t i;
    size_t start_index = kt_map_index(map, start);

    for (i = 0u; i < count; ++i) {
        g_ref_cost[i] = UINT64_MAX;
        g_ref_done[i] = false;
    }
    if (start_index == SIZE_MAX) {
        return;
    }
    g_ref_cost[start_index] = 0u;

    for (;;) {
        size_t best = SIZE_MAX;
        uint64_t best_cost = UINT64_MAX;
        kt_cell_point at;
        int dir;

        for (i = 0u; i < count; ++i) {
            if (!g_ref_done[i] && g_ref_cost[i] < best_cost) {
                best_cost = g_ref_cost[i];
                best = i;
            }
        }
        if (best == SIZE_MAX) {
            return;
        }
        g_ref_done[best] = true;
        if (!kt_map_point_from_index(map, best, &at)) {
            return;
        }
        for (dir = 0; dir < KT_DIR_COUNT; ++dir) {
            kt_cell_point dest = kt_cell_point_make(
                at.x + kt_direction_dx[dir], at.y + kt_direction_dy[dir], at.z);
            uint32_t step;
            size_t index;
            uint64_t total;

            step = prop_step(ctx, at, (kt_direction)dir, &dest);
            if (step == KT_STEP_BLOCKED) {
                continue;
            }
            index = kt_map_index(map, dest);
            if (index == SIZE_MAX) {
                continue;
            }
            total = best_cost + (uint64_t)step;
            if (total > max_cost) {
                continue;
            }
            if (total < g_ref_cost[index]) {
                g_ref_cost[index] = total;
            }
        }
    }
}

static void build_random_map(kt_map *map, unsigned wall_percent,
                             unsigned block_percent, int32_t levels)
{
    size_t i;
    size_t count;

    kt_map_init(map, PW, PH, levels, g_cells, PCELLS);
    count = kt_map_cell_count(map);
    for (i = 0u; i < count; ++i) {
        kt_cell *cell = &g_cells[i];

        cell->move_cost = next_below(100u) < block_percent
                              ? KT_MOVE_BLOCKED
                              : (uint8_t)(2u + next_below(6u));
        cell->wall[KT_WALL_WEST] =
            next_below(100u) < wall_percent
                ? (uint8_t)(KT_WALL_BLOCKS_MOVE | KT_WALL_BLOCKS_SIGHT)
                : 0u;
        cell->wall[KT_WALL_NORTH] =
            next_below(100u) < wall_percent
                ? (uint8_t)(KT_WALL_BLOCKS_MOVE | KT_WALL_BLOCKS_SIGHT)
                : 0u;
        cell->occupy = next_below(100u) < 3u
                           ? (uint8_t)KT_OCCUPY_BLOCKS_SIGHT
                           : 0u;
        cell->cover_half = 0u;
        cell->cover_full = 0u;
        cell->flags = 0u;
        cell->elevation = 0;
    }
    kt_map_validate(map);
}

/* ---- property 1: A* returns a genuinely optimal route ---- */
static void test_astar_optimality(void)
{
    int iteration;

    printf("A* optimality vs brute-force Dijkstra\n");
    for (iteration = 0; iteration < 60; ++iteration) {
        kt_map map;
        kt_nav_workspace workspace;
        kt_nav_hooks hooks;
        step_ctx ctx;
        kt_cell_point start;
        size_t probe;
        char label[64];

        g_state = UINT64_C(0xC0FFEE) + (uint64_t)iteration;
        snprintf(label, sizeof(label), "astar iter %d", iteration);
        g_case = label;

        /*
         * Sequenced deliberately. C leaves argument evaluation order
         * unspecified, so drawing both percentages inside one call expression
         * made gcc and clang explore different maps -- which quietly destroys
         * the suite's value as a reproducible golden.
         */
        {
            unsigned walls = 12u + next_below(18u);
            unsigned blocks = 8u + next_below(12u);

            build_random_map(&map, walls, blocks, 1);
        }
        ctx.map = &map;
        ctx.allow_diagonal = (iteration % 3) != 0;
        ctx.level_links = false;

        /*
         * Alternate heap disciplines. Both must return OPTIMAL routes; they
         * are allowed to disagree about WHICH equal-cost route, which is
         * exactly why the selector exists. A regression in the decrease-key
         * path would otherwise surface only as a game's replay drifting.
         */
        if ((iteration % 2) == 0) {
            kt_nav_workspace_init_indexed(&workspace, g_nodes, PCELLS, g_heap,
                                      PCELLS, g_heap_pos, PCELLS);
        } else {
            kt_nav_workspace_init_indexed(&workspace, g_nodes, PCELLS, g_heap,
                                          sizeof(g_heap) / sizeof(g_heap[0]),
                                          g_heap_pos, PCELLS);
        }
        kt_nav_hooks_init(&hooks);
        hooks.step_cost = prop_step;
        hooks.user = &ctx;
        /* Cheapest possible step, so the default heuristic stays admissible. */
        hooks.min_step_cost = 2u;
        hooks.allow_diagonal = ctx.allow_diagonal;
        hooks.order = (iteration % 2) == 0 ? KT_NAV_ORDER_SEQUENCE
                                           : KT_NAV_ORDER_DECREASE_KEY;

        /* Find a walkable start. */
        start = kt_cell_point_make(0, 0, 0);
        for (probe = 0u; probe < kt_map_cell_count(&map); ++probe) {
            kt_cell_point p;

            kt_map_point_from_index(&map, probe, &p);
            if (kt_map_cell_const(&map, p)->move_cost != KT_MOVE_BLOCKED) {
                start = p;
                break;
            }
        }

        reference_dijkstra(&map, &ctx, start, (uint64_t)UINT32_MAX);

        for (probe = 0u; probe < kt_map_cell_count(&map); ++probe) {
            kt_cell_point goal;
            kt_path path;
            kt_status status;

            kt_map_point_from_index(&map, probe, &goal);
            status = kt_nav_find_path(&map, &workspace, &hooks, start, goal,
                                      UINT32_MAX, &path);
            ++g_checks;
            if (g_ref_cost[probe] == UINT64_MAX) {
                /* Reference says unreachable; engine must agree. */
                if (status != KT_ERR_UNREACHABLE) {
                    fail("engine reached a cell Dijkstra could not");
                }
                continue;
            }
            if (status != KT_OK) {
                fail("engine failed to reach a cell Dijkstra reached");
                continue;
            }
            if ((uint64_t)path.total_cost != g_ref_cost[probe]) {
                printf("  FAIL [%s] suboptimal: engine %u, optimal %llu\n",
                       label, path.total_cost,
                       (unsigned long long)g_ref_cost[probe]);
                ++g_failures;
                continue;
            }

            /* The returned route must actually be walkable and priced right. */
            {
                kt_cell_point at = start;
                uint64_t walked = 0u;
                uint16_t s;
                bool ok = true;

                for (s = 0u; s < path.count; ++s) {
                    kt_direction dir = (kt_direction)path.dirs[s];
                    kt_cell_point dest = kt_cell_point_make(
                        at.x + kt_direction_dx[dir],
                        at.y + kt_direction_dy[dir], at.z);
                    uint32_t step = prop_step(&ctx, at, dir, &dest);

                    if (step == KT_STEP_BLOCKED) {
                        ok = false;
                        break;
                    }
                    walked += step;
                    at = dest;
                }
                ++g_checks;
                if (!ok) {
                    fail("returned route walks through a blocked step");
                } else if (!kt_cell_point_equal(at, goal)) {
                    fail("returned route does not end at the goal");
                } else if (walked != (uint64_t)path.total_cost) {
                    fail("total_cost disagrees with the walked route");
                }
            }
        }
    }
    g_case = NULL;
}

/* ---- property 2: the flood and the path search agree ---- */
static void test_reachable_agrees(void)
{
    int iteration;

    printf("reachability agrees with pathfinding\n");
    for (iteration = 0; iteration < 30; ++iteration) {
        kt_map map;
        kt_nav_workspace workspace;
        kt_nav_hooks hooks;
        step_ctx ctx;
        kt_cell_point start = kt_cell_point_make(1, 1, 0);
        uint32_t budget;
        size_t reached = 0u;
        size_t probe;
        char label[64];

        g_state = UINT64_C(0xBEEF01) + (uint64_t)iteration;
        snprintf(label, sizeof(label), "flood iter %d", iteration);
        g_case = label;

        build_random_map(&map, 15u, 10u, 1);
        kt_map_cell(&map, start)->move_cost = 4u;
        kt_map_validate(&map);

        ctx.map = &map;
        ctx.allow_diagonal = true;
        ctx.level_links = false;
        kt_nav_workspace_init_indexed(&workspace, g_nodes, PCELLS, g_heap,
                                      PCELLS, g_heap_pos, PCELLS);
        kt_nav_hooks_init(&hooks);
        hooks.step_cost = prop_step;
        hooks.user = &ctx;
        hooks.min_step_cost = 2u;

        budget = 10u + next_below(40u);
        check(kt_nav_reachable(&map, &workspace, &hooks, start, budget,
                               &reached) == KT_OK,
              "flood ok");

        /* Snapshot the flood before running any path search, since both
         * share the workspace. */
        {
            static uint32_t flood_cost[PCELLS];
            static bool flood_hit[PCELLS];

            for (probe = 0u; probe < kt_map_cell_count(&map); ++probe) {
                kt_cell_point p;

                kt_map_point_from_index(&map, probe, &p);
                flood_hit[probe] = kt_nav_was_reached(&map, &workspace, p);
                flood_cost[probe] = kt_nav_cost_to(&map, &workspace, p);
            }

            for (probe = 0u; probe < kt_map_cell_count(&map); ++probe) {
                kt_cell_point goal;
                kt_path path;
                kt_status status;

                kt_map_point_from_index(&map, probe, &goal);
                status = kt_nav_find_path(&map, &workspace, &hooks, start, goal,
                                          budget, &path);
                ++g_checks;
                if (flood_hit[probe]) {
                    if (status != KT_OK) {
                        fail("flood reached a cell the path search cannot");
                    } else if (path.total_cost != flood_cost[probe]) {
                        fail("flood and path search disagree on cost");
                    }
                } else if (status == KT_OK) {
                    fail("path search reached a cell the flood did not");
                }
            }
        }
    }
    g_case = NULL;
}

/* ---- property 3: picking inverts projection under random cameras ---- */
static void test_pick_inverts_project(void)
{
    int iteration;

    printf("pick inverts project under random cameras\n");
    for (iteration = 0; iteration < 40; ++iteration) {
        kt_map map;
        kt_projection projection;
        kt_camera camera;
        size_t probe;
        char label[64];
        int32_t levels = 1 + (int32_t)next_below(PD);

        g_state = UINT64_C(0x515E) + (uint64_t)iteration;
        snprintf(label, sizeof(label), "pick iter %d", iteration);
        g_case = label;

        /* Flat map: with no elevation offsets the inverse is exact, so any
         * mismatch is a genuine projection or rotation bug. */
        build_random_map(&map, 0u, 0u, levels);
        for (probe = 0u; probe < kt_map_cell_count(&map); ++probe) {
            g_cells[probe].elevation = 0;
            g_cells[probe].move_cost = 4u;
        }
        kt_map_validate(&map);

        kt_projection_init(&projection, 32, 16,
                           (int32_t)(8u + next_below(4u) * 8u),
                           (iteration % 2) ? KT_ROTATE_CW : KT_ROTATE_CCW);
        kt_camera_init(&camera);
        camera.origin_x = (int32_t)next_below(2000u) - 1000;
        camera.origin_y = (int32_t)next_below(2000u) - 1000;
        camera.rotation = (uint8_t)next_below(4u);
        camera.view_level = levels - 1;
        /*
         * Zoom MUST vary here. Holding it at 100 hid a real defect: the
         * closed-form inverse inverts the unrounded transform, so at zoom 80
         * it missed 256 of 400 projected cell centres. KAT renders at 80.
         */
        {
            static const uint16_t zooms[] = {100u, 80u, 65u, 50u, 125u, 175u,
                                             33u, 250u};
            camera.zoom_percent =
                zooms[next_below((uint32_t)(sizeof(zooms) / sizeof(zooms[0])))];
        }

        for (probe = 0u; probe < kt_map_cell_count(&map); ++probe) {
            kt_cell_point want;
            kt_cell_point got;
            kt_screen_point at;

            kt_map_point_from_index(&map, probe, &want);
            /* Only the topmost visible level can be picked; lower cells are
             * legitimately occluded by the one above. */
            if (want.z != camera.view_level) {
                continue;
            }
            if (kt_project(&map, &projection, &camera, want, &at) != KT_OK) {
                fail("project failed");
                continue;
            }
            ++g_checks;
            if (kt_pick_cell(&map, &projection, &camera, at, &got) != KT_OK) {
                fail("pick found nothing at a projected cell origin");
            } else if (!kt_cell_point_equal(got, want)) {
                fail("pick returned a different cell than was projected");
            }
        }
    }
    g_case = NULL;
}

/*
 * ---- property 4: sight symmetry ----
 * Not asserted blindly. A Bresenham trace is not symmetric in general, and
 * C-COM's is described as an exact legacy walk, so this MEASURES the engine's
 * symmetry and reports it. The migration needs to know the real answer: if
 * the engine is asymmetric, per-side visibility must always be evaluated from
 * the viewer, never cached as a mutual fact.
 */
static void test_sight_symmetry(void)
{
    int iteration;
    long asymmetric = 0;
    long pairs = 0;

    printf("sight symmetry (measured, not assumed)\n");
    for (iteration = 0; iteration < 25; ++iteration) {
        kt_map map;
        size_t a;

        g_state = UINT64_C(0x516907) + (uint64_t)iteration;
        build_random_map(&map, 10u + next_below(15u), 0u, 1);

        for (a = 0u; a < kt_map_cell_count(&map); ++a) {
            size_t trials;

            for (trials = 0u; trials < 6u; ++trials) {
                kt_cell_point pa;
                kt_cell_point pb;
                bool ab;
                bool ba;

                kt_map_point_from_index(&map, a, &pa);
                kt_map_point_from_index(
                    &map, next_below((uint32_t)kt_map_cell_count(&map)), &pb);
                if (kt_cell_point_equal(pa, pb)) {
                    continue;
                }
                ab = kt_sight_line(&map, NULL, pa, pb, KT_CHANNEL_SIGHT);
                ba = kt_sight_line(&map, NULL, pb, pa, KT_CHANNEL_SIGHT);
                ++pairs;
                if (ab != ba) {
                    ++asymmetric;
                }
            }
        }
    }
    printf("    %ld of %ld ordered pairs disagree on mutual visibility"
           " (%.2f%%)\n",
           asymmetric, pairs,
           pairs ? 100.0 * (double)asymmetric / (double)pairs : 0.0);
    /* Recorded as a fact about the trace, not a pass/fail gate. */
    check(pairs > 0, "symmetry sampled");
}

/* ---- property 5: draw queue sorting is a stable total order ---- */
static void test_queue_is_permutation(void)
{
    static kt_draw_item items[PCELLS * 2];
    int iteration;

    printf("draw queue sorting is a stable permutation\n");
    for (iteration = 0; iteration < 20; ++iteration) {
        kt_map map;
        kt_projection projection;
        kt_camera camera;
        kt_draw_queue queue;
        size_t count;
        size_t i;
        uint64_t handle_sum_before = 0u;
        uint64_t handle_sum_after = 0u;
        char label[64];

        g_state = UINT64_C(0xD8A9) + (uint64_t)iteration;
        snprintf(label, sizeof(label), "queue iter %d", iteration);
        g_case = label;

        build_random_map(&map, 0u, 0u, 1 + (int32_t)next_below(PD));
        kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW);
        kt_camera_init(&camera);
        camera.rotation = (uint8_t)next_below(4u);
        kt_draw_queue_init(&queue, items, sizeof(items) / sizeof(items[0]));

        count = kt_map_cell_count(&map);
        for (i = 0u; i < count; ++i) {
            kt_cell_point p;
            uint32_t handle = (uint32_t)next_u64();

            kt_map_point_from_index(&map, i, &p);
            if (kt_draw_submit_cell(&queue, &map, &projection, &camera, p,
                                    (uint16_t)next_below(200u), handle,
                                    NULL) == KT_OK) {
                handle_sum_before += handle;
            }
        }
        check(kt_draw_queue_sort(&queue) == KT_OK, "sort ok");
        for (i = 0u; i < queue.count; ++i) {
            handle_sum_after += queue.items[i].handle;
        }
        check(handle_sum_before == handle_sum_after,
              "sorting preserves the multiset of items");
        for (i = 1u; i < queue.count; ++i) {
            const kt_draw_item *prev = &queue.items[i - 1u];
            const kt_draw_item *cur = &queue.items[i];
            bool ordered = prev->depth < cur->depth ||
                           (prev->depth == cur->depth &&
                            (prev->layer < cur->layer ||
                             (prev->layer == cur->layer &&
                              prev->sequence < cur->sequence)));

            check(ordered, "adjacent pair is strictly ordered");
        }
    }
    g_case = NULL;
}


/* ---- property 6: settle-ordered collection is deterministic and ordered ---- */
static void test_collect_properties(void)
{
    int iteration;
    static kt_cell_point first[PCELLS];
    static kt_cell_point again[PCELLS];

    printf("settle-ordered collection\n");
    for (iteration = 0; iteration < 25; ++iteration) {
        kt_map map;
        kt_nav_workspace workspace;
        kt_nav_hooks hooks;
        step_ctx ctx;
        size_t count_a = 0u;
        size_t count_b = 0u;
        size_t i;
        char label[64];

        g_state = UINT64_C(0xC01AEC) + (uint64_t)iteration;
        snprintf(label, sizeof(label), "collect iter %d", iteration);
        g_case = label;

        {
            unsigned walls = 10u + next_below(15u);
            unsigned blocks = 5u + next_below(10u);

            build_random_map(&map, walls, blocks, 1);
        }
        kt_map_cell(&map, kt_cell_point_make(1, 1, 0))->move_cost = 4u;
        kt_map_validate(&map);
        ctx.map = &map;
        ctx.allow_diagonal = true;
        ctx.level_links = false;

        kt_nav_workspace_init_indexed(&workspace, g_nodes, PCELLS, g_heap,
                                      PCELLS, g_heap_pos, PCELLS);
        kt_nav_hooks_init(&hooks);
        hooks.step_cost = prop_step;
        hooks.user = &ctx;
        hooks.min_step_cost = 2u;

        check(kt_nav_reachable_collect(&map, &workspace, &hooks,
                                       kt_cell_point_make(1, 1, 0), 40u, first,
                                       PCELLS, &count_a) == KT_OK,
              "collect ok");
        check(kt_nav_reachable_collect(&map, &workspace, &hooks,
                                       kt_cell_point_make(1, 1, 0), 40u, again,
                                       PCELLS, &count_b) == KT_OK,
              "recollect ok");
        check(count_a == count_b, "collection count is stable");
        check(memcmp(first, again, count_a * sizeof(first[0])) == 0,
              "collection ORDER is stable, not just the set");

        /* Non-decreasing cost is what makes it a settle order. */
        for (i = 1u; i < count_a; ++i) {
            uint32_t prev = kt_nav_cost_to(&map, &workspace, first[i - 1u]);
            uint32_t cur = kt_nav_cost_to(&map, &workspace, first[i]);

            check(prev <= cur, "settle order is non-decreasing in cost");
        }
    }
    g_case = NULL;
}

/* ---- property 7: both depth orders are total over distinct cells ---- */
static void test_depth_order_totality(void)
{
    static int64_t keys[PCELLS];
    int mode;

    printf("depth-order totality\n");
    for (mode = 0; mode < 2; ++mode) {
        kt_map map;
        kt_projection projection;
        kt_camera camera;
        size_t count;
        size_t i;
        size_t j;
        int rotation;

        g_state = UINT64_C(0xDEBD) + (uint64_t)mode;
        build_random_map(&map, 0u, 0u, PD);
        kt_map_validate(&map);
        kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW);
        projection.depth_order = mode == 0 ? KT_DEPTH_DIAGONAL_MAJOR
                                           : KT_DEPTH_LEVEL_MAJOR;
        kt_camera_init(&camera);
        count = kt_map_cell_count(&map);

        for (rotation = 0; rotation < 4; ++rotation) {
            camera.rotation = (uint8_t)rotation;
            for (i = 0u; i < count; ++i) {
                kt_cell_point p;

                kt_map_point_from_index(&map, i, &p);
                kt_depth_key(&map, &projection, &camera, p, &keys[i]);
            }
            /*
             * Distinct cells must get distinct keys, or paint order falls
             * through to submission order and the queue hash stops being a
             * usable golden. Checked by sorting rather than O(n^2).
             */
            for (i = 0u; i < count; ++i) {
                for (j = i + 1u; j < count; ++j) {
                    if (keys[i] == keys[j]) {
                        fail("two distinct cells share a depth key");
                        i = count;
                        break;
                    }
                }
                if (i >= count) {
                    break;
                }
            }
            ++g_checks;
        }
    }
}

int main(void)
{
    test_astar_optimality();
    test_reachable_agrees();
    test_pick_inverts_project();
    test_sight_symmetry();
    test_queue_is_permutation();
    test_collect_properties();
    test_depth_order_totality();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
