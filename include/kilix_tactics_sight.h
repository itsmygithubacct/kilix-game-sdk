/*
 * kilix_tactics_sight.h — edge blocking, sight traces, and cover reporting.
 *
 * Part of libkilix-tactics-core. Standard library only.
 *
 * FROZEN CONTRACT. The direction-to-wall table and the two-way diagonal
 * sight rule are lifted verbatim from C-COM's path.h and los.h and must not
 * drift. See DECISIONS.md T-007.
 */
#ifndef KILIX_TACTICS_SIGHT_H
#define KILIX_TACTICS_SIGHT_H

#include "kilix_tactics_map.h"
#include "kilix_tactics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KT_TRACE_MAX_STEPS = 512
};

/*
 * Movement edge test (frozen table). W(x,y) and N(x,y) are the west and
 * north walls of that cell. Leaving (x,y) in direction:
 *
 *   0 N : N(x,y)
 *   1 NE: N(x,y), W(x+1,y-1), W(x+1,y), N(x+1,y)
 *   2 E : W(x+1,y)
 *   3 SE: W(x+1,y), N(x,y+1), N(x+1,y+1), W(x+1,y+1)
 *   4 S : N(x,y+1)
 *   5 SW: W(x,y), W(x,y+1), N(x,y+1), N(x-1,y+1)
 *   6 W : W(x,y)
 *   7 NW: W(x,y), N(x,y), N(x-1,y), W(x,y-1)
 *
 * The four-entry diagonal rows are exactly why corners cannot be cut. An
 * off-map neighbour blocks.
 */
bool kt_edge_blocked(const kt_map *map, kt_cell_point from, kt_direction dir,
                     kt_channel channel);

enum {
    KT_EDGE_TESTS_MAX = 4
};

/* One boundary the frozen table requires testing, as an offset from the
 * source cell plus which side of that cell owns the boundary. */
typedef struct kt_edge_test {
    int8_t dx;
    int8_t dy;
    uint8_t side;    /* kt_wall_side */
} kt_edge_test;

/*
 * Enumerate the boundaries the frozen table requires for one step, so a game
 * can apply its OWN predicate to each without duplicating the table.
 *
 * This exists because a game may need two different movement predicates over
 * the same map state simultaneously, which no single per-channel bit can
 * express -- C-COM asks both "is this edge blocked" and "is it blocked if
 * closed doors also count". Exposing the geometry and letting the caller
 * supply the predicate keeps the frozen table single-sourced here, and is the
 * same split already used for kt_nav_hooks.step_cost and kt_sight_hooks.veto.
 *
 * Returns the number written: 1 for a cardinal direction, 4 for a diagonal,
 * 0 for an invalid direction or insufficient capacity.
 */
size_t kt_edge_tests(kt_direction dir, kt_edge_test *out, size_t capacity);

/*
 * Sight across one step between adjacent cells on the same level (frozen).
 * An orthogonal step tests the single crossed wall. A diagonal step is open
 * if EITHER two-step detour around the corner is open, which is the
 * two-way diagonal visibility rule and is deliberately more permissive than
 * the movement table above.
 */
bool kt_sight_blocked_step(const kt_map *map, kt_cell_point from,
                           kt_cell_point to, kt_channel channel);

/*
 * Optional per-cell veto, so a game can add state the engine does not model:
 * C-COM's smoke accumulation and its separate line-of-fire object rules,
 * KAT's temporary effects. Return true to stop the trace at that cell.
 * Endpoints are never offered to the veto.
 */
typedef bool (*kt_sight_veto_fn)(void *user, kt_cell_point cell);

typedef struct kt_sight_hooks {
    kt_sight_veto_fn veto;   /* may be NULL */
    void *user;
} kt_sight_hooks;

/*
 * Full cell-to-cell trace. Within a level this is the exact Bresenham walk
 * both games already use. Across levels it is an integer 3D trace in which
 * an intact floor seals the boundary except at a cell flagged
 * KT_CELL_LEVEL_LINK, and a missing or destroyed floor opens sight.
 *
 * Consumes no random stream and allocates nothing.
 */
bool kt_sight_line(const kt_map *map, const kt_sight_hooks *hooks,
                   kt_cell_point from, kt_cell_point to, kt_channel channel);

/*
 * Emit the traced cells, endpoints excluded, stopping where sight stops.
 * Reports how many were written and returns KT_ERR_CAPACITY if the buffer was
 * too small.
 */
kt_status kt_sight_trace(const kt_map *map, kt_cell_point from,
                         kt_cell_point to, kt_cell_point *out_cells,
                         size_t capacity, size_t *out_count);

/*
 * Pure ray geometry: every cell the ray passes through, endpoints excluded,
 * with NO blocking applied. Same frozen stepping and level interpolation as
 * the sight trace.
 *
 * This is the escape hatch for rules the engine deliberately does not model.
 * A per-cell boolean veto cannot express a rule that ACCUMULATES along a ray
 * -- C-COM's sight stops once summed smoke density crosses a threshold, and
 * its shot accuracy penalty counts how many dense cells were crossed. Both
 * need the whole ray up front, so the engine hands over the geometry and the
 * game applies its own predicate and accumulator.
 *
 * Prefer kt_sight_line() when the engine's compiled facts are sufficient;
 * reach for this only when they are not.
 */
kt_status kt_ray_cells(const kt_map *map, kt_cell_point from, kt_cell_point to,
                       kt_cell_point *out_cells, size_t capacity,
                       size_t *out_count);

typedef enum kt_cover_level {
    KT_COVER_NONE = 0,
    KT_COVER_HALF,
    KT_COVER_FULL
} kt_cover_level;

typedef struct kt_cover_report {
    kt_cover_level strongest;
    kt_cover_level faces[2];      /* the one or two boundaries facing the
                                     attacker; unused entries are NONE */
    kt_direction face_dirs[2];
    uint8_t face_count;
} kt_cover_report;

/*
 * Report the cover facts on the boundaries of `target` that face `attacker`.
 * The engine reports; it never converts cover into accuracy or damage.
 */
kt_status kt_cover_query(const kt_map *map, kt_cell_point attacker,
                         kt_cell_point target, kt_cover_report *out);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_SIGHT_H */
