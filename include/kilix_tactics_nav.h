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

typedef struct kt_nav_hooks {
    kt_step_cost_fn step_cost;
    kt_heuristic_fn heuristic;   /* may be NULL */
    void *user;
    uint32_t min_step_cost;      /* used by the default heuristic; >= 1 */
    bool allow_diagonal;
} kt_nav_hooks;

void kt_nav_hooks_init(kt_nav_hooks *hooks);

/*
 * Per-cell search bookkeeping. The generation counter avoids clearing the
 * whole grid per query, which matters at C-COM's 6400 cells.
 */
typedef struct kt_nav_node {
    uint32_t generation;
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
    uint32_t *heap;           /* caller storage, one per map cell */
    size_t heap_capacity;
    size_t heap_count;
    uint32_t generation;
    uint32_t sequence;
} kt_nav_workspace;

kt_status kt_nav_workspace_init(kt_nav_workspace *workspace, kt_nav_node *nodes,
                                size_t node_capacity, uint32_t *heap,
                                size_t heap_capacity);

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

/* Valid only against the most recent search on this workspace. */
bool kt_nav_was_reached(const kt_map *map, const kt_nav_workspace *workspace,
                        kt_cell_point point);
uint32_t kt_nav_cost_to(const kt_map *map, const kt_nav_workspace *workspace,
                        kt_cell_point point);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_NAV_H */
