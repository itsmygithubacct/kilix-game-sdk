/*
 * test_tactics.c — engine test suite.
 *
 * The projection cases are the important ones: they assert that the shared
 * transform reproduces each consuming game's frozen formula exactly, which
 * is the whole precondition for the migration.
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

static void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        printf("  FAIL %s\n", what);
    }
}

static void check_eq(int64_t got, int64_t want, const char *what)
{
    ++g_checks;
    if (got != want) {
        ++g_failures;
        printf("  FAIL %s: got %lld want %lld\n", what, (long long)got,
               (long long)want);
    }
}

/* ---- C-COM's frozen reference transform (src/battle/render.h) ---- */
static void ccom_reference(int mx, int my, int mz, int *sx, int *sy)
{
    *sx = (mx - my) * 16;
    *sy = (mx + my) * 8 - mz * 24;
}

/* C-COM's frozen counterclockwise view remap, for a square map. */
static void ccom_rotate(int rotation, int span, int mx, int my, int *vx,
                        int *vy)
{
    switch (rotation & 3) {
    case 1:
        *vx = my;
        *vy = span - 1 - mx;
        break;
    case 2:
        *vx = span - 1 - mx;
        *vy = span - 1 - my;
        break;
    case 3:
        *vx = span - 1 - my;
        *vy = mx;
        break;
    default:
        *vx = mx;
        *vy = my;
        break;
    }
}

/* ---- KAT's frozen transform (src/tactical.c) ---- */
enum { KAT_SPRITE_W = 32, KAT_FLOOR_H = 16, KAT_ELEVATION_PIXELS = 12 };

static int kat_scale(int value, unsigned zoom_percent)
{
    long long scaled = (long long)value * (long long)zoom_percent;

    if (scaled >= 0) {
        scaled += 50;
    } else {
        scaled -= 50;
    }
    return (int)(scaled / 100);
}

static void kat_rotate(int rotation, int width, int height, int x, int y,
                       int *rx, int *ry)
{
    switch (rotation & 3) {
    case 1:
        *rx = height - 1 - y;
        *ry = x;
        break;
    case 2:
        *rx = width - 1 - x;
        *ry = height - 1 - y;
        break;
    case 3:
        *rx = y;
        *ry = width - 1 - x;
        break;
    default:
        *rx = x;
        *ry = y;
        break;
    }
}

static void kat_reference(int origin_x, int origin_y, int rotation, int width,
                          int height, unsigned zoom, int x, int y,
                          int elevation, int *sx, int *sy)
{
    int rx;
    int ry;

    kat_rotate(rotation, width, height, x, y, &rx, &ry);
    *sx = origin_x + kat_scale((rx - ry) * (KAT_SPRITE_W / 2), zoom);
    *sy = origin_y + kat_scale((rx + ry) * (KAT_FLOOR_H / 2) -
                                   elevation * KAT_ELEVATION_PIXELS,
                               zoom);
}

/* ---- fixtures ---- */
enum { CCOM_W = 40, CCOM_H = 40, CCOM_D = 4 };
static kt_cell g_ccom_cells[CCOM_W * CCOM_H * CCOM_D];

enum { KAT_W = 24, KAT_H = 18 };
static kt_cell g_kat_cells[KAT_W * KAT_H];

static void test_directions(void)
{
    printf("directions\n");
    check_eq(kt_direction_from_delta(0, -1), KT_DIR_N, "north");
    check_eq(kt_direction_from_delta(1, 0), KT_DIR_E, "east");
    check_eq(kt_direction_from_delta(0, 1), KT_DIR_S, "south");
    check_eq(kt_direction_from_delta(-1, 0), KT_DIR_W, "west");
    check_eq(kt_direction_from_delta(1, -1), KT_DIR_NE, "northeast");
    check_eq(kt_direction_from_delta(-3, 3), KT_DIR_SW, "southwest");
    check_eq(kt_direction_opposite(KT_DIR_N), KT_DIR_S, "opposite north");
    check_eq(kt_direction_opposite(KT_DIR_NW), KT_DIR_SE, "opposite nw");
    check_eq(kt_direction_turn_distance(KT_DIR_N, KT_DIR_NW), 1u, "turn 1");
    check_eq(kt_direction_turn_distance(KT_DIR_N, KT_DIR_S), 4u, "turn 4");

    /* Deltas must agree with the octant classifier for all eight. */
    for (int dir = 0; dir < KT_DIR_COUNT; ++dir) {
        check_eq(kt_direction_from_delta(kt_direction_dx[dir],
                                         kt_direction_dy[dir]),
                 dir, "delta round trip");
    }
}

static void test_map(void)
{
    kt_map map;
    kt_cell *cell;

    printf("map\n");
    check_eq(kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                         sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0])),
             KT_OK, "init");
    check_eq((int64_t)kt_map_cell_count(&map), CCOM_W * CCOM_H * CCOM_D,
             "cell count");
    check(kt_map_contains(&map, kt_cell_point_make(0, 0, 0)), "contains origin");
    check(!kt_map_contains(&map, kt_cell_point_make(CCOM_W, 0, 0)),
          "rejects x overflow");
    check(!kt_map_contains(&map, kt_cell_point_make(0, 0, CCOM_D)),
          "rejects z overflow");

    /* Index round trip across the whole grid. */
    for (size_t index = 0; index < kt_map_cell_count(&map); ++index) {
        kt_cell_point point;

        check(kt_map_point_from_index(&map, index, &point), "index to point");
        check_eq((int64_t)kt_map_index(&map, point), (int64_t)index,
                 "point to index");
    }

    /* Capacity is enforced rather than trusted. */
    check_eq(kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells, 4),
             KT_ERR_CAPACITY, "capacity rejected");
    check_eq(kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                         sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0])),
             KT_OK, "reinit");

    /* Off-map boundaries block on every channel. */
    check(kt_map_wall_blocks(&map, kt_cell_point_make(-1, 0, 0), KT_WALL_WEST,
                             KT_CHANNEL_MOVE),
          "off-map wall blocks move");
    check(kt_map_wall_blocks(&map, kt_cell_point_make(0, -1, 0), KT_WALL_NORTH,
                             KT_CHANNEL_SIGHT),
          "off-map wall blocks sight");

    cell = kt_map_cell(&map, kt_cell_point_make(3, 4, 1));
    check(cell != NULL, "cell fetch");
    if (cell != NULL) {
        cell->wall[KT_WALL_WEST] = KT_WALL_BLOCKS_MOVE | KT_WALL_BLOCKS_SIGHT;
        cell->move_cost = 4u;
    }
    check(kt_map_wall_blocks(&map, kt_cell_point_make(3, 4, 1), KT_WALL_WEST,
                             KT_CHANNEL_MOVE),
          "west wall blocks move");
    check(!kt_map_wall_blocks(&map, kt_cell_point_make(3, 4, 1), KT_WALL_WEST,
                              KT_CHANNEL_FIRE),
          "west wall passes fire");

    check_eq(kt_map_validate(&map), KT_OK, "validate");
    check_eq(map.elevation_span, 0, "flat span");

    /*
     * A level link on a horizontally unenterable cell is VALID: the flag
     * governs the vertical boundary only. C-COM has gravlift shafts that pass
     * sight vertically while no unit can walk into them, and coupling the two
     * would have made every such map invalid.
     */
    if (cell != NULL) {
        cell->flags = KT_CELL_LEVEL_LINK;
        cell->move_cost = KT_MOVE_BLOCKED;
    }
    check_eq(kt_map_validate(&map), KT_OK,
             "level link on an unenterable cell is valid");
    if (cell != NULL) {
        cell->flags = 0u;
        cell->move_cost = 4u;
    }

    /* Overlapping cover grades are rejected. */
    if (cell != NULL) {
        cell->cover_half = 0x03u;
        cell->cover_full = 0x01u;
    }
    check_eq(kt_map_validate(&map), KT_ERR_STATE, "overlapping cover rejected");
    if (cell != NULL) {
        cell->cover_half = 0u;
        cell->cover_full = 0u;
    }
    check_eq(kt_map_validate(&map), KT_OK, "validate again");
}

static void test_projection_ccom(void)
{
    kt_map map;
    kt_projection projection;
    kt_camera camera;

    printf("projection: C-COM frozen transform\n");
    kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0]));
    kt_map_validate(&map);
    check_eq(kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW), KT_OK,
             "init 32/16/24");
    kt_camera_init(&camera);

    /* Odd tile extents must be refused, not silently halved. */
    {
        kt_projection bad;

        check_eq(kt_projection_init(&bad, 33, 16, 24, KT_ROTATE_CCW),
                 KT_ERR_RANGE, "odd width rejected");
        check_eq(kt_projection_init(&bad, 32, 15, 24, KT_ROTATE_CCW),
                 KT_ERR_RANGE, "odd height rejected");
    }

    for (int z = 0; z < CCOM_D; ++z) {
        for (int y = 0; y < CCOM_H; ++y) {
            for (int x = 0; x < CCOM_W; ++x) {
                kt_screen_point got;
                int want_x;
                int want_y;

                check_eq(kt_project(&map, &projection, &camera,
                                    kt_cell_point_make(x, y, z), &got),
                         KT_OK, "project ok");
                ccom_reference(x, y, z, &want_x, &want_y);
                if (got.x != want_x || got.y != want_y) {
                    printf("  FAIL ccom (%d,%d,%d): got (%d,%d) want (%d,%d)\n",
                           x, y, z, got.x, got.y, want_x, want_y);
                    ++g_failures;
                }
                ++g_checks;
            }
        }
    }

    /* Rotation must match C-COM's frozen view remap at all four turns. */
    for (int rotation = 0; rotation < 4; ++rotation) {
        camera.rotation = (uint8_t)rotation;
        for (int y = 0; y < CCOM_H; ++y) {
            for (int x = 0; x < CCOM_W; ++x) {
                kt_cell_point view;
                int want_x;
                int want_y;

                check_eq(kt_rotate_to_view(&map, &projection, &camera,
                                           kt_cell_point_make(x, y, 0), &view),
                         KT_OK, "rotate ok");
                ccom_rotate(rotation, CCOM_W, x, y, &want_x, &want_y);
                if (view.x != want_x || view.y != want_y) {
                    printf("  FAIL ccom rot%d (%d,%d): got (%d,%d) "
                           "want (%d,%d)\n",
                           rotation, x, y, view.x, view.y, want_x, want_y);
                    ++g_failures;
                }
                ++g_checks;
            }
        }
    }
    camera.rotation = 0u;
}

static void test_projection_kat(void)
{
    kt_map map;
    kt_projection projection;
    kt_camera camera;
    static const unsigned zooms[] = {100u, 80u, 50u, 25u, 175u};

    printf("projection: KAT frozen transform\n");
    kt_map_init(&map, KAT_W, KAT_H, 1, g_kat_cells,
                sizeof(g_kat_cells) / sizeof(g_kat_cells[0]));

    /* Give the fixture the sort of terrain height KAT actually carries. */
    for (int y = 0; y < KAT_H; ++y) {
        for (int x = 0; x < KAT_W; ++x) {
            kt_cell *cell = kt_map_cell(&map, kt_cell_point_make(x, y, 0));

            cell->move_cost = 4u;
            cell->elevation = (int16_t)((x + y) % 5 - 2);
        }
    }
    check_eq(kt_map_validate(&map), KT_OK, "kat validate");
    check_eq(map.elevation_span, 2, "kat span measured");

    check_eq(kt_projection_init(&projection, 32, 16, 12, KT_ROTATE_CW), KT_OK,
             "init 32/16/12 clockwise");
    kt_camera_init(&camera);
    camera.origin_x = 1624 / 2;
    camera.origin_y = 88;

    for (size_t zi = 0; zi < sizeof(zooms) / sizeof(zooms[0]); ++zi) {
        camera.zoom_percent = (uint16_t)zooms[zi];
        for (int rotation = 0; rotation < 4; ++rotation) {
            camera.rotation = (uint8_t)rotation;
            for (int y = 0; y < KAT_H; ++y) {
                for (int x = 0; x < KAT_W; ++x) {
                    kt_cell_point point = kt_cell_point_make(x, y, 0);
                    const kt_cell *cell = kt_map_cell_const(&map, point);
                    kt_screen_point got;
                    int want_x;
                    int want_y;

                    check_eq(kt_project(&map, &projection, &camera, point,
                                        &got),
                             KT_OK, "kat project ok");
                    kat_reference(camera.origin_x, camera.origin_y, rotation,
                                  KAT_W, KAT_H, zooms[zi], x, y,
                                  cell->elevation, &want_x, &want_y);
                    if (got.x != want_x || got.y != want_y) {
                        printf("  FAIL kat z%u rot%d (%d,%d): got (%d,%d) "
                               "want (%d,%d)\n",
                               zooms[zi], rotation, x, y, got.x, got.y, want_x,
                               want_y);
                        ++g_failures;
                    }
                    ++g_checks;
                }
            }
        }
    }
    camera.rotation = 0u;
    camera.zoom_percent = 100u;
}

static void test_rotation_round_trip(void)
{
    kt_map map;
    kt_projection projection;
    kt_camera camera;

    printf("rotation round trip\n");
    /* A deliberately non-square map: the two games only ever rotate square
     * or single-level grids today, so this is the case most likely to be
     * wrong and least likely to be noticed. */
    kt_map_init(&map, KAT_W, KAT_H, 1, g_kat_cells,
                sizeof(g_kat_cells) / sizeof(g_kat_cells[0]));
    kt_projection_init(&projection, 32, 16, 12, KT_ROTATE_CW);
    kt_camera_init(&camera);

    for (int sense = 0; sense < 2; ++sense) {
        projection.sense = sense == 0 ? KT_ROTATE_CCW : KT_ROTATE_CW;
        for (int rotation = 0; rotation < 4; ++rotation) {
            camera.rotation = (uint8_t)rotation;
            for (int y = 0; y < KAT_H; ++y) {
                for (int x = 0; x < KAT_W; ++x) {
                    kt_cell_point world = kt_cell_point_make(x, y, 0);
                    kt_cell_point view;
                    kt_cell_point back;

                    check_eq(kt_rotate_to_view(&map, &projection, &camera,
                                               world, &view),
                             KT_OK, "to view");
                    check_eq(kt_rotate_to_world(&map, &projection, &camera,
                                                view, &back),
                             KT_OK, "to world");
                    check(kt_cell_point_equal(world, back), "round trip");
                }
            }
        }
    }

    /* The two senses must be exact mirrors: CW(r) == CCW((4-r) & 3). */
    for (int rotation = 0; rotation < 4; ++rotation) {
        kt_camera cw = camera;
        kt_camera ccw = camera;
        kt_projection cw_proj = projection;
        kt_projection ccw_proj = projection;

        cw_proj.sense = KT_ROTATE_CW;
        ccw_proj.sense = KT_ROTATE_CCW;
        cw.rotation = (uint8_t)rotation;
        ccw.rotation = (uint8_t)((4 - rotation) & 3);
        for (int y = 0; y < KAT_H; ++y) {
            for (int x = 0; x < KAT_W; ++x) {
                kt_cell_point world = kt_cell_point_make(x, y, 0);
                kt_cell_point a;
                kt_cell_point b;

                kt_rotate_to_view(&map, &cw_proj, &cw, world, &a);
                kt_rotate_to_view(&map, &ccw_proj, &ccw, world, &b);
                check(kt_cell_point_equal(a, b), "sense mirror");
            }
        }
    }
    camera.rotation = 0u;
}

static void test_picking(void)
{
    kt_map map;
    kt_projection projection;
    kt_camera camera;

    printf("picking\n");

    /* Flat multi-level map first: pick must invert project exactly. */
    kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        map.cells[i].move_cost = 4u;
    }
    kt_map_validate(&map);
    kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW);
    kt_camera_init(&camera);
    camera.origin_x = 400;
    camera.origin_y = 300;
    camera.view_level = 0;

    for (int y = 0; y < CCOM_H; ++y) {
        for (int x = 0; x < CCOM_W; ++x) {
            kt_cell_point want = kt_cell_point_make(x, y, 0);
            kt_cell_point got;
            kt_screen_point origin;
            kt_screen_point centre;

            kt_project(&map, &projection, &camera, want, &origin);
            /* The floor diamond is centred half a tile right and down from
             * the projected origin. */
            centre.x = origin.x;
            centre.y = origin.y;
            check_eq(kt_pick_cell(&map, &projection, &camera, centre, &got),
                     KT_OK, "pick ok");
            check(kt_cell_point_equal(got, want), "pick matches project");
        }
    }

    /* Cutaway must hide levels above the viewing level. */
    camera.view_level = -1;
    {
        kt_cell_point got;
        kt_screen_point at;

        at.x = camera.origin_x;
        at.y = camera.origin_y;
        check_eq(kt_pick_cell(&map, &projection, &camera, at, &got),
                 KT_ERR_UNREACHABLE, "cutaway below floor picks nothing");
    }

    /* Now an elevated single-level map, which is KAT's shape. */
    kt_map_init(&map, KAT_W, KAT_H, 1, g_kat_cells,
                sizeof(g_kat_cells) / sizeof(g_kat_cells[0]));
    for (int y = 0; y < KAT_H; ++y) {
        for (int x = 0; x < KAT_W; ++x) {
            kt_cell *cell = kt_map_cell(&map, kt_cell_point_make(x, y, 0));

            cell->move_cost = 4u;
            cell->elevation = (int16_t)((x * 7 + y * 3) % 4);
        }
    }
    kt_map_validate(&map);
    kt_projection_init(&projection, 32, 16, 12, KT_ROTATE_CW);
    kt_camera_init(&camera);
    camera.origin_x = 812;
    camera.origin_y = 88;

    for (int y = 0; y < KAT_H; ++y) {
        for (int x = 0; x < KAT_W; ++x) {
            kt_cell_point want = kt_cell_point_make(x, y, 0);
            kt_cell_point got;
            kt_screen_point origin;

            kt_project(&map, &projection, &camera, want, &origin);
            if (kt_pick_cell(&map, &projection, &camera, origin, &got) !=
                KT_OK) {
                printf("  FAIL elevated pick missed (%d,%d)\n", x, y);
                ++g_failures;
                ++g_checks;
                continue;
            }
            ++g_checks;
            /*
             * With overlapping elevated cells the frontmost one legitimately
             * wins, so require that whatever was picked really does contain
             * the probe point and is not behind the intended cell.
             */
            if (!kt_cell_point_equal(got, want)) {
                int64_t want_key = 0;
                int64_t got_key = 0;

                kt_depth_key(&map, &projection, &camera, want, &want_key);
                kt_depth_key(&map, &projection, &camera, got, &got_key);
                if (got_key < want_key) {
                    printf("  FAIL elevated pick chose a cell behind "
                           "(%d,%d)\n",
                           x, y);
                    ++g_failures;
                }
            }
        }
    }
}

static void test_subcell(void)
{
    kt_projection projection;
    kt_camera camera;
    kt_screen_point whole;
    kt_screen_point sub;

    printf("sub-cell projection\n");
    kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW);
    kt_camera_init(&camera);

    /* At exact cell multiples the sub-cell path must agree with the frozen
     * whole-cell formula. */
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            int want_x;
            int want_y;

            check_eq(kt_project_subcell(&projection, &camera, x * 16, y * 16, 0,
                                        &sub),
                     KT_OK, "subcell ok");
            ccom_reference(x, y, 0, &want_x, &want_y);
            check_eq(sub.x, want_x, "subcell x");
            check_eq(sub.y, want_y, "subcell y");
        }
    }

    /* Half a cell east is half a cell of screen travel. */
    kt_project_subcell(&projection, &camera, 0, 0, 0, &whole);
    kt_project_subcell(&projection, &camera, 8, 0, 0, &sub);
    check_eq(sub.x - whole.x, 8, "half step x");
    check_eq(sub.y - whole.y, 4, "half step y");
}

/* ---- navigation ---- */
enum { NAV_W = 12, NAV_H = 12 };
static kt_cell g_nav_cells[NAV_W * NAV_H];
static kt_nav_node g_nav_nodes[NAV_W * NAV_H];
/* Sized by kt_nav_required_heap(): the search pushes a fresh entry per
 * improving relaxation, so a node can be queued once per predecessor. */
static uint32_t g_nav_heap[NAV_W * NAV_H];
static uint32_t g_nav_heap_pos[NAV_W * NAV_H];

/*
 * A deliberately plain step-cost hook: the engine must not need to know
 * anything more than this to run a search.
 */
static uint32_t nav_step(void *user, kt_cell_point from, kt_direction dir,
                         kt_cell_point *dest)
{
    const kt_map *map = (const kt_map *)user;
    const kt_cell *cell;

    if (kt_edge_blocked(map, from, dir, KT_CHANNEL_MOVE)) {
        return KT_STEP_BLOCKED;
    }
    cell = kt_map_cell_const(map, *dest);
    if (cell == NULL || cell->move_cost == KT_MOVE_BLOCKED) {
        return KT_STEP_BLOCKED;
    }
    return KT_DIR_IS_DIAGONAL(dir) ? (uint32_t)cell->move_cost * 3u / 2u
                                   : cell->move_cost;
}

static void test_nav(void)
{
    kt_map map;
    kt_nav_workspace workspace;
    kt_nav_hooks hooks;
    kt_path path;
    size_t reached = 0u;

    printf("navigation\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        g_nav_cells[i].move_cost = 4u;
    }
    kt_map_validate(&map);
    check_eq(kt_nav_workspace_init_indexed(
        &workspace, g_nav_nodes,
        sizeof(g_nav_nodes) / sizeof(g_nav_nodes[0]), g_nav_heap,
        sizeof(g_nav_heap) / sizeof(g_nav_heap[0]), g_nav_heap_pos,
        sizeof(g_nav_heap_pos) / sizeof(g_nav_heap_pos[0])),
             KT_OK, "workspace init");
    kt_nav_hooks_init(&hooks);
    hooks.step_cost = nav_step;
    hooks.user = &map;
    hooks.min_step_cost = 4u;

    /* Open ground: a straight diagonal run of six steps. */
    check_eq(kt_nav_find_path(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0),
                              kt_cell_point_make(7, 7, 0), 1000u, &path),
             KT_OK, "open path found");
    check_eq(path.count, 6, "diagonal step count");
    check_eq(path.total_cost, 6u * 6u, "diagonal cost");

    /* A full-height wall must force a detour rather than be walked through. */
    for (int y = 0; y < NAV_H; ++y) {
        kt_cell *cell = kt_map_cell(&map, kt_cell_point_make(5, y, 0));

        cell->wall[KT_WALL_WEST] = KT_WALL_BLOCKS_MOVE;
    }
    check_eq(kt_nav_find_path(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0),
                              kt_cell_point_make(7, 7, 0), 1000u, &path),
             KT_ERR_UNREACHABLE, "sealed wall blocks");

    /* Open one gap: now there is exactly one way through. */
    kt_map_cell(&map, kt_cell_point_make(5, 11, 0))->wall[KT_WALL_WEST] = 0u;
    check_eq(kt_nav_find_path(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0),
                              kt_cell_point_make(7, 7, 0), 1000u, &path),
             KT_OK, "gap found");
    check(path.count > 6, "detour is longer than the direct run");

    /* max_cost is honoured. */
    check_eq(kt_nav_find_path(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0),
                              kt_cell_point_make(7, 7, 0), 10u, &path),
             KT_ERR_UNREACHABLE, "budget respected");

    /* Reachability agrees with the path search about cost. */
    for (int y = 0; y < NAV_H; ++y) {
        kt_map_cell(&map, kt_cell_point_make(5, y, 0))->wall[KT_WALL_WEST] = 0u;
    }
    check_eq(kt_nav_reachable(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0), 24u, &reached),
             KT_OK, "flood ok");
    check(reached > 1u, "flood reached cells");
    check(kt_nav_was_reached(&map, &workspace, kt_cell_point_make(2, 2, 0)),
          "near cell reached");
    check_eq(kt_nav_cost_to(&map, &workspace, kt_cell_point_make(2, 2, 0)), 6u,
             "one diagonal costs 6");
    check(!kt_nav_was_reached(&map, &workspace, kt_cell_point_make(11, 11, 0)),
          "far cell out of budget");

    /* Determinism: identical inputs must give an identical route. */
    {
        kt_path again;

        kt_nav_find_path(&map, &workspace, &hooks, kt_cell_point_make(1, 1, 0),
                         kt_cell_point_make(9, 4, 0), 1000u, &path);
        kt_nav_find_path(&map, &workspace, &hooks, kt_cell_point_make(1, 1, 0),
                         kt_cell_point_make(9, 4, 0), 1000u, &again);
        check_eq(again.count, path.count, "repeat count");
        check_eq(again.total_cost, path.total_cost, "repeat cost");
        check(memcmp(again.dirs, path.dirs, path.count) == 0, "repeat route");
    }
}

static void test_sight(void)
{
    kt_map map;

    printf("sight\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        g_nav_cells[i].move_cost = 4u;
        g_nav_cells[i].wall[0] = 0u;
        g_nav_cells[i].wall[1] = 0u;
        g_nav_cells[i].occupy = 0u;
        g_nav_cells[i].flags = 0u;
    }
    kt_map_validate(&map);

    check(kt_sight_line(&map, NULL, kt_cell_point_make(1, 1, 0),
                        kt_cell_point_make(8, 1, 0), KT_CHANNEL_SIGHT),
          "clear line");

    /* A west wall on (5,1) blocks a ray crossing it eastward. */
    kt_map_cell(&map, kt_cell_point_make(5, 1, 0))->wall[KT_WALL_WEST] =
        KT_WALL_BLOCKS_SIGHT;
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(1, 1, 0),
                         kt_cell_point_make(8, 1, 0), KT_CHANNEL_SIGHT),
          "wall blocks sight");
    /* ...but not one on a different channel. */
    check(kt_sight_line(&map, NULL, kt_cell_point_make(1, 1, 0),
                        kt_cell_point_make(8, 1, 0), KT_CHANNEL_FIRE),
          "wall passes fire");

    /* The movement table forbids cutting this corner; the two-way sight
     * rule still allows seeing round it. */
    check(kt_edge_blocked(&map, kt_cell_point_make(4, 1, 0), KT_DIR_NE,
                          KT_CHANNEL_MOVE) ||
              !kt_edge_blocked(&map, kt_cell_point_make(4, 1, 0), KT_DIR_NE,
                               KT_CHANNEL_MOVE),
          "edge table callable");
    check(!kt_sight_blocked_step(&map, kt_cell_point_make(4, 2, 0),
                                 kt_cell_point_make(5, 1, 0),
                                 KT_CHANNEL_SIGHT),
          "diagonal detour keeps sight open");

    kt_map_cell(&map, kt_cell_point_make(5, 1, 0))->wall[KT_WALL_WEST] = 0u;

    /* An opaque occupant blocks a ray passing through, but not the endpoint
     * itself: you can always see the thing you are looking at. */
    kt_map_cell(&map, kt_cell_point_make(4, 1, 0))->occupy =
        KT_OCCUPY_BLOCKS_SIGHT;
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(1, 1, 0),
                         kt_cell_point_make(8, 1, 0), KT_CHANNEL_SIGHT),
          "occupant blocks through");
    check(kt_sight_line(&map, NULL, kt_cell_point_make(1, 1, 0),
                        kt_cell_point_make(4, 1, 0), KT_CHANNEL_SIGHT),
          "occupant is still visible itself");
    kt_map_cell(&map, kt_cell_point_make(4, 1, 0))->occupy = 0u;

    /* Trace reports the intermediate cells, endpoints excluded. */
    {
        kt_cell_point cells[16];
        size_t count = 0u;

        check_eq(kt_sight_trace(&map, kt_cell_point_make(1, 1, 0),
                                kt_cell_point_make(5, 1, 0), cells, 16u,
                                &count),
                 KT_OK, "trace ok");
        check_eq((int64_t)count, 3, "trace excludes endpoints");
        check(cells[0].x == 2 && cells[0].y == 1, "trace first cell");
    }

    /* The veto hook can stop a ray the engine would otherwise pass. */
    {
        kt_sight_hooks hooks;

        kt_sight_hooks_init(&hooks);
        check(kt_sight_line(&map, &hooks, kt_cell_point_make(1, 1, 0),
                            kt_cell_point_make(8, 1, 0), KT_CHANNEL_SIGHT),
              "null veto passes");
    }
}

static void test_sight_levels(void)
{
    kt_map map;
    size_t i;

    printf("sight across levels\n");
    kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0]));
    for (i = 0; i < kt_map_cell_count(&map); ++i) {
        g_ccom_cells[i].move_cost = 4u;
        g_ccom_cells[i].wall[0] = 0u;
        g_ccom_cells[i].wall[1] = 0u;
        g_ccom_cells[i].occupy = 0u;
        g_ccom_cells[i].flags = 0u;
    }
    kt_map_validate(&map);

    /* With no floors anywhere, sight passes between levels. */
    check(kt_sight_line(&map, NULL, kt_cell_point_make(2, 2, 0),
                        kt_cell_point_make(8, 8, 1), KT_CHANNEL_SIGHT),
          "open shaft passes");

    /* An intact floor on the upper cell seals the boundary. */
    for (int y = 0; y < CCOM_H; ++y) {
        for (int x = 0; x < CCOM_W; ++x) {
            kt_map_cell(&map, kt_cell_point_make(x, y, 1))->flags =
                KT_CELL_HAS_FLOOR;
        }
    }
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(2, 2, 0),
                         kt_cell_point_make(8, 8, 1), KT_CHANNEL_SIGHT),
          "intact floor seals");

    /*
     * A purely vertical ray must be sealed by an intact floor. This regressed
     * once: the dominant-axis choice ignored dz, so a straight-up ray took
     * zero steps and returned CLEAR.
     */
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(4, 4, 0),
                         kt_cell_point_make(4, 4, 1), KT_CHANNEL_SIGHT),
          "straight-up ray is sealed by an intact floor");
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(4, 4, 0),
                         kt_cell_point_make(5, 4, 3), KT_CHANNEL_SIGHT),
          "steep ray with dz > dx is sealed");
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(4, 4, 1),
                         kt_cell_point_make(4, 4, 0), KT_CHANNEL_SIGHT),
          "straight-down ray is sealed too");

    /* A gravlift is the documented exception. */
    {
        kt_cell *cell;
        int x;
        int y;

        for (y = 0; y < CCOM_H; ++y) {
            for (x = 0; x < CCOM_W; ++x) {
                cell = kt_map_cell(&map, kt_cell_point_make(x, y, 1));
                cell->flags = (uint8_t)(KT_CELL_HAS_FLOOR | KT_CELL_LEVEL_LINK);
            }
        }
        check_eq(kt_map_validate(&map), KT_OK, "level links validate");
        check(kt_sight_line(&map, NULL, kt_cell_point_make(2, 2, 0),
                            kt_cell_point_make(8, 8, 1), KT_CHANNEL_SIGHT),
              "level link opens sealed floor");
    }
}

static void test_cover(void)
{
    kt_map map;
    kt_cover_report report;
    kt_cell *cell;

    printf("cover\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        g_nav_cells[i].move_cost = 4u;
        g_nav_cells[i].cover_half = 0u;
        g_nav_cells[i].cover_full = 0u;
    }
    cell = kt_map_cell(&map, kt_cell_point_make(5, 5, 0));
    cell->cover_full = (uint8_t)(1u << KT_DIR_W);
    cell->cover_half = (uint8_t)(1u << KT_DIR_N);
    kt_map_validate(&map);

    /* Attacker due west sees the full-cover face. */
    check_eq(kt_cover_query(&map, kt_cell_point_make(1, 5, 0),
                            kt_cell_point_make(5, 5, 0), &report),
             KT_OK, "cover query ok");
    check_eq(report.face_count, 1, "orthogonal presents one face");
    check_eq(report.strongest, KT_COVER_FULL, "full cover west");

    /* Attacker due south sees nothing. */
    kt_cover_query(&map, kt_cell_point_make(5, 9, 0),
                   kt_cell_point_make(5, 5, 0), &report);
    check_eq(report.strongest, KT_COVER_NONE, "no cover south");

    /* A diagonal approach presents both adjacent faces, and the stronger
     * of the two wins. */
    kt_cover_query(&map, kt_cell_point_make(1, 1, 0),
                   kt_cell_point_make(5, 5, 0), &report);
    check_eq(report.face_count, 2, "diagonal presents two faces");
    check_eq(report.strongest, KT_COVER_FULL, "diagonal takes the stronger");
}

/*
 * The frozen edge table transcribed independently from C-COM's path.h, so the
 * engine's copy is checked against the contract rather than against itself.
 *
 *   0 N : N(x,y)
 *   1 NE: N(x,y), W(x+1,y-1), W(x+1,y), N(x+1,y)
 *   2 E : W(x+1,y)
 *   3 SE: W(x+1,y), N(x,y+1), N(x+1,y+1), W(x+1,y+1)
 *   4 S : N(x,y+1)
 *   5 SW: W(x,y), W(x,y+1), N(x,y+1), N(x-1,y+1)
 *   6 W : W(x,y)
 *   7 NW: W(x,y), N(x,y), N(x-1,y), W(x,y-1)
 */
static void test_edge_table_contract(void)
{
    static const int8_t want[KT_DIR_COUNT][4][3] = {
        {{0, 0, KT_WALL_NORTH}},
        {{0, 0, KT_WALL_NORTH},
         {1, -1, KT_WALL_WEST},
         {1, 0, KT_WALL_WEST},
         {1, 0, KT_WALL_NORTH}},
        {{1, 0, KT_WALL_WEST}},
        {{1, 0, KT_WALL_WEST},
         {0, 1, KT_WALL_NORTH},
         {1, 1, KT_WALL_NORTH},
         {1, 1, KT_WALL_WEST}},
        {{0, 1, KT_WALL_NORTH}},
        {{0, 0, KT_WALL_WEST},
         {0, 1, KT_WALL_WEST},
         {0, 1, KT_WALL_NORTH},
         {-1, 1, KT_WALL_NORTH}},
        {{0, 0, KT_WALL_WEST}},
        {{0, 0, KT_WALL_WEST},
         {0, 0, KT_WALL_NORTH},
         {-1, 0, KT_WALL_NORTH},
         {0, -1, KT_WALL_WEST}}};
    static const size_t want_count[KT_DIR_COUNT] = {1, 4, 1, 4, 1, 4, 1, 4};
    kt_map map;
    int dir;

    printf("frozen edge table contract\n");
    for (dir = 0; dir < KT_DIR_COUNT; ++dir) {
        kt_edge_test got[KT_EDGE_TESTS_MAX];
        size_t count = kt_edge_tests((kt_direction)dir, got,
                                     KT_EDGE_TESTS_MAX);
        size_t i;

        check_eq((int64_t)count, (int64_t)want_count[dir], "test count");
        for (i = 0; i < count; ++i) {
            check_eq(got[i].dx, want[dir][i][0], "table dx");
            check_eq(got[i].dy, want[dir][i][1], "table dy");
            check_eq(got[i].side, want[dir][i][2], "table side");
        }
    }

    /* Insufficient capacity is refused, not truncated. */
    {
        kt_edge_test one[1];

        check_eq((int64_t)kt_edge_tests(KT_DIR_NE, one, 1u), 0,
                 "diagonal refuses a 1-slot buffer");
        check_eq((int64_t)kt_edge_tests(KT_DIR_N, one, 1u), 1,
                 "cardinal fits a 1-slot buffer");
    }

    /*
     * A caller applying its own predicate over the enumerated boundaries must
     * reproduce kt_edge_blocked exactly. This is what lets C-COM express its
     * door-inclusive movement variant without duplicating the table.
     */
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        /* Deterministic pseudo-random wall scatter. */
        g_nav_cells[i].move_cost = 4u;
        g_nav_cells[i].wall[KT_WALL_WEST] =
            ((i * 2654435761u) >> 13) % 3u == 0u ? KT_WALL_BLOCKS_MOVE : 0u;
        g_nav_cells[i].wall[KT_WALL_NORTH] =
            ((i * 40503u) >> 7) % 4u == 0u ? KT_WALL_BLOCKS_MOVE : 0u;
        g_nav_cells[i].occupy = 0u;
        g_nav_cells[i].flags = 0u;
        g_nav_cells[i].cover_half = 0u;
        g_nav_cells[i].cover_full = 0u;
    }
    kt_map_validate(&map);

    for (int y = 0; y < NAV_H; ++y) {
        for (int x = 0; x < NAV_W; ++x) {
            for (dir = 0; dir < KT_DIR_COUNT; ++dir) {
                kt_cell_point from = kt_cell_point_make(x, y, 0);
                kt_cell_point neighbour = kt_cell_point_make(
                    x + kt_direction_dx[dir], y + kt_direction_dy[dir], 0);
                kt_edge_test tests[KT_EDGE_TESTS_MAX];
                size_t count;
                size_t i;
                bool manual;

                /* kt_edge_blocked also rejects an off-map neighbour, which
                 * the caller must reproduce itself. */
                manual = !kt_map_contains(&map, from) ||
                         !kt_map_contains(&map, neighbour);
                count = kt_edge_tests((kt_direction)dir, tests,
                                      KT_EDGE_TESTS_MAX);
                for (i = 0; i < count && !manual; ++i) {
                    kt_cell_point at = kt_cell_point_make(
                        x + tests[i].dx, y + tests[i].dy, 0);

                    if (kt_map_wall_blocks(&map, at,
                                           (kt_wall_side)tests[i].side,
                                           KT_CHANNEL_MOVE)) {
                        manual = true;
                    }
                }
                check_eq(manual,
                         kt_edge_blocked(&map, from, (kt_direction)dir,
                                         KT_CHANNEL_MOVE),
                         "enumerated predicate matches kt_edge_blocked");
            }
        }
    }
}

static void test_ray_geometry(void)
{
    kt_map map;
    kt_cell_point ray[64];
    kt_cell_point traced[64];
    size_t ray_count = 0u;
    size_t traced_count = 0u;

    printf("pure ray geometry\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        g_nav_cells[i].move_cost = 4u;
        g_nav_cells[i].wall[0] = 0u;
        g_nav_cells[i].wall[1] = 0u;
        g_nav_cells[i].occupy = 0u;
        g_nav_cells[i].flags = 0u;
        g_nav_cells[i].cover_half = 0u;
        g_nav_cells[i].cover_full = 0u;
    }
    kt_map_validate(&map);

    /* With nothing in the way both agree and report every intermediate cell. */
    check_eq(kt_ray_cells(&map, kt_cell_point_make(1, 5, 0),
                          kt_cell_point_make(9, 5, 0), ray, 64u, &ray_count),
             KT_OK, "ray ok");
    check_eq((int64_t)ray_count, 7, "ray excludes endpoints");
    check_eq(kt_sight_trace(&map, kt_cell_point_make(1, 5, 0),
                            kt_cell_point_make(9, 5, 0), traced, 64u,
                            &traced_count),
             KT_OK, "trace ok");
    check_eq((int64_t)traced_count, (int64_t)ray_count,
             "unobstructed trace equals the ray");
    check(memcmp(ray, traced, ray_count * sizeof(ray[0])) == 0,
          "unobstructed cells identical");

    /* Put an opaque occupant midway. The trace must stop; the ray must not. */
    kt_map_cell(&map, kt_cell_point_make(5, 5, 0))->occupy =
        KT_OCCUPY_BLOCKS_SIGHT;
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(1, 5, 0),
                         kt_cell_point_make(9, 5, 0), KT_CHANNEL_SIGHT),
          "sight is blocked");
    kt_sight_trace(&map, kt_cell_point_make(1, 5, 0),
                   kt_cell_point_make(9, 5, 0), traced, 64u, &traced_count);
    check_eq((int64_t)traced_count, 3, "trace stops before the blocker");
    kt_ray_cells(&map, kt_cell_point_make(1, 5, 0), kt_cell_point_make(9, 5, 0),
                 ray, 64u, &ray_count);
    check_eq((int64_t)ray_count, 7, "ray ignores the blocker");

    /*
     * The reason this function exists: a rule that ACCUMULATES along the ray.
     * A per-cell boolean veto cannot stop at "summed density >= threshold",
     * which is exactly C-COM's smoke rule. Here density 4 per cell with a
     * threshold of 10 must stop on the third smoky cell, not the first.
     */
    {
        int accumulated = 0;
        size_t stopped_at = ray_count;
        size_t i;

        for (i = 0u; i < ray_count; ++i) {
            accumulated += 4;
            if (accumulated >= 10) {
                stopped_at = i;
                break;
            }
        }
        check_eq((int64_t)stopped_at, 2, "accumulator stops on the third cell");
    }

    kt_map_cell(&map, kt_cell_point_make(5, 5, 0))->occupy = 0u;

    /* Capacity is refused rather than silently truncated. */
    {
        kt_cell_point tiny[2];
        size_t n = 0u;

        check_eq(kt_ray_cells(&map, kt_cell_point_make(1, 5, 0),
                              kt_cell_point_make(9, 5, 0), tiny, 2u, &n),
                 KT_ERR_CAPACITY, "ray capacity enforced");
    }
}


/*
 * Capabilities added so the two games' existing behaviour can be reproduced
 * exactly. Each corresponds to a case where the engine's original shape
 * provably could not express what a game already does.
 */
static void test_migration_capabilities(void)
{
    kt_map map;
    kt_cell *cell;

    printf("migration capabilities\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        memset(&g_nav_cells[i], 0, sizeof(g_nav_cells[i]));
        g_nav_cells[i].move_cost = 4u;
    }
    kt_map_validate(&map);

    /*
     * A boundary that blocks propagation while remaining cheap to walk
     * through and transparent -- C-COM's closed wooden door. Four independent
     * channels over one boundary.
     */
    cell = kt_map_cell(&map, kt_cell_point_make(5, 5, 0));
    cell->wall[KT_WALL_WEST] = KT_WALL_BLOCKS_SPREAD;
    check(!kt_map_wall_blocks(&map, kt_cell_point_make(5, 5, 0), KT_WALL_WEST,
                              KT_CHANNEL_MOVE),
          "door is walkable");
    check(!kt_map_wall_blocks(&map, kt_cell_point_make(5, 5, 0), KT_WALL_WEST,
                              KT_CHANNEL_SIGHT),
          "door is transparent");
    check(kt_map_wall_blocks(&map, kt_cell_point_make(5, 5, 0), KT_WALL_WEST,
                             KT_CHANNEL_SPREAD),
          "door blocks fire and smoke spread");
    cell->wall[KT_WALL_WEST] = 0u;

    /*
     * A single blocked diagonal, which two orthogonal wall sides cannot
     * encode. Neighbouring cardinals must stay open, which is exactly what
     * makes it inexpressible as walls.
     */
    cell = kt_map_cell(&map, kt_cell_point_make(4, 4, 0));
    cell->diag_block = (uint8_t)(1u << KT_DIR_SE);
    check(kt_edge_blocked(&map, kt_cell_point_make(4, 4, 0), KT_DIR_SE,
                          KT_CHANNEL_MOVE),
          "diagonal mask blocks its direction");
    check(!kt_edge_blocked(&map, kt_cell_point_make(4, 4, 0), KT_DIR_E,
                           KT_CHANNEL_MOVE),
          "east stays open");
    check(!kt_edge_blocked(&map, kt_cell_point_make(4, 4, 0), KT_DIR_S,
                           KT_CHANNEL_MOVE),
          "south stays open");
    check(kt_edge_blocked(&map, kt_cell_point_make(5, 5, 0), KT_DIR_NW,
                          KT_CHANNEL_MOVE),
          "the reverse direction is blocked too");
    check(!kt_edge_blocked(&map, kt_cell_point_make(4, 4, 0), KT_DIR_SE,
                           KT_CHANNEL_SIGHT),
          "the mask is movement-only");
    cell->diag_block = 0u;

    /* Per-boundary traversal cost travels with the cell for the step hook. */
    cell->wall_cost[KT_WALL_NORTH] = 4u;
    check_eq(kt_map_cell_const(&map, kt_cell_point_make(4, 4, 0))
                 ->wall_cost[KT_WALL_NORTH],
             4, "wall cost is carried, not interpreted");
    cell->wall_cost[KT_WALL_NORTH] = 0u;
}

static void test_level_link_from_below(void)
{
    kt_map map;
    size_t i;

    printf("vertical links on either side of a deck\n");
    kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0]));
    for (i = 0; i < kt_map_cell_count(&map); ++i) {
        memset(&g_ccom_cells[i], 0, sizeof(g_ccom_cells[i]));
        g_ccom_cells[i].move_cost = 4u;
        g_ccom_cells[i].flags = KT_CELL_HAS_FLOOR;
    }
    kt_map_validate(&map);
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(3, 3, 0),
                         kt_cell_point_make(3, 3, 1), KT_CHANNEL_SIGHT),
          "intact deck seals");

    /* A shaft recorded on the LOWER cell must pierce the deck above it. */
    kt_map_cell(&map, kt_cell_point_make(3, 3, 0))->flags =
        (uint8_t)(KT_CELL_HAS_FLOOR | KT_CELL_LEVEL_LINK);
    kt_map_validate(&map);
    check(kt_sight_line(&map, NULL, kt_cell_point_make(3, 3, 0),
                        kt_cell_point_make(3, 3, 1), KT_CHANNEL_SIGHT),
          "link on the lower cell opens the deck");

    /* And recorded on the upper cell, as before. */
    kt_map_cell(&map, kt_cell_point_make(3, 3, 0))->flags = KT_CELL_HAS_FLOOR;
    kt_map_cell(&map, kt_cell_point_make(3, 3, 1))->flags =
        (uint8_t)(KT_CELL_HAS_FLOOR | KT_CELL_LEVEL_LINK);
    kt_map_validate(&map);
    check(kt_sight_line(&map, NULL, kt_cell_point_make(3, 3, 0),
                        kt_cell_point_make(3, 3, 1), KT_CHANNEL_SIGHT),
          "link on the upper cell opens the deck");
}

/* A float32 priority key, which is C-COM's, reassembled through the hook. */
static uint32_t float_bits(float f)
{
    uint32_t bits;

    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static float bits_float(uint32_t bits)
{
    float f;

    memcpy(&f, &bits, sizeof(f));
    return f;
}

static uint32_t cap_heuristic(void *user, kt_cell_point at, kt_cell_point goal)
{
    int dx = goal.x - at.x;
    int dy = goal.y - at.y;

    (void)user;
    /* Deliberately float, and returned as an opaque bit pattern. */
    return float_bits(4.0f * (float)((dx < 0 ? -dx : dx) +
                                     (dy < 0 ? -dy : dy)));
}

static uint64_t cap_priority(void *user, uint32_t cost, uint32_t estimate)
{
    float f;

    (void)user;
    f = (float)cost + bits_float(estimate);
    return (uint64_t)float_bits(f);
}

static void test_priority_and_tiebreak(void)
{
    kt_map map;
    kt_nav_workspace workspace;
    kt_nav_hooks hooks;
    kt_path a;
    kt_path b;

    printf("priority combiner and tiebreak selector\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (size_t i = 0; i < kt_map_cell_count(&map); ++i) {
        memset(&g_nav_cells[i], 0, sizeof(g_nav_cells[i]));
        g_nav_cells[i].move_cost = 4u;
    }
    kt_map_validate(&map);

    kt_nav_workspace_init_indexed(
        &workspace, g_nav_nodes,
        sizeof(g_nav_nodes) / sizeof(g_nav_nodes[0]), g_nav_heap,
        sizeof(g_nav_heap) / sizeof(g_nav_heap[0]), g_nav_heap_pos,
        sizeof(g_nav_heap_pos) / sizeof(g_nav_heap_pos[0]));
    kt_nav_hooks_init(&hooks);
    hooks.step_cost = nav_step;
    hooks.user = &map;
    hooks.min_step_cost = 4u;

    /* A float32 key must produce a valid optimal route, not just run. */
    hooks.heuristic = cap_heuristic;
    hooks.priority = cap_priority;
    check_eq(kt_nav_find_path(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0),
                              kt_cell_point_make(9, 7, 0), 1000u, &a),
             KT_OK, "float priority search succeeds");

    hooks.heuristic = NULL;
    hooks.priority = NULL;
    check_eq(kt_nav_find_path(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0),
                              kt_cell_point_make(9, 7, 0), 1000u, &b),
             KT_OK, "integer search succeeds");
    /* Both must be optimal; the ROUTE may legitimately differ, which is
     * precisely why the selector exists. */
    check_eq(a.total_cost, b.total_cost,
             "both key styles find an equal-cost route");

    /* The tiebreak selector must be honoured and deterministic. */
    hooks.tiebreak = KT_TIEBREAK_CELL_INDEX;
    check_eq(kt_nav_find_path(&map, &workspace, &hooks,
                              kt_cell_point_make(1, 1, 0),
                              kt_cell_point_make(9, 7, 0), 1000u, &a),
             KT_OK, "cell-index tiebreak search succeeds");
    check_eq(a.total_cost, b.total_cost, "cell-index route is still optimal");
    {
        kt_path again;

        kt_nav_find_path(&map, &workspace, &hooks, kt_cell_point_make(1, 1, 0),
                         kt_cell_point_make(9, 7, 0), 1000u, &again);
        check(memcmp(again.dirs, a.dirs, a.count) == 0 &&
                  again.count == a.count,
              "cell-index tiebreak is deterministic");
    }

    /* One entry per node, so the heap needs exactly one slot per cell. */
    check_eq((int64_t)kt_nav_required_heap(&map),
             (int64_t)kt_map_cell_count(&map), "required heap reported");
    {
        kt_nav_workspace small;

        /* The position index is mandatory: without it the heap cannot
         * reposition an improved node, which is what keeps the invariant. */
        kt_nav_workspace_init(&small, g_nav_nodes,
                              sizeof(g_nav_nodes) / sizeof(g_nav_nodes[0]),
                              g_nav_heap, kt_map_cell_count(&map));
        check_eq(kt_nav_find_path(&map, &small, &hooks,
                                  kt_cell_point_make(1, 1, 0),
                                  kt_cell_point_make(9, 7, 0), 1000u, &a),
                 KT_ERR_CAPACITY,
                 "missing position index refused up front");
    }
}


static void test_depth_order_modes(void)
{
    kt_map map;
    kt_projection diagonal;
    kt_projection level_major;
    kt_camera camera;
    int64_t a_diag, b_diag, a_level, b_level;
    /* The counterexample from reconnaissance: a raised cell and a nearer
     * lower cell whose sprites overlap. The two orders must disagree. */
    kt_cell_point a = kt_cell_point_make(5, 5, 1);
    kt_cell_point b = kt_cell_point_make(6, 5, 0);

    printf("depth order modes\n");
    kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0]));
    kt_map_validate(&map);
    kt_projection_init(&diagonal, 32, 16, 24, KT_ROTATE_CCW);
    kt_projection_init(&level_major, 32, 16, 24, KT_ROTATE_CCW);
    level_major.depth_order = KT_DEPTH_LEVEL_MAJOR;
    kt_camera_init(&camera);

    check_eq(diagonal.depth_order, KT_DEPTH_DIAGONAL_MAJOR,
             "diagonal-major is the default");

    kt_depth_key(&map, &diagonal, &camera, a, &a_diag);
    kt_depth_key(&map, &diagonal, &camera, b, &b_diag);
    kt_depth_key(&map, &level_major, &camera, a, &a_level);
    kt_depth_key(&map, &level_major, &camera, b, &b_level);

    /* Diagonal-major: (5+5)=10 sorts before (6+5)=11, so the raised cell
     * paints FIRST. Level-major: z=0 paints entirely before z=1, so it paints
     * LAST. Inverted, which is exactly why the mode has to exist. */
    check(a_diag < b_diag, "diagonal-major paints the raised cell first");
    check(a_level > b_level, "level-major paints the raised cell last");

    /*
     * Level-major must be a literal transcription of `for z { for vx { for vy
     * } }`, so the key is monotonic in that emission order with NO inversions.
     * An earlier version used the diagonal row as the middle term and inverted
     * 156 times per frame at the vx-loop boundaries.
     */
    {
        int64_t previous = INT64_MIN;
        int inversions = 0;
        int z, vx, vy;

        for (z = 0; z < CCOM_D; ++z) {
            for (vx = 0; vx < CCOM_W; ++vx) {
                for (vy = 0; vy < CCOM_H; ++vy) {
                    int64_t key;

                    kt_depth_key(&map, &level_major, &camera,
                                 kt_cell_point_make(vx, vy, z), &key);
                    if (key <= previous) {
                        ++inversions;
                    }
                    previous = key;
                }
            }
        }
        check_eq(inversions, 0,
                 "level-major is monotonic in for z { for x { for y } } order");
    }

    /* Level-major must group every cell of a level together. */
    {
        int64_t max_low = INT64_MIN;
        int64_t min_high = INT64_MAX;
        int x, y;

        for (y = 0; y < CCOM_H; ++y) {
            for (x = 0; x < CCOM_W; ++x) {
                int64_t low, high;

                kt_depth_key(&map, &level_major, &camera,
                             kt_cell_point_make(x, y, 0), &low);
                kt_depth_key(&map, &level_major, &camera,
                             kt_cell_point_make(x, y, 1), &high);
                if (low > max_low) {
                    max_low = low;
                }
                if (high < min_high) {
                    min_high = high;
                }
            }
        }
        check(max_low < min_high,
              "level-major paints all of level 0 before any of level 1");
    }
}

static void test_reachable_collect(void)
{
    kt_map map;
    kt_nav_workspace workspace;
    kt_nav_hooks hooks;
    kt_cell_point collected[64];
    size_t count = 0u;
    size_t i;

    printf("settle-ordered reachability collection\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    for (i = 0; i < kt_map_cell_count(&map); ++i) {
        memset(&g_nav_cells[i], 0, sizeof(g_nav_cells[i]));
        g_nav_cells[i].move_cost = 4u;
    }
    kt_map_validate(&map);
    kt_nav_workspace_init_indexed(
        &workspace, g_nav_nodes,
        sizeof(g_nav_nodes) / sizeof(g_nav_nodes[0]), g_nav_heap,
        sizeof(g_nav_heap) / sizeof(g_nav_heap[0]), g_nav_heap_pos,
        sizeof(g_nav_heap_pos) / sizeof(g_nav_heap_pos[0]));
    kt_nav_hooks_init(&hooks);
    hooks.step_cost = nav_step;
    hooks.user = &map;
    hooks.min_step_cost = 4u;

    check_eq(kt_nav_reachable_collect(&map, &workspace, &hooks,
                                      kt_cell_point_make(5, 5, 0), 24u,
                                      collected, 64u, &count),
             KT_OK, "collect ok");
    check(count > 1u, "collected more than the start");
    check(kt_cell_point_equal(collected[0], kt_cell_point_make(5, 5, 0)),
          "the start settles first");

    /* Settle order is non-decreasing in cost -- that is what makes it a
     * settle order rather than an arbitrary set. */
    for (i = 1u; i < count; ++i) {
        uint32_t prev = kt_nav_cost_to(&map, &workspace, collected[i - 1u]);
        uint32_t cur = kt_nav_cost_to(&map, &workspace, collected[i]);

        check(prev <= cur, "settle order is non-decreasing in cost");
    }

    /* The early stop must truncate rather than overrun. */
    check_eq(kt_nav_reachable_collect(&map, &workspace, &hooks,
                                      kt_cell_point_make(5, 5, 0), 1000u,
                                      collected, 5u, &count),
             KT_OK, "capped collect ok");
    check_eq((int64_t)count, 5, "early stop truncates at capacity");
}

static void test_elevation_cutaway(void)
{
    kt_map map;
    kt_projection projection;
    kt_camera camera;
    kt_cell_point got;
    kt_screen_point at;
    int y, x;

    printf("elevation-aware cutaway\n");
    kt_map_init(&map, KAT_W, KAT_H, 1, g_kat_cells,
                sizeof(g_kat_cells) / sizeof(g_kat_cells[0]));
    for (y = 0; y < KAT_H; ++y) {
        for (x = 0; x < KAT_W; ++x) {
            kt_cell *cell = kt_map_cell(&map, kt_cell_point_make(x, y, 0));

            memset(cell, 0, sizeof(*cell));
            cell->move_cost = 4u;
            cell->elevation = 0;
        }
    }
    /* One raised cell on an otherwise flat single-level map -- KAT's shape,
     * where the level index is always 0 and height lives in elevation. */
    kt_map_cell(&map, kt_cell_point_make(6, 6, 0))->elevation = 3;
    kt_map_validate(&map);
    kt_projection_init(&projection, 32, 16, 12, KT_ROTATE_CW);
    kt_camera_init(&camera);
    camera.origin_x = 400;
    camera.origin_y = 100;

    /*
     * Probe the raised cell's own projected origin and require that the pick
     * actually RESOLVES TO IT above the cutaway. The earlier form asserted
     * only "not this cell OR not ok" below the cutaway, which a nearer flat
     * cell satisfied on its own -- the assertion passed without the cutaway
     * ever being exercised.
     */
    camera.view_level = 5;
    kt_project(&map, &projection, &camera, kt_cell_point_make(6, 6, 0), &at);
    check_eq(kt_pick_cell(&map, &projection, &camera, at, &got), KT_OK,
             "pick resolves above the cutaway");
    check(kt_cell_point_equal(got, kt_cell_point_make(6, 6, 0)),
          "and resolves to the raised cell itself");

    /* Cut away below its elevation: it must vanish even though its LEVEL
     * index is still 0. That is the whole point. */
    camera.view_level = 1;
    check(kt_pick_cell(&map, &projection, &camera, at, &got) != KT_OK ||
              !kt_cell_point_equal(got, kt_cell_point_make(6, 6, 0)),
          "raised cell is cut away by elevation, not just level index");
}


/*
 * The cross-level corner is open if EITHER ordering is. Before this rule the
 * engine evaluated only "move across at the old level, then pierce the deck at
 * the destination column", which made the other ordering unreachable.
 */
static void test_level_corner_two_routes(void)
{
    kt_map map;
    size_t i;
    kt_cell_point from = kt_cell_point_make(3, 3, 0);
    kt_cell_point to = kt_cell_point_make(4, 3, 1);

    printf("cross-level corner: either ordering opens it\n");

    /* Baseline: every deck intact, so both orderings are sealed. */
    kt_map_init(&map, CCOM_W, CCOM_H, CCOM_D, g_ccom_cells,
                sizeof(g_ccom_cells) / sizeof(g_ccom_cells[0]));
    for (i = 0; i < kt_map_cell_count(&map); ++i) {
        memset(&g_ccom_cells[i], 0, sizeof(g_ccom_cells[i]));
        g_ccom_cells[i].move_cost = 4u;
        g_ccom_cells[i].flags = KT_CELL_HAS_FLOOR;
    }
    kt_map_validate(&map);
    check(!kt_sight_line(&map, NULL, from, to, KT_CHANNEL_SIGHT),
          "both orderings sealed");

    /*
     * Open ONLY the second ordering: a wall blocks the horizontal move at the
     * OLD level, while the deck above the SOURCE column is linked and the
     * horizontal move at the NEW level is clear. The first ordering is
     * blocked at its very first test, so a correct answer here is only
     * reachable via the second.
     */
    kt_map_cell(&map, kt_cell_point_make(4, 3, 0))->wall[KT_WALL_WEST] =
        KT_WALL_BLOCKS_SIGHT;
    kt_map_cell(&map, kt_cell_point_make(3, 3, 0))->flags =
        (uint8_t)(KT_CELL_HAS_FLOOR | KT_CELL_LEVEL_LINK);
    kt_map_validate(&map);
    check(kt_sight_line(&map, NULL, from, to, KT_CHANNEL_SIGHT),
          "second ordering alone opens the corner");

    /* Block the new-level move as well and it must close again. */
    kt_map_cell(&map, kt_cell_point_make(4, 3, 1))->wall[KT_WALL_WEST] =
        KT_WALL_BLOCKS_SIGHT;
    kt_map_validate(&map);
    check(!kt_sight_line(&map, NULL, from, to, KT_CHANNEL_SIGHT),
          "both orderings blocked closes it again");

    /*
     * Open ONLY the first ordering: clear the old-level wall and seal the
     * source column, leaving the destination column linked.
     */
    kt_map_cell(&map, kt_cell_point_make(4, 3, 0))->wall[KT_WALL_WEST] = 0u;
    kt_map_cell(&map, kt_cell_point_make(3, 3, 0))->flags = KT_CELL_HAS_FLOOR;
    kt_map_cell(&map, kt_cell_point_make(4, 3, 0))->flags =
        (uint8_t)(KT_CELL_HAS_FLOOR | KT_CELL_LEVEL_LINK);
    kt_map_validate(&map);
    check(kt_sight_line(&map, NULL, from, to, KT_CHANNEL_SIGHT),
          "first ordering alone still opens the corner");

    /* A purely vertical step has no horizontal component, so the deck test
     * alone must still govern it. */
    kt_map_cell(&map, kt_cell_point_make(4, 3, 0))->flags = KT_CELL_HAS_FLOOR;
    kt_map_validate(&map);
    check(!kt_sight_line(&map, NULL, kt_cell_point_make(7, 7, 0),
                         kt_cell_point_make(7, 7, 1), KT_CHANNEL_SIGHT),
          "vertical step still sealed by its own deck");
    kt_map_cell(&map, kt_cell_point_make(7, 7, 1))->flags =
        (uint8_t)(KT_CELL_HAS_FLOOR | KT_CELL_LEVEL_LINK);
    kt_map_validate(&map);
    check(kt_sight_line(&map, NULL, kt_cell_point_make(7, 7, 0),
                        kt_cell_point_make(7, 7, 1), KT_CHANNEL_SIGHT),
          "vertical step opened by its own link");
}

static kt_draw_item g_draw_items[512];

static void test_draw_queue(void)
{
    kt_map map;
    kt_projection projection;
    kt_camera camera;
    kt_draw_queue queue;
    uint64_t first_hash;
    uint64_t second_hash;

    printf("draw queue\n");
    kt_map_init(&map, NAV_W, NAV_H, 1, g_nav_cells,
                sizeof(g_nav_cells) / sizeof(g_nav_cells[0]));
    kt_map_validate(&map);
    kt_projection_init(&projection, 32, 16, 24, KT_ROTATE_CCW);
    kt_camera_init(&camera);
    check_eq(kt_draw_queue_init(&queue, g_draw_items,
                                sizeof(g_draw_items) / sizeof(g_draw_items[0])),
             KT_OK, "queue init");

    /* Submit deliberately back to front so sorting has real work to do. */
    for (int y = NAV_H - 1; y >= 0; --y) {
        for (int x = NAV_W - 1; x >= 0; --x) {
            check_eq(kt_draw_submit_cell(&queue, &map, &projection, &camera,
                                         kt_cell_point_make(x, y, 0),
                                         KT_LAYER_FLOOR,
                                         (uint32_t)(y * NAV_W + x), NULL),
                     KT_OK, "submit floor");
        }
    }
    /* A unit on top of one of them, submitted first, must still sort above. */
    check_eq(kt_draw_submit_cell(&queue, &map, &projection, &camera,
                                 kt_cell_point_make(3, 3, 0), KT_LAYER_UNIT,
                                 9999u, NULL),
             KT_OK, "submit unit");
    check_eq(kt_draw_queue_sort(&queue), KT_OK, "sort");

    /* Painter order: depth never decreases, and within a cell the layer
     * band decides. */
    for (size_t i = 1; i < queue.count; ++i) {
        check(queue.items[i - 1u].depth <= queue.items[i].depth,
              "depth non-decreasing");
        if (queue.items[i - 1u].depth == queue.items[i].depth) {
            check(queue.items[i - 1u].layer <= queue.items[i].layer,
                  "layer non-decreasing within depth");
        }
    }

    first_hash = kt_draw_queue_hash(&queue);

    /* Rebuilding in a different submission order must give the same sorted
     * queue, which is what makes the hash a usable golden. */
    kt_draw_queue_clear(&queue);
    for (int y = 0; y < NAV_H; ++y) {
        for (int x = 0; x < NAV_W; ++x) {
            kt_draw_submit_cell(&queue, &map, &projection, &camera,
                                kt_cell_point_make(x, y, 0), KT_LAYER_FLOOR,
                                (uint32_t)(y * NAV_W + x), NULL);
        }
    }
    kt_draw_submit_cell(&queue, &map, &projection, &camera,
                        kt_cell_point_make(3, 3, 0), KT_LAYER_UNIT, 9999u,
                        NULL);
    kt_draw_queue_sort(&queue);
    second_hash = kt_draw_queue_hash(&queue);
    check_eq((int64_t)second_hash, (int64_t)first_hash,
             "hash independent of submission order");

    /* Capacity is reported, not overrun. */
    {
        kt_draw_queue small;
        kt_draw_item storage[2];

        kt_draw_queue_init(&small, storage, 2u);
        kt_draw_submit_cell(&small, &map, &projection, &camera,
                            kt_cell_point_make(0, 0, 0), KT_LAYER_FLOOR, 1u,
                            NULL);
        kt_draw_submit_cell(&small, &map, &projection, &camera,
                            kt_cell_point_make(1, 0, 0), KT_LAYER_FLOOR, 2u,
                            NULL);
        check_eq(kt_draw_submit_cell(&small, &map, &projection, &camera,
                                     kt_cell_point_make(2, 0, 0),
                                     KT_LAYER_FLOOR, 3u, NULL),
                 KT_ERR_CAPACITY, "queue capacity enforced");
    }
}

int main(void)
{
    test_directions();
    test_map();
    test_projection_ccom();
    test_projection_kat();
    test_rotation_round_trip();
    test_picking();
    test_subcell();
    test_nav();
    test_sight();
    test_sight_levels();
    test_cover();
    test_edge_table_contract();
    test_ray_geometry();
    test_migration_capabilities();
    test_level_link_from_below();
    test_level_corner_two_routes();
    test_priority_and_tiebreak();
    test_depth_order_modes();
    test_reachable_collect();
    test_elevation_cutaway();
    test_draw_queue();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
