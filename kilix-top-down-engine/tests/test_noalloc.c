#include "kilix_top_down.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void __wrap_free(void *pointer);

static int watch_allocations;
static size_t allocation_count;

void *__wrap_malloc(size_t size)
{
    if (watch_allocations) allocation_count++;
    return __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    if (watch_allocations) allocation_count++;
    return __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
    if (watch_allocations) allocation_count++;
    return __real_realloc(pointer, size);
}

void __wrap_free(void *pointer)
{
    if (watch_allocations) allocation_count++;
    __real_free(pointer);
}

int main(void)
{
    static const unsigned char pixels[] = {
        255, 0, 0, 255, 0, 255, 0, 128,
        0, 0, 255, 255, 255, 255, 255, 0
    };
    static const unsigned char panel_pixels[36] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 80, 90, 100, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
    };
    static const uint32_t cells[4] = {0u, 1u, 2u, 3u};
    ki_td_soft_renderer renderer = {0};
    ki_td_fit_spec spec;
    ki_td_view view;
    ki_td_rgba8 sprite = ki_td_rgba8_make(pixels, 2, 2);
    ki_td_rgba8 panel = ki_td_rgba8_make(panel_pixels, 3, 3);
    ki_td_nine_slice slice;
    ki_td_tile_batch batch = {
        &sprite, cells, 4u, 2u, 2u, 2u, 2u, UINT32_MAX,
        24.0f, 20.0f, 2, 2, 1.0f
    };
    ki_td_sprite_command commands[2] = {
        {&sprite, 32, 20, 4, 4, 1, 2, 22, 0},
        {&sprite, 28, 18, 4, 4, 1, 1, 20, 0}
    };
    size_t order[2];
    if (!ki_td_soft_renderer_init(&renderer, 320, 240) ||
        !ki_td_fit_spec_init(&spec, 64, 48, 320, 240) ||
        !ki_td_view_fit(&view, &spec) ||
        !ki_td_nine_slice_init(&slice, &panel, 1, 1, 1, 1))
        return 1;

    watch_allocations = 1;
    for (int i = 0; i < 20; i++) {
        ki_td_view_set_offset(&view, i % 3, -(i % 2));
        ki_td_soft_clear(&renderer, 0x05060a);
        ki_td_soft_fill_rect(&renderer, &view, 2, 2, 20, 10,
                             0x74c0fc, 0.8f);
        ki_td_soft_fill_circle(&renderer, &view, 20, 20, 6,
                               0xffd166, 1.0f);
        ki_td_soft_rgba_pixel_art(&renderer, &view, 10, 12, &sprite, 1.0f);
        ki_td_soft_nine_slice(&renderer, &view, 40, 4, 12, 8, &slice, 1.0f);
        ki_td_soft_tile_batch(&renderer, &view, &batch);
        ki_td_soft_sprite_layers(&renderer, &view, commands, 2u, order, 2u);
        if (!ki_td_soft_pack_rgba(&renderer)) return 1;
    }
    watch_allocations = 0;
    ki_td_soft_renderer_destroy(&renderer);
    if (allocation_count != 0) {
        fprintf(stderr, "FAIL: rendering performed %zu allocation operations\n",
                allocation_count);
        return 1;
    }
    printf("PASS: initialized render path performs no allocation\n");
    return 0;
}
