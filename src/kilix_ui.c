#include "kilix_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool item_enabled(const bool *enabled, size_t index)
{
    return !enabled || enabled[index];
}

static void reveal_focus(kilix_ui_focus *focus)
{
    if (!focus || focus->item_count == 0u) return;
    if (focus->page_size == 0u || focus->page_size > focus->item_count)
        focus->page_size = focus->item_count;
    if (focus->selected < focus->first_visible)
        focus->first_visible = focus->selected;
    else if (focus->selected - focus->first_visible >= focus->page_size)
        focus->first_visible = focus->selected - focus->page_size + 1u;
    if (focus->page_size >= focus->item_count) focus->first_visible = 0u;
    else if (focus->first_visible > focus->item_count - focus->page_size)
        focus->first_visible = focus->item_count - focus->page_size;
}

void kilix_ui_focus_init(kilix_ui_focus *focus, size_t item_count,
                         size_t page_size)
{
    if (!focus) return;
    *focus = (kilix_ui_focus){0};
    focus->item_count = item_count;
    focus->page_size = page_size == 0u ? item_count : page_size;
    if (focus->page_size > item_count) focus->page_size = item_count;
    focus->wrap = true;
}

bool kilix_ui_focus_set_items(kilix_ui_focus *focus, size_t item_count,
                              const bool *enabled)
{
    size_t old;
    size_t index;
    if (!focus) return false;
    old = focus->selected;
    focus->item_count = item_count;
    if (item_count == 0u) {
        focus->selected = 0u;
        focus->first_visible = 0u;
        return old != 0u;
    }
    if (focus->page_size == 0u) focus->page_size = item_count;
    if (focus->selected >= item_count) focus->selected = item_count - 1u;
    if (!item_enabled(enabled, focus->selected)) {
        for (index = 0u; index < item_count; ++index) {
            if (item_enabled(enabled, index)) {
                focus->selected = index;
                break;
            }
        }
    }
    reveal_focus(focus);
    return focus->selected != old;
}

static bool move_focus(kilix_ui_focus *focus, long long delta,
                       const bool *enabled)
{
    size_t origin;
    size_t attempts;
    size_t candidate;
    if (!focus || focus->item_count == 0u || delta == 0) return false;
    origin = focus->selected;
    candidate = origin;
    for (attempts = 0u; attempts < focus->item_count; ++attempts) {
        if (delta > 0) {
            if (candidate + 1u < focus->item_count) ++candidate;
            else if (focus->wrap) candidate = 0u;
            else return false;
        } else {
            if (candidate > 0u) --candidate;
            else if (focus->wrap) candidate = focus->item_count - 1u;
            else return false;
        }
        if (item_enabled(enabled, candidate)) {
            focus->selected = candidate;
            reveal_focus(focus);
            return candidate != origin;
        }
    }
    return false;
}

static bool jump_focus(kilix_ui_focus *focus, size_t candidate,
                       const bool *enabled, int direction)
{
    size_t origin;
    if (!focus || focus->item_count == 0u) return false;
    if (candidate >= focus->item_count) candidate = focus->item_count - 1u;
    origin = focus->selected;
    while (!item_enabled(enabled, candidate)) {
        if (direction < 0) {
            if (candidate == 0u) return false;
            --candidate;
        } else {
            if (candidate + 1u >= focus->item_count) return false;
            ++candidate;
        }
    }
    focus->selected = candidate;
    reveal_focus(focus);
    return candidate != origin;
}

bool kilix_ui_focus_apply(kilix_ui_focus *focus, kilix_ui_action action,
                          const bool *enabled)
{
    size_t page;
    if (!focus) return false;
    page = focus->page_size == 0u ? 1u : focus->page_size;
    switch (action) {
    case KILIX_UI_ACTION_UP:
    case KILIX_UI_ACTION_LEFT:
        return move_focus(focus, -1, enabled);
    case KILIX_UI_ACTION_DOWN:
    case KILIX_UI_ACTION_RIGHT:
        return move_focus(focus, 1, enabled);
    case KILIX_UI_ACTION_PAGE_UP:
        return jump_focus(focus, focus->selected > page ?
                          focus->selected - page : 0u, enabled, -1);
    case KILIX_UI_ACTION_PAGE_DOWN:
        return jump_focus(focus, page > SIZE_MAX - focus->selected ?
                          SIZE_MAX : focus->selected + page,
                          enabled, 1);
    case KILIX_UI_ACTION_HOME:
        return jump_focus(focus, 0u, enabled, 1);
    case KILIX_UI_ACTION_END:
        return jump_focus(focus, focus->item_count == 0u ? 0u :
                          focus->item_count - 1u, enabled, -1);
    default:
        return false;
    }
}

bool kilix_ui_focus_accepts(const kilix_ui_focus *focus,
                            kilix_ui_action action, const bool *enabled)
{
    return focus && action == KILIX_UI_ACTION_ACCEPT &&
           focus->item_count != 0u && focus->selected < focus->item_count &&
           item_enabled(enabled, focus->selected);
}

void kilix_ui_style_init(kilix_ui_style *style)
{
    if (!style) return;
    style->panel_color = UINT32_C(0x102039);
    style->border_color = UINT32_C(0x4f78a8);
    style->text_color = UINT32_C(0xf2f5f8);
    style->muted_color = UINT32_C(0x778899);
    style->accent_color = UINT32_C(0xffcc55);
    style->meter_color = UINT32_C(0x58c878);
    style->padding = 6;
    style->row_height = 18;
    style->font_scale = 1;
    style->panel_alpha = 0.96f;
}

static const kilix_ui_style *selected_style(const kilix_ui_style *style,
                                             kilix_ui_style *fallback)
{
    if (style) return style;
    kilix_ui_style_init(fallback);
    return fallback;
}

static int text_scale(const ki_td_view *view, const kilix_ui_style *style)
{
    int view_scale = view && isfinite(view->scale) ?
                     (int)(view->scale + 0.5f) : 1;
    int style_scale = style->font_scale > 0 ? style->font_scale : 1;
    if (view_scale < 1) view_scale = 1;
    if (style_scale > 8 / view_scale) style_scale = 8 / view_scale;
    if (style_scale < 1) style_scale = 1;
    return view_scale * style_scale;
}

static void draw_text(ki_td_soft_renderer *renderer, const ki_td_view *view,
                      const kilix_ui_style *style, int x, int y,
                      const char *text, uint32_t color)
{
    sr_canvas *canvas;
    if (!renderer || !view || !text) return;
    canvas = ki_td_soft_canvas(renderer);
    if (!canvas) return;
    sr_text(canvas, (float)ki_td_screen_x(view, (float)x),
            (float)ki_td_screen_y(view, (float)y), text, color, 1.0f,
            text_scale(view, style));
}

static bool rect_valid(ki_td_rect rect)
{
    return rect.width > 0 && rect.height > 0;
}

static void stroke_rect(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        uint32_t color)
{
    ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                         (float)rect.width, 1.0f, color, 1.0f);
    ki_td_soft_fill_rect(renderer, view, (float)rect.x,
                         (float)(rect.y + rect.height - 1),
                         (float)rect.width, 1.0f, color, 1.0f);
    ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                         1.0f, (float)rect.height, color, 1.0f);
    ki_td_soft_fill_rect(renderer, view,
                         (float)(rect.x + rect.width - 1), (float)rect.y,
                         1.0f, (float)rect.height, color, 1.0f);
}

void kilix_ui_draw_panel(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style,
                         const ki_td_nine_slice *skin)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected = selected_style(style, &fallback);
    if (!renderer || !view || !rect_valid(rect)) return;
    if (skin && ki_td_rgba8_is_valid(&skin->image) && skin->left > 0 &&
        skin->top > 0 && skin->right > 0 && skin->bottom > 0 &&
        skin->right < skin->image.width &&
        skin->left < skin->image.width - skin->right &&
        skin->bottom < skin->image.height &&
        skin->top < skin->image.height - skin->bottom)
        ki_td_soft_nine_slice(renderer, view, (float)rect.x, (float)rect.y,
                              rect.width, rect.height, skin,
                              selected->panel_alpha);
    else {
        ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                             (float)rect.width, (float)rect.height,
                             selected->panel_color, selected->panel_alpha);
        stroke_rect(renderer, view, rect, selected->border_color);
    }
}

static void save_clip(sr_canvas *canvas, int saved[4])
{
    saved[0] = canvas->clip_x0;
    saved[1] = canvas->clip_y0;
    saved[2] = canvas->clip_x1;
    saved[3] = canvas->clip_y1;
}

static void set_logical_clip(sr_canvas *canvas, const ki_td_view *view,
                             ki_td_rect rect)
{
    int x = ki_td_screen_x(view, (float)rect.x);
    int y = ki_td_screen_y(view, (float)rect.y);
    int right = ki_td_screen_x(view, (float)(rect.x + rect.width));
    int bottom = ki_td_screen_y(view, (float)(rect.y + rect.height));
    sr_canvas_set_clip(canvas, x, y, right - x, bottom - y);
}

static void restore_clip(sr_canvas *canvas, const int saved[4])
{
    sr_canvas_set_clip(canvas, saved[0], saved[1],
                       saved[2] - saved[0], saved[3] - saved[1]);
}

void kilix_ui_draw_list(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        const kilix_ui_style *style,
                        const ki_td_nine_slice *skin,
                        const kilix_ui_focus *focus,
                        const char *const *items, const bool *enabled,
                        size_t item_count)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected = selected_style(style, &fallback);
    sr_canvas *canvas;
    size_t first;
    size_t visible;
    size_t index;
    int saved[4];
    if (!renderer || !view || !rect_valid(rect) || !focus ||
        (item_count != 0u && !items)) return;
    kilix_ui_draw_panel(renderer, view, rect, selected, skin);
    canvas = ki_td_soft_canvas(renderer);
    if (!canvas) return;
    save_clip(canvas, saved);
    set_logical_clip(canvas, view, rect);
    first = focus->first_visible < item_count ? focus->first_visible : 0u;
    visible = focus->page_size == 0u ? item_count : focus->page_size;
    for (index = first; index < item_count && index - first < visible; ++index) {
        int row = (int)(index - first);
        int y = rect.y + selected->padding + row * selected->row_height;
        bool active = item_enabled(enabled, index);
        if (index == focus->selected) {
            ki_td_soft_fill_rect(renderer, view,
                (float)(rect.x + 2), (float)y, (float)(rect.width - 4),
                (float)selected->row_height, selected->accent_color,
                active ? 0.24f : 0.10f);
            draw_text(renderer, view, selected, rect.x + selected->padding,
                      y + 1, ">", active ? selected->accent_color :
                      selected->muted_color);
        }
        draw_text(renderer, view, selected,
                  rect.x + selected->padding + 12, y + 1,
                  items[index] ? items[index] : "",
                  active ? selected->text_color : selected->muted_color);
    }
    restore_clip(canvas, saved);
}

void kilix_ui_draw_portrait(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const ki_td_rgba8 *portrait, float alpha)
{
    if (!renderer || !view || !rect_valid(rect) ||
        !ki_td_rgba8_is_valid(portrait)) return;
    ki_td_soft_rgba_resized(renderer, view, (float)rect.x, (float)rect.y,
                            portrait, rect.width, rect.height, alpha);
}

void kilix_ui_draw_dialogue(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const kilix_ui_style *style,
                            const ki_td_nine_slice *skin,
                            const ki_td_rgba8 *portrait,
                            const char *speaker,
                            const char *const *lines, size_t line_count,
                            const char *continue_prompt)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected = selected_style(style, &fallback);
    sr_canvas *canvas;
    int saved[4];
    int portrait_size = 0;
    int text_x;
    size_t index;
    if (!renderer || !view || !rect_valid(rect) ||
        (line_count != 0u && !lines)) return;
    kilix_ui_draw_panel(renderer, view, rect, selected, skin);
    if (portrait && ki_td_rgba8_is_valid(portrait)) {
        portrait_size = rect.height - selected->padding * 2;
        if (portrait_size > 48) portrait_size = 48;
        if (portrait_size > 0)
            kilix_ui_draw_portrait(renderer, view,
                (ki_td_rect){rect.x + selected->padding,
                             rect.y + selected->padding,
                             portrait_size, portrait_size}, portrait, 1.0f);
    }
    text_x = rect.x + selected->padding +
             (portrait_size > 0 ? portrait_size + selected->padding : 0);
    canvas = ki_td_soft_canvas(renderer);
    if (!canvas) return;
    save_clip(canvas, saved);
    set_logical_clip(canvas, view, rect);
    if (speaker && speaker[0] != '\0')
        draw_text(renderer, view, selected, text_x,
                  rect.y + selected->padding, speaker,
                  selected->accent_color);
    for (index = 0u; index < line_count; ++index)
        draw_text(renderer, view, selected, text_x,
                  rect.y + selected->padding + selected->row_height +
                  (int)index * selected->row_height,
                  lines[index] ? lines[index] : "", selected->text_color);
    if (continue_prompt)
        draw_text(renderer, view, selected, text_x,
                  rect.y + rect.height - selected->padding - 16,
                  continue_prompt, selected->muted_color);
    restore_clip(canvas, saved);
}

void kilix_ui_draw_meter(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style, float value,
                         float maximum, const char *label)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected = selected_style(style, &fallback);
    float fraction = 0.0f;
    char text[96];
    if (!renderer || !view || !rect_valid(rect)) return;
    if (isfinite(value) && isfinite(maximum) && maximum > 0.0f)
        fraction = value / maximum;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                         (float)rect.width, (float)rect.height,
                         selected->panel_color, 1.0f);
    if (rect.width > 2 && rect.height > 2)
        ki_td_soft_fill_rect(renderer, view, (float)(rect.x + 1),
                             (float)(rect.y + 1),
                             (float)(rect.width - 2) * fraction,
                             (float)(rect.height - 2), selected->meter_color,
                             1.0f);
    stroke_rect(renderer, view, rect, selected->border_color);
    if (label) {
        (void)snprintf(text, sizeof text, "%s %.0f/%.0f", label,
                       (double)value, (double)maximum);
        draw_text(renderer, view, selected, rect.x + 3,
                  rect.y + (rect.height - 16) / 2, text,
                  selected->text_color);
    }
}

void kilix_ui_draw_prompts(ki_td_soft_renderer *renderer,
                           const ki_td_view *view, int x, int y,
                           int available_width,
                           const kilix_ui_style *style,
                           const kilix_ui_prompt *prompts,
                           size_t prompt_count)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected = selected_style(style, &fallback);
    int cursor = x;
    size_t index;
    if (!renderer || !view || !prompts || available_width <= 0) return;
    for (index = 0u; index < prompt_count; ++index) {
        const char *key = prompts[index].key ? prompts[index].key : "";
        const char *label = prompts[index].label ? prompts[index].label : "";
        size_t characters = strlen(key) + strlen(label) + 4u;
        int logical_scale = selected->font_scale > 0 ?
                            selected->font_scale : 1;
        int width;
        char text[96];
        if (logical_scale > 8) logical_scale = 8;
        if (characters > (size_t)INT32_MAX /
                         ((size_t)8u * (size_t)logical_scale)) break;
        width = (int)characters * 8 * logical_scale;
        if (cursor + width > x + available_width) break;
        (void)snprintf(text, sizeof text, "[%s] %s", key, label);
        draw_text(renderer, view, selected, cursor, y, text,
                  prompts[index].enabled ? selected->text_color :
                  selected->muted_color);
        cursor += width;
    }
}

static size_t composite_first(const kilix_ui_focus *focus, size_t item_count)
{
    return focus->first_visible < item_count ? focus->first_visible : 0u;
}

static size_t composite_visible(const kilix_ui_focus *focus,
                                size_t item_count)
{
    return focus->page_size == 0u ? item_count : focus->page_size;
}

static void draw_row_selection(ki_td_soft_renderer *renderer,
                               const ki_td_view *view, ki_td_rect rect,
                               const kilix_ui_style *style, int y,
                               int height, bool selected, bool enabled)
{
    if (!selected) return;
    ki_td_soft_fill_rect(renderer, view, (float)(rect.x + 2), (float)y,
                         (float)(rect.width - 4), (float)height,
                         style->accent_color, enabled ? 0.24f : 0.10f);
    draw_text(renderer, view, style, rect.x + style->padding, y + 1, ">",
              enabled ? style->accent_color : style->muted_color);
}

static bool begin_composite(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const kilix_ui_style *style,
                            const ki_td_nine_slice *skin,
                            sr_canvas **canvas, int saved[4])
{
    if (!renderer || !view || !rect_valid(rect) || !style ||
        !canvas || !saved) return false;
    kilix_ui_draw_panel(renderer, view, rect, style, skin);
    *canvas = ki_td_soft_canvas(renderer);
    if (!*canvas) return false;
    save_clip(*canvas, saved);
    set_logical_clip(*canvas, view, rect);
    return true;
}

void kilix_ui_draw_party(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style,
                         const ki_td_nine_slice *skin,
                         const kilix_ui_focus *focus,
                         const kilix_ui_party_member *members,
                         size_t member_count)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected_style_value =
        selected_style(style, &fallback);
    sr_canvas *canvas;
    int saved[4];
    size_t first;
    size_t visible;
    size_t index;
    int row_height = selected_style_value->row_height * 2;
    if (!focus || (member_count != 0u && !members) ||
        !begin_composite(renderer, view, rect, selected_style_value, skin,
                         &canvas, saved)) return;
    first = composite_first(focus, member_count);
    visible = composite_visible(focus, member_count);
    for (index = first; index < member_count && index - first < visible;
         ++index) {
        const kilix_ui_party_member *member = &members[index];
        int row = (int)(index - first);
        int y = rect.y + selected_style_value->padding + row * row_height;
        uint32_t color = member->enabled ? selected_style_value->text_color :
                         selected_style_value->muted_color;
        char heading[128];
        char stats[160];
        (void)snprintf(heading, sizeof heading, "%s%s%s%s%s",
                       member->name ? member->name : "",
                       member->level && member->level[0] ? "  " : "",
                       member->level ? member->level : "",
                       member->status && member->status[0] ? "  " : "",
                       member->status ? member->status : "");
        (void)snprintf(stats, sizeof stats, "%s %.0f/%.0f  %s %.0f/%.0f",
                       member->primary_label ? member->primary_label : "",
                       (double)member->primary,
                       (double)member->primary_maximum,
                       member->secondary_label ? member->secondary_label : "",
                       (double)member->secondary,
                       (double)member->secondary_maximum);
        draw_row_selection(renderer, view, rect, selected_style_value, y,
                           row_height, index == focus->selected,
                           member->enabled);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12, y + 1,
                  heading, color);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12,
                  y + selected_style_value->row_height, stats,
                  member->enabled ? selected_style_value->accent_color :
                  selected_style_value->muted_color);
    }
    restore_clip(canvas, saved);
}

void kilix_ui_draw_inventory(ki_td_soft_renderer *renderer,
                             const ki_td_view *view, ki_td_rect rect,
                             const kilix_ui_style *style,
                             const ki_td_nine_slice *skin,
                             const kilix_ui_focus *focus,
                             const kilix_ui_inventory_item *items,
                             size_t item_count)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected_style_value =
        selected_style(style, &fallback);
    sr_canvas *canvas;
    int saved[4];
    size_t first;
    size_t visible;
    size_t index;
    int row_height = selected_style_value->row_height * 2;
    if (!focus || (item_count != 0u && !items) ||
        !begin_composite(renderer, view, rect, selected_style_value, skin,
                         &canvas, saved)) return;
    first = composite_first(focus, item_count);
    visible = composite_visible(focus, item_count);
    for (index = first; index < item_count && index - first < visible;
         ++index) {
        const kilix_ui_inventory_item *item = &items[index];
        int row = (int)(index - first);
        int y = rect.y + selected_style_value->padding + row * row_height;
        uint32_t color = item->enabled ? selected_style_value->text_color :
                         selected_style_value->muted_color;
        char heading[128];
        char detail[160];
        (void)snprintf(heading, sizeof heading, "%s  x%d%s",
                       item->name ? item->name : "", item->quantity,
                       item->equipped ? "  [E]" : "");
        (void)snprintf(detail, sizeof detail, "%s",
                       item->detail ? item->detail : "");
        draw_row_selection(renderer, view, rect, selected_style_value, y,
                           row_height, index == focus->selected,
                           item->enabled);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12, y + 1,
                  heading, color);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12,
                  y + selected_style_value->row_height, detail,
                  item->enabled ? selected_style_value->muted_color : color);
    }
    restore_clip(canvas, saved);
}

void kilix_ui_draw_commands(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const kilix_ui_style *style,
                            const ki_td_nine_slice *skin,
                            const kilix_ui_focus *focus,
                            const kilix_ui_command *commands,
                            size_t command_count)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected_style_value =
        selected_style(style, &fallback);
    sr_canvas *canvas;
    int saved[4];
    size_t first;
    size_t visible;
    size_t index;
    if (!focus || (command_count != 0u && !commands) ||
        !begin_composite(renderer, view, rect, selected_style_value, skin,
                         &canvas, saved)) return;
    first = composite_first(focus, command_count);
    visible = composite_visible(focus, command_count);
    for (index = first; index < command_count && index - first < visible;
         ++index) {
        const kilix_ui_command *command = &commands[index];
        int row = (int)(index - first);
        int y = rect.y + selected_style_value->padding +
                row * selected_style_value->row_height;
        uint32_t color = command->enabled ? selected_style_value->text_color :
                         selected_style_value->muted_color;
        char text[160];
        (void)snprintf(text, sizeof text, "[%s] %s%s%s",
                       command->key ? command->key : "",
                       command->label ? command->label : "",
                       command->cost && command->cost[0] ? "  " : "",
                       command->cost ? command->cost : "");
        draw_row_selection(renderer, view, rect, selected_style_value, y,
                           selected_style_value->row_height,
                           index == focus->selected, command->enabled);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12, y + 1,
                  text, color);
    }
    restore_clip(canvas, saved);
}

void kilix_ui_draw_targets(ki_td_soft_renderer *renderer,
                           const ki_td_view *view, ki_td_rect rect,
                           const kilix_ui_style *style,
                           const ki_td_nine_slice *skin,
                           const kilix_ui_focus *focus,
                           const kilix_ui_target *targets,
                           size_t target_count)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected_style_value =
        selected_style(style, &fallback);
    sr_canvas *canvas;
    int saved[4];
    size_t first;
    size_t visible;
    size_t index;
    int row_height = selected_style_value->row_height * 2;
    if (!focus || (target_count != 0u && !targets) ||
        !begin_composite(renderer, view, rect, selected_style_value, skin,
                         &canvas, saved)) return;
    first = composite_first(focus, target_count);
    visible = composite_visible(focus, target_count);
    for (index = first; index < target_count && index - first < visible;
         ++index) {
        const kilix_ui_target *target = &targets[index];
        int row = (int)(index - first);
        int y = rect.y + selected_style_value->padding + row * row_height;
        uint32_t color = target->enabled ? selected_style_value->text_color :
                         selected_style_value->muted_color;
        char heading[128];
        char vitality[96];
        (void)snprintf(heading, sizeof heading, "%s%s%s",
                       target->name ? target->name : "",
                       target->status && target->status[0] ? "  " : "",
                       target->status ? target->status : "");
        (void)snprintf(vitality, sizeof vitality, "HP %.0f/%.0f",
                       (double)target->vitality,
                       (double)target->vitality_maximum);
        draw_row_selection(renderer, view, rect, selected_style_value, y,
                           row_height, index == focus->selected,
                           target->enabled);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12, y + 1,
                  heading, color);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12,
                  y + selected_style_value->row_height, vitality,
                  target->enabled ? selected_style_value->accent_color :
                  selected_style_value->muted_color);
    }
    restore_clip(canvas, saved);
}

void kilix_ui_draw_shop(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        const kilix_ui_style *style,
                        const ki_td_nine_slice *skin,
                        const kilix_ui_focus *focus,
                        const kilix_ui_shop_item *items,
                        size_t item_count, const char *currency,
                        int balance)
{
    kilix_ui_style fallback;
    const kilix_ui_style *selected_style_value =
        selected_style(style, &fallback);
    sr_canvas *canvas;
    int saved[4];
    size_t first;
    size_t visible;
    size_t index;
    char heading[128];
    if (!focus || (item_count != 0u && !items) ||
        !begin_composite(renderer, view, rect, selected_style_value, skin,
                         &canvas, saved)) return;
    (void)snprintf(heading, sizeof heading, "%s: %d",
                   currency ? currency : "Funds", balance);
    draw_text(renderer, view, selected_style_value,
              rect.x + selected_style_value->padding,
              rect.y + selected_style_value->padding, heading,
              selected_style_value->accent_color);
    first = composite_first(focus, item_count);
    visible = composite_visible(focus, item_count);
    for (index = first; index < item_count && index - first < visible;
         ++index) {
        const kilix_ui_shop_item *item = &items[index];
        int row = (int)(index - first);
        int y = rect.y + selected_style_value->padding +
                selected_style_value->row_height +
                row * selected_style_value->row_height;
        uint32_t color = item->enabled ? selected_style_value->text_color :
                         selected_style_value->muted_color;
        char text[160];
        (void)snprintf(text, sizeof text, "%s  %d  owned %d",
                       item->name ? item->name : "", item->price,
                       item->owned);
        draw_row_selection(renderer, view, rect, selected_style_value, y,
                           selected_style_value->row_height,
                           index == focus->selected, item->enabled);
        draw_text(renderer, view, selected_style_value,
                  rect.x + selected_style_value->padding + 12, y + 1,
                  text, color);
    }
    restore_clip(canvas, saved);
}
