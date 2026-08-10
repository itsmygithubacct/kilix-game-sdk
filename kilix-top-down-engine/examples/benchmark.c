#define _POSIX_C_SOURCE 200809L

#include "kilix_top_down.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LARGE_IMAGE_WIDTH 1024
#define LARGE_IMAGE_HEIGHT 1024
#define TILE_COLUMNS 512u
#define TILE_ROWS 512u
#define LARGE_SPRITES 8192u
#define SMALL_SPRITES 128u

static uint8_t atlas_pixels[16u * 16u * 4u];
static uint8_t small_pixels[64u * 64u * 4u];

static uint64_t now_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0) return 0u;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static uint64_t hash_mix(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static uint64_t canvas_hash(const sr_canvas *canvas)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t count = (size_t)canvas->w * (size_t)canvas->h;
    size_t index;
    for (index = 0u; index < count; ++index)
        hash = hash_mix(hash, canvas->px[index]);
    return hash;
}

static uint64_t index_hash(const size_t *indices, size_t count)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t index;
    for (index = 0u; index < count; ++index)
        hash = hash_mix(hash, (uint64_t)indices[index]);
    return hash;
}

static void print_metric(const char *name, uint64_t elapsed, size_t rounds,
                         uint64_t checksum)
{
    (void)printf("%s %.3f checksum=%" PRIu64 "\n", name,
                 (double)elapsed / (double)rounds, checksum);
}

static uint32_t random_u32(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void fill_image(uint8_t *pixels, size_t width, size_t height)
{
    size_t y;
    for (y = 0u; y < height; ++y) {
        size_t x;
        for (x = 0u; x < width; ++x) {
            size_t offset = (y * width + x) * 4u;
            pixels[offset] = (uint8_t)((x * 13u + y * 3u) & 255u);
            pixels[offset + 1u] =
                (uint8_t)((x * 5u + y * 17u) & 255u);
            pixels[offset + 2u] =
                (uint8_t)((x * 19u + y * 7u) & 255u);
            pixels[offset + 3u] =
                (uint8_t)(32u + ((x * 11u + y * 23u) % 224u));
        }
    }
}

static void benchmark_view_transforms(void)
{
    const size_t rounds = 2000000u;
    ki_td_view view = {
        .logical_width = 320,
        .logical_height = 180,
        .scale = 2.75f,
        .origin_x = 31,
        .origin_y = 17,
        .offset_x = -4,
        .offset_y = 9
    };
    uint64_t checksum = UINT64_C(14695981039346656037);
    uint64_t started = now_ns();
    size_t index;
    for (index = 0u; index < rounds; ++index) {
        float x = (float)(index % 640u) - 160.0f;
        float y = (float)((index * 7u) % 360u) - 90.0f;
        checksum = hash_mix(
            checksum, (uint64_t)(uint32_t)ki_td_screen_x(&view, x));
        checksum = hash_mix(
            checksum, (uint64_t)(uint32_t)ki_td_screen_y(&view, y));
    }
    print_metric("view-transform-ns-pair", now_ns() - started, rounds,
                 checksum);
}

static void benchmark_visible_cells(void)
{
    const size_t rounds = 500000u;
    ki_td_view view = {
        .logical_width = 320,
        .logical_height = 180,
        .scale = 2.75f,
        .origin_x = 31,
        .origin_y = 17
    };
    ki_td_cell_bounds bounds;
    uint64_t checksum = UINT64_C(14695981039346656037);
    uint64_t started = now_ns();
    size_t index;
    for (index = 0u; index < rounds; ++index) {
        view.offset_x = (int)(index % 97u) - 48;
        view.offset_y = (int)((index * 3u) % 83u) - 41;
        if (!ki_td_view_visible_cells(
                &view, (ki_td_rect){0, 0, 960, 540}, -64.0f, -32.0f,
                16, 16, 512, 512, 1, &bounds))
            exit(EXIT_FAILURE);
        checksum = hash_mix(checksum,
                            (uint64_t)(uint32_t)bounds.first_column);
        checksum = hash_mix(checksum,
                            (uint64_t)(uint32_t)bounds.first_row);
        checksum = hash_mix(checksum,
                            (uint64_t)(uint32_t)bounds.column_count);
        checksum = hash_mix(checksum,
                            (uint64_t)(uint32_t)bounds.row_count);
    }
    print_metric("visible-cells-ns-call", now_ns() - started, rounds,
                 checksum);
}

static bool benchmark_frame(ki_td_soft_renderer *renderer)
{
    const size_t rounds = 240u;
    ki_td_fit_spec spec;
    ki_td_view view;
    uint64_t started;
    size_t frame;
    if (!ki_td_soft_renderer_resize(renderer, 960, 540) ||
        !ki_td_fit_spec_init(&spec, 256, 176, 960, 540) ||
        !ki_td_view_fit(&view, &spec))
        return false;
    started = now_ns();
    for (frame = 0u; frame < rounds; ++frame) {
        int y;
        ki_td_soft_clear(renderer, UINT32_C(0x05060a));
        for (y = 0; y < 11; ++y) {
            int x;
            for (x = 0; x < 16; ++x)
                ki_td_soft_fill_rect(
                    renderer, &view, (float)(x * 16), (float)(y * 16),
                    16.0f, 16.0f,
                    ((x + y + (int)frame) & 1) != 0 ?
                        UINT32_C(0x315b36) : UINT32_C(0x3d7042),
                    1.0f);
        }
        if (!ki_td_soft_pack_rgba(renderer)) return false;
    }
    print_metric("frame-960x540-ns-frame", now_ns() - started, rounds,
                 canvas_hash(&renderer->canvas));
    return true;
}

static void benchmark_offscreen_image(ki_td_soft_renderer *renderer,
                                      const ki_td_rgba8 *image)
{
    const size_t rounds = 16u;
    uint64_t started;
    size_t round;
    ki_td_soft_clear(renderer, UINT32_C(0x010203));
    started = now_ns();
    for (round = 0u; round < rounds; ++round)
        ki_td_soft_rgba_px(renderer, 1000, 1000, image, 0.75f);
    print_metric("rgba-offscreen-1024x1024-ns-call", now_ns() - started,
                 rounds, canvas_hash(&renderer->canvas));
}

static void benchmark_clipped_resize(ki_td_soft_renderer *renderer,
                                     const ki_td_rgba8 *image)
{
    const size_t rounds = 4u;
    const ki_td_view view = {
        .logical_width = 160, .logical_height = 90, .scale = 1.0f
    };
    uint64_t started;
    size_t round;
    ki_td_soft_clear(renderer, UINT32_C(0x010203));
    started = now_ns();
    for (round = 0u; round < rounds; ++round)
        ki_td_soft_rgba_resized(renderer, &view, -1000.0f, -1000.0f,
                                image, 2048, 2048, 0.5f);
    print_metric("rgba-resized-clipped-2048x2048-ns-call",
                 now_ns() - started, rounds, canvas_hash(&renderer->canvas));
}

static void benchmark_tile_batch(ki_td_soft_renderer *renderer,
                                 const ki_td_rgba8 *atlas,
                                 const uint32_t *cells)
{
    const size_t rounds = 2u;
    const ki_td_view view = {
        .logical_width = 160, .logical_height = 90, .scale = 1.0f
    };
    ki_td_tile_batch batch = {
        .atlas = atlas,
        .cells = cells,
        .cell_count = (size_t)TILE_COLUMNS * TILE_ROWS,
        .columns = TILE_COLUMNS,
        .rows = TILE_ROWS,
        .atlas_columns = 2u,
        .atlas_rows = 2u,
        .empty_cell = UINT32_MAX,
        .x = -2048.0f,
        .y = -2048.0f,
        .tile_width = 8,
        .tile_height = 8,
        .alpha = 0.5f
    };
    uint64_t started;
    size_t round;
    ki_td_soft_clear(renderer, UINT32_C(0x010203));
    started = now_ns();
    for (round = 0u; round < rounds; ++round)
        ki_td_soft_tile_batch(renderer, &view, &batch);
    print_metric("tile-batch-clipped-512x512-ns-call", now_ns() - started,
                 rounds, canvas_hash(&renderer->canvas));
}

static bool benchmark_sprite_order(const char *name,
                                   const ki_td_sprite_command *commands,
                                   size_t count, size_t rounds)
{
    size_t *scratch = malloc(count * sizeof scratch[0]);
    uint64_t started;
    size_t round;
    if (!scratch) return false;
    started = now_ns();
    for (round = 0u; round < rounds; ++round) {
        if (ki_td_sprite_order(commands, count, scratch, count) != count) {
            free(scratch);
            return false;
        }
    }
    print_metric(name, now_ns() - started, rounds,
                 index_hash(scratch, count));
    free(scratch);
    return true;
}

static void prepare_sorted(ki_td_sprite_command *commands, size_t count)
{
    size_t index;
    for (index = 0u; index < count; ++index) {
        commands[index] = (ki_td_sprite_command){
            .sort_y = (float)index,
            .order = (uint32_t)index
        };
    }
}

static void prepare_reverse(ki_td_sprite_command *commands, size_t count)
{
    size_t index;
    for (index = 0u; index < count; ++index) {
        commands[index] = (ki_td_sprite_command){
            .sort_y = (float)(count - index),
            .order = (uint32_t)index
        };
    }
}

static void prepare_random(ki_td_sprite_command *commands, size_t count,
                           uint32_t seed)
{
    size_t index;
    for (index = 0u; index < count; ++index) {
        uint32_t layer = random_u32(&seed);
        uint32_t sort_y = random_u32(&seed);
        uint32_t order = random_u32(&seed);
        commands[index] = (ki_td_sprite_command){
            .layer = (int)(layer % 17u) - 8,
            .sort_y = (float)(sort_y % 4096u),
            .order = order
        };
    }
}

int main(void)
{
    const size_t large_image_bytes =
        (size_t)LARGE_IMAGE_WIDTH * LARGE_IMAGE_HEIGHT * 4u;
    const size_t tile_count = (size_t)TILE_COLUMNS * TILE_ROWS;
    ki_td_soft_renderer renderer = {0};
    ki_td_sprite_command *commands =
        malloc(LARGE_SPRITES * sizeof commands[0]);
    uint8_t *large_pixels = malloc(large_image_bytes);
    uint32_t *cells = malloc(tile_count * sizeof cells[0]);
    ki_td_rgba8 large_image;
    ki_td_rgba8 small_image;
    ki_td_rgba8 atlas;
    size_t index;
    bool ok = commands && large_pixels && cells;

    if (!ok) goto done;
    fill_image(large_pixels, LARGE_IMAGE_WIDTH, LARGE_IMAGE_HEIGHT);
    fill_image(small_pixels, 64u, 64u);
    fill_image(atlas_pixels, 16u, 16u);
    for (index = 0u; index < tile_count; ++index)
        cells[index] = (uint32_t)(index % 4u);
    large_image = ki_td_rgba8_make(
        large_pixels, LARGE_IMAGE_WIDTH, LARGE_IMAGE_HEIGHT);
    small_image = ki_td_rgba8_make(small_pixels, 64, 64);
    atlas = ki_td_rgba8_make(atlas_pixels, 16, 16);
    ok = ki_td_rgba8_is_valid(&large_image) &&
         ki_td_rgba8_is_valid(&small_image) &&
         ki_td_rgba8_is_valid(&atlas) &&
         ki_td_soft_renderer_init(&renderer, 960, 540);
    if (!ok) goto done;

    benchmark_view_transforms();
    benchmark_visible_cells();
    if (!benchmark_frame(&renderer) ||
        !ki_td_soft_renderer_resize(&renderer, 160, 90)) {
        ok = false;
        goto done;
    }
    benchmark_offscreen_image(&renderer, &large_image);
    benchmark_clipped_resize(&renderer, &small_image);
    benchmark_tile_batch(&renderer, &atlas, cells);

    prepare_sorted(commands, LARGE_SPRITES);
    ok = benchmark_sprite_order(
        "sprite-order-sorted-8192-ns-call", commands, LARGE_SPRITES, 100u);
    if (!ok) goto done;
    prepare_reverse(commands, LARGE_SPRITES);
    ok = benchmark_sprite_order(
        "sprite-order-reverse-8192-ns-call", commands, LARGE_SPRITES, 2u);
    if (!ok) goto done;
    prepare_random(commands, LARGE_SPRITES, UINT32_C(0x8c21d739));
    ok = benchmark_sprite_order(
        "sprite-order-random-8192-ns-call", commands, LARGE_SPRITES, 2u);
    if (!ok) goto done;
    prepare_random(commands, SMALL_SPRITES, UINT32_C(0x517cc1b7));
    ok = benchmark_sprite_order(
        "sprite-order-random-128-ns-call", commands, SMALL_SPRITES, 1000u);

done:
    ki_td_soft_renderer_destroy(&renderer);
    free(cells);
    free(large_pixels);
    free(commands);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
