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
/* As kilix_ui_draw_meter, but the caller supplies the text drawn inside the
 * bar. NULL value_text draws the label alone; NULL label draws value_text
 * alone; both NULL draws the bar only. Identical geometry, fraction clamping,
 * clip intersection, style normalization, and invalid-input no-op rules.
 *
 * It exists because kilix_ui_draw_meter unconditionally appends "%.0f/%.0f":
 * fine for hit points, wrong for a quantity whose units the player should
 * never see. "Moisture 62/100" is a number; "Moisture - thirsty" is a
 * reading. */
void kilix_ui_draw_meter_text(ki_td_soft_renderer *renderer,
                              const ki_td_view *view, ki_td_rect rect,
                              const kilix_ui_style *style, float value,
                              float maximum, const char *label,
                              const char *value_text);

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
/* ---- calendar ----------------------------------------------------------
 *
 * The one common widget this module lacked. It performs NO date arithmetic:
 * which cell is today, which cells are filler, and what a mark means all stay
 * with the caller -- exactly as party, inventory and shop semantics already
 * do. A calendar that knew about months would have to know about calendars,
 * and every consumer disagrees about those. */
typedef struct kilix_ui_calendar_day {
    uint8_t marks;     /* bitmask of caller-defined event classes */
    bool in_month;     /* false for leading/trailing filler cells */
    bool enabled;
} kilix_ui_calendar_day;

typedef struct kilix_ui_calendar {
    const char *title;                   /* e.g. "AUGUST 2026" */
    const char *const *weekday_labels;   /* exactly 7 entries, or NULL */
    const kilix_ui_calendar_day *days;   /* week-aligned, day_count entries */
    size_t day_count;                    /* positive multiple of 7, 28..42 */
    size_t today;                        /* index into days, or SIZE_MAX */
    uint32_t mark_colors[8];             /* 0xRRGGBB, one per mark bit */
    sr_font_id font;                     /* face for the day numbers */
} kilix_ui_calendar;

/* Draws a 7-column week grid inside rect under this module's existing drawing
 * contract: caller-owned borrowed data, no allocation or I/O, intersects and
 * restores the caller's active clip, and a no-op on an invalid context,
 * geometry or record -- including a day_count that is not a positive multiple
 * of 7. focus may be NULL; when non-NULL its selected index picks a day cell.
 * Mark bits paint dots beneath the day number in mark_colors bit order.
 *
 * The font lives on this struct and NOT on kilix_ui_style deliberately.
 * kilix_ui_style is a frozen ten-field layout that every existing consumer
 * compiles against and every frozen golden hashes; a new struct breaks
 * nothing. */
void kilix_ui_draw_calendar(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const kilix_ui_style *style,
                            const ki_td_nine_slice *skin,
                            const kilix_ui_focus *focus,
                            const kilix_ui_calendar *calendar);

/* ---- hit testing --------------------------------------------------------
 *
 * Every mouse-driven consumer currently re-derives row geometry by hand, and
 * a consumer that computes it differently from the draw call is a bug nobody
 * sees until a click lands on the wrong row. These use the SAME normalized
 * style, padding, row height and first_visible rules as the draw calls, so a
 * hit always agrees with the pixels. Pure; they need no renderer. */

/* The item index whose row contains the logical point, or SIZE_MAX. */
size_t kilix_ui_list_hit(const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style,
                         const kilix_ui_focus *focus,
                         float logical_x, float logical_y);

/* Index into calendar->days, or SIZE_MAX. */
size_t kilix_ui_calendar_hit(const ki_td_view *view, ki_td_rect rect,
                             const kilix_ui_style *style,
                             const kilix_ui_calendar *calendar,
                             float logical_x, float logical_y);

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
