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

/*
 * Insertion sort. The queue is rebuilt in near-painter order every frame, so
 * it arrives almost sorted; this is linear on that input, allocates nothing,
 * and is stable by construction.
 */
kt_status kt_draw_queue_sort(kt_draw_queue *queue)
{
    size_t i;

    if (queue == NULL || queue->items == NULL) {
        return KT_ERR_ARGUMENT;
    }
    for (i = 1u; i < queue->count; ++i) {
        kt_draw_item pivot = queue->items[i];
        size_t j = i;

        while (j > 0u && kt_draw_before(&pivot, &queue->items[j - 1u])) {
            queue->items[j] = queue->items[j - 1u];
            --j;
        }
        queue->items[j] = pivot;
    }
    queue->sorted = true;
    return KT_OK;
}

uint64_t kt_draw_queue_hash(const kt_draw_queue *queue)
{
    /* FNV-1a 64, matching the identifier hashing both games already use. */
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;

    if (queue == NULL || queue->items == NULL) {
        return hash;
    }
    for (i = 0u; i < queue->count; ++i) {
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
