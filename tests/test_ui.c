#include "kilix_ui.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                     \
        (void)fprintf(stderr, "%s:%d: check failed: %s\n",               \
                      __FILE__, __LINE__, #condition);                      \
        return false;                                                       \
    }                                                                       \
} while (false)

static uint64_t hash_bytes(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t index;
    for (index = 0u; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool test_focus(void)
{
    static const bool enabled[5] = {true, false, true, true, false};
    kilix_ui_focus focus;
    kilix_ui_focus_init(&focus, 5u, 2u);
    CHECK(focus.selected == 0u && focus.first_visible == 0u);
    CHECK(kilix_ui_focus_apply(&focus, KILIX_UI_ACTION_DOWN, enabled));
    CHECK(focus.selected == 2u && focus.first_visible == 1u);
    CHECK(kilix_ui_focus_apply(&focus, KILIX_UI_ACTION_END, enabled));
    CHECK(focus.selected == 3u);
    CHECK(kilix_ui_focus_apply(&focus, KILIX_UI_ACTION_DOWN, enabled));
    CHECK(focus.selected == 0u);
    CHECK(kilix_ui_focus_accepts(&focus, KILIX_UI_ACTION_ACCEPT, enabled));
    (void)kilix_ui_focus_set_items(&focus, 3u, enabled);
    CHECK(focus.item_count == 3u && focus.selected == 0u);
    return true;
}

static bool test_drawing(void)
{
    static const uint8_t panel_pixels[36] = {
        20, 40, 80, 255, 30, 60, 100, 255, 20, 40, 80, 255,
        30, 60, 100, 255, 8, 16, 32, 255, 30, 60, 100, 255,
        20, 40, 80, 255, 30, 60, 100, 255, 20, 40, 80, 255
    };
    static const uint8_t portrait_pixels[16] = {
        230, 160, 80, 255, 190, 100, 60, 255,
        90, 180, 220, 255, 240, 220, 170, 255
    };
    static const char *const items[] = {"Inventory", "PSI", "Status"};
    static const bool enabled[] = {true, false, true};
    static const char *const lines[] = {"The road is open.", "Keep moving."};
    static const kilix_ui_prompt prompts[] = {
        {"Enter", "Choose", true}, {"Esc", "Back", true}
    };
    ki_td_rgba8 panel = ki_td_rgba8_make(panel_pixels, 3, 3);
    ki_td_rgba8 portrait = ki_td_rgba8_make(portrait_pixels, 2, 2);
    ki_td_nine_slice slice;
    ki_td_soft_renderer renderer = {0};
    ki_td_view view = {.logical_width = 320, .logical_height = 180,
                       .scale = 1.0f};
    kilix_ui_style style;
    kilix_ui_focus focus;
    uint8_t *rgba;
    uint64_t hash;
    CHECK(ki_td_nine_slice_init(&slice, &panel, 1, 1, 1, 1));
    CHECK(ki_td_soft_renderer_init(&renderer, 320, 180));
    kilix_ui_style_init(&style);
    kilix_ui_focus_init(&focus, 3u, 3u);
    ki_td_soft_clear(&renderer, UINT32_C(0x080b12));
    kilix_ui_draw_list(&renderer, &view, (ki_td_rect){8, 8, 116, 72},
                       &style, &slice, &focus, items, enabled, 3u);
    kilix_ui_draw_dialogue(&renderer, &view,
                           (ki_td_rect){8, 92, 304, 80}, &style, NULL,
                           &portrait, "Mira", lines, 2u, "Enter: continue");
    kilix_ui_draw_meter(&renderer, &view, (ki_td_rect){140, 12, 160, 20},
                        &style, 37.0f, 50.0f, "HP");
    kilix_ui_draw_prompts(&renderer, &view, 140, 44, 160, &style,
                          prompts, 2u);
    rgba = ki_td_soft_pack_rgba(&renderer);
    CHECK(rgba != NULL);
    hash = hash_bytes(rgba, renderer.rgba_size);
    CHECK(hash != 0u && hash != UINT64_MAX);
    CHECK(renderer.canvas.px[8 + 8 * renderer.canvas.w] !=
          UINT32_C(0xff080b12));
    CHECK(renderer.canvas.px[150 + 20 * renderer.canvas.w] !=
          UINT32_C(0xff080b12));
    ki_td_soft_renderer_destroy(&renderer);
    return true;
}

int main(void)
{
    if (!test_focus() || !test_drawing()) return EXIT_FAILURE;
    (void)puts("PASS kilix-ui focus list dialogue portrait meter prompts");
    return EXIT_SUCCESS;
}
