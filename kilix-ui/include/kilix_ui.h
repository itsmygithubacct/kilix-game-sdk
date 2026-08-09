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
#define KILIX_UI_VERSION_MINOR 3
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

/* Focus is caller-owned and must first be initialized.  A non-null enabled
 * array contains item_count entries; null means every item is enabled.
 * Movement skips disabled items, preserves a visible selected row, and never
 * allocates.  ACCEPT reports eligibility without mutating focus. */
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

/* Initializes the complete default theme.  Draw calls copy, normalize, and
 * never mutate a supplied style: negative padding becomes zero, non-positive
 * row height becomes the default, font scale is clamped to [1,8], and panel
 * alpha is clamped to [0,1] (non-finite alpha becomes the default). */
void kilix_ui_style_init(kilix_ui_style *style);

typedef struct kilix_ui_prompt {
    const char *key;
    const char *label;
    bool enabled;
} kilix_ui_prompt;

/*
 * Drawing contract
 * ----------------
 * The renderer must have an initialized canvas and the view must have a
 * finite positive scale.  Rectangles need positive dimensions and
 * representable x+width/y+height edges.  Calls with invalid context, geometry,
 * counted arrays, or required records are no-ops.  Every draw intersects and
 * preserves the caller's active canvas clip.
 *
 * Strings, images, styles, focus records, and arrays remain caller-owned and
 * are borrowed only for the call.  Drawing performs no allocation or I/O.
 * Panel skins that are invalid or too large for the destination use the
 * colored fallback.  Portrait alpha is clamped to [0,1]; non-finite alpha is
 * a no-op.  Text work is bounded to glyphs that can intersect the active clip.
 */
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

typedef struct kilix_ui_party_member {
    const char *name;
    const char *level;
    const char *status;
    const char *primary_label;
    float primary;
    float primary_maximum;
    const char *secondary_label;
    float secondary;
    float secondary_maximum;
    bool enabled;
} kilix_ui_party_member;

typedef struct kilix_ui_inventory_item {
    const char *name;
    const char *detail;
    int quantity;
    bool equipped;
    bool enabled;
} kilix_ui_inventory_item;

typedef struct kilix_ui_command {
    const char *key;
    const char *label;
    const char *cost;
    bool enabled;
} kilix_ui_command;

typedef struct kilix_ui_target {
    const char *name;
    const char *status;
    float vitality;
    float vitality_maximum;
    bool enabled;
} kilix_ui_target;

typedef struct kilix_ui_shop_item {
    const char *name;
    int price;
    int owned;
    bool enabled;
} kilix_ui_shop_item;

/* Higher-level RPG composites follow the drawing contract above.  Focus and
 * all inventory, economy, combat, and other game semantics remain caller-owned. */
void kilix_ui_draw_party(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style,
                         const ki_td_nine_slice *skin,
                         const kilix_ui_focus *focus,
                         const kilix_ui_party_member *members,
                         size_t member_count);
void kilix_ui_draw_inventory(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, ki_td_rect rect,
                             const kilix_ui_style *style,
                             const ki_td_nine_slice *skin,
                             const kilix_ui_focus *focus,
                             const kilix_ui_inventory_item *items,
                             size_t item_count);
void kilix_ui_draw_commands(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const kilix_ui_style *style,
                            const ki_td_nine_slice *skin,
                            const kilix_ui_focus *focus,
                            const kilix_ui_command *commands,
                            size_t command_count);
void kilix_ui_draw_targets(ki_td_soft_renderer *renderer,
                           const ki_td_view *view, ki_td_rect rect,
                           const kilix_ui_style *style,
                           const ki_td_nine_slice *skin,
                           const kilix_ui_focus *focus,
                           const kilix_ui_target *targets,
                           size_t target_count);
void kilix_ui_draw_shop(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        const kilix_ui_style *style,
                        const ki_td_nine_slice *skin,
                        const kilix_ui_focus *focus,
                        const kilix_ui_shop_item *items,
                        size_t item_count, const char *currency,
                        int balance);

#ifdef __cplusplus
}
#endif

#endif
