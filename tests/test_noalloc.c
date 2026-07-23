#include "kilix_ui.h"

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

static int watching;
static size_t operations;

void *__wrap_malloc(size_t size)
{
    if (watching) ++operations;
    return __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    if (watching) ++operations;
    return __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
    if (watching) ++operations;
    return __real_realloc(pointer, size);
}

void __wrap_free(void *pointer)
{
    if (watching) ++operations;
    __real_free(pointer);
}

int main(void)
{
    static const uint8_t pixels[36] = {
        30, 50, 80, 255, 30, 50, 80, 255, 30, 50, 80, 255,
        30, 50, 80, 255, 10, 20, 30, 255, 30, 50, 80, 255,
        30, 50, 80, 255, 30, 50, 80, 255, 30, 50, 80, 255
    };
    static const char *const items[2] = {"Items", "Status"};
    static const char *const lines[1] = {"Allocation-free dialogue."};
    static const kilix_ui_prompt prompts[1] = {{"Enter", "Choose", true}};
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 3, 3);
    ki_td_nine_slice skin;
    ki_td_soft_renderer renderer = {0};
    ki_td_view view = {.logical_width = 160, .logical_height = 90,
                       .scale = 2.0f};
    kilix_ui_style style;
    kilix_ui_focus focus;
    if (!ki_td_nine_slice_init(&skin, &image, 1, 1, 1, 1) ||
        !ki_td_soft_renderer_init(&renderer, 320, 180)) return EXIT_FAILURE;
    kilix_ui_style_init(&style);
    kilix_ui_focus_init(&focus, 2u, 2u);
    watching = 1;
    kilix_ui_draw_list(&renderer, &view, (ki_td_rect){2, 2, 60, 42},
                       &style, &skin, &focus, items, NULL, 2u);
    kilix_ui_draw_dialogue(&renderer, &view,
                           (ki_td_rect){2, 48, 154, 40}, &style, NULL,
                           &image, "Guide", lines, 1u, "Enter");
    kilix_ui_draw_meter(&renderer, &view, (ki_td_rect){68, 4, 80, 12},
                        &style, 3.0f, 5.0f, "HP");
    kilix_ui_draw_prompts(&renderer, &view, 68, 22, 80, &style,
                          prompts, 1u);
    watching = 0;
    ki_td_soft_renderer_destroy(&renderer);
    if (operations != 0u) {
        (void)fprintf(stderr, "UI drawing used %zu allocation operations\n",
                      operations);
        return EXIT_FAILURE;
    }
    (void)puts("PASS kilix-ui initialized draw path performs no allocation");
    return EXIT_SUCCESS;
}
