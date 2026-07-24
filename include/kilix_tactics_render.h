/*
 * kilix_tactics_render.h — stable painter-order draw queue.
 *
 * Part of libkilix-tactics-core. Standard library only, and free of any
 * rasterizer dependency: submission and ordering are testable headlessly.
 *
 * C-COM's fourteen-step draw order becomes a sequence of game-side
 * submissions into this queue rather than engine-owned passes. See
 * DECISIONS.md T-009.
 */
#ifndef KILIX_TACTICS_RENDER_H
#define KILIX_TACTICS_RENDER_H

#include "kilix_tactics_projection.h"
#include "kilix_tactics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Layer within a single cell. The engine sorts by these but assigns no
 * meaning beyond the order: a game decides what occupies each band, and may
 * use the intermediate values freely.
 */
enum {
    KT_LAYER_FLOOR = 0,
    KT_LAYER_WALL_WEST = 16,
    KT_LAYER_WALL_NORTH = 32,
    KT_LAYER_OBJECT = 48,
    KT_LAYER_ITEM = 64,
    KT_LAYER_UNIT = 80,
    KT_LAYER_EFFECT = 96,
    KT_LAYER_OVERLAY = 112,
    KT_LAYER_MAX = 255
};

typedef struct kt_draw_item {
    int64_t depth;         /* engine-computed painter key                  */
    kt_screen_point at;    /* projected screen position                    */
    kt_cell_point cell;    /* originating world cell                       */
    uint32_t handle;       /* caller sprite or frame identity              */
    uint32_t tint;         /* caller-defined; passed through untouched     */
    uint32_t sequence;     /* submission index, the stability tiebreak     */
    uint16_t layer;        /* KT_LAYER_* band                              */
    uint16_t flags;        /* caller-defined                               */
    void *user;
} kt_draw_item;

typedef struct kt_draw_queue {
    kt_draw_item *items;   /* caller storage */
    size_t capacity;
    size_t count;
    uint32_t sequence;
    bool sorted;
} kt_draw_queue;

kt_status kt_draw_queue_init(kt_draw_queue *queue, kt_draw_item *storage,
                             size_t capacity);
void kt_draw_queue_clear(kt_draw_queue *queue);

/*
 * Submit a sprite anchored to a world cell. The engine projects it and
 * computes the depth key, so painting and picking cannot disagree.
 */
kt_status kt_draw_submit_cell(kt_draw_queue *queue, const kt_map *map,
                              const kt_projection *projection,
                              const kt_camera *camera, kt_cell_point cell,
                              uint16_t layer, uint32_t handle,
                              kt_draw_item **out_item);

/*
 * Submit at an explicit screen position while still sorting as if it sat in
 * `cell`. This is the path for anything mid-step: a walking unit between two
 * tiles sorts against the terrain it is crossing.
 */
kt_status kt_draw_submit_at(kt_draw_queue *queue, const kt_map *map,
                            const kt_projection *projection,
                            const kt_camera *camera, kt_cell_point sort_cell,
                            kt_screen_point at, uint16_t layer, uint32_t handle,
                            kt_draw_item **out_item);

/*
 * Sort into painter order: depth, then layer, then submission index. The
 * order is total, so identical submissions always yield an identical queue.
 */
kt_status kt_draw_queue_sort(kt_draw_queue *queue);

/*
 * Order-sensitive hash of the sorted queue, for golden tests that need to
 * detect draw-order drift without rendering anything.
 */
uint64_t kt_draw_queue_hash(const kt_draw_queue *queue);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_RENDER_H */
