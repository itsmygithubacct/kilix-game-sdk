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
     *
     * The comparisons run in int64 because the public domain is the whole of
     * int32: a renderer legitimately asks for the octant of a delta between
     * two arbitrary positions, and doubling or negating an int32 near its
     * limits is undefined. Widening keeps every intermediate exact -- twice
     * an int32 and the negation of INT32_MIN are both representable in
     * int64 -- so the wedge boundaries are unchanged on every input the old
     * code could evaluate without overflow.
     */
    int64_t wide_dx = (int64_t)dx;
    int64_t wide_dy = (int64_t)dy;

    if (wide_dx == 0 && wide_dy == 0) {
        return KT_DIR_N;
    }
    if (wide_dx > 0) {
        if (wide_dy < 0) {
            if (wide_dx * 2 < -wide_dy) {
                return KT_DIR_N;
            }
            if (-wide_dy * 2 < wide_dx) {
                return KT_DIR_E;
            }
            return KT_DIR_NE;
        }
        if (wide_dy > 0) {
            if (wide_dx * 2 < wide_dy) {
                return KT_DIR_S;
            }
            if (wide_dy * 2 < wide_dx) {
                return KT_DIR_E;
            }
            return KT_DIR_SE;
        }
        return KT_DIR_E;
    }
    if (wide_dx < 0) {
        if (wide_dy < 0) {
            if (-wide_dx * 2 < -wide_dy) {
                return KT_DIR_N;
            }
            if (-wide_dy * 2 < -wide_dx) {
                return KT_DIR_W;
            }
            return KT_DIR_NW;
        }
        if (wide_dy > 0) {
            if (-wide_dx * 2 < wide_dy) {
                return KT_DIR_S;
            }
            if (wide_dy * 2 < -wide_dx) {
                return KT_DIR_W;
            }
            return KT_DIR_SW;
        }
        return KT_DIR_W;
    }
    return wide_dy < 0 ? KT_DIR_N : KT_DIR_S;
}

kt_direction kt_direction_opposite(kt_direction dir)
{
    return (kt_direction)(((unsigned)dir + 4u) & 7u);
}

uint32_t kt_direction_turn_distance(kt_direction from, kt_direction to)
{
    /*
     * Both facings are reduced into the frozen eight-way ring before the
     * difference is taken. Out-of-range values previously fell through the
     * `delta > 4` fold with a negative result and were reinterpreted as a
     * huge unsigned turn count, which no caller can act on; the documented
     * range is 0..4 and is now total.
     */
    int32_t delta = (int32_t)(((unsigned)to & 7u)) -
                    (int32_t)(((unsigned)from & 7u));

    if (delta < 0) {
        delta = -delta;
    }
    if (delta > 4) {
        delta = 8 - delta;
    }
    return (uint32_t)delta;
}
