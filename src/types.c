/*
 * types.c — direction tables and octant helpers.
 */
#include "kilix_tactics_types.h"

/* N, NE, E, SE, S, SW, W, NW with +y running south. */
const int8_t kt_direction_dx[KT_DIR_COUNT] = {0, 1, 1, 1, 0, -1, -1, -1};
const int8_t kt_direction_dy[KT_DIR_COUNT] = {-1, -1, 0, 1, 1, 1, 0, -1};

kt_direction kt_direction_from_delta(int32_t dx, int32_t dy)
{
    /*
     * Frozen octant rule shared by both games: compare each axis against
     * twice the other to find the 45-degree wedge. Integer only.
     */
    if (dx == 0 && dy == 0) {
        return KT_DIR_N;
    }
    if (dx > 0) {
        if (dy < 0) {
            if (dx * 2 < -dy) {
                return KT_DIR_N;
            }
            if (-dy * 2 < dx) {
                return KT_DIR_E;
            }
            return KT_DIR_NE;
        }
        if (dy > 0) {
            if (dx * 2 < dy) {
                return KT_DIR_S;
            }
            if (dy * 2 < dx) {
                return KT_DIR_E;
            }
            return KT_DIR_SE;
        }
        return KT_DIR_E;
    }
    if (dx < 0) {
        if (dy < 0) {
            if (-dx * 2 < -dy) {
                return KT_DIR_N;
            }
            if (-dy * 2 < -dx) {
                return KT_DIR_W;
            }
            return KT_DIR_NW;
        }
        if (dy > 0) {
            if (-dx * 2 < dy) {
                return KT_DIR_S;
            }
            if (dy * 2 < -dx) {
                return KT_DIR_W;
            }
            return KT_DIR_SW;
        }
        return KT_DIR_W;
    }
    return dy < 0 ? KT_DIR_N : KT_DIR_S;
}

kt_direction kt_direction_opposite(kt_direction dir)
{
    return (kt_direction)(((unsigned)dir + 4u) & 7u);
}

uint32_t kt_direction_turn_distance(kt_direction from, kt_direction to)
{
    int32_t delta = (int32_t)to - (int32_t)from;

    if (delta < 0) {
        delta = -delta;
    }
    if (delta > 4) {
        delta = 8 - delta;
    }
    return (uint32_t)delta;
}
