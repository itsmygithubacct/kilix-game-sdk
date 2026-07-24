/*
 * sight.c — frozen edge table, sight traces, and cover reporting.
 */
#include "kilix_tactics_sight.h"

typedef struct kt_edge_test {
    int8_t dx;
    int8_t dy;
    uint8_t side;
} kt_edge_test;

/* Frozen movement edge table; see the header for the authoritative listing. */
static const kt_edge_test kt_edge_table[KT_DIR_COUNT][4] = {
    {{0, 0, KT_WALL_NORTH}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, KT_WALL_NORTH},
     {1, -1, KT_WALL_WEST},
     {1, 0, KT_WALL_WEST},
     {1, 0, KT_WALL_NORTH}},
    {{1, 0, KT_WALL_WEST}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{1, 0, KT_WALL_WEST},
     {0, 1, KT_WALL_NORTH},
     {1, 1, KT_WALL_NORTH},
     {1, 1, KT_WALL_WEST}},
    {{0, 1, KT_WALL_NORTH}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, KT_WALL_WEST},
     {0, 1, KT_WALL_WEST},
     {0, 1, KT_WALL_NORTH},
     {-1, 1, KT_WALL_NORTH}},
    {{0, 0, KT_WALL_WEST}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, KT_WALL_WEST},
     {0, 0, KT_WALL_NORTH},
     {-1, 0, KT_WALL_NORTH},
     {0, -1, KT_WALL_WEST}}};

static const uint8_t kt_edge_count[KT_DIR_COUNT] = {1, 4, 1, 4, 1, 4, 1, 4};

bool kt_edge_blocked(const kt_map *map, kt_cell_point from, kt_direction dir,
                     kt_channel channel)
{
    uint8_t index;
    kt_cell_point neighbour;

    if (map == NULL || (unsigned)dir >= (unsigned)KT_DIR_COUNT) {
        return true;
    }
    neighbour = kt_cell_point_make(from.x + kt_direction_dx[dir],
                                   from.y + kt_direction_dy[dir], from.z);
    if (!kt_map_contains(map, from) || !kt_map_contains(map, neighbour)) {
        return true;
    }
    for (index = 0u; index < kt_edge_count[dir]; ++index) {
        const kt_edge_test *test = &kt_edge_table[dir][index];
        kt_cell_point at = kt_cell_point_make(from.x + test->dx,
                                              from.y + test->dy, from.z);

        if (kt_map_wall_blocks(map, at, (kt_wall_side)test->side, channel)) {
            return true;
        }
    }
    return false;
}

/* Wall crossed by one orthogonal step, expressed as the owning cell's side. */
static bool kt_orthogonal_wall_blocks(const kt_map *map, kt_cell_point from,
                                      kt_direction dir, kt_channel channel)
{
    switch (dir) {
    case KT_DIR_N:
        return kt_map_wall_blocks(map, from, KT_WALL_NORTH, channel);
    case KT_DIR_S:
        return kt_map_wall_blocks(
            map, kt_cell_point_make(from.x, from.y + 1, from.z), KT_WALL_NORTH,
            channel);
    case KT_DIR_W:
        return kt_map_wall_blocks(map, from, KT_WALL_WEST, channel);
    case KT_DIR_E:
        return kt_map_wall_blocks(
            map, kt_cell_point_make(from.x + 1, from.y, from.z), KT_WALL_WEST,
            channel);
    default:
        return true;
    }
}

bool kt_sight_blocked_step(const kt_map *map, kt_cell_point from,
                           kt_cell_point to, kt_channel channel)
{
    int32_t dx;
    int32_t dy;
    kt_direction dir;

    if (map == NULL || !kt_map_contains(map, from) ||
        !kt_map_contains(map, to) || from.z != to.z) {
        return true;
    }
    dx = to.x - from.x;
    dy = to.y - from.y;
    if (dx == 0 && dy == 0) {
        return false;
    }
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
        return true;
    }
    dir = kt_direction_from_delta(dx, dy);
    if (!KT_DIR_IS_DIAGONAL(dir)) {
        return kt_orthogonal_wall_blocks(map, from, dir, channel);
    }

    /*
     * Two-way diagonal rule: the step is open if either L-shaped detour is
     * open. Deliberately more permissive than the movement table, which
     * forbids corner cutting.
     */
    {
        kt_cell_point side_a = kt_cell_point_make(to.x, from.y, from.z);
        kt_cell_point side_b = kt_cell_point_make(from.x, to.y, from.z);
        bool route_a;
        bool route_b;

        route_a = !kt_sight_blocked_step(map, from, side_a, channel) &&
                  !kt_sight_blocked_step(map, side_a, to, channel) &&
                  !kt_map_cell_blocks(map, side_a, channel);
        route_b = !kt_sight_blocked_step(map, from, side_b, channel) &&
                  !kt_sight_blocked_step(map, side_b, to, channel) &&
                  !kt_map_cell_blocks(map, side_b, channel);
        return !(route_a || route_b);
    }
}

/*
 * Integer Bresenham over the dominant horizontal axis, stepping level with
 * the same interpolation so a cross-level ray advances monotonically.
 */
static kt_status kt_walk_line(const kt_map *map, kt_cell_point from,
                              kt_cell_point to, const kt_sight_hooks *hooks,
                              kt_channel channel, kt_cell_point *out_cells,
                              size_t capacity, size_t *out_count,
                              bool *out_blocked)
{
    int32_t dx = to.x - from.x;
    int32_t dy = to.y - from.y;
    int32_t dz = to.z - from.z;
    int32_t steps;
    int32_t index;
    size_t written = 0u;
    kt_cell_point previous = from;

    if (out_blocked != NULL) {
        *out_blocked = false;
    }
    steps = dx < 0 ? -dx : dx;
    if ((dy < 0 ? -dy : dy) > steps) {
        steps = dy < 0 ? -dy : dy;
    }
    if (steps == 0) {
        if (out_count != NULL) {
            *out_count = 0u;
        }
        return KT_OK;
    }
    if (steps > KT_TRACE_MAX_STEPS) {
        return KT_ERR_CAPACITY;
    }

    for (index = 1; index <= steps; ++index) {
        kt_cell_point at;
        int32_t next_z;

        /* Round to nearest, away from zero, so the ray is symmetric. */
        at.x = from.x + (dx * index * 2 + (dx >= 0 ? steps : -steps)) /
                            (steps * 2);
        at.y = from.y + (dy * index * 2 + (dy >= 0 ? steps : -steps)) /
                            (steps * 2);
        next_z = from.z + (dz * index * 2 + (dz >= 0 ? steps : -steps)) /
                              (steps * 2);
        at.z = previous.z;

        if (!kt_map_contains(map, at)) {
            if (out_blocked != NULL) {
                *out_blocked = true;
            }
            break;
        }
        if (kt_sight_blocked_step(map, previous, at, channel)) {
            if (out_blocked != NULL) {
                *out_blocked = true;
            }
            break;
        }

        /*
         * Level change. An intact floor on the cell being entered seals the
         * boundary unless that cell is a level link; a missing or destroyed
         * floor opens it.
         */
        if (next_z != at.z) {
            kt_cell_point above = kt_cell_point_make(at.x, at.y, next_z);
            const kt_cell *cell;
            int32_t sealed_z = next_z > at.z ? next_z : at.z;
            kt_cell_point sealed = kt_cell_point_make(at.x, at.y, sealed_z);

            if (!kt_map_contains(map, above)) {
                if (out_blocked != NULL) {
                    *out_blocked = true;
                }
                break;
            }
            cell = kt_map_cell_const(map, sealed);
            if (cell != NULL && (cell->flags & KT_CELL_HAS_FLOOR) != 0u &&
                (cell->flags & KT_CELL_LEVEL_LINK) == 0u) {
                if (out_blocked != NULL) {
                    *out_blocked = true;
                }
                break;
            }
            at.z = next_z;
        }

        if (index < steps) {
            if (kt_map_cell_blocks(map, at, channel)) {
                if (out_blocked != NULL) {
                    *out_blocked = true;
                }
                break;
            }
            if (hooks != NULL && hooks->veto != NULL &&
                hooks->veto(hooks->user, at)) {
                if (out_blocked != NULL) {
                    *out_blocked = true;
                }
                break;
            }
            if (out_cells != NULL) {
                if (written >= capacity) {
                    return KT_ERR_CAPACITY;
                }
                out_cells[written] = at;
            }
            ++written;
        }
        previous = at;
    }

    if (out_count != NULL) {
        *out_count = written;
    }
    return KT_OK;
}

bool kt_sight_line(const kt_map *map, const kt_sight_hooks *hooks,
                   kt_cell_point from, kt_cell_point to, kt_channel channel)
{
    bool blocked = false;

    if (map == NULL || !kt_map_contains(map, from) ||
        !kt_map_contains(map, to)) {
        return false;
    }
    if (kt_cell_point_equal(from, to)) {
        return true;
    }
    if (kt_walk_line(map, from, to, hooks, channel, NULL, 0u, NULL, &blocked) !=
        KT_OK) {
        return false;
    }
    return !blocked;
}

kt_status kt_sight_trace(const kt_map *map, kt_cell_point from,
                         kt_cell_point to, kt_cell_point *out_cells,
                         size_t capacity, size_t *out_count)
{
    if (map == NULL || out_count == NULL ||
        (out_cells == NULL && capacity != 0u)) {
        return KT_ERR_ARGUMENT;
    }
    if (!kt_map_contains(map, from) || !kt_map_contains(map, to)) {
        return KT_ERR_RANGE;
    }
    return kt_walk_line(map, from, to, NULL, KT_CHANNEL_SIGHT, out_cells,
                        capacity, out_count, NULL);
}

static kt_cover_level kt_cover_on(const kt_cell *cell, kt_direction dir)
{
    uint8_t mask = (uint8_t)(1u << (unsigned)dir);

    if (cell == NULL) {
        return KT_COVER_NONE;
    }
    if ((cell->cover_full & mask) != 0u) {
        return KT_COVER_FULL;
    }
    if ((cell->cover_half & mask) != 0u) {
        return KT_COVER_HALF;
    }
    return KT_COVER_NONE;
}

kt_status kt_cover_query(const kt_map *map, kt_cell_point attacker,
                         kt_cell_point target, kt_cover_report *out)
{
    const kt_cell *cell;
    int32_t dx;
    int32_t dy;
    kt_direction approach;

    if (map == NULL || out == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (!kt_map_contains(map, attacker) || !kt_map_contains(map, target)) {
        return KT_ERR_RANGE;
    }
    out->strongest = KT_COVER_NONE;
    out->faces[0] = KT_COVER_NONE;
    out->faces[1] = KT_COVER_NONE;
    out->face_dirs[0] = KT_DIR_N;
    out->face_dirs[1] = KT_DIR_N;
    out->face_count = 0u;

    cell = kt_map_cell_const(map, target);
    dx = attacker.x - target.x;
    dy = attacker.y - target.y;
    if (dx == 0 && dy == 0) {
        return KT_OK;
    }

    approach = kt_direction_from_delta(dx, dy);
    if (KT_DIR_IS_DIAGONAL(approach)) {
        /* A diagonal approach presents both adjacent orthogonal faces. */
        kt_direction first = (kt_direction)(((unsigned)approach + 7u) & 7u);
        kt_direction second = (kt_direction)(((unsigned)approach + 1u) & 7u);

        out->face_dirs[0] = first;
        out->face_dirs[1] = second;
        out->faces[0] = kt_cover_on(cell, first);
        out->faces[1] = kt_cover_on(cell, second);
        out->face_count = 2u;
    } else {
        out->face_dirs[0] = approach;
        out->faces[0] = kt_cover_on(cell, approach);
        out->face_count = 1u;
    }

    out->strongest = out->faces[0];
    if (out->face_count == 2u && out->faces[1] > out->strongest) {
        out->strongest = out->faces[1];
    }
    return KT_OK;
}
