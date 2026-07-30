/*
 * kilix_tactics_soft.h — soft-raster adapter for the tactical draw queue.
 *
 * Part of libkilix-tactics-soft. This is the only header that depends on a
 * rasterizer; the core archive stays standard-library-only so ordering and
 * spatial logic remain testable headlessly.
 *
 * The adapter references but does not bundle soft-raster: the consuming
 * project pins it, normally through kilix-game-kit's exported
 * SOFT_RASTER_DIR, so only one implementation is ever linked.
 */
#ifndef KILIX_TACTICS_SOFT_H
#define KILIX_TACTICS_SOFT_H

#include "kilix_tactics_render.h"
#include "kilix_tactics_types.h"
#include "soft_raster.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A resolved sprite. The offsets move the sprite's top-left away from the
 * item's projected point, which is how each game applies its own anchor
 * convention: C-COM's 32x40 sprite cell sits directly on the projected
 * origin, while a game treating that point as the floor centre offsets by
 * half a tile.
 */
typedef struct kt_soft_sprite {
    const sr_canvas *canvas;
    int32_t offset_x;
    int32_t offset_y;
    float alpha;          /* 1.0 for opaque */
    bool tint_enabled;
    uint32_t tint_rgb;
} kt_soft_sprite;

/*
 * Resolve a queued item's handle to a sprite. Return false to skip the item,
 * which is how cutaway, fog, and per-frame culling stay game-owned.
 */
typedef bool (*kt_soft_resolve_fn)(void *user, const kt_draw_item *item,
                                   kt_soft_sprite *out);

/*
 * Draw a sorted queue into a caller-owned canvas, in order. Allocates
 * nothing. Returns KT_ERR_STATE if the queue has not been sorted, since
 * drawing an unsorted queue would silently paint out of order.
 */
kt_status kt_soft_draw_queue(sr_canvas *dst, const kt_draw_queue *queue,
                             kt_soft_resolve_fn resolve, void *user,
                             size_t *out_drawn);

/*
 * Floor-diamond overlay, the shape both games draw for selection, movement
 * previews, and target markers. The diamond is inscribed in the
 * tile_width x tile_height box whose top-left corner is (x, y).
 *
 * dash_on/dash_off are passed through to the rasterizer; both zero draws a
 * solid outline. Dashes exist here because both games mark provisional
 * state that way rather than with colour alone.
 */
void kt_soft_diamond_outline(sr_canvas *dst, int32_t x, int32_t y,
                             int32_t tile_width, int32_t tile_height,
                             float width, uint32_t rgb, float alpha,
                             int dash_on, int dash_off);
void kt_soft_diamond_fill(sr_canvas *dst, int32_t x, int32_t y,
                          int32_t tile_width, int32_t tile_height,
                          uint32_t rgb, float alpha);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_SOFT_H */
