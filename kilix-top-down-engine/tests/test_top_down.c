#include "kilix_top_down.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static uint64_t hash_bytes(const uint8_t *bytes, size_t count)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < count; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void test_fit_and_transform(void)
{
    ki_td_fit_spec spec;
    ki_td_view view = {0};
    ki_td_cell_bounds bounds;
    float logical_x;
    float logical_y;
    EXPECT(!ki_td_fit_spec_init(NULL, 1, 1, 1, 1));
    EXPECT(!ki_td_fit_spec_init(&spec, 0, 1, 1, 1));
    EXPECT(ki_td_fit_spec_init(&spec, 256, 176, 960, 540));
    spec.fit_bounds = (ki_td_rect){0, 64, 960, 466};
    spec.align_bounds = (ki_td_rect){0, 64, 960, 476};
    spec.scale_policy = KI_TD_SCALE_PIXEL_ART;
    spec.minimum_scale = 1.0f;
    spec.integer_scale_threshold = 2.0f;
    spec.clamp_origin_y = true;
    EXPECT(ki_td_view_fit(&view, &spec));
    EXPECT(view.scale == 2.0f);
    EXPECT(view.origin_x == 224);
    EXPECT(view.origin_y == 126);
    EXPECT(ki_td_screen_x(&view, 0.0f) == 224);
    EXPECT(ki_td_screen_y(&view, 0.0f) == 126);
    EXPECT(ki_td_screen_x(&view, 12.25f) == 249);
    EXPECT(ki_td_screen_scale(&view, 3.25f) == 6.5f);

    ki_td_view_set_offset(&view, -3, 4);
    EXPECT(ki_td_screen_x(&view, 12.25f) == 246);
    EXPECT(ki_td_screen_y(&view, 5.0f) == 140);
    EXPECT(ki_td_screen_to_logical(&view, 246.0f, 140.0f,
                                   &logical_x, &logical_y));
    EXPECT(fabsf(logical_x - 12.5f) < 0.0001f);
    EXPECT(fabsf(logical_y - 5.0f) < 0.0001f);
    EXPECT(!ki_td_screen_to_logical(NULL, 0.0f, 0.0f,
                                    &logical_x, &logical_y));

    EXPECT(ki_td_fit_spec_init(&spec, 100, 100, 150, 170));
    spec.scale_policy = KI_TD_SCALE_PIXEL_ART;
    spec.integer_scale_threshold = 2.0f;
    EXPECT(ki_td_view_fit(&view, &spec));
    EXPECT(fabsf(view.scale - 1.5f) < 0.0001f);
    EXPECT(view.origin_x == 0 && view.origin_y == 10);

    spec.fit_bounds = (ki_td_rect){0, 0, 290, 290};
    spec.align_bounds = spec.fit_bounds;
    EXPECT(ki_td_view_fit(&view, &spec));
    EXPECT(view.scale == 2.0f);
    EXPECT(view.origin_x == 45 && view.origin_y == 45);

    spec.scale_policy = KI_TD_SCALE_INTEGER;
    EXPECT(ki_td_view_fit(&view, &spec));
    EXPECT(view.scale == 2.0f);

    spec.fit_bounds = (ki_td_rect){0, 0, 50, 50};
    spec.align_bounds = spec.fit_bounds;
    spec.minimum_scale = 1.0f;
    EXPECT(ki_td_view_fit(&view, &spec));
    EXPECT(view.scale == 1.0f);
    EXPECT(view.origin_x == -25 && view.origin_y == -25);
    spec.clamp_origin_x = true;
    spec.clamp_origin_y = true;
    EXPECT(ki_td_view_fit(&view, &spec));
    EXPECT(view.origin_x == 0 && view.origin_y == 0);

    view = (ki_td_view){.scale = 2.0f, .origin_x = 10, .origin_y = 20};
    EXPECT(ki_td_screen_x(&view, -0.25f) == 10);
    EXPECT(ki_td_screen_x(&view, -0.75f) == 9);
    EXPECT(ki_td_view_visible_cells(
        &view, (ki_td_rect){10, 20, 40, 20}, 0.0f, 0.0f,
        5, 5, 10, 8, 0, &bounds));
    EXPECT(bounds.first_column == 0 && bounds.first_row == 0);
    EXPECT(bounds.column_count == 4 && bounds.row_count == 2);
    EXPECT(ki_td_view_visible_cells(
        &view, (ki_td_rect){20, 30, 20, 20}, 0.0f, 0.0f,
        5, 5, 10, 8, 1, &bounds));
    EXPECT(bounds.first_column == 0 && bounds.first_row == 0);
    EXPECT(bounds.column_count == 4 && bounds.row_count == 4);
    EXPECT(ki_td_view_visible_cells(
        &view, (ki_td_rect){200, 200, 20, 20}, 0.0f, 0.0f,
        5, 5, 10, 8, 0, &bounds));
    EXPECT(bounds.column_count == 0 && bounds.row_count == 0);
    EXPECT(!ki_td_view_visible_cells(
        &view, (ki_td_rect){0, 0, 0, 20}, 0.0f, 0.0f,
        5, 5, 10, 8, 0, &bounds));
    EXPECT(ki_td_view_visible_cells(
        &view, (ki_td_rect){20, 30, 20, 20}, 5.0f, 5.0f,
        5, 5, 10, 8, 0, &bounds));
    EXPECT(bounds.first_column == 0 && bounds.first_row == 0);
    EXPECT(bounds.column_count == 2 && bounds.row_count == 2);
}

static void test_shake(void)
{
    int x = ki_td_shake_axis(UINT32_C(12345), 7.0f,
                             UINT32_C(0x6d2b79f5));
    int y = ki_td_shake_axis(UINT32_C(12345), 7.0f,
                             UINT32_C(0x1b873593));
    EXPECT(x == ki_td_shake_axis(UINT32_C(12345), 7.0f,
                                 UINT32_C(0x6d2b79f5)));
    EXPECT(y == ki_td_shake_axis(UINT32_C(12345), 7.0f,
                                 UINT32_C(0x1b873593)));
    EXPECT(x >= -3 && x <= 3);
    EXPECT(y >= -3 && y <= 3);
    EXPECT(ki_td_shake_axis(1u, 0.0f, 2u) == 0);
    EXPECT(ki_td_shake_axis(1u, -1.0f, 2u) == 0);
}

static void test_extreme_numeric_inputs(void)
{
    ki_td_fit_spec spec;
    ki_td_view view = {
        .logical_width = 7,
        .logical_height = 9,
        .scale = 1.0f,
        .origin_x = 11,
        .origin_y = 13
    };
    ki_td_view original = view;

    EXPECT(ki_td_fit_spec_init(&spec, 100, 100, 100, 100));
    spec.minimum_scale = FLT_MAX;
    EXPECT(!ki_td_view_fit(&view, &spec));
    EXPECT(memcmp(&view, &original, sizeof view) == 0);

    view = (ki_td_view){.scale = 1.0f};
    EXPECT(ki_td_screen_x(&view, FLT_MAX) == 0);
    EXPECT(ki_td_screen_y(&view, -FLT_MAX) == 0);
    view.origin_x = INT_MAX;
    view.offset_x = 1;
    EXPECT(ki_td_screen_x(&view, 0.0f) == 0);
    view = (ki_td_view){.scale = 2.0f};
    EXPECT(ki_td_screen_scale(&view, FLT_MAX) == 0.0f);
    view.scale = INFINITY;
    EXPECT(ki_td_screen_x(&view, 1.0f) == 0);
    EXPECT(ki_td_screen_scale(&view, 1.0f) == 0.0f);
    EXPECT(ki_td_shake_axis(0u, FLT_MAX, 0u) == 0);
}

static void test_renderer_lifetime(void)
{
    ki_td_soft_renderer renderer = {0};
    EXPECT(!ki_td_soft_renderer_resize(&renderer, 0, 12));
    EXPECT(ki_td_soft_renderer_init(&renderer, 16, 12));
    EXPECT(ki_td_soft_width(&renderer) == 16);
    EXPECT(ki_td_soft_height(&renderer) == 12);
    EXPECT(ki_td_soft_canvas(&renderer) != NULL);
    EXPECT(ki_td_soft_canvas_const(&renderer) == &renderer.canvas);

    uint8_t *original_rgba = renderer.rgba;
    uint32_t *original_pixels = renderer.canvas.px;
    EXPECT(ki_td_soft_renderer_resize(&renderer, 16, 12));
    EXPECT(renderer.rgba == original_rgba);
    EXPECT(renderer.canvas.px == original_pixels);
    EXPECT(!ki_td_soft_renderer_resize(&renderer, -1, 12));
    EXPECT(renderer.rgba == original_rgba);
    EXPECT(renderer.canvas.px == original_pixels);

    ki_td_soft_clear(&renderer, UINT32_C(0x123456));
    uint8_t *packed = ki_td_soft_pack_rgba(&renderer);
    EXPECT(packed != NULL);
    EXPECT(packed[0] == 0x12u && packed[1] == 0x34u && packed[2] == 0x56u &&
           packed[3] == 0xffu);
    ki_td_soft_clear(&renderer, 0u);
    ki_td_soft_blend_pixel(&renderer, 1, 1, UINT32_C(0xe03070), 1.0f);
    EXPECT(renderer.canvas.px[1 + renderer.canvas.w] ==
           UINT32_C(0xffe03070));
    EXPECT(ki_td_soft_renderer_resize(&renderer, 9, 7));
    EXPECT(ki_td_soft_width(&renderer) == 9);
    EXPECT(ki_td_soft_height(&renderer) == 7);
    ki_td_soft_renderer_destroy(&renderer);
    EXPECT(renderer.rgba == NULL && renderer.canvas.px == NULL);
    EXPECT(ki_td_soft_width(&renderer) == 0);
    ki_td_soft_renderer_destroy(&renderer);
}

static void draw_adapter_golden(ki_td_soft_renderer *renderer)
{
    static const uint8_t sprite_pixels[] = {
        255,   0,   0, 255,    0, 255,   0,   7,    0,   0, 255, 128,
        255, 255, 255, 255,  255, 192,   0, 192,   32, 224, 255, 255
    };
    ki_td_rgba8 sprite = ki_td_rgba8_make(sprite_pixels, 3, 2);
    ki_td_fit_spec spec;
    ki_td_view view;
    EXPECT(ki_td_fit_spec_init(&spec, 24, 18, renderer->width,
                               renderer->height));
    spec.scale_policy = KI_TD_SCALE_PIXEL_ART;
    spec.minimum_scale = 1.0f;
    EXPECT(ki_td_view_fit(&view, &spec));

    ki_td_soft_clear(renderer, UINT32_C(0x07111d));
    ki_td_soft_fill_rect(renderer, &view, 1.0f, 1.0f, 22.0f, 16.0f,
                         UINT32_C(0x183153), 1.0f);
    ki_td_soft_fill_circle(renderer, &view, 6.0f, 7.0f, 3.5f,
                           UINT32_C(0xe64980), 0.85f);
    ki_td_soft_fill_ellipse(renderer, &view, 17.0f, 6.0f, 4.0f, 2.0f,
                            UINT32_C(0x74c0fc), 0.75f);
    ki_td_soft_line(renderer, &view, 2.0f, 15.0f, 22.0f, 10.0f, 1.25f,
                    UINT32_C(0xffd166), 0.9f);
    ki_td_soft_rgba_pixel_art(renderer, &view, 3.0f, 2.0f, &sprite, 1.0f);
    ki_td_soft_rgba_resized(renderer, &view, 10.0f, 2.0f, &sprite, 6, 4,
                            0.8f);
    ki_td_soft_rgba_rotated(renderer, &view, 3.0f, 11.0f, &sprite, 1, 1.0f);
    ki_td_soft_rgba_rotated(renderer, &view, 8.0f, 11.0f, &sprite, 2, 1.0f);
    ki_td_soft_rgba_rotated(renderer, &view, 14.0f, 11.0f, &sprite, 3, 1.0f);
    ki_td_soft_rgba_px(renderer, 1, 1, &sprite, 0.65f);
}

static void test_adapter(void)
{
    static const uint8_t cutoff_pixels[] = {
        255, 0, 0, 255, 0, 255, 0, 7
    };
    ki_td_rgba8 cutoff = ki_td_rgba8_make(cutoff_pixels, 2, 1);
    EXPECT(ki_td_rgba8_is_valid(&cutoff));
    ki_td_rgba8 invalid = cutoff;
    invalid.stride = 3;
    EXPECT(!ki_td_rgba8_is_valid(&invalid));

    ki_td_soft_renderer renderer = {0};
    EXPECT(ki_td_soft_renderer_init(&renderer, 96, 72));
    ki_td_soft_clear(&renderer, 0);
    ki_td_soft_rgba_px(&renderer, 2, 2, &cutoff, 1.0f);
    EXPECT(renderer.canvas.px[2 + 2 * renderer.canvas.w] ==
           UINT32_C(0xffff0000));
    EXPECT(renderer.canvas.px[3 + 2 * renderer.canvas.w] ==
           UINT32_C(0xff000000));

    {
        ki_td_view view = {.scale = 1.5f};
        size_t bytes = (size_t)renderer.canvas.w *
                       (size_t)renderer.canvas.h *
                       sizeof renderer.canvas.px[0];
        uint64_t expected;
        ki_td_soft_clear(&renderer, UINT32_C(0x172033));
        ki_td_soft_rgba(&renderer, &view, 2.0f, 3.0f, &cutoff, 0.75f);
        expected = hash_bytes((const uint8_t *)renderer.canvas.px, bytes);
        ki_td_soft_clear(&renderer, UINT32_C(0x172033));
        ki_td_soft_rgba_rotated(
            &renderer, &view, 2.0f, 3.0f, &cutoff, 4, 0.75f);
        EXPECT(hash_bytes((const uint8_t *)renderer.canvas.px, bytes) ==
               expected);
        ki_td_soft_clear(&renderer, UINT32_C(0x172033));
        ki_td_soft_rgba_pixel_art(
            &renderer, &view, 2.0f, 3.0f, &cutoff, 0.75f);
        EXPECT(hash_bytes((const uint8_t *)renderer.canvas.px, bytes) ==
               expected);
    }

    draw_adapter_golden(&renderer);
    uint8_t *rgba = ki_td_soft_pack_rgba(&renderer);
    EXPECT(rgba != NULL);
    uint64_t hash = hash_bytes(rgba, renderer.rgba_size);
    const uint64_t expected = UINT64_C(0xf541ab62fc7fee0c);
    if (hash != expected) {
        fprintf(stderr, "adapter golden hash: %016" PRIx64 "\n", hash);
        failures++;
    }
    ki_td_soft_renderer_destroy(&renderer);
}

static void reference_fractional_resize(
    ki_td_soft_renderer *renderer, const ki_td_view *view,
    float x, float y, const ki_td_rgba8 *image,
    int width, int height, float alpha)
{
    int yy;
    for (yy = 0; yy < height; ++yy) {
        int source_y = yy * image->height / height;
        int xx;
        for (xx = 0; xx < width; ++xx) {
            int source_x = xx * image->width / width;
            const uint8_t *pixel =
                image->pixels +
                (size_t)source_y * image->stride +
                (size_t)source_x * 4u;
            uint32_t rgb;
            if (pixel[3] < 8u) continue;
            rgb = ((uint32_t)pixel[0] << 16) |
                  ((uint32_t)pixel[1] << 8) |
                  (uint32_t)pixel[2];
            sr_fill_rect(
                &renderer->canvas,
                (float)ki_td_screen_x(view, x + (float)xx),
                (float)ki_td_screen_y(view, y + (float)yy),
                ki_td_screen_scale(view, 1.0f),
                ki_td_screen_scale(view, 1.0f),
                rgb, alpha * ((float)pixel[3] / 255.0f));
        }
    }
}

static void test_fractional_resize_equivalence(void)
{
    static const uint8_t pixels[] = {
        240,  20,  40, 255,   30, 220,  70, 128,
         10,  30, 250,   7,  250, 190,  20, 192
    };
    ki_td_soft_renderer optimized = {0};
    ki_td_soft_renderer reference = {0};
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 2, 2);
    ki_td_view view = {
        .logical_width = 12,
        .logical_height = 10,
        .scale = 1.5f,
        .origin_x = 1,
        .origin_y = 2,
        .offset_x = -1,
        .offset_y = 1
    };
    EXPECT(ki_td_soft_renderer_init(&optimized, 24, 22));
    EXPECT(ki_td_soft_renderer_init(&reference, 24, 22));
    ki_td_soft_clear(&optimized, UINT32_C(0x172033));
    ki_td_soft_clear(&reference, UINT32_C(0x172033));
    ki_td_soft_rgba_resized(
        &optimized, &view, 0.25f, 0.75f, &image, 5, 4, 1.0f);
    reference_fractional_resize(
        &reference, &view, 0.25f, 0.75f, &image, 5, 4, 1.0f);
    ki_td_soft_rgba_resized(
        &optimized, &view, 2.5f, 3.25f, &image, 4, 5, 0.63f);
    reference_fractional_resize(
        &reference, &view, 2.5f, 3.25f, &image, 4, 5, 0.63f);
    EXPECT(memcmp(
        optimized.canvas.px, reference.canvas.px,
        (size_t)optimized.canvas.w *
        (size_t)optimized.canvas.h * sizeof optimized.canvas.px[0]) == 0);
    ki_td_soft_renderer_destroy(&reference);
    ki_td_soft_renderer_destroy(&optimized);
}

static void test_tinted_resize(void)
{
    static const uint8_t pixels[] = {
        200, 160, 120, 255,   30, 220,  70, 128,
         10,  30, 250,   7,  250, 190,  20, 192
    };
    ki_td_soft_renderer resized = {0};
    ki_td_soft_renderer tinted = {0};
    ki_td_soft_renderer unready = {0};
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 2, 2);
    ki_td_rgba8 invalid = image;
    ki_td_view view = {
        .scale = 2.0f,
        .origin_x = 1,
        .origin_y = 2
    };
    uint32_t unchanged[24 * 22];
    size_t canvas_size = sizeof unchanged;
    uint8_t *rgba;
    size_t offset;
    invalid.stride = 3u;
    EXPECT(ki_td_soft_renderer_init(&resized, 24, 22));
    EXPECT(ki_td_soft_renderer_init(&tinted, 24, 22));
    ki_td_soft_clear(&resized, UINT32_C(0x172033));
    ki_td_soft_clear(&tinted, UINT32_C(0x172033));
    ki_td_soft_rgba_resized(
        &resized, &view, 0.25f, 0.75f, &image, 5, 4, 0.63f);
    ki_td_soft_rgba_tinted(
        &tinted, &view, 0.25f, 0.75f, &image, 5, 4,
        UINT32_C(0xffffff), 0.63f);
    view.scale = 1.5f;
    ki_td_soft_rgba_resized(
        &resized, &view, 2.5f, 3.25f, &image, 4, 5, 1.0f);
    ki_td_soft_rgba_tinted(
        &tinted, &view, 2.5f, 3.25f, &image, 4, 5,
        UINT32_C(0xffffff), 1.0f);
    EXPECT(memcmp(resized.canvas.px, tinted.canvas.px, canvas_size) == 0);

    view = (ki_td_view){.scale = 1.0f};
    ki_td_soft_clear(&tinted, UINT32_C(0x172033));
    ki_td_soft_rgba_tinted(
        &tinted, &view, 1.0f, 1.0f, &image, 1, 1,
        UINT32_C(0x40c020), 1.0f);
    rgba = ki_td_soft_pack_rgba(&tinted);
    EXPECT(rgba != NULL);
    offset = ((size_t)tinted.canvas.w + 1u) * 4u;
    EXPECT(rgba[offset] == 50u);
    EXPECT(rgba[offset + 1u] == 120u);
    EXPECT(rgba[offset + 2u] == 15u);
    EXPECT(rgba[offset + 3u] == 255u);

    ki_td_soft_clear(&tinted, UINT32_C(0x314159));
    memcpy(unchanged, tinted.canvas.px, canvas_size);
    ki_td_soft_rgba_tinted(NULL, &view, 0.0f, 0.0f, &image, 2, 2,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&unready, &view, 0.0f, 0.0f, &image, 2, 2,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&tinted, NULL, 0.0f, 0.0f, &image, 2, 2,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&tinted, &view, 0.0f, 0.0f, NULL, 2, 2,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&tinted, &view, 0.0f, 0.0f, &invalid, 2, 2,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&tinted, &view, 0.0f, 0.0f, &image, 0, 2,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&tinted, &view, 0.0f, 0.0f, &image, 2, INT_MIN,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&tinted, &view, -1000.0f, -1000.0f, &image, 2, 2,
                           UINT32_C(0xffffff), 1.0f);
    ki_td_soft_rgba_tinted(&tinted, &view, 1000.0f, 1000.0f, &image, 2, 2,
                           UINT32_C(0xffffff), 1.0f);
    EXPECT(memcmp(unchanged, tinted.canvas.px, canvas_size) == 0);
    ki_td_soft_renderer_destroy(&tinted);
    ki_td_soft_renderer_destroy(&resized);
}

static void test_atlas_and_layers(void)
{
    static const uint8_t nine_pixels[36] = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
        255, 255, 0, 255, 20, 30, 40, 255, 0, 255, 255, 255,
        255, 0, 255, 255, 255, 255, 255, 255, 80, 90, 100, 255
    };
    static const uint8_t atlas_pixels[16] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 0, 255
    };
    static const uint32_t cells[4] = {3u, 2u, 1u, 0u};
    ki_td_rgba8 nine = ki_td_rgba8_make(nine_pixels, 3, 3);
    ki_td_rgba8 atlas = ki_td_rgba8_make(atlas_pixels, 2, 2);
    ki_td_rgba8 center = ki_td_rgba8_subimage(&nine, 1, 1, 1, 1);
    ki_td_rgba8 invalid = ki_td_rgba8_subimage(&nine, 2, 2, 2, 2);
    ki_td_nine_slice slice;
    ki_td_tile_batch batch = {
        &atlas, cells, 4u, 2u, 2u, 2u, 2u, UINT32_MAX,
        0.0f, 0.0f, 1, 1, 1.0f
    };
    ki_td_sprite_command commands[4] = {
        {&atlas, 0, 0, 0, 0, 1, 2, 1, 0},
        {&atlas, 0, 0, 0, 0, 1, 1, 8, 2},
        {&atlas, 0, 0, 0, 0, 1, 1, 8, 1},
        {&atlas, 0, 0, 0, 0, 1, 1, 2, 0}
    };
    size_t order[4] = {0};
    ki_td_soft_renderer renderer = {0};
    ki_td_view view = {.scale = 1.0f};
    EXPECT(ki_td_rgba8_is_valid(&center));
    EXPECT(center.pixels == nine_pixels + 16u && center.stride == 12u);
    EXPECT(!ki_td_rgba8_is_valid(&invalid));
    EXPECT(ki_td_nine_slice_init(&slice, &nine, 1, 1, 1, 1));
    EXPECT(ki_td_tile_batch_is_valid(&batch));
    EXPECT(ki_td_sprite_order(commands, 4u, order, 4u) == 4u);
    EXPECT(order[0] == 3u && order[1] == 2u && order[2] == 1u &&
           order[3] == 0u);
    EXPECT(ki_td_sprite_order(commands, 4u, order, 3u) == 0u);
    EXPECT(ki_td_soft_renderer_init(&renderer, 8, 8));
    ki_td_soft_clear(&renderer, 0u);
    ki_td_soft_nine_slice(&renderer, &view, 1.0f, 1.0f, 5, 5, &slice,
                          1.0f);
    EXPECT(renderer.canvas.px[1 + renderer.canvas.w] ==
           UINT32_C(0xffff0000));
    EXPECT(renderer.canvas.px[3 + 3 * renderer.canvas.w] ==
           UINT32_C(0xff141e28));
    EXPECT(renderer.canvas.px[5 + 5 * renderer.canvas.w] ==
           UINT32_C(0xff505a64));
    ki_td_soft_clear(&renderer, 0u);
    ki_td_soft_tile_batch(&renderer, &view, &batch);
    EXPECT(renderer.canvas.px[0] == UINT32_C(0xffffff00));
    EXPECT(renderer.canvas.px[1] == UINT32_C(0xff0000ff));
    EXPECT(renderer.canvas.px[renderer.canvas.w] == UINT32_C(0xff00ff00));
    EXPECT(renderer.canvas.px[1 + renderer.canvas.w] ==
           UINT32_C(0xffff0000));
    ki_td_soft_sprite_layers(&renderer, &view, commands, 4u, order, 4u);
    ki_td_soft_renderer_destroy(&renderer);
}

static void test_invalid_draw_transactions(void)
{
    static const uint8_t pixels[] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u
    };
    static const uint32_t cells[] = {0u};
    ki_td_soft_renderer renderer = {0};
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 2, 1);
    ki_td_view view = {.scale = 1.0f};
    ki_td_fit_spec spec;
    ki_td_view fitted = {
        .logical_width = 7, .logical_height = 9, .scale = 1.0f,
        .origin_x = 11, .origin_y = 13
    };
    ki_td_view original = fitted;
    ki_td_tile_batch batch = {
        &image, cells, 1u, 1u, 1u, 2u, 1u, UINT32_MAX,
        0.0f, 0.0f, 2, 1, 1.0f
    };
    uint32_t unchanged[16 * 16];
    float logical = 17.0f;

    EXPECT(ki_td_soft_renderer_init(&renderer, 16, 16));
    ki_td_soft_clear(&renderer, UINT32_C(0x010203));
    memcpy(unchanged, renderer.canvas.px, sizeof unchanged);

    ki_td_soft_fill_rect(&renderer, &view, NAN, 2.0f, 3.0f, 3.0f,
                         UINT32_C(0xff0000), 1.0f);
    ki_td_soft_fill_circle(&renderer, &view, 2.0f, INFINITY, 1.0f,
                           UINT32_C(0xff0000), 1.0f);
    ki_td_soft_fill_ellipse_px(&renderer, 2.0f, 2.0f, NAN, 1.0f,
                               UINT32_C(0xff0000), 1.0f);
    ki_td_soft_line_px(&renderer, NAN, 0.0f, 1.0f, 1.0f, 1.0f,
                       UINT32_C(0xff0000), 1.0f);
    ki_td_soft_rgba_px(&renderer, 0, 0, &image, NAN);
    ki_td_soft_rgba_px(&renderer, INT_MAX, 0, &image, 1.0f);
    ki_td_soft_rgba_resized(&renderer, &view, NAN, 0.0f, &image,
                            2, 1, 1.0f);
    ki_td_soft_rgba_rotated(&renderer, &view, 0.0f, INFINITY, &image,
                            1, 1.0f);
    view.scale = FLT_MAX;
    ki_td_soft_rgba_pixel_art(&renderer, &view, 0.0f, 0.0f, &image, 1.0f);
    batch.alpha = NAN;
    ki_td_soft_tile_batch(&renderer, &view, &batch);
    EXPECT(memcmp(unchanged, renderer.canvas.px, sizeof unchanged) == 0);

    view = (ki_td_view){.scale = 1.0f};
    ki_td_soft_fill_rect_px(&renderer, 1.0f, 1.0f, 2.0f, 2.0f,
                            UINT32_C(0xff0000), FLT_MAX);
    EXPECT(renderer.canvas.px[1 + renderer.canvas.w] ==
           UINT32_C(0xffff0000));

    EXPECT(!ki_td_screen_to_logical(
        &view, 2.0f, 3.0f, &logical, &logical));
    EXPECT(logical == 17.0f);
    EXPECT(ki_td_fit_spec_init(&spec, 1, 1, 16, 16));
    spec.align_bounds = (ki_td_rect){INT_MAX, 0, 1, 1};
    EXPECT(!ki_td_view_fit(&fitted, &spec));
    EXPECT(memcmp(&fitted, &original, sizeof fitted) == 0);
    EXPECT(ki_td_fit_spec_init(&spec, 1, 1, INT_MAX, INT_MAX));
    EXPECT(!ki_td_view_fit(&fitted, &spec));
    EXPECT(memcmp(&fitted, &original, sizeof fitted) == 0);

    batch.columns = (uint32_t)INT_MAX + 1u;
    EXPECT(!ki_td_tile_batch_is_valid(&batch));
    ki_td_soft_renderer_destroy(&renderer);
}

static int reference_sprite_compare(
    const ki_td_sprite_command *commands, size_t first, size_t second)
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

static void reference_sprite_order(
    const ki_td_sprite_command *commands, size_t count, size_t *order)
{
    size_t index;
    for (index = 0u; index < count; ++index) {
        size_t cursor = index;
        order[index] = index;
        while (cursor > 0u &&
               reference_sprite_compare(
                   commands, order[cursor], order[cursor - 1u]) < 0) {
            size_t swap = order[cursor];
            order[cursor] = order[cursor - 1u];
            order[cursor - 1u] = swap;
            --cursor;
        }
    }
}

static uint32_t random_u32(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static float random_float_bits(uint32_t *state)
{
    uint32_t bits = random_u32(state);
    float result;
    memcpy(&result, &bits, sizeof result);
    return result;
}

static void test_sprite_order_model(void)
{
    enum { MAX_COMMANDS = 96, LARGE_COMMANDS = 8192 };
    ki_td_sprite_command commands[MAX_COMMANDS];
    size_t actual[MAX_COMMANDS];
    size_t expected[MAX_COMMANDS];
    uint32_t state = UINT32_C(0x17c4b92d);
    size_t iteration;
    static ki_td_sprite_command large[LARGE_COMMANDS];
    static size_t large_order[LARGE_COMMANDS];

    for (iteration = 0u; iteration < 4000u; ++iteration) {
        size_t count = (size_t)(random_u32(&state) % MAX_COMMANDS) + 1u;
        size_t index;
        for (index = 0u; index < count; ++index) {
            commands[index] = (ki_td_sprite_command){0};
            commands[index].layer =
                (int)(random_u32(&state) % 9u) - 4;
            commands[index].sort_y =
                random_u32(&state) % 17u == 0u ? NAN :
                (float)((int)(random_u32(&state) % 33u) - 16) * 0.25f;
            commands[index].order = random_u32(&state) % 13u;
        }
        reference_sprite_order(commands, count, expected);
        EXPECT(ki_td_sprite_order(commands, count, actual, count) == count);
        EXPECT(memcmp(actual, expected, count * sizeof actual[0]) == 0);
    }

    for (iteration = 0u; iteration < LARGE_COMMANDS; ++iteration) {
        large[iteration] = (ki_td_sprite_command){0};
        large[iteration].layer = LARGE_COMMANDS - (int)iteration;
    }
    EXPECT(ki_td_sprite_order(
               large, LARGE_COMMANDS, large_order, LARGE_COMMANDS) ==
           LARGE_COMMANDS);
    for (iteration = 0u; iteration < LARGE_COMMANDS; ++iteration)
        EXPECT(large_order[iteration] == LARGE_COMMANDS - 1u - iteration);

    {
        union {
            ki_td_sprite_command commands[4];
            size_t scratch[
                (sizeof(ki_td_sprite_command) * 4u + sizeof(size_t) - 1u) /
                sizeof(size_t)
            ];
        } overlap = {0};
        unsigned char before[sizeof overlap];
        for (iteration = 0u; iteration < 4u; ++iteration)
            overlap.commands[iteration].layer = (int)iteration;
        memcpy(before, &overlap, sizeof before);
        EXPECT(ki_td_sprite_order(
                   overlap.commands, 4u, overlap.scratch, 4u) == 0u);
        EXPECT(memcmp(before, &overlap, sizeof before) == 0);
    }
}

static void test_culling_equivalence(void)
{
    static const uint8_t pixels[] = {
        240, 20, 40, 255, 30, 220, 70, 128,
        10, 30, 250, 7, 250, 190, 20, 192
    };
    static uint32_t cells[64u * 64u];
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 2, 2);
    ki_td_soft_renderer optimized = {0};
    ki_td_soft_renderer reference = {0};
    uint32_t state = UINT32_C(0xa691d4e3);
    size_t iteration;

    EXPECT(ki_td_soft_renderer_init(&optimized, 32, 24));
    EXPECT(ki_td_soft_renderer_init(&reference, 32, 24));
    for (iteration = 0u; iteration < 1200u; ++iteration) {
        static const float scales[] = {
            0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.99f, 2.0f, 3.0f
        };
        ki_td_view view = {
            .scale = scales[random_u32(&state) %
                            (sizeof scales / sizeof scales[0])],
            .origin_x = (int)(random_u32(&state) % 31u) - 15,
            .origin_y = (int)(random_u32(&state) % 25u) - 12,
            .offset_x = (int)(random_u32(&state) % 9u) - 4,
            .offset_y = (int)(random_u32(&state) % 9u) - 4
        };
        float x = (float)((int)(random_u32(&state) % 81u) - 40) * 0.25f;
        float y = (float)((int)(random_u32(&state) % 65u) - 32) * 0.25f;
        int width = (int)(random_u32(&state) % 12u) + 1;
        int height = (int)(random_u32(&state) % 10u) + 1;
        float alpha = (float)(random_u32(&state) % 101u) * 0.01f;
        if (alpha == 0.0f) alpha = 0.01f;
        ki_td_soft_clear(&optimized, UINT32_C(0x172033));
        ki_td_soft_clear(&reference, UINT32_C(0x172033));
        ki_td_soft_rgba_resized(
            &optimized, &view, x, y, &image, width, height, alpha);
        reference_fractional_resize(
            &reference, &view, x, y, &image, width, height, alpha);
        EXPECT(memcmp(
            optimized.canvas.px, reference.canvas.px,
            (size_t)optimized.canvas.w * (size_t)optimized.canvas.h *
            sizeof optimized.canvas.px[0]) == 0);
    }

    for (iteration = 0u; iteration < 64u * 64u; ++iteration)
        cells[iteration] = 0u;
    {
        static const uint8_t atlas_pixel[] = {80u, 160u, 240u, 255u};
        ki_td_rgba8 atlas = ki_td_rgba8_make(atlas_pixel, 1, 1);
        ki_td_view view = {.scale = 1.0f};
        ki_td_tile_batch batch = {
            &atlas, cells, 64u * 64u, 64u, 64u, 1u, 1u, UINT32_MAX,
            -200.0f, -200.0f, 4, 4, 0.75f
        };
        int row;
        ki_td_soft_clear(&optimized, UINT32_C(0x172033));
        ki_td_soft_clear(&reference, UINT32_C(0x172033));
        ki_td_soft_tile_batch(&optimized, &view, &batch);
        for (row = 0; row < 64; ++row) {
            int column;
            for (column = 0; column < 64; ++column)
                ki_td_soft_rgba_resized(
                    &reference, &view, -200.0f + (float)column * 4.0f,
                    -200.0f + (float)row * 4.0f, &atlas, 4, 4, 0.75f);
        }
        EXPECT(memcmp(
            optimized.canvas.px, reference.canvas.px,
            (size_t)optimized.canvas.w * (size_t)optimized.canvas.h *
            sizeof optimized.canvas.px[0]) == 0);
    }
    ki_td_soft_renderer_destroy(&reference);
    ki_td_soft_renderer_destroy(&optimized);
}

static void test_randomized_render_safety(void)
{
    static const uint8_t pixels[] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 128u,
        0u, 0u, 255u, 7u, 255u, 255u, 255u, 255u
    };
    static const uint32_t cells[] = {0u, 1u, 2u, 3u};
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 2, 2);
    ki_td_nine_slice slice;
    ki_td_tile_batch batch = {
        &image, cells, 4u, 2u, 2u, 2u, 2u, UINT32_MAX,
        0.0f, 0.0f, 1, 1, 1.0f
    };
    ki_td_soft_renderer renderer = {0};
    ki_td_sprite_command command = {
        &image, 0.0f, 0.0f, 0, 0, 1.0f, 0, 0.0f, 0u
    };
    size_t scratch;
    uint32_t state = UINT32_C(0x4b11f073);
    size_t iteration;

    EXPECT(ki_td_soft_renderer_init(&renderer, 24, 20));
    EXPECT(!ki_td_nine_slice_init(&slice, &image, 1, 1, 1, 1));
    for (iteration = 0u; iteration < 5000u; ++iteration) {
        ki_td_view view = {
            .scale = random_float_bits(&state),
            .origin_x = (int)random_u32(&state),
            .origin_y = (int)random_u32(&state),
            .offset_x = (int)random_u32(&state),
            .offset_y = (int)random_u32(&state)
        };
        float x = random_float_bits(&state);
        float y = random_float_bits(&state);
        float width = random_float_bits(&state);
        float height = random_float_bits(&state);
        float alpha = random_float_bits(&state);
        command.x = x;
        command.y = y;
        command.alpha = alpha;
        command.sort_y = random_float_bits(&state);
        batch.x = x;
        batch.y = y;
        batch.alpha = alpha;
        ki_td_soft_fill_rect_px(
            &renderer, x, y, width, height, random_u32(&state), alpha);
        ki_td_soft_fill_circle(
            &renderer, &view, x, y, width, random_u32(&state), alpha);
        ki_td_soft_fill_ellipse(
            &renderer, &view, x, y, width, height,
            random_u32(&state), alpha);
        ki_td_soft_line(
            &renderer, &view, x, y, width, height, 1.0f,
            random_u32(&state), alpha);
        ki_td_soft_rgba_px(
            &renderer, (int)random_u32(&state), (int)random_u32(&state),
            &image, alpha);
        ki_td_soft_rgba_resized(
            &renderer, &view, x, y, &image,
            (int)(random_u32(&state) % 8u),
            (int)(random_u32(&state) % 8u), alpha);
        ki_td_soft_rgba_rotated(
            &renderer, &view, x, y, &image, (int)random_u32(&state), alpha);
        ki_td_soft_rgba_pixel_art(
            &renderer, &view, x, y, &image, alpha);
        ki_td_soft_tile_batch(&renderer, &view, &batch);
        ki_td_soft_sprite_layers(
            &renderer, &view, &command, 1u, &scratch, 1u);
    }
    EXPECT(ki_td_soft_pack_rgba(&renderer) != NULL);
    ki_td_soft_renderer_destroy(&renderer);
}

int main(void)
{
    static const struct {
        const char *name;
        void (*run)(void);
    } tests[] = {
        {"fit and transform", test_fit_and_transform},
        {"deterministic shake", test_shake},
        {"extreme numeric inputs", test_extreme_numeric_inputs},
        {"renderer lifetime", test_renderer_lifetime},
        {"adapter golden", test_adapter},
        {"fractional resize equivalence", test_fractional_resize_equivalence},
        {"tinted resize", test_tinted_resize},
        {"atlas and layers", test_atlas_and_layers},
        {"invalid draw transactions", test_invalid_draw_transactions},
        {"sprite ordering model", test_sprite_order_model},
        {"culling equivalence", test_culling_equivalence},
        {"randomized render safety", test_randomized_render_safety}
    };
    size_t index;
    for (index = 0u; index < sizeof tests / sizeof tests[0]; ++index) {
        int before = failures;
        tests[index].run();
        if (failures == before)
            (void)printf("PASS %s\n", tests[index].name);
    }
    if (failures != 0) {
        fprintf(stderr, "FAIL: %d top-down checks\n", failures);
        return 1;
    }
    (void)printf("PASS: all %zu kilix-top-down suites\n",
                 sizeof tests / sizeof tests[0]);
    return 0;
}
