#define _POSIX_C_SOURCE 200809L

#include "kilix_ui.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LARGE_ITEMS 8192u
#define DIALOGUE_LINES 4096u
#define PARTY_ITEMS 2048u
#define LONG_TEXT_BYTES (1024u * 1024u)

static const char *large_items[LARGE_ITEMS];
static const char *dialogue_lines[DIALOGUE_LINES];
static kilix_ui_party_member party_items[PARTY_ITEMS];
static char long_text[LONG_TEXT_BYTES + 1u];
static bool focus_enabled[LARGE_ITEMS];

static uint64_t now_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0) return 0u;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static uint64_t canvas_hash(const sr_canvas *canvas)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t count = (size_t)canvas->w * (size_t)canvas->h;
    size_t index;
    for (index = 0u; index < count; ++index) {
        hash ^= canvas->px[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void print_metric(const char *name, uint64_t elapsed, size_t rounds,
                         uint64_t checksum)
{
    (void)printf("%s %.6f checksum=%" PRIu64 "\n", name,
                 (double)elapsed / (double)rounds, checksum);
}

static void benchmark_focus(void)
{
    const size_t rounds = 1000000u;
    kilix_ui_focus focus;
    size_t index;
    uint64_t checksum = 0u;
    uint64_t started;
    for (index = 0u; index < LARGE_ITEMS; ++index)
        focus_enabled[index] = (index % 5u) == 0u;
    kilix_ui_focus_init(&focus, LARGE_ITEMS, 12u);
    started = now_ns();
    for (index = 0u; index < rounds; ++index) {
        (void)kilix_ui_focus_apply(
            &focus, KILIX_UI_ACTION_DOWN, focus_enabled);
        checksum += focus.selected + focus.first_visible;
    }
    print_metric("focus-sparse-ns-step", now_ns() - started,
                 rounds, checksum);
}

static void benchmark_normal_list(ki_td_soft_renderer *renderer,
                                  const ki_td_view *view,
                                  const kilix_ui_style *style)
{
    static const char *const items[12] = {
        "Inventory", "Status", "Skills", "Party", "Map", "Journal",
        "Settings", "Save", "Load", "Help", "Credits", "Quit"
    };
    const size_t rounds = 5000u;
    kilix_ui_focus focus;
    size_t round;
    uint64_t started;
    kilix_ui_focus_init(&focus, 12u, 12u);
    started = now_ns();
    for (round = 0u; round < rounds; ++round) {
        focus.selected = round % 12u;
        ki_td_soft_clear(renderer, UINT32_C(0x010203));
        kilix_ui_draw_list(renderer, view, (ki_td_rect){2, 2, 156, 86},
                           style, NULL, &focus, items, NULL, 12u);
    }
    print_metric("list-12-ns-frame", now_ns() - started, rounds,
                 canvas_hash(&renderer->canvas));
}

static void benchmark_clipped_list(ki_td_soft_renderer *renderer,
                                   const ki_td_view *view,
                                   const kilix_ui_style *style)
{
    const size_t rounds = 100u;
    kilix_ui_focus focus;
    size_t index;
    size_t round;
    uint64_t started;
    for (index = 0u; index < LARGE_ITEMS; ++index)
        large_items[index] = "Item";
    kilix_ui_focus_init(&focus, LARGE_ITEMS, LARGE_ITEMS);
    started = now_ns();
    for (round = 0u; round < rounds; ++round) {
        ki_td_soft_clear(renderer, UINT32_C(0x010203));
        kilix_ui_draw_list(renderer, view, (ki_td_rect){2, 2, 156, 86},
                           style, NULL, &focus, large_items, NULL,
                           LARGE_ITEMS);
    }
    print_metric("list-clipped-8192-ns-frame", now_ns() - started, rounds,
                 canvas_hash(&renderer->canvas));
}

static void benchmark_dialogue(ki_td_soft_renderer *renderer,
                               const ki_td_view *view,
                               const kilix_ui_style *style)
{
    const size_t rounds = 100u;
    size_t index;
    size_t round;
    uint64_t started;
    for (index = 0u; index < DIALOGUE_LINES; ++index)
        dialogue_lines[index] = "A clipped dialogue line.";
    started = now_ns();
    for (round = 0u; round < rounds; ++round) {
        ki_td_soft_clear(renderer, UINT32_C(0x010203));
        kilix_ui_draw_dialogue(
            renderer, view, (ki_td_rect){2, 2, 156, 86}, style, NULL,
            NULL, "Guide", dialogue_lines, DIALOGUE_LINES, "Continue");
    }
    print_metric("dialogue-clipped-4096-ns-frame", now_ns() - started,
                 rounds, canvas_hash(&renderer->canvas));
}

static void benchmark_party(ki_td_soft_renderer *renderer,
                            const ki_td_view *view,
                            const kilix_ui_style *style)
{
    const size_t rounds = 100u;
    kilix_ui_focus focus;
    size_t index;
    size_t round;
    uint64_t started;
    for (index = 0u; index < PARTY_ITEMS; ++index)
        party_items[index] = (kilix_ui_party_member){
            "Hero", "Lv 10", "Ready", "HP", 40.0f, 50.0f,
            "MP", 12.0f, 20.0f, true
        };
    kilix_ui_focus_init(&focus, PARTY_ITEMS, PARTY_ITEMS);
    started = now_ns();
    for (round = 0u; round < rounds; ++round) {
        ki_td_soft_clear(renderer, UINT32_C(0x010203));
        kilix_ui_draw_party(renderer, view, (ki_td_rect){2, 2, 156, 86},
                            style, NULL, &focus, party_items, PARTY_ITEMS);
    }
    print_metric("party-clipped-2048-ns-frame", now_ns() - started, rounds,
                 canvas_hash(&renderer->canvas));
}

static void benchmark_long_text(ki_td_soft_renderer *renderer,
                                const ki_td_view *view,
                                const kilix_ui_style *style)
{
    const size_t rounds = 3u;
    const char *items[1] = {long_text};
    kilix_ui_focus focus;
    size_t index;
    size_t round;
    uint64_t started;
    for (index = 0u; index < LONG_TEXT_BYTES; ++index)
        long_text[index] = (char)('A' + (int)(index % 26u));
    long_text[LONG_TEXT_BYTES] = '\0';
    kilix_ui_focus_init(&focus, 1u, 1u);
    started = now_ns();
    for (round = 0u; round < rounds; ++round) {
        ki_td_soft_clear(renderer, UINT32_C(0x010203));
        kilix_ui_draw_list(renderer, view, (ki_td_rect){2, 2, 156, 86},
                           style, NULL, &focus, items, NULL, 1u);
    }
    print_metric("list-1mib-label-ns-frame", now_ns() - started, rounds,
                 canvas_hash(&renderer->canvas));
}

int main(void)
{
    ki_td_soft_renderer renderer = {0};
    ki_td_view view = {.logical_width = 160, .logical_height = 90,
                       .scale = 1.0f};
    kilix_ui_style style;
    if (!ki_td_soft_renderer_init(&renderer, 160, 90)) return EXIT_FAILURE;
    kilix_ui_style_init(&style);
    benchmark_focus();
    benchmark_normal_list(&renderer, &view, &style);
    benchmark_clipped_list(&renderer, &view, &style);
    benchmark_dialogue(&renderer, &view, &style);
    benchmark_party(&renderer, &view, &style);
    benchmark_long_text(&renderer, &view, &style);
    ki_td_soft_renderer_destroy(&renderer);
    return EXIT_SUCCESS;
}
