#include "kilix_top_down_soft.h"
#include "kilix_top_down_view.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

static bool renderer_is_ready(const ki_td_soft_renderer *renderer)
{
    return renderer && renderer->canvas.px && renderer->rgba &&
           renderer->width > 0 && renderer->height > 0;
}

ki_td_rgba8 ki_td_rgba8_make(const void *pixels, int width, int height)
{
    ki_td_rgba8 image = {0};
    if (!pixels || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / 4u)
        return image;
    image.pixels = pixels;
    image.width = width;
    image.height = height;
    image.stride = (size_t)width * 4u;
    return image;
}

bool ki_td_rgba8_is_valid(const ki_td_rgba8 *image)
{
    if (!image || !image->pixels || image->width <= 0 || image->height <= 0 ||
        (size_t)image->width > SIZE_MAX / 4u)
        return false;
    return image->stride >= (size_t)image->width * 4u &&
           (size_t)image->height <= SIZE_MAX / image->stride;
}

ki_td_rgba8 ki_td_rgba8_subimage(const ki_td_rgba8 *image, int x, int y,
                                  int width, int height)
{
    ki_td_rgba8 result = {0};
    if (!ki_td_rgba8_is_valid(image) || x < 0 || y < 0 || width <= 0 ||
        height <= 0 || x >= image->width || y >= image->height ||
        width > image->width - x || height > image->height - y)
        return result;
    result.pixels = image->pixels + (size_t)y * image->stride +
                    (size_t)x * 4u;
    result.width = width;
    result.height = height;
    result.stride = image->stride;
    return result;
}

bool ki_td_nine_slice_init(ki_td_nine_slice *slice,
                           const ki_td_rgba8 *image, int left, int top,
                           int right, int bottom)
{
    if (!slice || !ki_td_rgba8_is_valid(image) || left <= 0 || top <= 0 ||
        right <= 0 || bottom <= 0 || left >= image->width - right ||
        top >= image->height - bottom)
        return false;
    slice->image = *image;
    slice->left = left;
    slice->top = top;
    slice->right = right;
    slice->bottom = bottom;
    return true;
}

bool ki_td_tile_batch_is_valid(const ki_td_tile_batch *batch)
{
    uint64_t cell_count;
    if (!batch || !ki_td_rgba8_is_valid(batch->atlas) || !batch->cells ||
        batch->columns == 0u || batch->rows == 0u ||
        batch->atlas_columns == 0u || batch->atlas_rows == 0u ||
        batch->tile_width <= 0 || batch->tile_height <= 0 ||
        !isfinite(batch->x) || !isfinite(batch->y) ||
        !isfinite(batch->alpha) || batch->alpha <= 0.0f ||
        batch->atlas_columns > (uint32_t)INT_MAX ||
        batch->atlas_rows > (uint32_t)INT_MAX ||
        batch->atlas->width % (int)batch->atlas_columns != 0 ||
        batch->atlas->height % (int)batch->atlas_rows != 0)
        return false;
    cell_count = (uint64_t)batch->columns * (uint64_t)batch->rows;
    return cell_count <= SIZE_MAX && batch->cell_count >= (size_t)cell_count;
}

static int sprite_compare(const ki_td_sprite_command *commands,
                          size_t first, size_t second)
{
    const ki_td_sprite_command *a = &commands[first];
    const ki_td_sprite_command *b = &commands[second];
    float a_y = isfinite(a->sort_y) ? a->sort_y : 0.0f;
    float b_y = isfinite(b->sort_y) ? b->sort_y : 0.0f;
    if (a->layer != b->layer) return a->layer < b->layer ? -1 : 1;
    if (a_y != b_y) return a_y < b_y ? -1 : 1;
    if (a->order != b->order) return a->order < b->order ? -1 : 1;
    return first < second ? -1 : first > second ? 1 : 0;
}

size_t ki_td_sprite_order(const ki_td_sprite_command *commands, size_t count,
                          size_t *scratch, size_t scratch_count)
{
    size_t index;
    if (count == 0u) return 0u;
    if (!commands || !scratch || scratch_count < count) return 0u;
    for (index = 0u; index < count; ++index) {
        size_t cursor = index;
        scratch[index] = index;
        while (cursor > 0u &&
               sprite_compare(commands, scratch[cursor],
                              scratch[cursor - 1u]) < 0) {
            size_t swap = scratch[cursor];
            scratch[cursor] = scratch[cursor - 1u];
            scratch[cursor - 1u] = swap;
            --cursor;
        }
    }
    return count;
}

bool ki_td_soft_renderer_resize(ki_td_soft_renderer *renderer, int width,
                                int height)
{
    if (!renderer || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height / 4u)
        return false;
    if (renderer_is_ready(renderer) && renderer->width == width &&
        renderer->height == height)
        return true;

    sr_canvas next_canvas = {0};
    if (!sr_canvas_init(&next_canvas, width, height))
        return false;
    size_t next_size = (size_t)width * (size_t)height * 4u;
    uint8_t *next_rgba = realloc(renderer->rgba, next_size);
    if (!next_rgba) {
        sr_canvas_free(&next_canvas);
        return false;
    }

    sr_canvas_free(&renderer->canvas);
    renderer->canvas = next_canvas;
    renderer->rgba = next_rgba;
    renderer->rgba_size = next_size;
    renderer->width = width;
    renderer->height = height;
    return true;
}

bool ki_td_soft_renderer_init(ki_td_soft_renderer *renderer, int width,
                              int height)
{
    if (!renderer) return false;
    ki_td_soft_renderer_destroy(renderer);
    return ki_td_soft_renderer_resize(renderer, width, height);
}

void ki_td_soft_renderer_destroy(ki_td_soft_renderer *renderer)
{
    if (!renderer) return;
    free(renderer->rgba);
    renderer->rgba = NULL;
    renderer->rgba_size = 0;
    sr_canvas_free(&renderer->canvas);
    renderer->width = 0;
    renderer->height = 0;
}

sr_canvas *ki_td_soft_canvas(ki_td_soft_renderer *renderer)
{
    return renderer ? &renderer->canvas : NULL;
}

const sr_canvas *ki_td_soft_canvas_const(const ki_td_soft_renderer *renderer)
{
    return renderer ? &renderer->canvas : NULL;
}

int ki_td_soft_width(const ki_td_soft_renderer *renderer)
{
    return renderer ? renderer->width : 0;
}

int ki_td_soft_height(const ki_td_soft_renderer *renderer)
{
    return renderer ? renderer->height : 0;
}

uint8_t *ki_td_soft_pack_rgba(ki_td_soft_renderer *renderer)
{
    if (!renderer_is_ready(renderer) ||
        !sr_pack_rgba(&renderer->canvas, renderer->rgba, renderer->rgba_size))
        return NULL;
    return renderer->rgba;
}

void ki_td_soft_clear(ki_td_soft_renderer *renderer, uint32_t rgb)
{
    if (renderer) sr_clear(&renderer->canvas, rgb);
}

void ki_td_soft_blend_pixel(ki_td_soft_renderer *renderer, int x, int y,
                            uint32_t rgb, float alpha)
{
    if (renderer) sr_blend(&renderer->canvas, x, y, rgb, alpha);
}

void ki_td_soft_fill_rect_px(ki_td_soft_renderer *renderer, float x, float y,
                             float width, float height, uint32_t rgb,
                             float alpha)
{
    if (renderer)
        sr_fill_rect(&renderer->canvas, x, y, width, height, rgb, alpha);
}

void ki_td_soft_fill_circle_px(ki_td_soft_renderer *renderer, float x, float y,
                               float radius, uint32_t rgb, float alpha)
{
    if (renderer)
        sr_fill_circle(&renderer->canvas, x, y, radius, rgb, alpha);
}

void ki_td_soft_fill_ellipse_px(ki_td_soft_renderer *renderer, float x,
                                float y, float radius_x, float radius_y,
                                uint32_t rgb, float alpha)
{
    if (renderer)
        sr_fill_ellipse(&renderer->canvas, x, y, radius_x, radius_y, rgb,
                        alpha);
}

void ki_td_soft_line_px(ki_td_soft_renderer *renderer, float x0, float y0,
                        float x1, float y1, float width, uint32_t rgb,
                        float alpha)
{
    if (renderer)
        sr_line(&renderer->canvas, x0, y0, x1, y1, width, rgb, alpha, 0, 0);
}

void ki_td_soft_fill_rect(ki_td_soft_renderer *renderer,
                          const ki_td_view *view, float x, float y,
                          float width, float height, uint32_t rgb,
                          float alpha)
{
    if (!view) return;
    ki_td_soft_fill_rect_px(renderer, (float)ki_td_screen_x(view, x),
                            (float)ki_td_screen_y(view, y),
                            ki_td_screen_scale(view, width),
                            ki_td_screen_scale(view, height), rgb, alpha);
}

void ki_td_soft_fill_circle(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, float x, float y,
                            float radius, uint32_t rgb, float alpha)
{
    if (!view) return;
    ki_td_soft_fill_circle_px(renderer, (float)ki_td_screen_x(view, x),
                              (float)ki_td_screen_y(view, y),
                              ki_td_screen_scale(view, radius), rgb, alpha);
}

void ki_td_soft_fill_ellipse(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             float radius_x, float radius_y, uint32_t rgb,
                             float alpha)
{
    if (!view) return;
    ki_td_soft_fill_ellipse_px(renderer, (float)ki_td_screen_x(view, x),
                               (float)ki_td_screen_y(view, y),
                               ki_td_screen_scale(view, radius_x),
                               ki_td_screen_scale(view, radius_y), rgb,
                               alpha);
}

void ki_td_soft_line(ki_td_soft_renderer *renderer, const ki_td_view *view,
                     float x0, float y0, float x1, float y1, float width,
                     uint32_t rgb, float alpha)
{
    if (!view) return;
    ki_td_soft_line_px(renderer, (float)ki_td_screen_x(view, x0),
                       (float)ki_td_screen_y(view, y0),
                       (float)ki_td_screen_x(view, x1),
                       (float)ki_td_screen_y(view, y1),
                       ki_td_screen_scale(view, width), rgb, alpha);
}

static const uint8_t *image_pixel(const ki_td_rgba8 *image, int x, int y)
{
    return image->pixels + (size_t)y * image->stride + (size_t)x * 4u;
}

static void blend_rgba_pixel(ki_td_soft_renderer *renderer, int x, int y,
                             const uint8_t *pixel, float alpha)
{
    if (pixel[3] < 8u) return;
    uint32_t rgb = ((uint32_t)pixel[0] << 16) |
                   ((uint32_t)pixel[1] << 8) | (uint32_t)pixel[2];
    ki_td_soft_blend_pixel(renderer, x, y, rgb,
                           alpha * (pixel[3] / 255.0f));
}

static void fill_rgba_world_pixel(ki_td_soft_renderer *renderer,
                                  const ki_td_view *view, float x, float y,
                                  const uint8_t *pixel, float alpha)
{
    if (pixel[3] < 8u) return;
    uint32_t rgb = ((uint32_t)pixel[0] << 16) |
                   ((uint32_t)pixel[1] << 8) | (uint32_t)pixel[2];
    ki_td_soft_fill_rect(renderer, view, x, y, 1.0f, 1.0f, rgb,
                         alpha * (pixel[3] / 255.0f));
}

void ki_td_soft_rgba_px(ki_td_soft_renderer *renderer, int x, int y,
                        const ki_td_rgba8 *image, float alpha)
{
    if (!renderer || !ki_td_rgba8_is_valid(image)) return;
    for (int yy = 0; yy < image->height; yy++)
        for (int xx = 0; xx < image->width; xx++)
            blend_rgba_pixel(renderer, x + xx, y + yy,
                             image_pixel(image, xx, yy), alpha);
}

void ki_td_soft_rgba(ki_td_soft_renderer *renderer, const ki_td_view *view,
                     float x, float y, const ki_td_rgba8 *image, float alpha)
{
    if (!renderer || !view || !ki_td_rgba8_is_valid(image)) return;
    for (int yy = 0; yy < image->height; yy++)
        for (int xx = 0; xx < image->width; xx++)
            fill_rgba_world_pixel(renderer, view, x + (float)xx,
                                  y + (float)yy,
                                  image_pixel(image, xx, yy), alpha);
}

void ki_td_soft_rgba_resized(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             const ki_td_rgba8 *image, int width, int height,
                             float alpha)
{
    if (!renderer || !view || !ki_td_rgba8_is_valid(image) || width <= 0 ||
        height <= 0)
        return;
    for (int yy = 0; yy < height; yy++) {
        int source_y = yy * image->height / height;
        for (int xx = 0; xx < width; xx++) {
            int source_x = xx * image->width / width;
            fill_rgba_world_pixel(renderer, view, x + (float)xx,
                                  y + (float)yy,
                                  image_pixel(image, source_x, source_y),
                                  alpha);
        }
    }
}

void ki_td_soft_rgba_rotated(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             const ki_td_rgba8 *image, int quarter_turns,
                             float alpha)
{
    if (!renderer || !view || !ki_td_rgba8_is_valid(image)) return;
    int turns = quarter_turns % 4;
    if (turns < 0) turns += 4;
    if (turns == 0) {
        ki_td_soft_rgba(renderer, view, x, y, image, alpha);
        return;
    }

    for (int source_y = 0; source_y < image->height; source_y++) {
        for (int source_x = 0; source_x < image->width; source_x++) {
            int dest_x = source_x;
            int dest_y = source_y;
            if (turns == 1) {
                dest_x = image->height - 1 - source_y;
                dest_y = source_x;
            } else if (turns == 2) {
                dest_x = image->width - 1 - source_x;
                dest_y = image->height - 1 - source_y;
            } else {
                dest_x = source_y;
                dest_y = image->width - 1 - source_x;
            }
            fill_rgba_world_pixel(renderer, view, x + (float)dest_x,
                                  y + (float)dest_y,
                                  image_pixel(image, source_x, source_y),
                                  alpha);
        }
    }
}

void ki_td_soft_rgba_pixel_art(ki_td_soft_renderer *renderer,
                               const ki_td_view *view, float x, float y,
                               const ki_td_rgba8 *image, float alpha)
{
    if (!renderer || !view || !ki_td_rgba8_is_valid(image)) return;
    int scale = (int)(view->scale + 0.5f);
    if (scale < 1 || fabsf(view->scale - (float)scale) > 0.01f) {
        ki_td_soft_rgba(renderer, view, x, y, image, alpha);
        return;
    }

    int origin_x = ki_td_screen_x(view, x);
    int origin_y = ki_td_screen_y(view, y);
    for (int yy = 0; yy < image->height; yy++) {
        for (int xx = 0; xx < image->width; xx++) {
            const uint8_t *pixel = image_pixel(image, xx, yy);
            if (pixel[3] < 8u) continue;
            uint32_t rgb = ((uint32_t)pixel[0] << 16) |
                           ((uint32_t)pixel[1] << 8) |
                           (uint32_t)pixel[2];
            int pixel_x = origin_x + xx * scale;
            int pixel_y = origin_y + yy * scale;
            if (pixel[3] == 255u) {
                ki_td_soft_fill_rect_px(renderer, (float)pixel_x,
                                        (float)pixel_y, (float)scale,
                                        (float)scale, rgb, alpha);
                continue;
            }
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++)
                    ki_td_soft_blend_pixel(
                        renderer, pixel_x + dx, pixel_y + dy, rgb,
                        alpha * (pixel[3] / 255.0f));
        }
    }
}

static void draw_subimage(ki_td_soft_renderer *renderer,
                          const ki_td_view *view, float x, float y,
                          int width, int height, const ki_td_rgba8 *source,
                          int source_x, int source_y, int source_width,
                          int source_height, float alpha)
{
    ki_td_rgba8 region = ki_td_rgba8_subimage(source, source_x, source_y,
                                               source_width, source_height);
    if (ki_td_rgba8_is_valid(&region))
        ki_td_soft_rgba_resized(renderer, view, x, y, &region, width, height,
                                alpha);
}

void ki_td_soft_nine_slice(ki_td_soft_renderer *renderer,
                           const ki_td_view *view, float x, float y,
                           int width, int height,
                           const ki_td_nine_slice *slice, float alpha)
{
    int source_center_width;
    int source_center_height;
    int center_width;
    int center_height;
    int source_right;
    int source_bottom;
    int destination_right;
    int destination_bottom;
    if (!renderer || !view || !slice || !isfinite(x) || !isfinite(y) ||
        !isfinite(alpha) || alpha <= 0.0f ||
        !ki_td_rgba8_is_valid(&slice->image) || slice->left <= 0 ||
        slice->top <= 0 || slice->right <= 0 || slice->bottom <= 0 ||
        slice->right >= slice->image.width ||
        slice->left >= slice->image.width - slice->right ||
        slice->bottom >= slice->image.height ||
        slice->top >= slice->image.height - slice->bottom ||
        width < slice->left || width - slice->left < slice->right ||
        height < slice->top || height - slice->top < slice->bottom)
        return;
    source_center_width = slice->image.width - slice->left - slice->right;
    source_center_height = slice->image.height - slice->top - slice->bottom;
    if (source_center_width <= 0 || source_center_height <= 0) return;
    center_width = width - slice->left - slice->right;
    center_height = height - slice->top - slice->bottom;
    source_right = slice->image.width - slice->right;
    source_bottom = slice->image.height - slice->bottom;
    destination_right = width - slice->right;
    destination_bottom = height - slice->bottom;

    draw_subimage(renderer, view, x, y, slice->left, slice->top,
                  &slice->image, 0, 0, slice->left, slice->top, alpha);
    if (center_width > 0)
        draw_subimage(renderer, view, x + (float)slice->left, y,
                      center_width, slice->top, &slice->image, slice->left,
                      0, source_center_width, slice->top, alpha);
    draw_subimage(renderer, view, x + (float)destination_right, y,
                  slice->right, slice->top, &slice->image, source_right, 0,
                  slice->right, slice->top, alpha);
    if (center_height > 0)
        draw_subimage(renderer, view, x, y + (float)slice->top,
                      slice->left, center_height, &slice->image, 0, slice->top,
                      slice->left, source_center_height, alpha);
    if (center_width > 0 && center_height > 0)
        draw_subimage(renderer, view, x + (float)slice->left,
                      y + (float)slice->top, center_width, center_height,
                      &slice->image, slice->left, slice->top,
                      source_center_width, source_center_height, alpha);
    if (center_height > 0)
        draw_subimage(renderer, view, x + (float)destination_right,
                      y + (float)slice->top, slice->right, center_height,
                      &slice->image, source_right, slice->top, slice->right,
                      source_center_height, alpha);
    draw_subimage(renderer, view, x, y + (float)destination_bottom,
                  slice->left, slice->bottom, &slice->image, 0, source_bottom,
                  slice->left, slice->bottom, alpha);
    if (center_width > 0)
        draw_subimage(renderer, view, x + (float)slice->left,
                      y + (float)destination_bottom, center_width,
                      slice->bottom, &slice->image, slice->left,
                      source_bottom, source_center_width, slice->bottom,
                      alpha);
    draw_subimage(renderer, view, x + (float)destination_right,
                  y + (float)destination_bottom, slice->right, slice->bottom,
                  &slice->image, source_right, source_bottom, slice->right,
                  slice->bottom, alpha);
}

void ki_td_soft_tile_batch(ki_td_soft_renderer *renderer,
                           const ki_td_view *view,
                           const ki_td_tile_batch *batch)
{
    uint32_t atlas_cell_width;
    uint32_t atlas_cell_height;
    uint64_t atlas_count;
    uint32_t row;
    if (!renderer || !view || !ki_td_tile_batch_is_valid(batch)) return;
    atlas_cell_width = (uint32_t)batch->atlas->width / batch->atlas_columns;
    atlas_cell_height = (uint32_t)batch->atlas->height / batch->atlas_rows;
    atlas_count = (uint64_t)batch->atlas_columns * batch->atlas_rows;
    for (row = 0u; row < batch->rows; ++row) {
        uint32_t column;
        for (column = 0u; column < batch->columns; ++column) {
            size_t index = (size_t)row * batch->columns + column;
            uint32_t cell = batch->cells[index];
            uint32_t source_column;
            uint32_t source_row;
            if (cell == batch->empty_cell || (uint64_t)cell >= atlas_count)
                continue;
            source_column = cell % batch->atlas_columns;
            source_row = cell / batch->atlas_columns;
            draw_subimage(renderer, view,
                batch->x + (float)column * (float)batch->tile_width,
                batch->y + (float)row * (float)batch->tile_height,
                batch->tile_width, batch->tile_height, batch->atlas,
                (int)(source_column * atlas_cell_width),
                (int)(source_row * atlas_cell_height),
                (int)atlas_cell_width, (int)atlas_cell_height, batch->alpha);
        }
    }
}

void ki_td_soft_sprite_layers(ki_td_soft_renderer *renderer,
                              const ki_td_view *view,
                              const ki_td_sprite_command *commands,
                              size_t count, size_t *scratch,
                              size_t scratch_count)
{
    size_t ordered;
    size_t cursor;
    if (!renderer || !view) return;
    ordered = ki_td_sprite_order(commands, count, scratch, scratch_count);
    if (ordered != count) return;
    for (cursor = 0u; cursor < ordered; ++cursor) {
        const ki_td_sprite_command *command = &commands[scratch[cursor]];
        if (!ki_td_rgba8_is_valid(command->image) ||
            !isfinite(command->x) || !isfinite(command->y) ||
            !isfinite(command->alpha) || command->alpha <= 0.0f)
            continue;
        if (command->width > 0 && command->height > 0)
            ki_td_soft_rgba_resized(renderer, view, command->x, command->y,
                                    command->image, command->width,
                                    command->height, command->alpha);
        else if (command->width == 0 && command->height == 0)
            ki_td_soft_rgba_pixel_art(renderer, view, command->x, command->y,
                                      command->image, command->alpha);
    }
}
