/*
 * render_soft.c — draws a sorted tactical queue into a caller-owned canvas.
 *
 * Everything here crosses from engine data into a rasterizer that takes int
 * pixel coordinates and float coverage. The rasterizer clips to its canvas
 * and documents that the clip "keeps the loop bounds small and the
 * float-to-int casts in range" -- an invariant that only holds if the values
 * reaching it are representable in the first place. Converting the boundary
 * is this adapter's job, so each entry point proves its geometry and coverage
 * before handing them over.
 */
#include <limits.h>
#include <math.h>

#include "kilix_tactics_soft.h"

/*
 * Screen position plus the game's anchor offset, in int64 so the sum is
 * exact, rejected when it cannot address a pixel. Anything outside the range
 * cannot intersect any canvas, so refusing it removes work rather than
 * output.
 */
static bool kt_soft_pixel(int32_t base, int32_t offset, int *out)
{
    int64_t sum = (int64_t)base + (int64_t)offset;

    if (sum < (int64_t)INT_MIN || sum > (int64_t)INT_MAX) {
        return false;
    }
    *out = (int)sum;
    return true;
}

/*
 * Normalise caller coverage. A NaN alpha compares false against every
 * threshold, so it previously reached the rasterizer, survived its
 * `alpha <= 0` guard and its clamp, and became an undefined float-to-int
 * conversion. Non-finite coverage is not a drawing request; finite coverage
 * above one is the opaque path the callers already expect.
 */
static bool kt_soft_coverage(float alpha, float *out)
{
    if (isnan(alpha) || alpha <= 0.0f) {
        return false;
    }
    *out = alpha > 1.0f ? 1.0f : alpha;
    return true;
}

kt_status kt_soft_draw_queue(sr_canvas *dst, const kt_draw_queue *queue,
                             kt_soft_resolve_fn resolve, void *user,
                             size_t *out_drawn)
{
    size_t index;
    size_t count;
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
    /* A count past the bound storage would read items the queue does not own,
     * matching what kt_draw_queue_sort() refuses. */
    if (queue->count > queue->capacity) {
        return KT_ERR_STATE;
    }
    count = queue->count;

    for (index = 0u; index < count; ++index) {
        const kt_draw_item *item = &queue->items[index];
        kt_soft_sprite sprite;
        float alpha;
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
        if (!kt_soft_pixel(item->at.x, sprite.offset_x, &x) ||
            !kt_soft_pixel(item->at.y, sprite.offset_y, &y)) {
            continue;
        }
        if (!kt_soft_coverage(sprite.alpha, &alpha)) {
            continue;
        }
        if (sprite.tint_enabled) {
            sr_blit_tint(dst, sprite.canvas, x, y, sprite.tint_rgb, alpha);
        } else if (alpha >= 1.0f) {
            sr_blit(dst, sprite.canvas, x, y);
        } else {
            sr_blit_alpha(dst, sprite.canvas, x, y, alpha);
        }
        ++drawn;
    }

    if (out_drawn != NULL) {
        *out_drawn = drawn;
    }
    return KT_OK;
}

/*
 * Does an axis-aligned box overlap the canvas at all?
 *
 * This is the guard the rasterizer's own clipping cannot supply. Its clip
 * helpers are `(int)fmaxf(floorf(v), 0)` and `(int)fminf(ceilf(v), limit)`,
 * which are only in range when the far side of the box is on the canvas side
 * of the conversion: a box entirely past the right edge reaches the first
 * with a value above INT_MAX, and a box entirely past the left edge reaches
 * the second with a value below INT_MIN. Both are undefined conversions, and
 * in practice one produced a loop from INT_MIN that made a single overlay
 * call run without terminating.
 *
 * Rejecting a box that cannot produce a pixel removes exactly those inputs,
 * and costs nothing for geometry that is on screen. Computed in double, in
 * which every int32 endpoint and its halves are exact.
 */
static bool kt_soft_box_visible(const sr_canvas *dst, double min_x,
                                double min_y, double max_x, double max_y)
{
    return max_x > 0.0 && max_y > 0.0 && min_x < (double)dst->w &&
           min_y < (double)dst->h;
}

/* Geometry of the diamond inscribed in the tile box whose top-left is (x, y). */
static bool kt_soft_diamond_geometry(const sr_canvas *dst, int32_t x, int32_t y,
                                     int32_t tile_width, int32_t tile_height,
                                     double pad, double *out_cx, double *out_cy,
                                     double *out_half_w, double *out_half_h)
{
    double half_w;
    double half_h;
    double cx;
    double cy;

    if (dst == NULL || dst->px == NULL || dst->w <= 0 || dst->h <= 0) {
        return false;
    }
    if (tile_width <= 0 || tile_height <= 0) {
        return false;
    }
    half_w = (double)tile_width / 2.0;
    half_h = (double)tile_height / 2.0;
    cx = (double)x + half_w;
    cy = (double)y + half_h;
    if (!kt_soft_box_visible(dst, cx - half_w - pad, cy - half_h - pad,
                             cx + half_w + pad, cy + half_h + pad)) {
        return false;
    }
    *out_cx = cx;
    *out_cy = cy;
    *out_half_w = half_w;
    *out_half_h = half_h;
    return true;
}

/* One diamond edge, skipped when that edge alone cannot reach the canvas. */
static void kt_soft_edge(sr_canvas *dst, double x0, double y0, double x1,
                         double y1, double pad, float width, uint32_t rgb,
                         float alpha, int dash_on, int dash_off)
{
    double min_x = x0 < x1 ? x0 : x1;
    double max_x = x0 < x1 ? x1 : x0;
    double min_y = y0 < y1 ? y0 : y1;
    double max_y = y0 < y1 ? y1 : y0;

    if (!kt_soft_box_visible(dst, min_x - pad, min_y - pad, max_x + pad,
                             max_y + pad)) {
        return;
    }
    sr_line(dst, (float)x0, (float)y0, (float)x1, (float)y1, width, rgb, alpha,
            dash_on, dash_off);
}

void kt_soft_diamond_outline(sr_canvas *dst, int32_t x, int32_t y,
                             int32_t tile_width, int32_t tile_height,
                             float width, uint32_t rgb, float alpha,
                             int dash_on, int dash_off)
{
    double half_w;
    double half_h;
    double cx;
    double cy;
    double pad;
    float coverage;

    if (isnan(width) || dash_on < 0 || dash_off < 0) {
        return;
    }
    if (!kt_soft_coverage(alpha, &coverage)) {
        return;
    }
    /* The rasterizer widens a stroke to at least half a pixel and pads its
     * scan box by one; matching that keeps this rejection conservative. */
    pad = (width > 1.0f ? (double)width : 1.0) + 1.0;
    if (!kt_soft_diamond_geometry(dst, x, y, tile_width, tile_height, pad, &cx,
                                  &cy, &half_w, &half_h)) {
        return;
    }

    kt_soft_edge(dst, cx - half_w, cy, cx, cy - half_h, pad, width, rgb,
                 coverage, dash_on, dash_off);
    kt_soft_edge(dst, cx, cy - half_h, cx + half_w, cy, pad, width, rgb,
                 coverage, dash_on, dash_off);
    kt_soft_edge(dst, cx + half_w, cy, cx, cy + half_h, pad, width, rgb,
                 coverage, dash_on, dash_off);
    kt_soft_edge(dst, cx, cy + half_h, cx - half_w, cy, pad, width, rgb,
                 coverage, dash_on, dash_off);
}

void kt_soft_diamond_fill(sr_canvas *dst, int32_t x, int32_t y,
                          int32_t tile_width, int32_t tile_height,
                          uint32_t rgb, float alpha)
{
    float xs[4];
    float ys[4];
    double half_w;
    double half_h;
    double cx;
    double cy;
    float coverage;

    if (!kt_soft_coverage(alpha, &coverage)) {
        return;
    }
    if (!kt_soft_diamond_geometry(dst, x, y, tile_width, tile_height, 1.0, &cx,
                                  &cy, &half_w, &half_h)) {
        return;
    }

    xs[0] = (float)(cx - half_w);
    ys[0] = (float)cy;
    xs[1] = (float)cx;
    ys[1] = (float)(cy - half_h);
    xs[2] = (float)(cx + half_w);
    ys[2] = (float)cy;
    xs[3] = (float)cx;
    ys[3] = (float)(cy + half_h);
    sr_fill_convex(dst, xs, ys, 4, rgb, coverage);
}
