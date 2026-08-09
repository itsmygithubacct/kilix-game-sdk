#include "kilix_ui.h"

#include <limits.h>
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

enum {
    KILIX_UI_MAX_TEXT_SCALE = 8,
    KILIX_UI_TEXT_CHUNK = 64
};

static bool canvas_ready(const sr_canvas *canvas)
{
    return canvas && canvas->px && canvas->w > 0 && canvas->h > 0 &&
           canvas->clip_x0 >= 0 && canvas->clip_x0 <= canvas->clip_x1 &&
           canvas->clip_x1 <= canvas->w && canvas->clip_y0 >= 0 &&
           canvas->clip_y0 <= canvas->clip_y1 &&
           canvas->clip_y1 <= canvas->h;
}

static bool view_valid(const ki_td_view *view)
{
    return view && isfinite(view->scale) && view->scale > 0.0f &&
           (double)view->scale <= (double)INT_MAX;
}

static bool screen_coordinate(const ki_td_view *view, int64_t logical,
                              int origin, int offset, int *result)
{
    float scaled;
    float rounded;
    int logical_pixels;
    int64_t screen;
    if (!result || !view_valid(view) || logical < INT_MIN ||
        logical > INT_MAX)
        return false;
    scaled = (float)(int)logical * view->scale;
    if (!isfinite(scaled)) return false;
    rounded = floorf(scaled + 0.5f);
    if (!isfinite(rounded) || (double)rounded < (double)INT_MIN ||
        (double)rounded > (double)INT_MAX)
        return false;
    logical_pixels = (int)rounded;
    screen = (int64_t)origin + (int64_t)offset + logical_pixels;
    if (screen < INT_MIN || screen > INT_MAX) return false;
    *result = (int)screen;
    return true;
}

static bool screen_x(const ki_td_view *view, int64_t logical, int *result)
{
    return view && screen_coordinate(view, logical, view->origin_x,
                                     view->offset_x, result);
}

static bool screen_y(const ki_td_view *view, int64_t logical, int *result)
{
    return view && screen_coordinate(view, logical, view->origin_y,
                                     view->offset_y, result);
}

static bool rect_valid(ki_td_rect rect)
{
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return rect.width > 0 && rect.height > 0 && right <= INT_MAX &&
           bottom <= INT_MAX;
}

static bool rect_screen_bounds(const ki_td_view *view, ki_td_rect rect,
                               int bounds[4])
{
    int64_t right;
    int64_t bottom;
    if (!bounds || !rect_valid(rect)) return false;
    right = (int64_t)rect.x + rect.width;
    bottom = (int64_t)rect.y + rect.height;
    return screen_x(view, rect.x, &bounds[0]) &&
           screen_y(view, rect.y, &bounds[1]) &&
           screen_x(view, right, &bounds[2]) &&
           screen_y(view, bottom, &bounds[3]) &&
           bounds[2] >= bounds[0] && bounds[3] >= bounds[1];
}

static bool draw_ready(ki_td_soft_renderer *renderer,
                       const ki_td_view *view, ki_td_rect rect)
{
    int bounds[4];
    return renderer && canvas_ready(ki_td_soft_canvas(renderer)) &&
           rect_screen_bounds(view, rect, bounds);
}

static kilix_ui_style normalized_style(const kilix_ui_style *style)
{
    kilix_ui_style selected;
    float default_alpha;
    kilix_ui_style_init(&selected);
    default_alpha = selected.panel_alpha;
    if (!style) return selected;
    selected = *style;
    if (selected.padding < 0) selected.padding = 0;
    if (selected.row_height <= 0) selected.row_height = 18;
    if (selected.font_scale < 1) selected.font_scale = 1;
    if (selected.font_scale > KILIX_UI_MAX_TEXT_SCALE)
        selected.font_scale = KILIX_UI_MAX_TEXT_SCALE;
    if (!isfinite(selected.panel_alpha)) selected.panel_alpha = default_alpha;
    if (selected.panel_alpha < 0.0f) selected.panel_alpha = 0.0f;
    if (selected.panel_alpha > 1.0f) selected.panel_alpha = 1.0f;
    return selected;
}

static int text_scale(const ki_td_view *view, const kilix_ui_style *style)
{
    int view_scale;
    int style_scale = style->font_scale;
    if (view->scale >= (float)KILIX_UI_MAX_TEXT_SCALE)
        return KILIX_UI_MAX_TEXT_SCALE;
    view_scale = (int)floorf(view->scale + 0.5f);
    if (view_scale < 1) view_scale = 1;
    if (style_scale > KILIX_UI_MAX_TEXT_SCALE / view_scale)
        style_scale = KILIX_UI_MAX_TEXT_SCALE / view_scale;
    if (style_scale < 1) style_scale = 1;
    return view_scale * style_scale;
}

/* Draw only glyphs that can reach the active clip.  Fixed-size chunks retain
 * soft-raster's exact glyph and blending behavior without scanning or
 * advancing through an unbounded offscreen suffix. */
static void draw_text(ki_td_soft_renderer *renderer, const ki_td_view *view,
                      const kilix_ui_style *style, int64_t x, int64_t y,
                      const char *text, uint32_t color)
{
    sr_canvas *canvas;
    const char *cursor = text;
    int scale;
    int screen_left;
    int screen_top;
    int advance;
    int glyph_height;
    int64_t draw_x;
    int64_t fully_left;
    int64_t visible_glyphs;
    char chunk[KILIX_UI_TEXT_CHUNK + 1];
    if (!renderer || !view_valid(view) || !style || !text) return;
    canvas = ki_td_soft_canvas(renderer);
    if (!canvas_ready(canvas) || !screen_x(view, x, &screen_left) ||
        !screen_y(view, y, &screen_top))
        return;
    scale = text_scale(view, style);
    advance = SR_FONT_W * scale;
    glyph_height = SR_FONT_H * scale;
    if ((int64_t)screen_top >= canvas->clip_y1 ||
        (int64_t)screen_top + glyph_height <= canvas->clip_y0)
        return;
    draw_x = screen_left;
    fully_left = draw_x < canvas->clip_x0 ?
        ((int64_t)canvas->clip_x0 - draw_x) / advance : 0;
    while (fully_left > 0 && *cursor != '\0') {
        ++cursor;
        draw_x += advance;
        --fully_left;
    }
    if (*cursor == '\0' || draw_x >= canvas->clip_x1) return;
    visible_glyphs = ((int64_t)canvas->clip_x1 - draw_x + advance - 1) /
                     advance;
    while (visible_glyphs > 0 && *cursor != '\0') {
        size_t capacity = visible_glyphs < KILIX_UI_TEXT_CHUNK ?
                          (size_t)visible_glyphs : KILIX_UI_TEXT_CHUNK;
        size_t length = 0u;
        while (length < capacity && cursor[length] != '\0') {
            chunk[length] = cursor[length];
            ++length;
        }
        if (length == 0u) break;
        chunk[length] = '\0';
        sr_text(canvas, (float)draw_x, (float)screen_top, chunk, color,
                1.0f, scale);
        cursor += length;
        draw_x += (int64_t)length * advance;
        visible_glyphs -= (int64_t)length;
    }
}

static void stroke_rect(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        uint32_t color)
{
    int bottom = (int)((int64_t)rect.y + rect.height - 1);
    int right = (int)((int64_t)rect.x + rect.width - 1);
    ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                         (float)rect.width, 1.0f, color, 1.0f);
    ki_td_soft_fill_rect(renderer, view, (float)rect.x,
                         (float)bottom,
                         (float)rect.width, 1.0f, color, 1.0f);
    ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                         1.0f, (float)rect.height, color, 1.0f);
    ki_td_soft_fill_rect(renderer, view, (float)right, (float)rect.y,
                         1.0f, (float)rect.height, color, 1.0f);
}

static bool skin_usable(const ki_td_nine_slice *skin, ki_td_rect rect)
{
    return skin && ki_td_rgba8_is_valid(&skin->image) && skin->left > 0 &&
           skin->top > 0 && skin->right > 0 && skin->bottom > 0 &&
           skin->right < skin->image.width &&
           skin->left < skin->image.width - skin->right &&
           skin->bottom < skin->image.height &&
           skin->top < skin->image.height - skin->bottom &&
           rect.width >= skin->left &&
           rect.width - skin->left >= skin->right &&
           rect.height >= skin->top &&
           rect.height - skin->top >= skin->bottom;
}

void kilix_ui_draw_panel(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style,
                         const ki_td_nine_slice *skin)
{
    kilix_ui_style selected = normalized_style(style);
    if (!draw_ready(renderer, view, rect)) return;
    if (skin_usable(skin, rect))
        ki_td_soft_nine_slice(renderer, view, (float)rect.x, (float)rect.y,
                              rect.width, rect.height, skin,
                              selected.panel_alpha);
    else {
        ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                             (float)rect.width, (float)rect.height,
                             selected.panel_color, selected.panel_alpha);
        stroke_rect(renderer, view, rect, selected.border_color);
    }
}

static void save_clip(sr_canvas *canvas, int saved[4])
{
    saved[0] = canvas->clip_x0;
    saved[1] = canvas->clip_y0;
    saved[2] = canvas->clip_x1;
    saved[3] = canvas->clip_y1;
}

static bool set_logical_clip(sr_canvas *canvas, const ki_td_view *view,
                             ki_td_rect rect, const int saved[4])
{
    int bounds[4];
    int left;
    int top;
    int right;
    int bottom;
    if (!canvas_ready(canvas) || !saved ||
        !rect_screen_bounds(view, rect, bounds))
        return false;
    left = bounds[0] > saved[0] ? bounds[0] : saved[0];
    top = bounds[1] > saved[1] ? bounds[1] : saved[1];
    right = bounds[2] < saved[2] ? bounds[2] : saved[2];
    bottom = bounds[3] < saved[3] ? bounds[3] : saved[3];
    if (right < left) right = left;
    if (bottom < top) bottom = top;
    sr_canvas_set_clip(canvas, left, top, right - left, bottom - top);
    return true;
}

static void restore_clip(sr_canvas *canvas, const int saved[4])
{
    sr_canvas_set_clip(canvas, saved[0], saved[1],
                       saved[2] - saved[0], saved[3] - saved[1]);
}

typedef struct ui_range {
    size_t first;
    size_t count;
} ui_range;

static size_t logical_row_capacity(ki_td_rect rect, int padding,
                                   int64_t row_offset, int64_t row_stride)
{
    int64_t bottom = (int64_t)rect.y + rect.height;
    int64_t first_y = (int64_t)rect.y + padding + row_offset;
    int64_t remaining;
    if (row_stride <= 0 || first_y >= bottom) return 0u;
    remaining = bottom - first_y;
    return (size_t)((remaining + row_stride - 1) / row_stride);
}

static ui_range visible_range(const kilix_ui_focus *focus,
                              size_t item_count, ki_td_rect rect,
                              int padding, int64_t row_offset,
                              int64_t row_stride)
{
    ui_range range = {0u, 0u};
    size_t available;
    size_t requested;
    size_t capacity;
    if (!focus || item_count == 0u) return range;
    range.first = focus->first_visible < item_count ?
                  focus->first_visible : 0u;
    available = item_count - range.first;
    requested = focus->page_size == 0u ? available : focus->page_size;
    if (requested > available) requested = available;
    capacity = logical_row_capacity(rect, padding, row_offset, row_stride);
    range.count = requested < capacity ? requested : capacity;
    return range;
}

static bool begin_panel(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        const kilix_ui_style *style,
                        const ki_td_nine_slice *skin,
                        sr_canvas **canvas, int saved[4])
{
    if (!style || !canvas || !saved || !draw_ready(renderer, view, rect))
        return false;
    kilix_ui_draw_panel(renderer, view, rect, style, skin);
    *canvas = ki_td_soft_canvas(renderer);
    save_clip(*canvas, saved);
    if (!set_logical_clip(*canvas, view, rect, saved)) {
        restore_clip(*canvas, saved);
        return false;
    }
    return true;
}

static void draw_row_selection(ki_td_soft_renderer *renderer,
                               const ki_td_view *view, ki_td_rect rect,
                               const kilix_ui_style *style, int64_t y,
                               int64_t height, bool selected, bool enabled)
{
    int64_t bottom;
    int clipped_height;
    if (!selected) return;
    bottom = (int64_t)rect.y + rect.height;
    if (y < rect.y || y >= bottom || height <= 0) return;
    if (height > bottom - y) height = bottom - y;
    clipped_height = (int)height;
    if (rect.width > 4)
        ki_td_soft_fill_rect(renderer, view, (float)(rect.x + 2),
                             (float)(int)y, (float)(rect.width - 4),
                             (float)clipped_height, style->accent_color,
                             enabled ? 0.24f : 0.10f);
    draw_text(renderer, view, style, (int64_t)rect.x + style->padding,
              y + 1, ">", enabled ? style->accent_color :
              style->muted_color);
}

void kilix_ui_draw_list(ki_td_soft_renderer *renderer,
                        const ki_td_view *view, ki_td_rect rect,
                        const kilix_ui_style *style,
                        const ki_td_nine_slice *skin,
                        const kilix_ui_focus *focus,
                        const char *const *items, const bool *enabled,
                        size_t item_count)
{
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    ui_range range;
    size_t offset;
    int saved[4];
    if (!focus || (item_count != 0u && !items) ||
        !begin_panel(renderer, view, rect, &selected, skin, &canvas, saved))
        return;
    range = visible_range(focus, item_count, rect, selected.padding, 0,
                          selected.row_height);
    for (offset = 0u; offset < range.count; ++offset) {
        size_t index = range.first + offset;
        int64_t y = (int64_t)rect.y + selected.padding +
                    (int64_t)offset * selected.row_height;
        bool active = item_enabled(enabled, index);
        draw_row_selection(renderer, view, rect, &selected, y,
                           selected.row_height,
                           index == focus->selected, active);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12, y + 1,
                  items[index] ? items[index] : "",
                  active ? selected.text_color : selected.muted_color);
    }
    restore_clip(canvas, saved);
}

void kilix_ui_draw_portrait(ki_td_soft_renderer *renderer,
                            const ki_td_view *view, ki_td_rect rect,
                            const ki_td_rgba8 *portrait, float alpha)
{
    if (!draw_ready(renderer, view, rect) ||
        !ki_td_rgba8_is_valid(portrait) || !isfinite(alpha) || alpha <= 0.0f)
        return;
    if (alpha > 1.0f) alpha = 1.0f;
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
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    int saved[4];
    int portrait_size = 0;
    int64_t text_x;
    int64_t portrait_space;
    size_t visible_lines;
    size_t index;
    if ((line_count != 0u && !lines) ||
        !begin_panel(renderer, view, rect, &selected, skin, &canvas, saved))
        return;
    portrait_space = (int64_t)rect.height - (int64_t)selected.padding * 2;
    if (portrait && ki_td_rgba8_is_valid(portrait)) {
        int64_t portrait_x;
        int64_t portrait_y;
        if (portrait_space > 0) portrait_size = (int)portrait_space;
        if (portrait_size > 48) portrait_size = 48;
        portrait_x = (int64_t)rect.x + selected.padding;
        portrait_y = (int64_t)rect.y + selected.padding;
        if (portrait_size > 0 && portrait_x >= INT_MIN &&
            portrait_x <= INT_MAX && portrait_y >= INT_MIN &&
            portrait_y <= INT_MAX)
            kilix_ui_draw_portrait(renderer, view,
                (ki_td_rect){(int)portrait_x, (int)portrait_y,
                             portrait_size, portrait_size}, portrait, 1.0f);
    }
    text_x = (int64_t)rect.x + selected.padding +
             (portrait_size > 0 ? portrait_size + selected.padding : 0);
    if (speaker && speaker[0] != '\0')
        draw_text(renderer, view, &selected, text_x,
                  (int64_t)rect.y + selected.padding, speaker,
                  selected.accent_color);
    visible_lines = logical_row_capacity(rect, selected.padding,
                                         selected.row_height,
                                         selected.row_height);
    if (visible_lines > line_count) visible_lines = line_count;
    for (index = 0u; index < visible_lines; ++index)
        draw_text(renderer, view, &selected, text_x,
                  (int64_t)rect.y + selected.padding + selected.row_height +
                  (int64_t)index * selected.row_height,
                  lines[index] ? lines[index] : "", selected.text_color);
    if (continue_prompt)
        draw_text(renderer, view, &selected, text_x,
                  (int64_t)rect.y + rect.height - selected.padding - 16,
                  continue_prompt, selected.muted_color);
    restore_clip(canvas, saved);
}

static void append_bounded(char *buffer, size_t capacity, size_t *length,
                           const char *text);

void kilix_ui_draw_meter(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style, float value,
                         float maximum, const char *label)
{
    kilix_ui_style selected = normalized_style(style);
    float fraction = 0.0f;
    char text[96];
    if (!draw_ready(renderer, view, rect)) return;
    if (isfinite(value) && isfinite(maximum) && maximum > 0.0f)
        fraction = value / maximum;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    ki_td_soft_fill_rect(renderer, view, (float)rect.x, (float)rect.y,
                         (float)rect.width, (float)rect.height,
                         selected.panel_color, 1.0f);
    if (rect.width > 2 && rect.height > 2)
        ki_td_soft_fill_rect(renderer, view, (float)(rect.x + 1),
                             (float)(rect.y + 1),
                             (float)(rect.width - 2) * fraction,
                             (float)(rect.height - 2), selected.meter_color,
                             1.0f);
    stroke_rect(renderer, view, rect, selected.border_color);
    if (label) {
        char numbers[640];
        size_t length = 0u;
        (void)snprintf(numbers, sizeof numbers, "%.0f/%.0f",
                       (double)value, (double)maximum);
        text[0] = '\0';
        append_bounded(text, sizeof text, &length, label);
        append_bounded(text, sizeof text, &length, " ");
        append_bounded(text, sizeof text, &length, numbers);
        draw_text(renderer, view, &selected, (int64_t)rect.x + 3,
                  (int64_t)rect.y + (rect.height - 16) / 2, text,
                  selected.text_color);
    }
}

static void append_bounded(char *buffer, size_t capacity, size_t *length,
                           const char *text)
{
    if (!buffer || !length || !text || capacity == 0u) return;
    while (*length + 1u < capacity && *text != '\0') {
        buffer[*length] = *text;
        ++*length;
        ++text;
    }
    buffer[*length] = '\0';
}

static void append_integer(char *buffer, size_t capacity, size_t *length,
                           int value)
{
    char number[16];
    (void)snprintf(number, sizeof number, "%d", value);
    append_bounded(buffer, capacity, length, number);
}

static void append_float0(char *buffer, size_t capacity, size_t *length,
                          float value)
{
    char number[320];
    (void)snprintf(number, sizeof number, "%.0f", (double)value);
    append_bounded(buffer, capacity, length, number);
}

void kilix_ui_draw_prompts(ki_td_soft_renderer *renderer,
                           const ki_td_view *view, int x, int y,
                           int available_width,
                           const kilix_ui_style *style,
                           const kilix_ui_prompt *prompts,
                           size_t prompt_count)
{
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    int64_t cursor = x;
    int64_t right = (int64_t)x + available_width;
    size_t index;
    if (!renderer || !view_valid(view) || !prompts || available_width <= 0)
        return;
    canvas = ki_td_soft_canvas(renderer);
    if (!canvas_ready(canvas)) return;
    for (index = 0u; index < prompt_count; ++index) {
        const char *key = prompts[index].key ? prompts[index].key : "";
        const char *label = prompts[index].label ? prompts[index].label : "";
        char text[96];
        size_t length = 0u;
        int64_t width;
        text[0] = '\0';
        append_bounded(text, sizeof text, &length, "[");
        append_bounded(text, sizeof text, &length, key);
        append_bounded(text, sizeof text, &length, "] ");
        append_bounded(text, sizeof text, &length, label);
        width = (int64_t)(length + 1u) * SR_FONT_W * selected.font_scale;
        if (cursor + width > right) break;
        draw_text(renderer, view, &selected, cursor, y, text,
                  prompts[index].enabled ? selected.text_color :
                  selected.muted_color);
        cursor += width;
    }
}

void kilix_ui_draw_party(ki_td_soft_renderer *renderer,
                         const ki_td_view *view, ki_td_rect rect,
                         const kilix_ui_style *style,
                         const ki_td_nine_slice *skin,
                         const kilix_ui_focus *focus,
                         const kilix_ui_party_member *members,
                         size_t member_count)
{
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    int saved[4];
    ui_range range;
    size_t offset;
    int64_t row_height = (int64_t)selected.row_height * 2;
    if (!focus || (member_count != 0u && !members) ||
        !begin_panel(renderer, view, rect, &selected, skin, &canvas, saved))
        return;
    range = visible_range(focus, member_count, rect, selected.padding, 0,
                          row_height);
    for (offset = 0u; offset < range.count; ++offset) {
        size_t index = range.first + offset;
        const kilix_ui_party_member *member = &members[index];
        int64_t y = (int64_t)rect.y + selected.padding +
                    (int64_t)offset * row_height;
        uint32_t color = member->enabled ? selected.text_color :
                         selected.muted_color;
        char heading[128];
        char stats[160];
        size_t heading_length = 0u;
        size_t stats_length = 0u;
        heading[0] = '\0';
        append_bounded(heading, sizeof heading, &heading_length,
                       member->name ? member->name : "");
        if (member->level && member->level[0] != '\0')
            append_bounded(heading, sizeof heading, &heading_length, "  ");
        append_bounded(heading, sizeof heading, &heading_length,
                       member->level ? member->level : "");
        if (member->status && member->status[0] != '\0')
            append_bounded(heading, sizeof heading, &heading_length, "  ");
        append_bounded(heading, sizeof heading, &heading_length,
                       member->status ? member->status : "");
        stats[0] = '\0';
        append_bounded(stats, sizeof stats, &stats_length,
                       member->primary_label ? member->primary_label : "");
        append_bounded(stats, sizeof stats, &stats_length, " ");
        append_float0(stats, sizeof stats, &stats_length, member->primary);
        append_bounded(stats, sizeof stats, &stats_length, "/");
        append_float0(stats, sizeof stats, &stats_length,
                      member->primary_maximum);
        append_bounded(stats, sizeof stats, &stats_length, "  ");
        append_bounded(stats, sizeof stats, &stats_length,
                       member->secondary_label ? member->secondary_label : "");
        append_bounded(stats, sizeof stats, &stats_length, " ");
        append_float0(stats, sizeof stats, &stats_length, member->secondary);
        append_bounded(stats, sizeof stats, &stats_length, "/");
        append_float0(stats, sizeof stats, &stats_length,
                      member->secondary_maximum);
        draw_row_selection(renderer, view, rect, &selected, y,
                           row_height, index == focus->selected,
                           member->enabled);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12, y + 1,
                  heading, color);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12,
                  y + selected.row_height, stats,
                  member->enabled ? selected.accent_color :
                  selected.muted_color);
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
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    int saved[4];
    ui_range range;
    size_t offset;
    int64_t row_height = (int64_t)selected.row_height * 2;
    if (!focus || (item_count != 0u && !items) ||
        !begin_panel(renderer, view, rect, &selected, skin, &canvas, saved))
        return;
    range = visible_range(focus, item_count, rect, selected.padding, 0,
                          row_height);
    for (offset = 0u; offset < range.count; ++offset) {
        size_t index = range.first + offset;
        const kilix_ui_inventory_item *item = &items[index];
        int64_t y = (int64_t)rect.y + selected.padding +
                    (int64_t)offset * row_height;
        uint32_t color = item->enabled ? selected.text_color :
                         selected.muted_color;
        char heading[128];
        char detail[160];
        size_t heading_length = 0u;
        size_t detail_length = 0u;
        heading[0] = '\0';
        append_bounded(heading, sizeof heading, &heading_length,
                       item->name ? item->name : "");
        append_bounded(heading, sizeof heading, &heading_length, "  x");
        append_integer(heading, sizeof heading, &heading_length,
                       item->quantity);
        if (item->equipped)
            append_bounded(heading, sizeof heading, &heading_length, "  [E]");
        detail[0] = '\0';
        append_bounded(detail, sizeof detail, &detail_length,
                       item->detail ? item->detail : "");
        draw_row_selection(renderer, view, rect, &selected, y,
                           row_height, index == focus->selected,
                           item->enabled);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12, y + 1,
                  heading, color);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12,
                  y + selected.row_height, detail,
                  item->enabled ? selected.muted_color : color);
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
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    int saved[4];
    ui_range range;
    size_t offset;
    if (!focus || (command_count != 0u && !commands) ||
        !begin_panel(renderer, view, rect, &selected, skin, &canvas, saved))
        return;
    range = visible_range(focus, command_count, rect, selected.padding, 0,
                          selected.row_height);
    for (offset = 0u; offset < range.count; ++offset) {
        size_t index = range.first + offset;
        const kilix_ui_command *command = &commands[index];
        int64_t y = (int64_t)rect.y + selected.padding +
                    (int64_t)offset * selected.row_height;
        uint32_t color = command->enabled ? selected.text_color :
                         selected.muted_color;
        char text[160];
        size_t length = 0u;
        text[0] = '\0';
        append_bounded(text, sizeof text, &length, "[");
        append_bounded(text, sizeof text, &length,
                       command->key ? command->key : "");
        append_bounded(text, sizeof text, &length, "] ");
        append_bounded(text, sizeof text, &length,
                       command->label ? command->label : "");
        if (command->cost && command->cost[0] != '\0')
            append_bounded(text, sizeof text, &length, "  ");
        append_bounded(text, sizeof text, &length,
                       command->cost ? command->cost : "");
        draw_row_selection(renderer, view, rect, &selected, y,
                           selected.row_height,
                           index == focus->selected, command->enabled);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12, y + 1,
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
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    int saved[4];
    ui_range range;
    size_t offset;
    int64_t row_height = (int64_t)selected.row_height * 2;
    if (!focus || (target_count != 0u && !targets) ||
        !begin_panel(renderer, view, rect, &selected, skin, &canvas, saved))
        return;
    range = visible_range(focus, target_count, rect, selected.padding, 0,
                          row_height);
    for (offset = 0u; offset < range.count; ++offset) {
        size_t index = range.first + offset;
        const kilix_ui_target *target = &targets[index];
        int64_t y = (int64_t)rect.y + selected.padding +
                    (int64_t)offset * row_height;
        uint32_t color = target->enabled ? selected.text_color :
                         selected.muted_color;
        char heading[128];
        char vitality[96];
        size_t heading_length = 0u;
        size_t vitality_length = 0u;
        heading[0] = '\0';
        append_bounded(heading, sizeof heading, &heading_length,
                       target->name ? target->name : "");
        if (target->status && target->status[0] != '\0')
            append_bounded(heading, sizeof heading, &heading_length, "  ");
        append_bounded(heading, sizeof heading, &heading_length,
                       target->status ? target->status : "");
        vitality[0] = '\0';
        append_bounded(vitality, sizeof vitality, &vitality_length, "HP ");
        append_float0(vitality, sizeof vitality, &vitality_length,
                      target->vitality);
        append_bounded(vitality, sizeof vitality, &vitality_length, "/");
        append_float0(vitality, sizeof vitality, &vitality_length,
                      target->vitality_maximum);
        draw_row_selection(renderer, view, rect, &selected, y,
                           row_height, index == focus->selected,
                           target->enabled);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12, y + 1,
                  heading, color);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12,
                  y + selected.row_height, vitality,
                  target->enabled ? selected.accent_color :
                  selected.muted_color);
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
    kilix_ui_style selected = normalized_style(style);
    sr_canvas *canvas;
    int saved[4];
    ui_range range;
    size_t offset;
    char heading[128];
    char balance_text[16];
    size_t heading_length = 0u;
    if (!focus || (item_count != 0u && !items) ||
        !begin_panel(renderer, view, rect, &selected, skin, &canvas, saved))
        return;
    (void)snprintf(balance_text, sizeof balance_text, "%d", balance);
    heading[0] = '\0';
    append_bounded(heading, sizeof heading, &heading_length,
                   currency ? currency : "Funds");
    append_bounded(heading, sizeof heading, &heading_length, ": ");
    append_bounded(heading, sizeof heading, &heading_length, balance_text);
    draw_text(renderer, view, &selected,
              (int64_t)rect.x + selected.padding,
              (int64_t)rect.y + selected.padding, heading,
              selected.accent_color);
    range = visible_range(focus, item_count, rect, selected.padding,
                          selected.row_height, selected.row_height);
    for (offset = 0u; offset < range.count; ++offset) {
        size_t index = range.first + offset;
        const kilix_ui_shop_item *item = &items[index];
        int64_t y = (int64_t)rect.y + selected.padding +
                    selected.row_height +
                    (int64_t)offset * selected.row_height;
        uint32_t color = item->enabled ? selected.text_color :
                         selected.muted_color;
        char text[160];
        size_t length = 0u;
        text[0] = '\0';
        append_bounded(text, sizeof text, &length,
                       item->name ? item->name : "");
        append_bounded(text, sizeof text, &length, "  ");
        append_integer(text, sizeof text, &length, item->price);
        append_bounded(text, sizeof text, &length, "  owned ");
        append_integer(text, sizeof text, &length, item->owned);
        draw_row_selection(renderer, view, rect, &selected, y,
                           selected.row_height,
                           index == focus->selected, item->enabled);
        draw_text(renderer, view, &selected,
                  (int64_t)rect.x + selected.padding + 12, y + 1,
                  text, color);
    }
    restore_clip(canvas, saved);
}
