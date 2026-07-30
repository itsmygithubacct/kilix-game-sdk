#ifndef KILIX_TOP_DOWN_SOFT_H
#define KILIX_TOP_DOWN_SOFT_H

#include "kilix_top_down_types.h"

#include "soft_raster.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ki_td_soft_renderer {
    sr_canvas canvas;
    uint8_t *rgba;
    size_t rgba_size;
    int width;
    int height;
} ki_td_soft_renderer;

/* Straight-alpha RGBA8 image. stride is measured in bytes and may exceed
 * width*4. Pixels with alpha below 8 are treated as transparent to match the
 * original Kilix top-down renderer. */
typedef struct ki_td_rgba8 {
    const uint8_t *pixels;
    int width;
    int height;
    size_t stride;
} ki_td_rgba8;

ki_td_rgba8 ki_td_rgba8_make(const void *pixels, int width, int height);
bool ki_td_rgba8_is_valid(const ki_td_rgba8 *image);
ki_td_rgba8 ki_td_rgba8_subimage(const ki_td_rgba8 *image, int x, int y,
                                  int width, int height);

typedef struct ki_td_nine_slice {
    ki_td_rgba8 image;
    int left;
    int top;
    int right;
    int bottom;
} ki_td_nine_slice;

bool ki_td_nine_slice_init(ki_td_nine_slice *slice,
                           const ki_td_rgba8 *image, int left, int top,
                           int right, int bottom);

/* Row-major atlas tile map. UINT32_MAX is a conventional empty_cell value,
 * but callers may choose another sentinel. */
typedef struct ki_td_tile_batch {
    const ki_td_rgba8 *atlas;
    const uint32_t *cells;
    size_t cell_count;
    uint32_t columns;
    uint32_t rows;
    uint32_t atlas_columns;
    uint32_t atlas_rows;
    uint32_t empty_cell;
    float x;
    float y;
    int tile_width;
    int tile_height;
    float alpha;
} ki_td_tile_batch;

bool ki_td_tile_batch_is_valid(const ki_td_tile_batch *batch);

/* Games assign semantic layers and sort_y. order is a deterministic tie
 * breaker; original array position breaks any remaining ties. */
typedef struct ki_td_sprite_command {
    const ki_td_rgba8 *image;
    float x;
    float y;
    int width;
    int height;
    float alpha;
    int layer;
    float sort_y;
    uint32_t order;
} ki_td_sprite_command;

/* Writes stable draw indices into caller-owned scratch and performs no
 * allocation. Returns zero when count exceeds scratch_count. */
size_t ki_td_sprite_order(const ki_td_sprite_command *commands, size_t count,
                          size_t *scratch, size_t scratch_count);

/* A renderer must be zero-initialized before its first init/resize/destroy.
 * Resize is transactional: failure preserves the active canvas and RGBA
 * buffer. Drawing performs no allocation. */
bool ki_td_soft_renderer_init(ki_td_soft_renderer *renderer, int width,
                              int height);
bool ki_td_soft_renderer_resize(ki_td_soft_renderer *renderer, int width,
                                int height);
void ki_td_soft_renderer_destroy(ki_td_soft_renderer *renderer);
sr_canvas *ki_td_soft_canvas(ki_td_soft_renderer *renderer);
const sr_canvas *ki_td_soft_canvas_const(const ki_td_soft_renderer *renderer);
int ki_td_soft_width(const ki_td_soft_renderer *renderer);
int ki_td_soft_height(const ki_td_soft_renderer *renderer);
uint8_t *ki_td_soft_pack_rgba(ki_td_soft_renderer *renderer);

void ki_td_soft_clear(ki_td_soft_renderer *renderer, uint32_t rgb);
void ki_td_soft_blend_pixel(ki_td_soft_renderer *renderer, int x, int y,
                            uint32_t rgb, float alpha);
void ki_td_soft_fill_rect_px(ki_td_soft_renderer *renderer, float x, float y,
                             float width, float height, uint32_t rgb,
                             float alpha);
void ki_td_soft_fill_circle_px(ki_td_soft_renderer *renderer, float x, float y,
                               float radius, uint32_t rgb, float alpha);
void ki_td_soft_fill_ellipse_px(ki_td_soft_renderer *renderer, float x,
                                float y, float radius_x, float radius_y,
                                uint32_t rgb, float alpha);
void ki_td_soft_line_px(ki_td_soft_renderer *renderer, float x0, float y0,
                        float x1, float y1, float width, uint32_t rgb,
                        float alpha);

void ki_td_soft_fill_rect(ki_td_soft_renderer *renderer,
                          const ki_td_view *view, float x, float y,
                          float width, float height, uint32_t rgb,
                          float alpha);
void ki_td_soft_fill_circle(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, float x, float y,
                            float radius, uint32_t rgb, float alpha);
void ki_td_soft_fill_ellipse(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             float radius_x, float radius_y, uint32_t rgb,
                             float alpha);
void ki_td_soft_line(ki_td_soft_renderer *renderer, const ki_td_view *view,
                     float x0, float y0, float x1, float y1, float width,
                     uint32_t rgb, float alpha);

void ki_td_soft_rgba_px(ki_td_soft_renderer *renderer, int x, int y,
                        const ki_td_rgba8 *image, float alpha);
void ki_td_soft_rgba(ki_td_soft_renderer *renderer, const ki_td_view *view,
                     float x, float y, const ki_td_rgba8 *image, float alpha);
void ki_td_soft_rgba_resized(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             const ki_td_rgba8 *image, int width, int height,
                             float alpha);
void ki_td_soft_rgba_rotated(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             const ki_td_rgba8 *image, int quarter_turns,
                             float alpha);

/* Fast path for logical pixel art at a near-integer view scale. It falls back
 * to ki_td_soft_rgba when the scale differs from the nearest integer by more
 * than 0.01. */
void ki_td_soft_rgba_pixel_art(ki_td_soft_renderer *renderer,
                               const ki_td_view *view, float x, float y,
                               const ki_td_rgba8 *image, float alpha);

void ki_td_soft_nine_slice(ki_td_soft_renderer *renderer,
                           const ki_td_view *view, float x, float y,
                           int width, int height,
                           const ki_td_nine_slice *slice, float alpha);
void ki_td_soft_tile_batch(ki_td_soft_renderer *renderer,
                           const ki_td_view *view,
                           const ki_td_tile_batch *batch);
void ki_td_soft_sprite_layers(ki_td_soft_renderer *renderer,
                              const ki_td_view *view,
                              const ki_td_sprite_command *commands,
                              size_t count, size_t *scratch,
                              size_t scratch_count);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TOP_DOWN_SOFT_H */
