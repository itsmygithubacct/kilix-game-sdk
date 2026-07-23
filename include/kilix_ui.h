#ifndef KILIX_UI_H
#define KILIX_UI_H

#include "kilix_top_down.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILIX_UI_VERSION_MAJOR 0
#define KILIX_UI_VERSION_MINOR 1
#define KILIX_UI_VERSION_PATCH 0

typedef enum kilix_ui_action {
    KILIX_UI_ACTION_NONE = 0,
    KILIX_UI_ACTION_UP,
    KILIX_UI_ACTION_DOWN,
    KILIX_UI_ACTION_LEFT,
    KILIX_UI_ACTION_RIGHT,
    KILIX_UI_ACTION_PAGE_UP,
    KILIX_UI_ACTION_PAGE_DOWN,
    KILIX_UI_ACTION_HOME,
    KILIX_UI_ACTION_END,
    KILIX_UI_ACTION_ACCEPT,
    KILIX_UI_ACTION_CANCEL
} kilix_ui_action;

typedef struct kilix_ui_focus {
    size_t selected;
    size_t item_count;
    size_t page_size;
    size_t first_visible;
    bool wrap;
} kilix_ui_focus;

void kilix_ui_focus_init(kilix_ui_focus *focus, size_t item_count,
                         size_t page_size);
bool kilix_ui_focus_set_items(kilix_ui_focus *focus, size_t item_count,
                              const bool *enabled);
bool kilix_ui_focus_apply(kilix_ui_focus *focus, kilix_ui_action action,
                          const bool *enabled);
bool kilix_ui_focus_accepts(const kilix_ui_focus *focus,
                            kilix_ui_action action, const bool *enabled);

typedef struct kilix_ui_style {
    uint32_t panel_color;
    uint32_t border_color;
    uint32_t text_color;
    uint32_t muted_color;
    uint32_t accent_color;
    uint32_t meter_color;
    int padding;
    int row_height;
    int font_scale;
    float panel_alpha;
} kilix_ui_style;

void kilix_ui_style_init(kilix_ui_style *style);

typedef struct kilix_ui_prompt {
    const char *key;
    const char *label;
    bool enabled;
} kilix_ui_prompt;

void kilix_ui_draw_panel(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style,
                         const ki_td_nine_slice *skin);
void kilix_ui_draw_list(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        const kilix_ui_style *style,
                        const ki_td_nine_slice *skin,
                        const kilix_ui_focus *focus,
                        const char *const *items, const bool *enabled,
                        size_t item_count);
void kilix_ui_draw_portrait(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const ki_td_rgba8 *portrait, float alpha);
void kilix_ui_draw_dialogue(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const kilix_ui_style *style,
                            const ki_td_nine_slice *skin,
                            const ki_td_rgba8 *portrait,
                            const char *speaker,
                            const char *const *lines, size_t line_count,
                            const char *continue_prompt);
void kilix_ui_draw_meter(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style, float value,
                         float maximum, const char *label);
void kilix_ui_draw_prompts(ki_td_soft_renderer *renderer,
                           const ki_td_view *view, int x, int y,
                           int available_width,
                           const kilix_ui_style *style,
                           const kilix_ui_prompt *prompts,
                           size_t prompt_count);

#ifdef __cplusplus
}
#endif

#endif
