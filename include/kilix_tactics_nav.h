/*
 * kilix_tactics_nav.h — A* and the reachability flood.
 *
 * Part of libkilix-tactics-core. Standard library only.
 *
 * The engine owns the search; the game owns what a step costs. Time units,
 * energy, stair and climb rules, unit occupancy, and reserved budgets all
 * live behind the step-cost hook. See DECISIONS.md T-006.
 */
#ifndef KILIX_TACTICS_NAV_H
#define KILIX_TACTICS_NAV_H

#include "kilix_tactics_map.h"
#include "kilix_tactics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KT_STEP_BLOCKED UINT32_MAX

enum {
    KT_PATH_MAX_STEPS = 256
};

/*
 * Cost of one step out of `from` in `dir`. Return KT_STEP_BLOCKED to refuse
 * it. *dest arrives holding the plain neighbour and may be rewritten, which
 * is how a game expresses a step that changes level: C-COM's stairs, KAT's
 * climb.
 *
 * Must be a pure function of the map and the game's own immutable state for
 * the duration of one search, and must consume no random stream.
 */
typedef uint32_t (*kt_step_cost_fn)(void *user, kt_cell_point from,
                                    kt_direction dir, kt_cell_point *dest);

/*
 * Optional admissible estimate from `at` to `goal`. NULL selects an integer
 * octile distance scaled by min_step_cost, which is admissible whenever no
 * step is cheaper than min_step_cost.
 */
typedef uint32_t (*kt_heuristic_fn)(void *user, kt_cell_point at,
                                    kt_cell_point goal);

/*
 * Optional priority combiner. NULL keeps the default
 * (uint64)cost + (uint64)estimate.
 *
 * This exists because a game's A* key need not be an integer sum. C-COM's is
 * IEEE-754 float32 -- both its heuristic and the addition round to float32 --
 * which manufactures orderings no integer key reproduces. Such a game returns
 * its float's raw bit pattern from the heuristic and reassembles it here; for
 * non-negative float32 the uint32 bit pattern orders identically to the float,
 * so the comparison is bit-exact.
 *
 * max_cost pruning always uses the integer cost, never this value.
 */
typedef uint64_t (*kt_nav_priority_fn)(void *user, uint32_t cost,
                                       uint32_t estimate);

/*
 * How equal-priority nodes are ordered. Ties are pervasive on uniform-cost
 * terrain, and because neither implementation reopens a closed node, tie order
 * directly selects among equal-cost routes -- which both games persist.
 */
typedef enum kt_nav_tiebreak {
    KT_TIEBREAK_SEQUENCE = 0,   /* insertion order; the default */
    KT_TIEBREAK_CELL_INDEX      /* lower map index wins */
} kt_nav_tiebreak;

/*
 * Heap discipline. The two are not interchangeable: they settle equal-priority
 * nodes in a different order, and because no closed node is ever reopened, the
 * first predecessor to relax a cell owns its route permanently. Measured on
 * C-COM: roughly 14 queries in 1000 returned a different equal-length route
 * under SEQUENCE, which its replay and render proofs would reject.
 */
typedef enum kt_nav_order {
    /* Ties broken explicitly by insertion order, then cell index. */
    KT_NAV_ORDER_SEQUENCE = 0,
    /*
     * One heap entry per node with decrease-key, ties resolved by array
     * layout: sift-up stops when the parent compares less-or-equal, and
     * sift-down prefers the left child and refuses to promote an equal one.
     */
    KT_NAV_ORDER_DECREASE_KEY
} kt_nav_order;

typedef struct kt_nav_hooks {
    kt_step_cost_fn step_cost;
    kt_heuristic_fn heuristic;   /* may be NULL */
    kt_nav_priority_fn priority; /* may be NULL */
    void *user;
    uint32_t min_step_cost;      /* used by the default heuristic; >= 1 */
    bool allow_diagonal;
    kt_nav_tiebreak tiebreak;
    kt_nav_order order;
} kt_nav_hooks;

void kt_nav_hooks_init(kt_nav_hooks *hooks);

/*
 * Per-cell search bookkeeping. The generation counter avoids clearing the
 * whole grid per query, which matters at C-COM's 6400 cells.
 */
/*
 * Minimum heap storage for a map: one slot per cell.
 *
 * Both disciplines keep exactly one heap entry per node and reposition it on
 * improvement. Queueing a duplicate instead is not viable here -- an entry
 * stores only a cell index and the comparator reads the node's CURRENT cost,
 * so improving an open node mutates the key of every entry naming it and
 * breaks the invariant with no re-sift.
 */
size_t kt_nav_required_heap(const kt_map *map);

typedef struct kt_nav_node {
    uint32_t generation;
    uint32_t cell_index;
    uint32_t cost;
    uint32_t estimate;
    uint32_t sequence;
    int64_t parent;       /* predecessor cell index, -1 at the root */
    uint8_t prev_dir;
    uint8_t state;
    uint8_t reserved[6];
} kt_nav_node;

typedef struct kt_nav_workspace {
    kt_nav_node *nodes;       /* caller storage, one per map cell */
    size_t node_capacity;
    uint32_t *heap;           /* caller storage */
    size_t heap_capacity;
    uint32_t *heap_pos;       /* KT_NAV_ORDER_DECREASE_KEY only; may be NULL */
    size_t heap_pos_capacity;
    size_t heap_count;
    uint32_t generation;
    uint32_t sequence;
} kt_nav_workspace;

kt_status kt_nav_workspace_init(kt_nav_workspace *workspace, kt_nav_node *nodes,
                                size_t node_capacity, uint32_t *heap,
                                size_t heap_capacity);

/*
 * The full form. heap and heap_pos each need one slot per map cell. The
 * position index is REQUIRED by both disciplines; kt_nav_workspace_init()
 * above is a convenience that leaves it unset and must be followed by this
 * call before searching.
 */
kt_status kt_nav_workspace_init_indexed(kt_nav_workspace *workspace,
                                        kt_nav_node *nodes,
                                        size_t node_capacity, uint32_t *heap,
                                        size_t heap_capacity,
                                        uint32_t *heap_pos,
                                        size_t heap_pos_capacity);

typedef struct kt_path {
    uint8_t dirs[KT_PATH_MAX_STEPS];  /* walk order, each a kt_direction */
    uint16_t count;
    bool truncated;                   /* route longer than KT_PATH_MAX_STEPS */
    uint32_t total_cost;
} kt_path;

/*
 * A* from start to goal. Returns KT_ERR_UNREACHABLE when no route exists
 * within max_cost. Consumes no random stream and allocates nothing.
 */
kt_status kt_nav_find_path(const kt_map *map, kt_nav_workspace *workspace,
                           const kt_nav_hooks *hooks, kt_cell_point start,
                           kt_cell_point goal, uint32_t max_cost,
                           kt_path *out_path);

/*
 * Reachability flood sharing the same loop with no goal. After it returns,
 * query the workspace for per-cell costs. Reports how many cells were
 * reached, including the start.
 */
kt_status kt_nav_reachable(const kt_map *map, kt_nav_workspace *workspace,
                           const kt_nav_hooks *hooks, kt_cell_point start,
                           uint32_t max_cost, size_t *out_reached);

/*
 * Reachability flood that also records cells in SETTLE order, stopping once
 * `capacity` cells are recorded.
 *
 * The order matters, not just the set: a game's AI consumes the sequence, and
 * the early stop changes which cells are collected at all. Under
 * KT_NAV_ORDER_DECREASE_KEY this reproduces a settle order that a game's own
 * heap would produce.
 */
kt_status kt_nav_reachable_collect(const kt_map *map,
                                   kt_nav_workspace *workspace,
                                   const kt_nav_hooks *hooks,
                                   kt_cell_point start, uint32_t max_cost,
                                   kt_cell_point *out_cells, size_t capacity,
                                   size_t *out_count);

/* Valid only against the most recent search on this workspace. */
bool kt_nav_was_reached(const kt_map *map, const kt_nav_workspace *workspace,
                        kt_cell_point point);
uint32_t kt_nav_cost_to(const kt_map *map, const kt_nav_workspace *workspace,
                        kt_cell_point point);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_NAV_H */
