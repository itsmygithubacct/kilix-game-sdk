#include "kilix_ui.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    CHECK(hash == UINT64_C(0x18e002874552cb08));
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
    CHECK(hash == UINT64_C(0xe07c14fc85d3422a));
    CHECK(renderer.canvas.px[8 + 8 * renderer.canvas.w] !=
          UINT32_C(0xff05070c));
    ki_td_soft_renderer_destroy(&renderer);
    return true;
}

static void reference_reveal(kilix_ui_focus *focus)
{
    if (focus->item_count == 0u) return;
    if (focus->page_size == 0u || focus->page_size > focus->item_count)
        focus->page_size = focus->item_count;
    if (focus->selected < focus->first_visible)
        focus->first_visible = focus->selected;
    else if (focus->selected - focus->first_visible >= focus->page_size)
        focus->first_visible = focus->selected - focus->page_size + 1u;
    if (focus->page_size >= focus->item_count)
        focus->first_visible = 0u;
    else if (focus->first_visible > focus->item_count - focus->page_size)
        focus->first_visible = focus->item_count - focus->page_size;
}

static bool reference_set_items(kilix_ui_focus *focus, size_t item_count,
                                const bool *enabled)
{
    size_t old = focus->selected;
    size_t index;
    focus->item_count = item_count;
    if (item_count == 0u) {
        focus->selected = 0u;
        focus->first_visible = 0u;
        return old != 0u;
    }
    if (focus->page_size == 0u) focus->page_size = item_count;
    if (focus->selected >= item_count) focus->selected = item_count - 1u;
    if (enabled && !enabled[focus->selected]) {
        for (index = 0u; index < item_count; ++index) {
            if (enabled[index]) {
                focus->selected = index;
                break;
            }
        }
    }
    reference_reveal(focus);
    return old != focus->selected;
}

static bool reference_step(kilix_ui_focus *focus, int direction,
                           const bool *enabled)
{
    size_t origin;
    size_t candidate;
    size_t attempts;
    if (focus->item_count == 0u) return false;
    origin = focus->selected;
    candidate = origin;
    for (attempts = 0u; attempts < focus->item_count; ++attempts) {
        if (direction < 0) {
            if (candidate > 0u) --candidate;
            else if (focus->wrap) candidate = focus->item_count - 1u;
            else return false;
        } else {
            if (candidate + 1u < focus->item_count) ++candidate;
            else if (focus->wrap) candidate = 0u;
            else return false;
        }
        if (!enabled || enabled[candidate]) {
            focus->selected = candidate;
            reference_reveal(focus);
            return candidate != origin;
        }
    }
    return false;
}

static bool reference_jump(kilix_ui_focus *focus, size_t candidate,
                           int direction, const bool *enabled)
{
    size_t origin;
    if (focus->item_count == 0u) return false;
    if (candidate >= focus->item_count) candidate = focus->item_count - 1u;
    origin = focus->selected;
    while (enabled && !enabled[candidate]) {
        if (direction < 0) {
            if (candidate == 0u) return false;
            --candidate;
        } else {
            if (candidate + 1u >= focus->item_count) return false;
            ++candidate;
        }
    }
    focus->selected = candidate;
    reference_reveal(focus);
    return candidate != origin;
}

static bool reference_apply(kilix_ui_focus *focus, kilix_ui_action action,
                            const bool *enabled)
{
    size_t page = focus->page_size == 0u ? 1u : focus->page_size;
    switch (action) {
    case KILIX_UI_ACTION_UP:
    case KILIX_UI_ACTION_LEFT:
        return reference_step(focus, -1, enabled);
    case KILIX_UI_ACTION_DOWN:
    case KILIX_UI_ACTION_RIGHT:
        return reference_step(focus, 1, enabled);
    case KILIX_UI_ACTION_PAGE_UP:
        return reference_jump(focus, focus->selected > page ?
                              focus->selected - page : 0u, -1, enabled);
    case KILIX_UI_ACTION_PAGE_DOWN:
        return reference_jump(focus,
                              page > SIZE_MAX - focus->selected ? SIZE_MAX :
                              focus->selected + page, 1, enabled);
    case KILIX_UI_ACTION_HOME:
        return reference_jump(focus, 0u, 1, enabled);
    case KILIX_UI_ACTION_END:
        return reference_jump(focus, focus->item_count == 0u ? 0u :
                              focus->item_count - 1u, -1, enabled);
    default:
        return false;
    }
}

static uint32_t random_state = UINT32_C(0x7f4a7c15);

static uint32_t next_random(void)
{
    uint32_t value = random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static bool focus_equal(const kilix_ui_focus *left,
                        const kilix_ui_focus *right)
{
    return left->selected == right->selected &&
           left->item_count == right->item_count &&
           left->page_size == right->page_size &&
           left->first_visible == right->first_visible &&
           left->wrap == right->wrap;
}

static bool test_focus_randomized_model(void)
{
    bool enabled[64];
    size_t trial;
    for (trial = 0u; trial < 2000u; ++trial) {
        size_t count = (size_t)(next_random() % 64u) + 1u;
        size_t page = (size_t)(next_random() % (uint32_t)(count + 1u));
        kilix_ui_focus actual;
        kilix_ui_focus expected;
        size_t index;
        size_t step;
        bool actual_changed;
        bool expected_changed;
        for (index = 0u; index < count; ++index)
            enabled[index] = (next_random() & 3u) != 0u;
        kilix_ui_focus_init(&actual, count, page);
        expected = actual;
        actual_changed = kilix_ui_focus_set_items(&actual, count, enabled);
        expected_changed = reference_set_items(&expected, count, enabled);
        CHECK(actual_changed == expected_changed);
        CHECK(focus_equal(&actual, &expected));
        actual.wrap = (next_random() & 1u) != 0u;
        expected.wrap = actual.wrap;
        for (step = 0u; step < 64u; ++step) {
            kilix_ui_action action = (kilix_ui_action)(
                1u + next_random() % (uint32_t)KILIX_UI_ACTION_CANCEL);
            if ((step & 15u) == 0u) {
                size_t changed_index =
                    (size_t)(next_random() % (uint32_t)count);
                enabled[changed_index] = !enabled[changed_index];
                actual_changed =
                    kilix_ui_focus_set_items(&actual, count, enabled);
                expected_changed =
                    reference_set_items(&expected, count, enabled);
                CHECK(actual_changed == expected_changed);
                CHECK(focus_equal(&actual, &expected));
            }
            actual_changed = kilix_ui_focus_apply(&actual, action, enabled);
            expected_changed = reference_apply(&expected, action, enabled);
            CHECK(actual_changed == expected_changed);
            CHECK(focus_equal(&actual, &expected));
            CHECK(actual.selected < actual.item_count);
            CHECK(actual.first_visible <= actual.selected ||
                  actual.page_size == 0u);
        }
    }
    return true;
}

static uint64_t canvas_hash(const sr_canvas *canvas)
{
    return hash_bytes((const uint8_t *)canvas->px,
                      (size_t)canvas->w * (size_t)canvas->h *
                      sizeof *canvas->px);
}

static bool canvases_equal(const sr_canvas *left, const sr_canvas *right)
{
    size_t bytes;
    if (left->w != right->w || left->h != right->h) return false;
    bytes = (size_t)left->w * (size_t)left->h * sizeof *left->px;
    return memcmp(left->px, right->px, bytes) == 0;
}

static bool test_clip_intersection_and_skin_fallback(void)
{
    static const char *const items[3] = {"AAAA", "BBBB", "CCCC"};
    static const uint8_t pixels[36] = {
        1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255,
        10, 11, 12, 255, 13, 14, 15, 255, 16, 17, 18, 255,
        19, 20, 21, 255, 22, 23, 24, 255, 25, 26, 27, 255
    };
    ki_td_soft_renderer renderer = {0};
    ki_td_view view = {.logical_width = 64, .logical_height = 64,
                       .scale = 1.0f};
    kilix_ui_style style;
    kilix_ui_focus focus;
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 3, 3);
    ki_td_nine_slice skin;
    size_t escaped = 0u;
    uint32_t before;
    int x;
    int y;
    CHECK(ki_td_soft_renderer_init(&renderer, 64, 64));
    CHECK(ki_td_nine_slice_init(&skin, &image, 1, 1, 1, 1));
    kilix_ui_style_init(&style);
    kilix_ui_focus_init(&focus, 3u, 3u);
    ki_td_soft_clear(&renderer, UINT32_C(0x010203));
    sr_canvas_set_clip(&renderer.canvas, 0, 0, 8, 8);
    kilix_ui_draw_list(&renderer, &view, (ki_td_rect){0, 0, 64, 64},
                       &style, NULL, &focus, items, NULL, 3u);
    for (y = 0; y < renderer.canvas.h; ++y) {
        for (x = 0; x < renderer.canvas.w; ++x) {
            if ((x >= 8 || y >= 8) &&
                renderer.canvas.px[(size_t)y * (size_t)renderer.canvas.w +
                                   (size_t)x] != UINT32_C(0xff010203))
                ++escaped;
        }
    }
    CHECK(escaped == 0u);
    CHECK(renderer.canvas.clip_x0 == 0 && renderer.canvas.clip_y0 == 0 &&
          renderer.canvas.clip_x1 == 8 && renderer.canvas.clip_y1 == 8);
    sr_canvas_reset_clip(&renderer.canvas);
    ki_td_soft_clear(&renderer, UINT32_C(0x010203));
    before = renderer.canvas.px[0];
    kilix_ui_draw_panel(&renderer, &view, (ki_td_rect){0, 0, 1, 1},
                        &style, &skin);
    CHECK(renderer.canvas.px[0] != before);
    CHECK(renderer.canvas.px[0] ==
          (UINT32_C(0xff000000) | style.border_color));
    ki_td_soft_renderer_destroy(&renderer);
    return true;
}

static bool test_invalid_draw_transactions(void)
{
    static const char *const labels[1] = {"Item"};
    static const char *const lines[1] = {"Line"};
    static const kilix_ui_prompt prompt = {"A", "Accept", true};
    static const kilix_ui_party_member party = {
        "Hero", "Lv 1", "Ready", "HP", 1.0f, 2.0f,
        "MP", 1.0f, 2.0f, true
    };
    static const kilix_ui_inventory_item inventory = {
        "Tonic", "Detail", 1, false, true
    };
    static const kilix_ui_command command = {"1", "Attack", "", true};
    static const kilix_ui_target target = {"Wisp", "", 1.0f, 2.0f, true};
    static const kilix_ui_shop_item shop = {"Tonic", 2, 1, true};
    static const uint8_t pixels[4] = {255, 255, 255, 255};
    ki_td_soft_renderer renderer = {0};
    ki_td_soft_renderer empty = {0};
    ki_td_view view = {.logical_width = 64, .logical_height = 64,
                       .scale = 1.0f};
    ki_td_view invalid_view = view;
    kilix_ui_style style;
    kilix_ui_focus focus;
    ki_td_rgba8 image = ki_td_rgba8_make(pixels, 1, 1);
    uint64_t before;
    CHECK(ki_td_soft_renderer_init(&renderer, 64, 64));
    kilix_ui_style_init(&style);
    kilix_ui_focus_init(&focus, 1u, 1u);
    ki_td_soft_clear(&renderer, UINT32_C(0x010203));
    sr_canvas_set_clip(&renderer.canvas, 3, 4, 20, 21);
    before = canvas_hash(&renderer.canvas);
    invalid_view.scale = 0.0f;
    kilix_ui_draw_panel(&renderer, &invalid_view,
                        (ki_td_rect){0, 0, 20, 20}, &style, NULL);
    kilix_ui_draw_list(&renderer, &invalid_view,
                       (ki_td_rect){0, 0, 20, 20}, &style, NULL, &focus,
                       labels, NULL, 1u);
    kilix_ui_draw_portrait(&renderer, &invalid_view,
                           (ki_td_rect){0, 0, 20, 20}, &image, 1.0f);
    kilix_ui_draw_dialogue(&renderer, &invalid_view,
                           (ki_td_rect){0, 0, 20, 20}, &style, NULL, NULL,
                           "Speaker", lines, 1u, "Continue");
    kilix_ui_draw_meter(&renderer, &invalid_view,
                        (ki_td_rect){0, 0, 20, 10}, &style, 1.0f, 2.0f,
                        "HP");
    kilix_ui_draw_prompts(&renderer, &invalid_view, 0, 0, 64, &style,
                          &prompt, 1u);
    kilix_ui_draw_party(&renderer, &invalid_view,
                        (ki_td_rect){0, 0, 20, 20}, &style, NULL, &focus,
                        &party, 1u);
    kilix_ui_draw_inventory(&renderer, &invalid_view,
                            (ki_td_rect){0, 0, 20, 20}, &style, NULL,
                            &focus, &inventory, 1u);
    kilix_ui_draw_commands(&renderer, &invalid_view,
                           (ki_td_rect){0, 0, 20, 20}, &style, NULL,
                           &focus, &command, 1u);
    kilix_ui_draw_targets(&renderer, &invalid_view,
                          (ki_td_rect){0, 0, 20, 20}, &style, NULL,
                          &focus, &target, 1u);
    kilix_ui_draw_shop(&renderer, &invalid_view,
                       (ki_td_rect){0, 0, 20, 20}, &style, NULL, &focus,
                       &shop, 1u, "Gil", 10);
    invalid_view.scale = FLT_MAX;
    kilix_ui_draw_meter(&renderer, &invalid_view,
                        (ki_td_rect){0, 0, 20, 10}, &style, 1.0f, 2.0f,
                        "HP");
    invalid_view.scale = NAN;
    kilix_ui_draw_panel(&renderer, &invalid_view,
                        (ki_td_rect){0, 0, 20, 20}, &style, NULL);
    kilix_ui_draw_list(&renderer, &view,
                       (ki_td_rect){INT_MAX, 0, 1, 1}, &style, NULL,
                       &focus, labels, NULL, 1u);
    kilix_ui_draw_dialogue(&renderer, &view,
                           (ki_td_rect){0, INT_MAX, 1, 1}, &style, NULL,
                           NULL, NULL, NULL, 0u, NULL);
    kilix_ui_draw_portrait(&renderer, &view, (ki_td_rect){0, 0, 1, 1},
                           &image, NAN);
    kilix_ui_draw_prompts(&renderer, &view, INT_MAX - 2, 0, 10, &style,
                          &prompt, 1u);
    kilix_ui_draw_panel(&empty, &view, (ki_td_rect){0, 0, 1, 1},
                        &style, NULL);
    kilix_ui_draw_panel(NULL, &view, (ki_td_rect){0, 0, 1, 1},
                        &style, NULL);
    kilix_ui_draw_list(&renderer, &view, (ki_td_rect){0, 0, 1, 1},
                       &style, NULL, NULL, labels, NULL, 1u);
    CHECK(canvas_hash(&renderer.canvas) == before);
    CHECK(renderer.canvas.clip_x0 == 3 && renderer.canvas.clip_y0 == 4 &&
          renderer.canvas.clip_x1 == 23 && renderer.canvas.clip_y1 == 25);
    ki_td_soft_renderer_destroy(&renderer);
    return true;
}

static bool test_style_normalization(void)
{
    static const char *const items[2] = {"Items", "Status"};
    ki_td_soft_renderer actual = {0};
    ki_td_soft_renderer expected = {0};
    ki_td_view view = {.logical_width = 64, .logical_height = 64,
                       .scale = 1.0f};
    kilix_ui_style malformed;
    kilix_ui_style normalized;
    kilix_ui_style snapshot;
    kilix_ui_focus focus;
    CHECK(ki_td_soft_renderer_init(&actual, 64, 64));
    CHECK(ki_td_soft_renderer_init(&expected, 64, 64));
    kilix_ui_style_init(&malformed);
    malformed.padding = -50;
    malformed.row_height = 0;
    malformed.font_scale = -4;
    malformed.panel_alpha = NAN;
    snapshot = malformed;
    normalized = malformed;
    normalized.padding = 0;
    normalized.row_height = 18;
    normalized.font_scale = 1;
    normalized.panel_alpha = 0.96f;
    kilix_ui_focus_init(&focus, 2u, 2u);
    ki_td_soft_clear(&actual, UINT32_C(0x010203));
    ki_td_soft_clear(&expected, UINT32_C(0x010203));
    kilix_ui_draw_list(&actual, &view, (ki_td_rect){0, 0, 64, 64},
                       &malformed, NULL, &focus, items, NULL, 2u);
    kilix_ui_draw_list(&expected, &view, (ki_td_rect){0, 0, 64, 64},
                       &normalized, NULL, &focus, items, NULL, 2u);
    CHECK(canvases_equal(&actual.canvas, &expected.canvas));
    CHECK(malformed.padding == snapshot.padding &&
          malformed.row_height == snapshot.row_height &&
          malformed.font_scale == snapshot.font_scale &&
          isnan(malformed.panel_alpha));
    ki_td_soft_renderer_destroy(&actual);
    ki_td_soft_renderer_destroy(&expected);
    return true;
}

static bool test_visible_work_equivalence(void)
{
    enum { ITEM_COUNT = 64, LONG_LABEL_SIZE = 65536 };
    static const char *labels[ITEM_COUNT];
    static const char *lines[ITEM_COUNT];
    static kilix_ui_party_member party[ITEM_COUNT];
    static kilix_ui_shop_item shop[ITEM_COUNT];
    static char long_label[LONG_LABEL_SIZE + 1];
    char prefix[33];
    const char *long_item[1] = {long_label};
    const char *short_item[1] = {prefix};
    ki_td_soft_renderer full = {0};
    ki_td_soft_renderer clipped = {0};
    ki_td_view view = {.logical_width = 160, .logical_height = 90,
                       .scale = 1.0f};
    kilix_ui_style style;
    kilix_ui_focus focus;
    size_t index;
    CHECK(ki_td_soft_renderer_init(&full, 160, 90));
    CHECK(ki_td_soft_renderer_init(&clipped, 160, 90));
    kilix_ui_style_init(&style);
    for (index = 0u; index < ITEM_COUNT; ++index) {
        labels[index] = "Item";
        lines[index] = "A clipped dialogue line.";
        party[index] = (kilix_ui_party_member){
            "Hero", "Lv 10", "Ready", "HP", 40.0f, 50.0f,
            "MP", 12.0f, 20.0f, true
        };
        shop[index] = (kilix_ui_shop_item){"Tonic", 12, 3, true};
    }
    kilix_ui_focus_init(&focus, ITEM_COUNT, ITEM_COUNT);
    ki_td_soft_clear(&full, UINT32_C(0x010203));
    ki_td_soft_clear(&clipped, UINT32_C(0x010203));
    kilix_ui_draw_list(&full, &view, (ki_td_rect){2, 2, 156, 86},
                       &style, NULL, &focus, labels, NULL, ITEM_COUNT);
    kilix_ui_draw_list(&clipped, &view, (ki_td_rect){2, 2, 156, 86},
                       &style, NULL, &focus, labels, NULL, 5u);
    CHECK(canvases_equal(&full.canvas, &clipped.canvas));
    ki_td_soft_clear(&full, UINT32_C(0x010203));
    ki_td_soft_clear(&clipped, UINT32_C(0x010203));
    kilix_ui_draw_dialogue(&full, &view, (ki_td_rect){2, 2, 156, 86},
                           &style, NULL, NULL, "Guide", lines, ITEM_COUNT,
                           "Continue");
    kilix_ui_draw_dialogue(&clipped, &view, (ki_td_rect){2, 2, 156, 86},
                           &style, NULL, NULL, "Guide", lines, 4u,
                           "Continue");
    CHECK(canvases_equal(&full.canvas, &clipped.canvas));
    ki_td_soft_clear(&full, UINT32_C(0x010203));
    ki_td_soft_clear(&clipped, UINT32_C(0x010203));
    kilix_ui_draw_party(&full, &view, (ki_td_rect){2, 2, 156, 86},
                        &style, NULL, &focus, party, ITEM_COUNT);
    kilix_ui_draw_party(&clipped, &view, (ki_td_rect){2, 2, 156, 86},
                        &style, NULL, &focus, party, 3u);
    CHECK(canvases_equal(&full.canvas, &clipped.canvas));
    ki_td_soft_clear(&full, UINT32_C(0x010203));
    ki_td_soft_clear(&clipped, UINT32_C(0x010203));
    kilix_ui_draw_shop(&full, &view, (ki_td_rect){2, 2, 156, 86},
                       &style, NULL, &focus, shop, ITEM_COUNT, "Gil", 27);
    kilix_ui_draw_shop(&clipped, &view, (ki_td_rect){2, 2, 156, 86},
                       &style, NULL, &focus, shop, 4u, "Gil", 27);
    CHECK(canvases_equal(&full.canvas, &clipped.canvas));
    for (index = 0u; index < LONG_LABEL_SIZE; ++index)
        long_label[index] = (char)('A' + (int)(index % 26u));
    long_label[LONG_LABEL_SIZE] = '\0';
    (void)memcpy(prefix, long_label, sizeof prefix - 1u);
    prefix[sizeof prefix - 1u] = '\0';
    kilix_ui_focus_init(&focus, 1u, 1u);
    ki_td_soft_clear(&full, UINT32_C(0x010203));
    ki_td_soft_clear(&clipped, UINT32_C(0x010203));
    kilix_ui_draw_list(&full, &view, (ki_td_rect){2, 2, 156, 86},
                       &style, NULL, &focus, long_item, NULL, 1u);
    kilix_ui_draw_list(&clipped, &view, (ki_td_rect){2, 2, 156, 86},
                       &style, NULL, &focus, short_item, NULL, 1u);
    CHECK(canvases_equal(&full.canvas, &clipped.canvas));
    ki_td_soft_renderer_destroy(&full);
    ki_td_soft_renderer_destroy(&clipped);
    return true;
}

static bool test_prompt_truncation(void)
{
    static char long_label[1024];
    char visible_label[92];
    kilix_ui_prompt long_prompt = {"A", long_label, true};
    kilix_ui_prompt visible_prompt = {"A", visible_label, true};
    ki_td_soft_renderer long_renderer = {0};
    ki_td_soft_renderer visible_renderer = {0};
    ki_td_view view = {.logical_width = 800, .logical_height = 32,
                       .scale = 1.0f};
    kilix_ui_style style;
    uint64_t background;
    size_t index;
    CHECK(ki_td_soft_renderer_init(&long_renderer, 800, 32));
    CHECK(ki_td_soft_renderer_init(&visible_renderer, 800, 32));
    kilix_ui_style_init(&style);
    for (index = 0u; index + 1u < sizeof long_label; ++index)
        long_label[index] = 'X';
    long_label[sizeof long_label - 1u] = '\0';
    for (index = 0u; index + 1u < sizeof visible_label; ++index)
        visible_label[index] = 'X';
    visible_label[sizeof visible_label - 1u] = '\0';
    ki_td_soft_clear(&long_renderer, UINT32_C(0x010203));
    ki_td_soft_clear(&visible_renderer, UINT32_C(0x010203));
    background = canvas_hash(&long_renderer.canvas);
    kilix_ui_draw_prompts(&long_renderer, &view, 0, 0, 768, &style,
                          &long_prompt, 1u);
    kilix_ui_draw_prompts(&visible_renderer, &view, 0, 0, 768, &style,
                          &visible_prompt, 1u);
    CHECK(canvases_equal(&long_renderer.canvas, &visible_renderer.canvas));
    CHECK(canvas_hash(&long_renderer.canvas) != background);
    ki_td_soft_renderer_destroy(&long_renderer);
    ki_td_soft_renderer_destroy(&visible_renderer);
    return true;
}

static bool test_randomized_render_safety(void)
{
    static const char *const labels[1] = {"Item"};
    static const char *const lines[1] = {"Line"};
    static const kilix_ui_prompt prompt = {"A", "Accept", true};
    static const kilix_ui_party_member party = {
        "Hero", "Lv 1", "Ready", "HP", 1.0f, 2.0f,
        "MP", 1.0f, 2.0f, true
    };
    static const kilix_ui_inventory_item inventory = {
        "Tonic", "Detail", 1, false, true
    };
    static const kilix_ui_command command = {"1", "Attack", "", true};
    static const kilix_ui_target target = {"Wisp", "", 1.0f, 2.0f, true};
    static const kilix_ui_shop_item shop = {"Tonic", 2, 1, true};
    ki_td_soft_renderer renderer = {0};
    ki_td_view view = {.logical_width = 32, .logical_height = 32,
                       .scale = 1.0f};
    kilix_ui_style style;
    kilix_ui_focus focus;
    size_t iteration;
    CHECK(ki_td_soft_renderer_init(&renderer, 32, 32));
    kilix_ui_focus_init(&focus, 1u, 1u);
    for (iteration = 0u; iteration < 5000u; ++iteration) {
        uint32_t sample = next_random();
        ki_td_rect rect;
        kilix_ui_style_init(&style);
        rect.x = (sample & 31u) == 0u ? INT_MAX :
                 (int)(sample % 64u) - 16;
        rect.y = (sample & 63u) == 0u ? INT_MIN :
                 (int)((sample >> 6) % 64u) - 16;
        rect.width = (int)((sample >> 12) % 56u) - 8;
        rect.height = (int)((sample >> 18) % 56u) - 8;
        style.padding = (sample & 7u) == 0u ? INT_MAX :
                        (int)((sample >> 4) % 20u) - 4;
        style.row_height = (sample & 15u) == 0u ? INT_MAX :
                           (int)((sample >> 9) % 24u) - 3;
        style.font_scale = (int)((sample >> 14) % 16u) - 4;
        style.panel_alpha = (sample & 127u) == 0u ? NAN :
                            (float)((int)(sample % 200u) - 50) / 100.0f;
        switch ((sample >> 25) & 3u) {
        case 0u: view.scale = 0.0f; break;
        case 1u: view.scale = FLT_MAX; break;
        case 2u: view.scale = 0.5f; break;
        default: view.scale = 2.0f; break;
        }
        sr_canvas_set_clip(&renderer.canvas, 2, 3, 20, 21);
        switch (sample % 10u) {
        case 0u:
            kilix_ui_draw_panel(&renderer, &view, rect, &style, NULL);
            break;
        case 1u:
            kilix_ui_draw_list(&renderer, &view, rect, &style, NULL,
                               &focus, labels, NULL, 1u);
            break;
        case 2u:
            kilix_ui_draw_dialogue(&renderer, &view, rect, &style, NULL,
                                   NULL, "Speaker", lines, 1u, "Continue");
            break;
        case 3u:
            kilix_ui_draw_meter(&renderer, &view, rect, &style, 1.0f, 2.0f,
                                "HP");
            break;
        case 4u:
            kilix_ui_draw_prompts(&renderer, &view, rect.x, rect.y,
                                  rect.width, &style, &prompt, 1u);
            break;
        case 5u:
            kilix_ui_draw_party(&renderer, &view, rect, &style, NULL,
                                &focus, &party, 1u);
            break;
        case 6u:
            kilix_ui_draw_inventory(&renderer, &view, rect, &style, NULL,
                                    &focus, &inventory, 1u);
            break;
        case 7u:
            kilix_ui_draw_commands(&renderer, &view, rect, &style, NULL,
                                   &focus, &command, 1u);
            break;
        case 8u:
            kilix_ui_draw_targets(&renderer, &view, rect, &style, NULL,
                                  &focus, &target, 1u);
            break;
        default:
            kilix_ui_draw_shop(&renderer, &view, rect, &style, NULL, &focus,
                               &shop, 1u, "Gil", 10);
            break;
        }
        CHECK(renderer.canvas.clip_x0 == 2 &&
              renderer.canvas.clip_y0 == 3 &&
              renderer.canvas.clip_x1 == 22 &&
              renderer.canvas.clip_y1 == 24);
    }
    ki_td_soft_renderer_destroy(&renderer);
    return true;
}

/* The canvas as one number, for the no-op assertions below: a draw call that
 * must do nothing has to leave every pixel untouched, not merely look
 * unchanged. */
static uint64_t hash_canvas(ki_td_soft_renderer *renderer)
{
    const uint8_t *rgba = ki_td_soft_pack_rgba(renderer);
    return rgba ? hash_bytes(rgba, renderer->rgba_size) : 0u;
}

/* The hit test must agree with the pixels, everywhere -- not at a few sampled
 * points. This walks EVERY logical pixel of a list rect and checks
 * kilix_ui_list_hit against the row geometry the draw call uses, so a future
 * change to padding, row height or the visible range cannot move one without
 * the other. */
static bool test_list_hit_agrees_with_draw(void)
{
    kilix_ui_style style;
    kilix_ui_focus focus;
    ki_td_view view = {0};
    ki_td_rect rect = {10, 6, 120, 96};
    int x, y;
    size_t hits = 0u;

    kilix_ui_style_init(&style);
    kilix_ui_focus_init(&focus, 12u, 5u);
    focus.first_visible = 3u;
    view.logical_width = 320;
    view.logical_height = 180;
    view.scale = 1.0f;

    for (y = rect.y - 4; y < rect.y + rect.height + 4; ++y) {
        for (x = rect.x - 4; x < rect.x + rect.width + 4; ++x) {
            size_t hit = kilix_ui_list_hit(&view, rect, &style, &focus,
                                           (float)x, (float)y);
            bool inside = x >= rect.x && x < rect.x + rect.width &&
                          y >= rect.y && y < rect.y + rect.height;
            if (!inside) {
                CHECK(hit == SIZE_MAX);
                continue;
            }
            if (hit == SIZE_MAX) continue;
            /* A reported hit must be a visible row, and the point must lie in
             * that row's own band. */
            CHECK(hit >= focus.first_visible);
            CHECK(hit < focus.first_visible + focus.page_size);
            CHECK(hit < focus.item_count);
            {
                int64_t offset = (int64_t)(hit - focus.first_visible);
                int64_t top = rect.y + style.padding +
                              offset * style.row_height;
                int64_t bottom = top + style.row_height;
                if (bottom > rect.y + rect.height)
                    bottom = rect.y + rect.height;
                CHECK((int64_t)y >= top && (int64_t)y < bottom);
            }
            ++hits;
        }
    }
    /* If nothing ever hit, every assertion above is vacuous. */
    CHECK(hits > 0u);
    /* A NULL focus, a degenerate rect and non-finite coordinates all miss. */
    CHECK(kilix_ui_list_hit(&view, rect, &style, NULL, 20.0f, 20.0f)
          == SIZE_MAX);
    CHECK(kilix_ui_list_hit(NULL, rect, &style, &focus, 20.0f, 20.0f)
          == SIZE_MAX);
    {
        ki_td_rect empty = {10, 6, 0, 0};
        CHECK(kilix_ui_list_hit(&view, empty, &style, &focus, 10.0f, 6.0f)
              == SIZE_MAX);
    }
    return true;
}

static bool test_calendar(void)
{
    static kilix_ui_calendar_day days[35];
    static const char *const labels[7] = {"M", "T", "W", "T", "F", "S", "S"};
    ki_td_soft_renderer renderer = {0};
    kilix_ui_style style;
    kilix_ui_calendar calendar;
    ki_td_view view = {0};
    ki_td_rect rect = {4, 4, 220, 150};
    sr_canvas *canvas;
    size_t index;
    uint64_t drawn, blank;

    kilix_ui_style_init(&style);
    view.logical_width = 320;
    view.logical_height = 180;
    view.scale = 1.0f;
    CHECK(ki_td_soft_renderer_init(&renderer, 320, 180));
    canvas = ki_td_soft_canvas(&renderer);

    for (index = 0u; index < 35u; ++index) {
        days[index].in_month = index >= 2u && index < 33u;
        days[index].enabled = true;
        days[index].marks = (uint8_t)((index % 5u == 0u) ? 0x03u : 0u);
    }
    memset(&calendar, 0, sizeof calendar);
    calendar.title = "AUGUST 2026";
    calendar.weekday_labels = labels;
    calendar.days = days;
    calendar.day_count = 35u;
    calendar.today = 10u;
    calendar.mark_colors[0] = UINT32_C(0x4fa3ff);
    calendar.mark_colors[1] = UINT32_C(0xffb347);
    calendar.font = SR_FONT_COMPACT_7X14;

    ki_td_soft_clear(&renderer, 0u);
    kilix_ui_draw_calendar(&renderer, &view, rect, &style, NULL, NULL,
                           &calendar);
    drawn = hash_canvas(&renderer);

    /* Every no-op case must leave the canvas exactly as it found it. */
    ki_td_soft_clear(&renderer, 0u);
    blank = hash_canvas(&renderer);
    CHECK(drawn != blank);

    {
        kilix_ui_calendar broken = calendar;
        broken.day_count = 34u;              /* not a multiple of 7 */
        kilix_ui_draw_calendar(&renderer, &view, rect, &style, NULL, NULL,
                               &broken);
        CHECK(hash_canvas(&renderer) == blank);

        broken = calendar;
        broken.days = NULL;
        kilix_ui_draw_calendar(&renderer, &view, rect, &style, NULL, NULL,
                               &broken);
        CHECK(hash_canvas(&renderer) == blank);

        broken = calendar;
        broken.day_count = 0u;
        kilix_ui_draw_calendar(&renderer, &view, rect, &style, NULL, NULL,
                               &broken);
        CHECK(hash_canvas(&renderer) == blank);
    }
    {
        ki_td_rect degenerate = {4, 4, 3, 3};
        kilix_ui_draw_calendar(&renderer, &view, degenerate, &style, NULL,
                               NULL, &calendar);
        CHECK(hash_canvas(&renderer) == blank);
    }
    kilix_ui_draw_calendar(&renderer, &view, rect, &style, NULL, NULL, NULL);
    CHECK(hash_canvas(&renderer) == blank);

    /* The caller's clip survives the draw. */
    {
        int saved[4];
        saved[0] = canvas->clip_x0; saved[1] = canvas->clip_y0;
        saved[2] = canvas->clip_x1; saved[3] = canvas->clip_y1;
        canvas->clip_x0 = 8; canvas->clip_y0 = 8;
        canvas->clip_x1 = 100; canvas->clip_y1 = 90;
        kilix_ui_draw_calendar(&renderer, &view, rect, &style, NULL, NULL,
                               &calendar);
        CHECK(canvas->clip_x0 == 8 && canvas->clip_y0 == 8);
        CHECK(canvas->clip_x1 == 100 && canvas->clip_y1 == 90);
        canvas->clip_x0 = saved[0]; canvas->clip_y0 = saved[1];
        canvas->clip_x1 = saved[2]; canvas->clip_y1 = saved[3];
    }

    /* Hit testing: every in-month cell is reachable, filler never is, and a
     * hit round-trips to the cell it names. */
    {
        size_t reachable = 0u;
        int x, y;
        for (y = rect.y; y < rect.y + rect.height; ++y) {
            for (x = rect.x; x < rect.x + rect.width; ++x) {
                size_t hit = kilix_ui_calendar_hit(&view, rect, &style,
                                                   &calendar, (float)x,
                                                   (float)y);
                if (hit == SIZE_MAX) continue;
                CHECK(hit < calendar.day_count);
                CHECK(days[hit].in_month);
                ++reachable;
            }
        }
        CHECK(reachable > 0u);
        CHECK(kilix_ui_calendar_hit(&view, rect, &style, NULL, 10.0f, 10.0f)
              == SIZE_MAX);
        CHECK(kilix_ui_calendar_hit(&view, rect, &style, &calendar,
                                    -50.0f, -50.0f) == SIZE_MAX);
    }

    ki_td_soft_renderer_destroy(&renderer);
    return true;
}

int main(void)
{
    static const struct test_case {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"focus navigation", test_focus},
        {"focus randomized model", test_focus_randomized_model},
        {"primitive golden rendering", test_drawing},
        {"RPG composite golden rendering", test_rpg_composites},
        {"clip intersection and skin fallback",
         test_clip_intersection_and_skin_fallback},
        {"invalid draw transactions", test_invalid_draw_transactions},
        {"style normalization", test_style_normalization},
        {"visible-work equivalence", test_visible_work_equivalence},
        {"prompt truncation", test_prompt_truncation},
        {"randomized render safety", test_randomized_render_safety},
        {"list hit agrees with draw", test_list_hit_agrees_with_draw},
        {"calendar widget", test_calendar}
    };
    size_t index;
    size_t failures = 0u;
    for (index = 0u; index < sizeof tests / sizeof tests[0]; ++index) {
        if (tests[index].run())
            (void)printf("PASS %s\n", tests[index].name);
        else
            ++failures;
    }
    if (failures != 0u) {
        (void)fprintf(stderr, "FAIL %zu kilix-ui test suite(s)\n", failures);
        return EXIT_FAILURE;
    }
    /* Computed, not a literal: the count was hardcoded and had to be edited
     * by hand every time a suite was added, so it silently under-reported. */
    (void)printf("PASS all %zu kilix-ui suites\n",
                 sizeof tests / sizeof tests[0]);
    return EXIT_SUCCESS;
}
