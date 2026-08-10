/*
 * render_queue.c — stable painter-order draw queue.
 */
#include <string.h>

#include "kilix_tactics_render.h"

kt_status kt_draw_queue_init(kt_draw_queue *queue, kt_draw_item *storage,
                             size_t capacity)
{
    if (queue == NULL || storage == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (capacity == 0u) {
        return KT_ERR_CAPACITY;
    }
    queue->items = storage;
    queue->capacity = capacity;
    queue->count = 0u;
    queue->sequence = 0u;
    queue->sorted = true;
    return KT_OK;
}

void kt_draw_queue_clear(kt_draw_queue *queue)
{
    if (queue == NULL) {
        return;
    }
    queue->count = 0u;
    queue->sequence = 0u;
    queue->sorted = true;
}

static kt_status kt_draw_push(kt_draw_queue *queue, int64_t depth,
                              kt_screen_point at, kt_cell_point cell,
                              uint16_t layer, uint32_t handle,
                              kt_draw_item **out_item)
{
    kt_draw_item *item;

    if (queue->count >= queue->capacity) {
        return KT_ERR_CAPACITY;
    }
    item = &queue->items[queue->count];
    memset(item, 0, sizeof(*item));
    item->depth = depth;
    item->at = at;
    item->cell = cell;
    item->layer = layer;
    item->handle = handle;
    item->sequence = queue->sequence++;
    ++queue->count;
    queue->sorted = false;
    if (out_item != NULL) {
        *out_item = item;
    }
    return KT_OK;
}

kt_status kt_draw_submit_cell(kt_draw_queue *queue, const kt_map *map,
                              const kt_projection *projection,
                              const kt_camera *camera, kt_cell_point cell,
                              uint16_t layer, uint32_t handle,
                              kt_draw_item **out_item)
{
    kt_screen_point at;
    int64_t depth;
    kt_status status;

    if (queue == NULL || queue->items == NULL) {
        return KT_ERR_ARGUMENT;
    }
    status = kt_project(map, projection, camera, cell, &at);
    if (status != KT_OK) {
        return status;
    }
    status = kt_depth_key(map, projection, camera, cell, &depth);
    if (status != KT_OK) {
        return status;
    }
    return kt_draw_push(queue, depth, at, cell, layer, handle, out_item);
}

kt_status kt_draw_submit_at(kt_draw_queue *queue, const kt_map *map,
                            const kt_projection *projection,
                            const kt_camera *camera, kt_cell_point sort_cell,
                            kt_screen_point at, uint16_t layer, uint32_t handle,
                            kt_draw_item **out_item)
{
    int64_t depth;
    kt_status status;

    if (queue == NULL || queue->items == NULL) {
        return KT_ERR_ARGUMENT;
    }
    status = kt_depth_key(map, projection, camera, sort_cell, &depth);
    if (status != KT_OK) {
        return status;
    }
    return kt_draw_push(queue, depth, at, sort_cell, layer, handle, out_item);
}

kt_status kt_draw_submit_keyed(kt_draw_queue *queue, int64_t depth,
                               uint16_t layer, uint32_t handle,
                               kt_draw_item **out_item)
{
    kt_screen_point at;

    if (queue == NULL || queue->items == NULL) {
        return KT_ERR_ARGUMENT;
    }
    at.x = 0;
    at.y = 0;
    return kt_draw_push(queue, depth, at, kt_cell_point_make(0, 0, 0), layer,
                        handle, out_item);
}

/*
 * Painter order: depth, then layer band, then submission order.
 *
 * The trailing sequence term makes this a TOTAL order on any queue built
 * through the submit entry points, because kt_draw_push() hands every item a
 * distinct sequence. Totality is what lets the sort below choose its
 * algorithm freely: with no two items comparing equal there is exactly one
 * correct permutation, so an unstable algorithm cannot produce a different
 * one. The suite pins that equivalence against the previous stable insertion
 * sort rather than leaving it as an argument.
 */
static bool kt_draw_before(const kt_draw_item *a, const kt_draw_item *b)
{
    if (a->depth != b->depth) {
        return a->depth < b->depth;
    }
    if (a->layer != b->layer) {
        return a->layer < b->layer;
    }
    return a->sequence < b->sequence;
}

static void kt_draw_swap(kt_draw_item *a, kt_draw_item *b)
{
    kt_draw_item tmp = *a;

    *a = *b;
    *b = tmp;
}

/* Sift one item down a max-heap rooted at `root` over items[0..count). */
static void kt_draw_sift(kt_draw_item *items, size_t root, size_t count)
{
    for (;;) {
        size_t child = root * 2u + 1u;
        size_t largest = root;

        if (child >= count) {
            return;
        }
        if (kt_draw_before(&items[largest], &items[child])) {
            largest = child;
        }
        if (child + 1u < count &&
            kt_draw_before(&items[largest], &items[child + 1u])) {
            largest = child + 1u;
        }
        if (largest == root) {
            return;
        }
        kt_draw_swap(&items[root], &items[largest]);
        root = largest;
    }
}

/*
 * Adaptive, allocation-free painter sort.
 *
 * The queue is rebuilt every frame and the previous implementation assumed
 * that made it nearly sorted, so a stable insertion sort would be linear.
 * That holds only when the game's terrain loop happens to match the active
 * depth policy. Measured on the 40x40x4 grid and the `for z { for y { for x } }`
 * submission the integration guide prescribes, the default
 * KT_DEPTH_DIAGONAL_MAJOR key produces 8.8-11.4 million inversions and the
 * insertion sort cost 17.9-24.3 ms per frame -- more than a whole 60 Hz
 * frame budget, spent entirely on ordering.
 *
 * Near-painter input keeps the insertion sort and its low constant, so the
 * case the original comment described stays as cheap as it was. Input that
 * exceeds a bounded work budget falls back to an in-place heap sort, which
 * allocates nothing and is O(n log n) on every input rather than only on the
 * lucky ones.
 */
kt_status kt_draw_queue_sort(kt_draw_queue *queue)
{
    size_t i;
    size_t count;
    size_t budget;
    size_t bits;

    if (queue == NULL || queue->items == NULL) {
        return KT_ERR_ARGUMENT;
    }
    /* A count beyond the bound storage would sort memory the queue does not
     * own; report it rather than walking off the array. */
    if (queue->count > queue->capacity) {
        return KT_ERR_STATE;
    }
    count = queue->count;
    if (count < 2u) {
        queue->sorted = true;
        return KT_OK;
    }

    /*
     * An insertion sort performs exactly one shift per inversion, so the work
     * it will do is unknown until it does it. Rather than predict from the
     * input, spend a budget on the order of what the heap sort would cost
     * (n log n) and abandon the attempt if it is exceeded. A near-painter
     * frame finishes well inside the budget and keeps insertion sort's much
     * lower constant; a frame whose submission order does not match the depth
     * policy stops early and pays the fallback instead of millions of shifts.
     *
     * Abandoning midway is safe: the array is still a permutation of the
     * submitted items, and kt_draw_before() is total, so the heap sort below
     * reaches the same single correct order from any permutation.
     */
    for (bits = 1u; (count >> bits) != 0u; ++bits) {
    }
    budget = count > SIZE_MAX / (2u * bits) ? SIZE_MAX : count * 2u * bits;

    for (i = 1u; i < count; ++i) {
        kt_draw_item pivot = queue->items[i];
        size_t j = i;

        while (j > 0u && kt_draw_before(&pivot, &queue->items[j - 1u])) {
            if (budget == 0u) {
                break;
            }
            queue->items[j] = queue->items[j - 1u];
            --j;
            --budget;
        }
        queue->items[j] = pivot;
        if (budget == 0u) {
            break;
        }
    }
    if (budget != 0u) {
        queue->sorted = true;
        return KT_OK;
    }

    i = count / 2u;
    while (i > 0u) {
        --i;
        kt_draw_sift(queue->items, i, count);
    }
    for (i = count; i > 1u; --i) {
        kt_draw_swap(&queue->items[0], &queue->items[i - 1u]);
        kt_draw_sift(queue->items, 0u, i - 1u);
    }
    queue->sorted = true;
    return KT_OK;
}

uint64_t kt_draw_queue_hash(const kt_draw_queue *queue)
{
    /* FNV-1a 64, matching the identifier hashing both games already use. */
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t count;
    size_t i;

    if (queue == NULL || queue->items == NULL) {
        return hash;
    }
    /* The hash is a golden a game compares across runs, so it reads only the
     * items the queue actually owns. */
    count = queue->count < queue->capacity ? queue->count : queue->capacity;
    for (i = 0u; i < count; ++i) {
        const kt_draw_item *item = &queue->items[i];
        uint64_t fields[6];
        size_t f;

        fields[0] = (uint64_t)item->depth;
        fields[1] = (uint64_t)(uint32_t)item->at.x |
                    ((uint64_t)(uint32_t)item->at.y << 32);
        fields[2] = (uint64_t)(uint32_t)item->cell.x |
                    ((uint64_t)(uint32_t)item->cell.y << 32);
        fields[3] = (uint64_t)(uint32_t)item->cell.z;
        fields[4] = (uint64_t)item->handle | ((uint64_t)item->layer << 32);
        fields[5] = (uint64_t)item->tint | ((uint64_t)item->flags << 32);
        for (f = 0u; f < sizeof(fields) / sizeof(fields[0]); ++f) {
            size_t byte;

            for (byte = 0u; byte < 8u; ++byte) {
                hash ^= (fields[f] >> (byte * 8u)) & 0xffu;
                hash *= UINT64_C(1099511628211);
            }
        }
    }
    return hash;
}
