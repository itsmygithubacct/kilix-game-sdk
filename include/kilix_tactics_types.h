/*
 * kilix_tactics_types.h — shared scalar types, status codes, and directions.
 *
 * Part of libkilix-tactics-core. Standard library only.
 */
#ifndef KILIX_TACTICS_TYPES_H
#define KILIX_TACTICS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum kt_status {
    KT_OK = 0,
    KT_ERR_ARGUMENT,
    KT_ERR_CAPACITY,
    KT_ERR_RANGE,
    KT_ERR_STATE,
    KT_ERR_UNREACHABLE
} kt_status;

/*
 * Direction order is N, NE, E, SE, S, SW, W, NW. Both consuming games
 * already use this order and both persist it, so it is a frozen contract:
 * the numeric value appears in saved battle data on the game side.
 * Odd values are diagonals.
 */
typedef enum kt_direction {
    KT_DIR_N = 0,
    KT_DIR_NE,
    KT_DIR_E,
    KT_DIR_SE,
    KT_DIR_S,
    KT_DIR_SW,
    KT_DIR_W,
    KT_DIR_NW,
    KT_DIR_COUNT
} kt_direction;

#define KT_DIR_IS_DIAGONAL(dir) (((unsigned)(dir) & 1u) != 0u)

/* Per-cell wall sides. A cell owns the boundary on its west and north
 * edges; the shared boundary with the east or south neighbour is that
 * neighbour's west or north wall. This matches both games' storage. */
typedef enum kt_wall_side {
    KT_WALL_WEST = 0,
    KT_WALL_NORTH = 1,
    KT_WALL_SIDE_COUNT
} kt_wall_side;

/* Blocking channels. A caller asks about exactly one at a time. */
typedef enum kt_channel {
    KT_CHANNEL_MOVE = 0,
    KT_CHANNEL_SIGHT,
    KT_CHANNEL_FIRE,
    KT_CHANNEL_COUNT
} kt_channel;

typedef struct kt_cell_point {
    int32_t x;
    int32_t y;
    int32_t z;
} kt_cell_point;

typedef struct kt_screen_point {
    int32_t x;
    int32_t y;
} kt_screen_point;

static inline kt_cell_point kt_cell_point_make(int32_t x, int32_t y, int32_t z)
{
    kt_cell_point point;

    point.x = x;
    point.y = y;
    point.z = z;
    return point;
}

static inline bool kt_cell_point_equal(kt_cell_point a, kt_cell_point b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

/* Step deltas indexed by kt_direction. */
extern const int8_t kt_direction_dx[KT_DIR_COUNT];
extern const int8_t kt_direction_dy[KT_DIR_COUNT];

/*
 * Octant of a delta, matching both games' frozen dir_from_delta. A zero
 * delta reports KT_DIR_N; callers that care must test for it first.
 */
kt_direction kt_direction_from_delta(int32_t dx, int32_t dy);
kt_direction kt_direction_opposite(kt_direction dir);

/* Smallest turn count between two facings, 0..4. */
uint32_t kt_direction_turn_distance(kt_direction from, kt_direction to);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_TYPES_H */
