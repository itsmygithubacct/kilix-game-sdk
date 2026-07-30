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

static bool test_rpg_composites(void)
{
    static const kilix_ui_party_member party[] = {
        {"Arden", "Lv 4", "Ready", "HP", 38.0f, 42.0f,
         "MP", 8.0f, 12.0f, true},
        {"Mira", "Lv 3", "Silenced", "HP", 25.0f, 31.0f,
         "MP", 19.0f, 24.0f, true}
    };
    static const kilix_ui_inventory_item inventory[] = {
        {"Tonic", "Restores vitality.", 3, false, true},
        {"Bronze Blade", "Equipped by Arden.", 1, true, true}
    };
    static const kilix_ui_command commands[] = {
        {"1", "Attack", "", true}, {"2", "Spell", "4 MP", true}
    };
    static const kilix_ui_target targets[] = {
        {"Glass Wisp", "Marked", 17.0f, 22.0f, true},
        {"Root Eye", "Asleep", 9.0f, 30.0f, false}
    };
    static const kilix_ui_shop_item shop[] = {
        {"Tonic", 12, 3, true}, {"Moon Charm", 40, 0, false}
    };
    ki_td_soft_renderer renderer = {0};
    ki_td_view view = {.logical_width = 480, .logical_height = 300,
                       .scale = 1.0f};
    kilix_ui_style style;
    kilix_ui_focus focus;
    uint8_t *rgba;
    uint64_t hash;
    CHECK(ki_td_soft_renderer_init(&renderer, 480, 300));
    kilix_ui_style_init(&style);
    kilix_ui_focus_init(&focus, 2u, 2u);
    ki_td_soft_clear(&renderer, UINT32_C(0x05070c));
    kilix_ui_draw_party(&renderer, &view, (ki_td_rect){4, 4, 230, 82},
                        &style, NULL, &focus, party, 2u);
    kilix_ui_draw_inventory(&renderer, &view,
                            (ki_td_rect){242, 4, 234, 82},
                            &style, NULL, &focus, inventory, 2u);
    kilix_ui_draw_commands(&renderer, &view,
                           (ki_td_rect){4, 94, 230, 48},
                           &style, NULL, &focus, commands, 2u);
    kilix_ui_draw_targets(&renderer, &view,
                          (ki_td_rect){242, 94, 234, 82},
                          &style, NULL, &focus, targets, 2u);
    kilix_ui_draw_shop(&renderer, &view, (ki_td_rect){4, 184, 472, 72},
                       &style, NULL, &focus, shop, 2u, "Gil", 27);
    rgba = ki_td_soft_pack_rgba(&renderer);
    CHECK(rgba != NULL);
    hash = hash_bytes(rgba, renderer.rgba_size);
    CHECK(hash != 0u && hash != UINT64_MAX);
    CHECK(renderer.canvas.px[8 + 8 * renderer.canvas.w] !=
          UINT32_C(0xff05070c));
    ki_td_soft_renderer_destroy(&renderer);
    return true;
}

int main(void)
{
    if (!test_focus() || !test_drawing() || !test_rpg_composites())
        return EXIT_FAILURE;
    (void)puts("PASS kilix-ui focus dialogue meters and RPG composites");
    return EXIT_SUCCESS;
}
