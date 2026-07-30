/*
 * nav.c — A* and the reachability flood over a caller step-cost hook.
 */
#include <string.h>

#include "kilix_tactics_nav.h"

enum { KT_NODE_UNSEEN = 0, KT_NODE_OPEN = 1, KT_NODE_CLOSED = 2 };

void kt_nav_hooks_init(kt_nav_hooks *hooks)
{
    if (hooks == NULL) {
        return;
    }
    hooks->step_cost = NULL;
    hooks->heuristic = NULL;
    hooks->priority = NULL;
    hooks->user = NULL;
    hooks->min_step_cost = 1u;
    hooks->allow_diagonal = true;
    hooks->tiebreak = KT_TIEBREAK_SEQUENCE;
    hooks->order = KT_NAV_ORDER_SEQUENCE;
}

kt_status kt_nav_workspace_init(kt_nav_workspace *workspace, kt_nav_node *nodes,
                                size_t node_capacity, uint32_t *heap,
                                size_t heap_capacity)
{
    if (workspace == NULL || nodes == NULL || heap == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (node_capacity == 0u || heap_capacity == 0u) {
        return KT_ERR_CAPACITY;
    }
    workspace->nodes = nodes;
    workspace->node_capacity = node_capacity;
    workspace->heap = heap;
    workspace->heap_capacity = heap_capacity;
    workspace->heap_pos = NULL;
    workspace->heap_pos_capacity = 0u;
    workspace->heap_count = 0u;
    workspace->generation = 0u;
    workspace->sequence = 0u;
    memset(nodes, 0, node_capacity * sizeof(*nodes));
    return KT_OK;
}

kt_status kt_nav_workspace_init_indexed(kt_nav_workspace *workspace,
                                        kt_nav_node *nodes,
                                        size_t node_capacity, uint32_t *heap,
                                        size_t heap_capacity,
                                        uint32_t *heap_pos,
                                        size_t heap_pos_capacity)
{
    kt_status status = kt_nav_workspace_init(workspace, nodes, node_capacity,
                                             heap, heap_capacity);

    if (status != KT_OK) {
        return status;
    }
    if (heap_pos == NULL || heap_pos_capacity == 0u) {
        return KT_ERR_ARGUMENT;
    }
    workspace->heap_pos = heap_pos;
    workspace->heap_pos_capacity = heap_pos_capacity;
    return KT_OK;
}

/*
 * Integer octile distance scaled by the cheapest step. Admissible whenever
 * no step costs less than min_step_cost.
 */
static uint32_t kt_default_heuristic(kt_cell_point at, kt_cell_point goal,
                                     uint32_t min_step_cost, bool diagonal)
{
    int32_t dx = goal.x - at.x;
    int32_t dy = goal.y - at.y;
    uint32_t adx = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t ady = (uint32_t)(dy < 0 ? -dy : dy);

    if (!diagonal) {
        return (adx + ady) * min_step_cost;
    }
    /*
     * With diagonals the shortest possible route is max(|dx|,|dy|) steps.
     * Charging each at the cheapest cardinal rate stays admissible, since a
     * diagonal is never cheaper than a cardinal step in either game.
     */
    return (adx < ady ? ady : adx) * min_step_cost;
}

static bool kt_node_is_better(const kt_nav_node *a, const kt_nav_node *b,
                              const kt_nav_hooks *hooks)
{
    uint64_t priority_a;
    uint64_t priority_b;

    if (hooks->priority != NULL) {
        priority_a = hooks->priority(hooks->user, a->cost, a->estimate);
        priority_b = hooks->priority(hooks->user, b->cost, b->estimate);
    } else {
        priority_a = (uint64_t)a->cost + (uint64_t)a->estimate;
        priority_b = (uint64_t)b->cost + (uint64_t)b->estimate;
    }
    if (priority_a != priority_b) {
        return priority_a < priority_b;
    }
    if (a->cost != b->cost) {
        return a->cost < b->cost;
    }
    if (hooks->tiebreak == KT_TIEBREAK_CELL_INDEX) {
        return a->cell_index < b->cell_index;
    }
    /* Insertion order is the final tiebreak, so the search is total and
     * reproducible rather than dependent on heap layout. */
    return a->sequence < b->sequence;
}

/* Key only, no tiebreak: the legacy discipline resolves ties by array layout,
 * which is what reproduces a game's existing route choice. */
static uint64_t kt_node_key(const kt_nav_node *node, const kt_nav_hooks *hooks)
{
    if (hooks->priority != NULL) {
        return hooks->priority(hooks->user, node->cost, node->estimate);
    }
    return (uint64_t)node->cost + (uint64_t)node->estimate;
}

size_t kt_nav_required_heap(const kt_map *map)
{
    /* One entry per node in both disciplines. */
    return kt_map_cell_count(map);
}

static void kt_heap_place(kt_nav_workspace *workspace, size_t slot,
                          uint32_t cell_index)
{
    workspace->heap[slot] = cell_index;
    if (workspace->heap_pos != NULL &&
        (size_t)cell_index < workspace->heap_pos_capacity) {
        workspace->heap_pos[cell_index] = (uint32_t)slot;
    }
}

static void kt_heap_swap(kt_nav_workspace *workspace, size_t a, size_t b)
{
    uint32_t tmp = workspace->heap[a];

    kt_heap_place(workspace, a, workspace->heap[b]);
    kt_heap_place(workspace, b, tmp);
}

/*
 * Faithful transcription of the decrease-key discipline: sift up while the
 * parent compares strictly greater, sift down preferring the left child and
 * refusing to promote an equal one.
 */
static void kt_heap_up_legacy(kt_nav_workspace *workspace, size_t index,
                              const kt_nav_hooks *hooks)
{
    uint32_t moving = workspace->heap[index];
    uint64_t key = kt_node_key(&workspace->nodes[moving], hooks);

    while (index > 0u) {
        size_t parent = (index - 1u) / 2u;

        if (kt_node_key(&workspace->nodes[workspace->heap[parent]], hooks) <=
            key) {
            break;
        }
        kt_heap_place(workspace, index, workspace->heap[parent]);
        index = parent;
    }
    kt_heap_place(workspace, index, moving);
}

static void kt_heap_down_legacy(kt_nav_workspace *workspace, size_t index,
                                const kt_nav_hooks *hooks)
{
    uint32_t moving = workspace->heap[index];
    uint64_t key = kt_node_key(&workspace->nodes[moving], hooks);

    for (;;) {
        size_t child = index * 2u + 1u;

        if (child >= workspace->heap_count) {
            break;
        }
        if (child + 1u < workspace->heap_count &&
            kt_node_key(&workspace->nodes[workspace->heap[child + 1u]],
                        hooks) <
                kt_node_key(&workspace->nodes[workspace->heap[child]],
                            hooks)) {
            ++child;
        }
        if (kt_node_key(&workspace->nodes[workspace->heap[child]], hooks) >=
            key) {
            break;
        }
        kt_heap_place(workspace, index, workspace->heap[child]);
        index = child;
    }
    kt_heap_place(workspace, index, moving);
}

static void kt_heap_up(kt_nav_workspace *workspace, size_t index,
                       const kt_nav_hooks *hooks)
{
    while (index > 0u) {
        size_t parent = (index - 1u) / 2u;

        if (!kt_node_is_better(&workspace->nodes[workspace->heap[index]],
                               &workspace->nodes[workspace->heap[parent]],
                               hooks)) {
            break;
        }
        kt_heap_swap(workspace, index, parent);
        index = parent;
    }
}

static void kt_heap_down(kt_nav_workspace *workspace, size_t index,
                         const kt_nav_hooks *hooks)
{
    for (;;) {
        size_t left = index * 2u + 1u;
        size_t right = left + 1u;
        size_t best = index;

        if (left < workspace->heap_count &&
            kt_node_is_better(&workspace->nodes[workspace->heap[left]],
                              &workspace->nodes[workspace->heap[best]],
                              hooks)) {
            best = left;
        }
        if (right < workspace->heap_count &&
            kt_node_is_better(&workspace->nodes[workspace->heap[right]],
                              &workspace->nodes[workspace->heap[best]],
                              hooks)) {
            best = right;
        }
        if (best == index) {
            return;
        }
        kt_heap_swap(workspace, index, best);
        index = best;
    }
}

static kt_status kt_heap_push(kt_nav_workspace *workspace, size_t cell_index,
                              const kt_nav_hooks *hooks)
{
    if (workspace->heap_count >= workspace->heap_capacity) {
        return KT_ERR_CAPACITY;
    }
    kt_heap_place(workspace, workspace->heap_count, (uint32_t)cell_index);
    ++workspace->heap_count;
    if (hooks->order == KT_NAV_ORDER_DECREASE_KEY) {
        kt_heap_up_legacy(workspace, workspace->heap_count - 1u, hooks);
    } else {
        kt_heap_up(workspace, workspace->heap_count - 1u, hooks);
    }
    return KT_OK;
}

static size_t kt_heap_pop(kt_nav_workspace *workspace,
                          const kt_nav_hooks *hooks)
{
    size_t top = workspace->heap[0];

    --workspace->heap_count;
    if (workspace->heap_count > 0u) {
        kt_heap_place(workspace, 0u, workspace->heap[workspace->heap_count]);
        if (hooks->order == KT_NAV_ORDER_DECREASE_KEY) {
            kt_heap_down_legacy(workspace, 0u, hooks);
        } else {
            kt_heap_down(workspace, 0u, hooks);
        }
    }
    return top;
}

static kt_nav_node *kt_touch(kt_nav_workspace *workspace, size_t index)
{
    kt_nav_node *node = &workspace->nodes[index];

    if (node->generation != workspace->generation) {
        node->generation = workspace->generation;
        node->cell_index = (uint32_t)index;
        node->cost = UINT32_MAX;
        node->estimate = 0u;
        node->sequence = 0u;
        node->parent = -1;
        node->prev_dir = 0u;
        node->state = KT_NODE_UNSEEN;
    }
    return node;
}

/*
 * Shared search. When `goal` is NULL this is the reachability flood; the
 * loop is otherwise identical so the two can never disagree about costs.
 */
static kt_status kt_search(const kt_map *map, kt_nav_workspace *workspace,
                           const kt_nav_hooks *hooks, kt_cell_point start,
                           const kt_cell_point *goal, uint32_t max_cost,
                           size_t *out_goal_index, size_t *out_reached,
                           kt_cell_point *collect, size_t collect_capacity)
{
    size_t start_index;
    size_t reached = 0u;
    size_t cells;

    if (map == NULL || workspace == NULL || hooks == NULL ||
        hooks->step_cost == NULL) {
        return KT_ERR_ARGUMENT;
    }
    cells = kt_map_cell_count(map);
    if (cells == 0u || workspace->node_capacity < cells) {
        return KT_ERR_CAPACITY;
    }
    /* Both disciplines keep one entry per node, so the heap needs one slot per
     * cell and the position index is mandatory. */
    if (workspace->heap_pos == NULL || workspace->heap_pos_capacity < cells ||
        workspace->heap_capacity < cells) {
        return KT_ERR_CAPACITY;
    }
    if (hooks->min_step_cost == 0u) {
        return KT_ERR_ARGUMENT;
    }
    start_index = kt_map_index(map, start);
    if (start_index == SIZE_MAX) {
        return KT_ERR_RANGE;
    }
    if (goal != NULL && kt_map_index(map, *goal) == SIZE_MAX) {
        return KT_ERR_RANGE;
    }

    ++workspace->generation;
    workspace->heap_count = 0u;
    workspace->sequence = 0u;

    {
        kt_nav_node *node = kt_touch(workspace, start_index);

        node->cost = 0u;
        node->estimate = 0u;
        node->sequence = workspace->sequence++;
        node->state = KT_NODE_OPEN;
        if (kt_heap_push(workspace, start_index, hooks) != KT_OK) {
            return KT_ERR_CAPACITY;
        }
    }

    while (workspace->heap_count > 0u) {
        size_t index = kt_heap_pop(workspace, hooks);
        kt_nav_node *node = kt_touch(workspace, index);
        kt_cell_point at;
        int dir;

        if (node->state == KT_NODE_CLOSED) {
            continue;
        }
        node->state = KT_NODE_CLOSED;
        ++reached;
        if (!kt_map_point_from_index(map, index, &at)) {
            return KT_ERR_STATE;
        }
        if (collect != NULL) {
            /* Recorded at the moment of settling, which is the order a
             * consuming game's AI depends on. */
            collect[reached - 1u] = at;
            if (reached >= collect_capacity) {
                if (out_reached != NULL) {
                    *out_reached = reached;
                }
                return KT_OK;
            }
        }
        if (goal != NULL && kt_cell_point_equal(at, *goal)) {
            if (out_goal_index != NULL) {
                *out_goal_index = index;
            }
            if (out_reached != NULL) {
                *out_reached = reached;
            }
            return KT_OK;
        }

        for (dir = 0; dir < KT_DIR_COUNT; ++dir) {
            kt_cell_point dest;
            size_t dest_index;
            kt_nav_node *neighbour;
            uint32_t step;
            uint64_t total;

            if (!hooks->allow_diagonal && KT_DIR_IS_DIAGONAL(dir)) {
                continue;
            }
            dest = kt_cell_point_make(at.x + kt_direction_dx[dir],
                                      at.y + kt_direction_dy[dir], at.z);
            step = hooks->step_cost(hooks->user, at, (kt_direction)dir, &dest);
            if (step == KT_STEP_BLOCKED) {
                continue;
            }
            dest_index = kt_map_index(map, dest);
            if (dest_index == SIZE_MAX) {
                continue;
            }
            total = (uint64_t)node->cost + (uint64_t)step;
            if (total > (uint64_t)max_cost) {
                continue;
            }
            neighbour = kt_touch(workspace, dest_index);
            if (neighbour->state == KT_NODE_CLOSED ||
                (uint32_t)total >= neighbour->cost) {
                continue;
            }
            neighbour->cost = (uint32_t)total;
            neighbour->parent = (int64_t)index;
            neighbour->prev_dir = (uint8_t)dir;
            {
                bool was_open = neighbour->state == KT_NODE_OPEN;

                neighbour->sequence = workspace->sequence++;
                neighbour->state = KT_NODE_OPEN;
                if (was_open) {
                    /*
                     * Reposition the existing entry rather than queueing a
                     * second one. Lazy duplicates cannot work here: a heap
                     * entry stores only the cell index and the comparator
                     * reads the node's CURRENT cost, so improving an open
                     * node silently mutates the key of every entry naming it,
                     * breaking the invariant with no re-sift. Measured
                     * consequence: a node settling at cost 12 ahead of one at
                     * cost 11, which risks closing a node before its cost is
                     * final.
                     *
                     * The estimate is not recomputed, matching both reference
                     * implementations -- for a consistent heuristic it is a
                     * function of the cell and goal alone.
                     */
                    if (hooks->order == KT_NAV_ORDER_DECREASE_KEY) {
                        kt_heap_up_legacy(workspace,
                                          workspace->heap_pos[dest_index],
                                          hooks);
                    } else {
                        kt_heap_up(workspace, workspace->heap_pos[dest_index],
                                   hooks);
                    }
                    continue;
                }
            }
            if (goal != NULL) {
                neighbour->estimate =
                    hooks->heuristic != NULL
                        ? hooks->heuristic(hooks->user, dest, *goal)
                        : kt_default_heuristic(dest, *goal, hooks->min_step_cost,
                                               hooks->allow_diagonal);
            } else {
                neighbour->estimate = 0u;
            }
            if (kt_heap_push(workspace, dest_index, hooks) != KT_OK) {
                return KT_ERR_CAPACITY;
            }
        }
    }

    if (out_reached != NULL) {
        *out_reached = reached;
    }
    return goal != NULL ? KT_ERR_UNREACHABLE : KT_OK;
}

kt_status kt_nav_find_path(const kt_map *map, kt_nav_workspace *workspace,
                           const kt_nav_hooks *hooks, kt_cell_point start,
                           kt_cell_point goal, uint32_t max_cost,
                           kt_path *out_path)
{
    size_t goal_index = SIZE_MAX;
    kt_status status;
    size_t walk;
    uint16_t count = 0u;
    uint16_t i;

    if (out_path == NULL) {
        return KT_ERR_ARGUMENT;
    }
    memset(out_path, 0, sizeof(*out_path));
    status = kt_search(map, workspace, hooks, start, &goal, max_cost,
                       &goal_index, NULL, NULL, 0u);
    if (status != KT_OK) {
        return status;
    }
    if (goal_index == SIZE_MAX) {
        return KT_ERR_UNREACHABLE;
    }

    out_path->total_cost = workspace->nodes[goal_index].cost;

    /* Walk parents back to the root, then reverse into walk order. */
    walk = goal_index;
    while (workspace->nodes[walk].parent >= 0) {
        if (count >= KT_PATH_MAX_STEPS) {
            out_path->truncated = true;
            break;
        }
        out_path->dirs[count] = workspace->nodes[walk].prev_dir;
        ++count;
        walk = (size_t)workspace->nodes[walk].parent;
    }
    for (i = 0u; i < count / 2u; ++i) {
        uint8_t tmp = out_path->dirs[i];

        out_path->dirs[i] = out_path->dirs[count - 1u - i];
        out_path->dirs[count - 1u - i] = tmp;
    }
    out_path->count = count;
    return KT_OK;
}

kt_status kt_nav_reachable(const kt_map *map, kt_nav_workspace *workspace,
                           const kt_nav_hooks *hooks, kt_cell_point start,
                           uint32_t max_cost, size_t *out_reached)
{
    return kt_search(map, workspace, hooks, start, NULL, max_cost, NULL,
                     out_reached, NULL, 0u);
}

kt_status kt_nav_reachable_collect(const kt_map *map,
                                   kt_nav_workspace *workspace,
                                   const kt_nav_hooks *hooks,
                                   kt_cell_point start, uint32_t max_cost,
                                   kt_cell_point *out_cells, size_t capacity,
                                   size_t *out_count)
{
    size_t reached = 0u;
    kt_status status;

    if (out_cells == NULL || capacity == 0u || out_count == NULL) {
        return KT_ERR_ARGUMENT;
    }
    *out_count = 0u;
    status = kt_search(map, workspace, hooks, start, NULL, max_cost, NULL,
                       &reached, out_cells, capacity);
    if (status != KT_OK) {
        return status;
    }
    *out_count = reached < capacity ? reached : capacity;
    return KT_OK;
}

kt_status kt_nav_predecessor(const kt_map *map,
                             const kt_nav_workspace *workspace,
                             kt_cell_point point, kt_cell_point *out)
{
    size_t index;

    if (map == NULL || workspace == NULL || out == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (!kt_nav_was_reached(map, workspace, point)) {
        return KT_ERR_UNREACHABLE;
    }
    index = kt_map_index(map, point);
    if (workspace->nodes[index].parent < 0) {
        return KT_ERR_UNREACHABLE;   /* the start cell has no predecessor */
    }
    if (!kt_map_point_from_index(map, (size_t)workspace->nodes[index].parent,
                                 out)) {
        return KT_ERR_STATE;
    }
    return KT_OK;
}

bool kt_nav_was_reached(const kt_map *map, const kt_nav_workspace *workspace,
                        kt_cell_point point)
{
    size_t index;

    if (map == NULL || workspace == NULL || workspace->nodes == NULL) {
        return false;
    }
    index = kt_map_index(map, point);
    if (index == SIZE_MAX || index >= workspace->node_capacity) {
        return false;
    }
    return workspace->nodes[index].generation == workspace->generation &&
           workspace->nodes[index].state == KT_NODE_CLOSED;
}

uint32_t kt_nav_cost_to(const kt_map *map, const kt_nav_workspace *workspace,
                        kt_cell_point point)
{
    size_t index;

    if (!kt_nav_was_reached(map, workspace, point)) {
        return KT_STEP_BLOCKED;
    }
    index = kt_map_index(map, point);
    return workspace->nodes[index].cost;
}
