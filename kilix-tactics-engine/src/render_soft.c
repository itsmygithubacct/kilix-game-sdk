/*
 * render_soft.c — draws a sorted tactical queue into a caller-owned canvas.
 */
#include "kilix_tactics_soft.h"

kt_status kt_soft_draw_queue(sr_canvas *dst, const kt_draw_queue *queue,
                             kt_soft_resolve_fn resolve, void *user,
                             size_t *out_drawn)
{
    size_t index;
    size_t drawn = 0u;

    if (dst == NULL || queue == NULL || queue->items == NULL ||
        resolve == NULL) {
        return KT_ERR_ARGUMENT;
    }
    /* Painting an unsorted queue would silently draw out of order, which is
     * far harder to notice than an error return. */
    if (!queue->sorted) {
        return KT_ERR_STATE;
    }

    for (index = 0u; index < queue->count; ++index) {
        const kt_draw_item *item = &queue->items[index];
        kt_soft_sprite sprite;
        int x;
        int y;

        sprite.canvas = NULL;
        sprite.offset_x = 0;
        sprite.offset_y = 0;
        sprite.alpha = 1.0f;
        sprite.tint_enabled = false;
        sprite.tint_rgb = 0u;

        if (!resolve(user, item, &sprite) || sprite.canvas == NULL) {
            continue;
        }
        x = (int)(item->at.x + sprite.offset_x);
        y = (int)(item->at.y + sprite.offset_y);
        if (sprite.tint_enabled) {
            sr_blit_tint(dst, sprite.canvas, x, y, sprite.tint_rgb,
                         sprite.alpha);
        } else if (sprite.alpha >= 1.0f) {
            sr_blit(dst, sprite.canvas, x, y);
        } else {
            sr_blit_alpha(dst, sprite.canvas, x, y, sprite.alpha);
        }
        ++drawn;
    }

    if (out_drawn != NULL) {
        *out_drawn = drawn;
    }
    return KT_OK;
}

void kt_soft_diamond_outline(sr_canvas *dst, int32_t x, int32_t y,
                             int32_t tile_width, int32_t tile_height,
                             float width, uint32_t rgb, float alpha,
                             int dash_on, int dash_off)
{
    float half_w;
    float half_h;
    float cx;
    float cy;

    if (dst == NULL || tile_width <= 0 || tile_height <= 0) {
        return;
    }
    half_w = (float)tile_width / 2.0f;
    half_h = (float)tile_height / 2.0f;
    cx = (float)x + half_w;
    cy = (float)y + half_h;

    sr_line(dst, cx - half_w, cy, cx, cy - half_h, width, rgb, alpha, dash_on,
            dash_off);
    sr_line(dst, cx, cy - half_h, cx + half_w, cy, width, rgb, alpha, dash_on,
            dash_off);
    sr_line(dst, cx + half_w, cy, cx, cy + half_h, width, rgb, alpha, dash_on,
            dash_off);
    sr_line(dst, cx, cy + half_h, cx - half_w, cy, width, rgb, alpha, dash_on,
            dash_off);
}

void kt_soft_diamond_fill(sr_canvas *dst, int32_t x, int32_t y,
                          int32_t tile_width, int32_t tile_height,
                          uint32_t rgb, float alpha)
{
    float xs[4];
    float ys[4];
    float half_w;
    float half_h;
    float cx;
    float cy;

    if (dst == NULL || tile_width <= 0 || tile_height <= 0) {
        return;
    }
    half_w = (float)tile_width / 2.0f;
    half_h = (float)tile_height / 2.0f;
    cx = (float)x + half_w;
    cy = (float)y + half_h;

    xs[0] = cx - half_w;
    ys[0] = cy;
    xs[1] = cx;
    ys[1] = cy - half_h;
    xs[2] = cx + half_w;
    ys[2] = cy;
    xs[3] = cx;
    ys[3] = cy + half_h;
    sr_fill_convex(dst, xs, ys, 4, rgb, alpha);
}
