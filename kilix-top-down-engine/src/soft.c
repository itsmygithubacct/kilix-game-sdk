#include "kilix_top_down_soft.h"
#include "kilix_top_down_view.h"
#include "internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

static bool renderer_is_ready(const ki_td_soft_renderer *renderer)
{
    size_t pixel_count;
    const sr_canvas *canvas;
    if (!renderer || !renderer->canvas.px || !renderer->rgba ||
        renderer->width <= 0 || renderer->height <= 0 ||
        (size_t)renderer->width >
            SIZE_MAX / (size_t)renderer->height)
        return false;
    canvas = &renderer->canvas;
    pixel_count = (size_t)renderer->width * (size_t)renderer->height;
    return pixel_count <= SIZE_MAX / 4u &&
           renderer->rgba_size >= pixel_count * 4u &&
           canvas->w == renderer->width && canvas->h == renderer->height &&
           canvas->clip_x0 >= 0 && canvas->clip_x0 <= canvas->clip_x1 &&
           canvas->clip_x1 <= canvas->w && canvas->clip_y0 >= 0 &&
           canvas->clip_y0 <= canvas->clip_y1 &&
           canvas->clip_y1 <= canvas->h;
}

static bool normalized_alpha(float alpha, float *result)
{
    if (!result || !isfinite(alpha) || alpha <= 0.0f) return false;
    *result = alpha > 1.0f ? 1.0f : alpha;
    return true;
}

static bool int_edge(double value)
{
    return isfinite(value) && value >= (double)INT_MIN &&
           value <= (double)INT_MAX;
}

static bool rect_geometry(float x, float y, float width, float height)
{
    return isfinite(x) && isfinite(y) && isfinite(width) &&
           isfinite(height) && width > 0.0f && height > 0.0f &&
           int_edge((double)x) && int_edge((double)y) &&
           int_edge((double)x + width) && int_edge((double)y + height);
}

static bool ellipse_geometry(float x, float y, float radius_x,
                             float radius_y)
{
    return isfinite(x) && isfinite(y) && isfinite(radius_x) &&
           isfinite(radius_y) && radius_x > 0.0f && radius_y > 0.0f &&
           int_edge((double)x - radius_x) &&
           int_edge((double)x + radius_x) &&
           int_edge((double)y - radius_y) &&
           int_edge((double)y + radius_y);
}

static bool line_geometry(float x0, float y0, float x1, float y1,
                          float width)
{
    double radius;
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1) ||
        !isfinite(width) || width <= 0.0f)
        return false;
    radius = (double)width * 0.5 + 1.0;
    return int_edge((double)x0 - radius) &&
           int_edge((double)x0 + radius) &&
           int_edge((double)y0 - radius) &&
           int_edge((double)y0 + radius) &&
           int_edge((double)x1 - radius) &&
           int_edge((double)x1 + radius) &&
           int_edge((double)y1 - radius) &&
           int_edge((double)y1 + radius);
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
        batch->columns > (uint32_t)INT_MAX ||
        batch->rows > (uint32_t)INT_MAX ||
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

static bool byte_ranges_overlap(const void *first, size_t first_size,
                                const void *second, size_t second_size)
{
    uintptr_t first_start = (uintptr_t)first;
    uintptr_t second_start = (uintptr_t)second;
    uintptr_t first_end;
    uintptr_t second_end;
    if (first_size == 0u || second_size == 0u) return false;
    if (first_size > UINTPTR_MAX - first_start ||
        second_size > UINTPTR_MAX - second_start)
        return true;
    first_end = first_start + first_size;
    second_end = second_start + second_size;
    return first_start < second_end && second_start < first_end;
}

static void sprite_sift_down(const ki_td_sprite_command *commands,
                             size_t *scratch, size_t root, size_t count)
{
    for (;;) {
        size_t child;
        size_t greater;
        if (root >= count / 2u) return;
        child = root * 2u + 1u;
        greater = child;
        if (child + 1u < count &&
            sprite_compare(commands, scratch[child],
                           scratch[child + 1u]) < 0)
            greater = child + 1u;
        if (sprite_compare(commands, scratch[root], scratch[greater]) >= 0)
            return;
        {
            size_t swap = scratch[root];
            scratch[root] = scratch[greater];
            scratch[greater] = swap;
        }
        root = greater;
    }
}

size_t ki_td_sprite_order(const ki_td_sprite_command *commands, size_t count,
                          size_t *scratch, size_t scratch_count)
{
    size_t index;
    bool ordered = true;
    if (count == 0u) return 0u;
    if (!commands || !scratch || scratch_count < count ||
        count > SIZE_MAX / sizeof commands[0] ||
        count > SIZE_MAX / sizeof scratch[0] ||
        byte_ranges_overlap(commands, count * sizeof commands[0],
                            scratch, count * sizeof scratch[0]))
        return 0u;
    for (index = 0u; index < count; ++index) {
        scratch[index] = index;
        if (index != 0u &&
            sprite_compare(commands, index - 1u, index) > 0)
            ordered = false;
    }
    if (ordered) return count;
    if (count <= 32u) {
        for (index = 1u; index < count; ++index) {
            size_t cursor = index;
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
    for (index = count / 2u; index > 0u; --index)
        sprite_sift_down(commands, scratch, index - 1u, count);
    for (index = count - 1u; index > 0u; --index) {
        size_t swap = scratch[0];
        scratch[0] = scratch[index];
        scratch[index] = swap;
        sprite_sift_down(commands, scratch, 0u, index);
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
    return renderer_is_ready(renderer) ? renderer->width : 0;
}

int ki_td_soft_height(const ki_td_soft_renderer *renderer)
{
    return renderer_is_ready(renderer) ? renderer->height : 0;
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
    if (renderer_is_ready(renderer)) sr_clear(&renderer->canvas, rgb);
}

void ki_td_soft_blend_pixel(ki_td_soft_renderer *renderer, int x, int y,
                            uint32_t rgb, float alpha)
{
    float selected;
    if (renderer_is_ready(renderer) && normalized_alpha(alpha, &selected))
        sr_blend(&renderer->canvas, x, y, rgb, selected);
}

static bool fill_integer_rect(ki_td_soft_renderer *renderer,
                              float x, float y, float width, float height,
                              uint32_t rgb, float alpha)
{
    sr_canvas *canvas;
    float right_value;
    float bottom_value;
    int left;
    int top;
    int right;
    int bottom;
    int alpha256;
    int source_red;
    int source_green;
    int source_blue;
    int row;
    right_value = x + width;
    bottom_value = y + height;
    if (!isfinite(right_value) || !isfinite(bottom_value) ||
        (double)x < (double)INT_MIN || (double)x > (double)INT_MAX ||
        (double)y < (double)INT_MIN || (double)y > (double)INT_MAX ||
        (double)right_value < (double)INT_MIN ||
        (double)right_value > (double)INT_MAX ||
        (double)bottom_value < (double)INT_MIN ||
        (double)bottom_value > (double)INT_MAX ||
        floorf(x) != x || floorf(y) != y ||
        floorf(right_value) != right_value ||
        floorf(bottom_value) != bottom_value)
        return false;
    left = (int)x;
    top = (int)y;
    right = (int)right_value;
    bottom = (int)bottom_value;
    canvas = &renderer->canvas;
    if (left < canvas->clip_x0) left = canvas->clip_x0;
    if (top < canvas->clip_y0) top = canvas->clip_y0;
    if (right > canvas->clip_x1) right = canvas->clip_x1;
    if (bottom > canvas->clip_y1) bottom = canvas->clip_y1;
    if (right <= left || bottom <= top) return true;
    alpha256 = (int)(alpha * 256.0f + 0.5f);
    if (alpha256 <= 0) return true;
    if (alpha256 > 256) alpha256 = 256;
    rgb &= UINT32_C(0x00ffffff);
    if (alpha256 == 256) {
        uint32_t value = UINT32_C(0xff000000) | rgb;
        for (row = top; row < bottom; ++row) {
            uint32_t *destination =
                &canvas->px[(size_t)row * (size_t)canvas->w +
                            (size_t)left];
            int column;
            for (column = left; column < right; ++column)
                *destination++ = value;
        }
        return true;
    }
    source_red = (int)((rgb >> 16) & UINT32_C(255));
    source_green = (int)((rgb >> 8) & UINT32_C(255));
    source_blue = (int)(rgb & UINT32_C(255));
    for (row = top; row < bottom; ++row) {
        uint32_t *destination =
            &canvas->px[(size_t)row * (size_t)canvas->w +
                        (size_t)left];
        int column;
        for (column = left; column < right; ++column) {
            uint32_t current = *destination;
            int red = (int)((current >> 16) & UINT32_C(255));
            int green = (int)((current >> 8) & UINT32_C(255));
            int blue = (int)(current & UINT32_C(255));
            int output_alpha = (int)(current >> 24);
            red += ((source_red - red) * alpha256) >> 8;
            green += ((source_green - green) * alpha256) >> 8;
            blue += ((source_blue - blue) * alpha256) >> 8;
            output_alpha +=
                ((255 - output_alpha) * alpha256) >> 8;
            *destination++ =
                (uint32_t)output_alpha << 24 |
                (uint32_t)red << 16 |
                (uint32_t)green << 8 |
                (uint32_t)blue;
        }
    }
    return true;
}

static void fill_rect_ready(ki_td_soft_renderer *renderer, float x, float y,
                            float width, float height, uint32_t rgb,
                            float alpha)
{
    if (!rect_geometry(x, y, width, height)) return;
    if (!fill_integer_rect(renderer, x, y, width, height, rgb, alpha))
        sr_fill_rect(&renderer->canvas, x, y, width, height, rgb, alpha);
}

void ki_td_soft_fill_rect_px(ki_td_soft_renderer *renderer, float x, float y,
                             float width, float height, uint32_t rgb,
                             float alpha)
{
    float selected;
    if (!renderer_is_ready(renderer) || !normalized_alpha(alpha, &selected))
        return;
    fill_rect_ready(renderer, x, y, width, height, rgb, selected);
}

void ki_td_soft_fill_circle_px(ki_td_soft_renderer *renderer, float x, float y,
                               float radius, uint32_t rgb, float alpha)
{
    float selected;
    if (renderer_is_ready(renderer) &&
        ellipse_geometry(x, y, radius, radius) &&
        normalized_alpha(alpha, &selected))
        sr_fill_circle(&renderer->canvas, x, y, radius, rgb, selected);
}

void ki_td_soft_fill_ellipse_px(ki_td_soft_renderer *renderer, float x,
                                float y, float radius_x, float radius_y,
                                uint32_t rgb, float alpha)
{
    float selected;
    if (renderer_is_ready(renderer) &&
        ellipse_geometry(x, y, radius_x, radius_y) &&
        normalized_alpha(alpha, &selected))
        sr_fill_ellipse(&renderer->canvas, x, y, radius_x, radius_y, rgb,
                        selected);
}

void ki_td_soft_line_px(ki_td_soft_renderer *renderer, float x0, float y0,
                        float x1, float y1, float width, uint32_t rgb,
                        float alpha)
{
    float selected;
    if (renderer_is_ready(renderer) &&
        line_geometry(x0, y0, x1, y1, width) &&
        normalized_alpha(alpha, &selected))
        sr_line(&renderer->canvas, x0, y0, x1, y1, width, rgb, selected,
                0, 0);
}

void ki_td_soft_fill_rect(ki_td_soft_renderer *renderer,
                          const ki_td_view *view, float x, float y,
                          float width, float height, uint32_t rgb,
                          float alpha)
{
    int screen_x;
    int screen_y;
    float screen_width;
    float screen_height;
    float selected;
    if (!renderer_is_ready(renderer) ||
        !normalized_alpha(alpha, &selected) ||
        !ki_td_internal_screen_x(view, x, &screen_x) ||
        !ki_td_internal_screen_y(view, y, &screen_y) ||
        !ki_td_internal_screen_scale(view, width, &screen_width) ||
        !ki_td_internal_screen_scale(view, height, &screen_height))
        return;
    fill_rect_ready(renderer, (float)screen_x, (float)screen_y,
                    screen_width, screen_height, rgb, selected);
}

void ki_td_soft_fill_circle(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, float x, float y,
                            float radius, uint32_t rgb, float alpha)
{
    int screen_x;
    int screen_y;
    float screen_radius;
    if (!ki_td_internal_screen_x(view, x, &screen_x) ||
        !ki_td_internal_screen_y(view, y, &screen_y) ||
        !ki_td_internal_screen_scale(view, radius, &screen_radius))
        return;
    ki_td_soft_fill_circle_px(renderer, (float)screen_x, (float)screen_y,
                              screen_radius, rgb, alpha);
}

void ki_td_soft_fill_ellipse(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             float radius_x, float radius_y, uint32_t rgb,
                             float alpha)
{
    int screen_x;
    int screen_y;
    float screen_radius_x;
    float screen_radius_y;
    if (!ki_td_internal_screen_x(view, x, &screen_x) ||
        !ki_td_internal_screen_y(view, y, &screen_y) ||
        !ki_td_internal_screen_scale(view, radius_x, &screen_radius_x) ||
        !ki_td_internal_screen_scale(view, radius_y, &screen_radius_y))
        return;
    ki_td_soft_fill_ellipse_px(renderer, (float)screen_x, (float)screen_y,
                               screen_radius_x, screen_radius_y, rgb, alpha);
}

void ki_td_soft_line(ki_td_soft_renderer *renderer, const ki_td_view *view,
                     float x0, float y0, float x1, float y1, float width,
                     uint32_t rgb, float alpha)
{
    int screen_x0;
    int screen_y0;
    int screen_x1;
    int screen_y1;
    float screen_width;
    if (!ki_td_internal_screen_x(view, x0, &screen_x0) ||
        !ki_td_internal_screen_y(view, y0, &screen_y0) ||
        !ki_td_internal_screen_x(view, x1, &screen_x1) ||
        !ki_td_internal_screen_y(view, y1, &screen_y1) ||
        !ki_td_internal_screen_scale(view, width, &screen_width))
        return;
    ki_td_soft_line_px(renderer, (float)screen_x0, (float)screen_y0,
                       (float)screen_x1, (float)screen_y1,
                       screen_width, rgb, alpha);
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
    sr_blend(&renderer->canvas, x, y, rgb,
             alpha * ((float)pixel[3] / 255.0f));
}

static void blend_rgba_integer_block(ki_td_soft_renderer *renderer,
                                     int64_t left, int64_t top,
                                     int64_t right, int64_t bottom,
                                     const uint8_t *pixel, float alpha)
{
    sr_canvas *canvas = &renderer->canvas;
    float pixel_alpha;
    int alpha256;
    int first_x;
    int first_y;
    int last_x;
    int last_y;
    uint32_t rgb;
    int y;
    if (pixel[3] < 8u || right <= canvas->clip_x0 ||
        bottom <= canvas->clip_y0 || left >= canvas->clip_x1 ||
        top >= canvas->clip_y1)
        return;
    first_x = left < canvas->clip_x0 ? canvas->clip_x0 : (int)left;
    first_y = top < canvas->clip_y0 ? canvas->clip_y0 : (int)top;
    last_x = right > canvas->clip_x1 ? canvas->clip_x1 : (int)right;
    last_y = bottom > canvas->clip_y1 ? canvas->clip_y1 : (int)bottom;
    pixel_alpha = alpha * ((float)pixel[3] / 255.0f);
    alpha256 = (int)(pixel_alpha * 256.0f + 0.5f);
    if (alpha256 <= 0) return;
    if (alpha256 > 256) alpha256 = 256;
    rgb = ((uint32_t)pixel[0] << 16) |
          ((uint32_t)pixel[1] << 8) |
          (uint32_t)pixel[2];
    for (y = first_y; y < last_y; ++y) {
        int x;
        for (x = first_x; x < last_x; ++x) {
            uint32_t *destination =
                &canvas->px[(size_t)y * (size_t)canvas->w + (size_t)x];
            if (alpha256 == 256) {
                *destination = UINT32_C(0xff000000) | rgb;
            } else {
                uint32_t current = *destination;
                int red = (int)((current >> 16) & UINT32_C(255));
                int green = (int)((current >> 8) & UINT32_C(255));
                int blue = (int)(current & UINT32_C(255));
                int output_alpha = (int)(current >> 24);
                red +=
                    (((int)pixel[0] - red) * alpha256) >> 8;
                green +=
                    (((int)pixel[1] - green) * alpha256) >> 8;
                blue +=
                    (((int)pixel[2] - blue) * alpha256) >> 8;
                output_alpha +=
                    ((255 - output_alpha) * alpha256) >> 8;
                *destination =
                    (uint32_t)output_alpha << 24 |
                    (uint32_t)red << 16 |
                    (uint32_t)green << 8 |
                    (uint32_t)blue;
            }
        }
    }
}

static void blend_rgba_scaled_block(ki_td_soft_renderer *renderer,
                                    int64_t left, int64_t top, int span,
                                    float edge_coverage,
                                    const uint8_t *pixel, float alpha)
{
    sr_canvas *canvas = &renderer->canvas;
    float pixel_alpha;
    int first_x;
    int first_y;
    int last_x;
    int last_y;
    uint32_t rgb;
    int y;
    if (pixel[3] < 8u || span <= 0 || edge_coverage <= 0.0f ||
        left >= canvas->clip_x1 || top >= canvas->clip_y1 ||
        left + span <= canvas->clip_x0 ||
        top + span <= canvas->clip_y0)
        return;
    first_x = left < canvas->clip_x0 ? canvas->clip_x0 : (int)left;
    first_y = top < canvas->clip_y0 ? canvas->clip_y0 : (int)top;
    last_x = left + span > canvas->clip_x1 ?
             canvas->clip_x1 : (int)(left + span);
    last_y = top + span > canvas->clip_y1 ?
             canvas->clip_y1 : (int)(top + span);
    pixel_alpha = alpha * ((float)pixel[3] / 255.0f);
    rgb = ((uint32_t)pixel[0] << 16) |
          ((uint32_t)pixel[1] << 8) |
          (uint32_t)pixel[2];
    for (y = first_y; y < last_y; ++y) {
        float coverage_y =
            (int64_t)y == top + span - 1 ? edge_coverage : 1.0f;
        int x;
        for (x = first_x; x < last_x; ++x) {
            float coverage_x =
                (int64_t)x == left + span - 1 ? edge_coverage : 1.0f;
            int alpha256;
            uint32_t *destination;
            uint32_t current;
            int red;
            int green;
            int blue;
            int output_alpha;
            destination =
                &canvas->px[(size_t)y * (size_t)canvas->w + (size_t)x];
            if (pixel[3] == 255u && alpha >= 1.0f &&
                coverage_x == 1.0f && coverage_y == 1.0f) {
                *destination = UINT32_C(0xff000000) | rgb;
                continue;
            }
            alpha256 = (int)(
                pixel_alpha * coverage_x * coverage_y * 256.0f + 0.5f);
            if (alpha256 <= 0) continue;
            if (alpha256 > 256) alpha256 = 256;
            if (alpha256 == 256) {
                *destination = UINT32_C(0xff000000) | rgb;
                continue;
            }
            current = *destination;
            red = (int)((current >> 16) & UINT32_C(255));
            green = (int)((current >> 8) & UINT32_C(255));
            blue = (int)(current & UINT32_C(255));
            output_alpha = (int)(current >> 24);
            red += (((int)pixel[0] - red) * alpha256) >> 8;
            green += (((int)pixel[1] - green) * alpha256) >> 8;
            blue += (((int)pixel[2] - blue) * alpha256) >> 8;
            output_alpha += ((255 - output_alpha) * alpha256) >> 8;
            *destination =
                (uint32_t)output_alpha << 24 |
                (uint32_t)red << 16 |
                (uint32_t)green << 8 |
                (uint32_t)blue;
        }
    }
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

static bool visible_logical_grid(ki_td_soft_renderer *renderer,
                                 const ki_td_view *view, float x, float y,
                                 int width, int height,
                                 ki_td_cell_bounds *bounds)
{
    sr_canvas *canvas;
    ki_td_rect clip;
    if (!renderer_is_ready(renderer) ||
        !ki_td_internal_view_valid(view) || !isfinite(x) || !isfinite(y) ||
        width <= 0 || height <= 0 || !bounds)
        return false;
    canvas = &renderer->canvas;
    if (canvas->clip_x1 <= canvas->clip_x0 ||
        canvas->clip_y1 <= canvas->clip_y0)
        return false;
    clip = (ki_td_rect){
        canvas->clip_x0, canvas->clip_y0,
        canvas->clip_x1 - canvas->clip_x0,
        canvas->clip_y1 - canvas->clip_y0
    };
    return ki_td_view_visible_cells(
               view, clip, x, y, 1, 1, width, height, 1, bounds) &&
           bounds->column_count > 0 && bounds->row_count > 0;
}

void ki_td_soft_rgba_px(ki_td_soft_renderer *renderer, int x, int y,
                        const ki_td_rgba8 *image, float alpha)
{
    sr_canvas *canvas;
    int first_x;
    int first_y;
    int last_x;
    int last_y;
    int64_t first_x64;
    int64_t first_y64;
    int64_t last_x64;
    int64_t last_y64;
    float selected;
    if (!renderer_is_ready(renderer) || !ki_td_rgba8_is_valid(image) ||
        !normalized_alpha(alpha, &selected))
        return;
    canvas = &renderer->canvas;
    first_x64 = (int64_t)canvas->clip_x0 - x;
    first_y64 = (int64_t)canvas->clip_y0 - y;
    last_x64 = (int64_t)canvas->clip_x1 - x;
    last_y64 = (int64_t)canvas->clip_y1 - y;
    if (first_x64 < 0) first_x64 = 0;
    if (first_y64 < 0) first_y64 = 0;
    if (last_x64 > image->width) last_x64 = image->width;
    if (last_y64 > image->height) last_y64 = image->height;
    if (first_x64 >= last_x64 || first_y64 >= last_y64 ||
        first_x64 >= image->width || first_y64 >= image->height ||
        last_x64 <= 0 || last_y64 <= 0)
        return;
    first_x = (int)first_x64;
    first_y = (int)first_y64;
    last_x = (int)last_x64;
    last_y = (int)last_y64;
    for (int yy = first_y; yy < last_y; ++yy)
        for (int xx = first_x; xx < last_x; ++xx)
            blend_rgba_pixel(
                renderer, (int)((int64_t)x + xx), (int)((int64_t)y + yy),
                image_pixel(image, xx, yy), selected);
}

void ki_td_soft_rgba(ki_td_soft_renderer *renderer, const ki_td_view *view,
                     float x, float y, const ki_td_rgba8 *image, float alpha)
{
    ki_td_cell_bounds visible;
    float selected;
    if (!ki_td_rgba8_is_valid(image) ||
        !normalized_alpha(alpha, &selected) ||
        !visible_logical_grid(renderer, view, x, y, image->width,
                              image->height, &visible))
        return;
    for (int yy = visible.first_row;
         yy < visible.first_row + visible.row_count; ++yy)
        for (int xx = visible.first_column;
             xx < visible.first_column + visible.column_count; ++xx)
            fill_rgba_world_pixel(renderer, view, x + (float)xx,
                                  y + (float)yy,
                                  image_pixel(image, xx, yy), selected);
}

static void modulate_rgba_pixel(const uint8_t *pixel, uint32_t tint_rgb,
                                uint8_t *result)
{
    result[0] = (uint8_t)(
        (uint32_t)pixel[0] * ((tint_rgb >> 16) & UINT32_C(255)) /
        UINT32_C(255));
    result[1] = (uint8_t)(
        (uint32_t)pixel[1] * ((tint_rgb >> 8) & UINT32_C(255)) /
        UINT32_C(255));
    result[2] = (uint8_t)(
        (uint32_t)pixel[2] * (tint_rgb & UINT32_C(255)) / UINT32_C(255));
    result[3] = pixel[3];
}

static void draw_rgba_resized(ki_td_soft_renderer *renderer,
                              const ki_td_view *view, float x, float y,
                              const ki_td_rgba8 *image, int width, int height,
                              uint32_t tint_rgb, float alpha)
{
    int integer_scale;
    int scaled_span;
    float edge_coverage;
    float selected;
    ki_td_cell_bounds visible;
    if (!ki_td_rgba8_is_valid(image) ||
        !normalized_alpha(alpha, &selected) ||
        !visible_logical_grid(renderer, view, x, y, width, height, &visible))
        return;
    if (!ki_td_internal_float_to_int(
            floorf(view->scale + 0.5f), &integer_scale))
        return;
    if (integer_scale >= 1 &&
        fabsf(view->scale - (float)integer_scale) <= 0.01f) {
        int origin_x;
        int origin_y;
        if (!ki_td_internal_screen_x(view, x, &origin_x) ||
            !ki_td_internal_screen_y(view, y, &origin_y))
            return;
        for (int yy = visible.first_row;
             yy < visible.first_row + visible.row_count; ++yy) {
            int source_y = (int)(
                (int64_t)yy * image->height / height);
            int64_t top =
                (int64_t)origin_y + (int64_t)yy * integer_scale;
            int64_t bottom = top + integer_scale;
            for (int xx = visible.first_column;
                 xx < visible.first_column + visible.column_count; ++xx) {
                const uint8_t *pixel;
                uint8_t tinted_pixel[4];
                int source_x = (int)(
                    (int64_t)xx * image->width / width);
                int64_t left =
                    (int64_t)origin_x + (int64_t)xx * integer_scale;
                int64_t right = left + integer_scale;
                pixel = image_pixel(image, source_x, source_y);
                modulate_rgba_pixel(pixel, tint_rgb, tinted_pixel);
                blend_rgba_integer_block(
                    renderer, left, top, right, bottom, tinted_pixel,
                    selected);
            }
        }
        return;
    }
    if (!ki_td_internal_float_to_int(ceilf(view->scale), &scaled_span) ||
        scaled_span <= 0)
        return;
    edge_coverage =
        view->scale - (float)(scaled_span - 1);
    for (int yy = visible.first_row;
         yy < visible.first_row + visible.row_count; ++yy) {
        int source_y = (int)((int64_t)yy * image->height / height);
        int top;
        if (!ki_td_internal_screen_y(view, y + (float)yy, &top)) continue;
        for (int xx = visible.first_column;
             xx < visible.first_column + visible.column_count; ++xx) {
            const uint8_t *pixel;
            uint8_t tinted_pixel[4];
            int source_x = (int)((int64_t)xx * image->width / width);
            int left;
            if (!ki_td_internal_screen_x(view, x + (float)xx, &left))
                continue;
            pixel = image_pixel(image, source_x, source_y);
            modulate_rgba_pixel(pixel, tint_rgb, tinted_pixel);
            blend_rgba_scaled_block(
                renderer, left, top, scaled_span, edge_coverage,
                tinted_pixel, selected);
        }
    }
}

void ki_td_soft_rgba_resized(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             const ki_td_rgba8 *image, int width, int height,
                             float alpha)
{
    draw_rgba_resized(renderer, view, x, y, image, width, height,
                      UINT32_C(0xffffff), alpha);
}

void ki_td_soft_rgba_tinted(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, float x, float y,
                            const ki_td_rgba8 *image, int width, int height,
                            uint32_t tint_rgb, float alpha)
{
    draw_rgba_resized(renderer, view, x, y, image, width, height, tint_rgb,
                      alpha);
}

void ki_td_soft_rgba_rotated(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, float x, float y,
                             const ki_td_rgba8 *image, int quarter_turns,
                             float alpha)
{
    ki_td_cell_bounds visible;
    float selected;
    int destination_width;
    int destination_height;
    if (!ki_td_rgba8_is_valid(image) ||
        !normalized_alpha(alpha, &selected))
        return;
    int turns = quarter_turns % 4;
    if (turns < 0) turns += 4;
    if (turns == 0) {
        ki_td_soft_rgba(renderer, view, x, y, image, selected);
        return;
    }
    destination_width = turns == 2 ? image->width : image->height;
    destination_height = turns == 2 ? image->height : image->width;
    if (!visible_logical_grid(
            renderer, view, x, y, destination_width, destination_height,
            &visible))
        return;

    for (int source_y = 0; source_y < image->height; source_y++) {
        for (int source_x = 0; source_x < image->width; source_x++) {
            int dest_x;
            int dest_y;
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
            if (dest_x < visible.first_column ||
                dest_x >= visible.first_column + visible.column_count ||
                dest_y < visible.first_row ||
                dest_y >= visible.first_row + visible.row_count)
                continue;
            fill_rgba_world_pixel(renderer, view, x + (float)dest_x,
                                  y + (float)dest_y,
                                  image_pixel(image, source_x, source_y),
                                  selected);
        }
    }
}

void ki_td_soft_rgba_pixel_art(ki_td_soft_renderer *renderer,
                               const ki_td_view *view, float x, float y,
                               const ki_td_rgba8 *image, float alpha)
{
    float selected;
    int scale;
    ki_td_cell_bounds visible;
    int origin_x;
    int origin_y;
    if (!ki_td_rgba8_is_valid(image) ||
        !normalized_alpha(alpha, &selected) ||
        !ki_td_internal_view_valid(view))
        return;
    if (!ki_td_internal_float_to_int(
            floorf(view->scale + 0.5f), &scale))
        return;
    if (scale < 1 || fabsf(view->scale - (float)scale) > 0.01f) {
        ki_td_soft_rgba(renderer, view, x, y, image, selected);
        return;
    }
    if (!visible_logical_grid(renderer, view, x, y, image->width,
                              image->height, &visible) ||
        !ki_td_internal_screen_x(view, x, &origin_x) ||
        !ki_td_internal_screen_y(view, y, &origin_y))
        return;
    for (int yy = visible.first_row;
         yy < visible.first_row + visible.row_count; ++yy) {
        for (int xx = visible.first_column;
             xx < visible.first_column + visible.column_count; ++xx) {
            const uint8_t *pixel = image_pixel(image, xx, yy);
            int64_t pixel_x = (int64_t)origin_x + (int64_t)xx * scale;
            int64_t pixel_y = (int64_t)origin_y + (int64_t)yy * scale;
            blend_rgba_integer_block(
                renderer, pixel_x, pixel_y, pixel_x + scale,
                pixel_y + scale, pixel, selected);
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
    float selected;
    if (!renderer_is_ready(renderer) || !ki_td_internal_view_valid(view) ||
        !slice || !isfinite(x) || !isfinite(y) ||
        !normalized_alpha(alpha, &selected) ||
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
                  &slice->image, 0, 0, slice->left, slice->top, selected);
    if (center_width > 0)
        draw_subimage(renderer, view, x + (float)slice->left, y,
                      center_width, slice->top, &slice->image, slice->left,
                      0, source_center_width, slice->top, selected);
    draw_subimage(renderer, view, x + (float)destination_right, y,
                  slice->right, slice->top, &slice->image, source_right, 0,
                  slice->right, slice->top, selected);
    if (center_height > 0)
        draw_subimage(renderer, view, x, y + (float)slice->top,
                      slice->left, center_height, &slice->image, 0, slice->top,
                      slice->left, source_center_height, selected);
    if (center_width > 0 && center_height > 0)
        draw_subimage(renderer, view, x + (float)slice->left,
                      y + (float)slice->top, center_width, center_height,
                      &slice->image, slice->left, slice->top,
                      source_center_width, source_center_height, selected);
    if (center_height > 0)
        draw_subimage(renderer, view, x + (float)destination_right,
                      y + (float)slice->top, slice->right, center_height,
                      &slice->image, source_right, slice->top, slice->right,
                      source_center_height, selected);
    draw_subimage(renderer, view, x, y + (float)destination_bottom,
                  slice->left, slice->bottom, &slice->image, 0, source_bottom,
                  slice->left, slice->bottom, selected);
    if (center_width > 0)
        draw_subimage(renderer, view, x + (float)slice->left,
                      y + (float)destination_bottom, center_width,
                      slice->bottom, &slice->image, slice->left,
                      source_bottom, source_center_width, slice->bottom,
                      selected);
    draw_subimage(renderer, view, x + (float)destination_right,
                  y + (float)destination_bottom, slice->right, slice->bottom,
                  &slice->image, source_right, source_bottom, slice->right,
                  slice->bottom, selected);
}

static bool visible_tile_bounds(ki_td_soft_renderer *renderer,
                                const ki_td_view *view,
                                const ki_td_tile_batch *batch,
                                ki_td_cell_bounds *bounds)
{
    sr_canvas *canvas;
    ki_td_rect clip;
    if (!renderer_is_ready(renderer) ||
        !ki_td_internal_view_valid(view) || !batch || !bounds)
        return false;
    canvas = &renderer->canvas;
    if (canvas->clip_x1 <= canvas->clip_x0 ||
        canvas->clip_y1 <= canvas->clip_y0)
        return false;
    clip = (ki_td_rect){
        canvas->clip_x0, canvas->clip_y0,
        canvas->clip_x1 - canvas->clip_x0,
        canvas->clip_y1 - canvas->clip_y0
    };
    return ki_td_view_visible_cells(
               view, clip, batch->x, batch->y,
               batch->tile_width, batch->tile_height,
               (int)batch->columns, (int)batch->rows, 1, bounds) &&
           bounds->column_count > 0 && bounds->row_count > 0;
}

void ki_td_soft_tile_batch(ki_td_soft_renderer *renderer,
                           const ki_td_view *view,
                           const ki_td_tile_batch *batch)
{
    uint32_t atlas_cell_width;
    uint32_t atlas_cell_height;
    uint64_t atlas_count;
    ki_td_cell_bounds visible;
    float selected;
    int row;
    if (!ki_td_tile_batch_is_valid(batch) ||
        !normalized_alpha(batch->alpha, &selected) ||
        !visible_tile_bounds(renderer, view, batch, &visible))
        return;
    atlas_cell_width = (uint32_t)batch->atlas->width / batch->atlas_columns;
    atlas_cell_height = (uint32_t)batch->atlas->height / batch->atlas_rows;
    atlas_count = (uint64_t)batch->atlas_columns * batch->atlas_rows;
    for (row = visible.first_row;
         row < visible.first_row + visible.row_count; ++row) {
        int column;
        for (column = visible.first_column;
             column < visible.first_column + visible.column_count;
             ++column) {
            size_t index = (size_t)row * batch->columns + (size_t)column;
            uint32_t cell = batch->cells[index];
            uint32_t source_column;
            uint32_t source_row;
            if (cell == batch->empty_cell || (uint64_t)cell >= atlas_count)
                continue;
            source_column = cell % batch->atlas_columns;
            source_row = cell / batch->atlas_columns;
            draw_subimage(
                renderer, view,
                batch->x + (float)column * (float)batch->tile_width,
                batch->y + (float)row * (float)batch->tile_height,
                batch->tile_width, batch->tile_height, batch->atlas,
                (int)(source_column * atlas_cell_width),
                (int)(source_row * atlas_cell_height),
                (int)atlas_cell_width, (int)atlas_cell_height, selected);
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
    if (!renderer_is_ready(renderer) ||
        !ki_td_internal_view_valid(view))
        return;
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
