#include "kilix_top_down_view.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

static bool rect_is_valid(ki_td_rect rect)
{
    return rect.width > 0 && rect.height > 0;
}

static bool finite_float_to_int(float value, int *result)
{
    if (!result || !isfinite(value) ||
        (double)value < (double)INT_MIN ||
        (double)value > (double)INT_MAX)
        return false;
    *result = (int)value;
    return true;
}

bool ki_td_fit_spec_init(ki_td_fit_spec *spec, int logical_width,
                         int logical_height, int framebuffer_width,
                         int framebuffer_height)
{
    if (!spec || logical_width <= 0 || logical_height <= 0 ||
        framebuffer_width <= 0 || framebuffer_height <= 0)
        return false;

    spec->logical_width = logical_width;
    spec->logical_height = logical_height;
    spec->fit_bounds = (ki_td_rect){0, 0, framebuffer_width,
                                    framebuffer_height};
    spec->align_bounds = spec->fit_bounds;
    spec->scale_policy = KI_TD_SCALE_FRACTIONAL;
    spec->minimum_scale = 0.0f;
    spec->integer_scale_threshold = 2.0f;
    spec->clamp_origin_x = false;
    spec->clamp_origin_y = false;
    return true;
}

bool ki_td_view_fit(ki_td_view *view, const ki_td_fit_spec *spec)
{
    if (!view || !spec || spec->logical_width <= 0 ||
        spec->logical_height <= 0 || !rect_is_valid(spec->fit_bounds) ||
        !rect_is_valid(spec->align_bounds) ||
        !isfinite(spec->minimum_scale) || spec->minimum_scale < 0.0f ||
        !isfinite(spec->integer_scale_threshold) ||
        spec->integer_scale_threshold < 1.0f ||
        spec->scale_policy < KI_TD_SCALE_FRACTIONAL ||
        spec->scale_policy > KI_TD_SCALE_PIXEL_ART)
        return false;

    float scale_x = (float)spec->fit_bounds.width /
                    (float)spec->logical_width;
    float scale_y = (float)spec->fit_bounds.height /
                    (float)spec->logical_height;
    float candidate = fminf(scale_x, scale_y);
    if (!isfinite(candidate) || candidate <= 0.0f)
        return false;

    float scale = candidate;
    if (spec->scale_policy == KI_TD_SCALE_INTEGER) {
        scale = floorf(candidate);
    } else if (spec->scale_policy == KI_TD_SCALE_PIXEL_ART) {
        scale = floorf(candidate);
        if (scale < spec->integer_scale_threshold)
            scale = candidate;
    }
    if (scale < spec->minimum_scale)
        scale = spec->minimum_scale;
    if (!isfinite(scale) || scale <= 0.0f)
        return false;

    ki_td_view next = {0};
    float centered_x =
        ((float)spec->align_bounds.width -
         (float)spec->logical_width * scale) * 0.5f;
    float centered_y =
        ((float)spec->align_bounds.height -
         (float)spec->logical_height * scale) * 0.5f;
    int centered_x_int;
    int centered_y_int;
    int64_t origin_x;
    int64_t origin_y;
    if (!finite_float_to_int(centered_x, &centered_x_int) ||
        !finite_float_to_int(centered_y, &centered_y_int))
        return false;
    origin_x = (int64_t)spec->align_bounds.x + (int64_t)centered_x_int;
    origin_y = (int64_t)spec->align_bounds.y + (int64_t)centered_y_int;
    if (origin_x < INT_MIN || origin_x > INT_MAX ||
        origin_y < INT_MIN || origin_y > INT_MAX)
        return false;
    next.logical_width = spec->logical_width;
    next.logical_height = spec->logical_height;
    next.scale = scale;
    next.origin_x = (int)origin_x;
    next.origin_y = (int)origin_y;
    if (spec->clamp_origin_x && next.origin_x < spec->align_bounds.x)
        next.origin_x = spec->align_bounds.x;
    if (spec->clamp_origin_y && next.origin_y < spec->align_bounds.y)
        next.origin_y = spec->align_bounds.y;
    *view = next;
    return true;
}

void ki_td_view_set_offset(ki_td_view *view, int x, int y)
{
    if (!view) return;
    view->offset_x = x;
    view->offset_y = y;
}

static int screen_coordinate(const ki_td_view *view, float logical,
                             int origin, int offset)
{
    float scaled;
    float rounded;
    int logical_pixels;
    int64_t screen;
    if (!view || !isfinite(logical) || !isfinite(view->scale) ||
        view->scale <= 0.0f)
        return 0;
    scaled = logical * view->scale;
    if (!isfinite(scaled)) return 0;
    rounded = floorf(scaled + 0.5f);
    if (!finite_float_to_int(rounded, &logical_pixels)) return 0;
    screen = (int64_t)origin + (int64_t)offset +
             (int64_t)logical_pixels;
    return screen < INT_MIN || screen > INT_MAX ? 0 : (int)screen;
}

int ki_td_screen_x(const ki_td_view *view, float logical_x)
{
    return view ? screen_coordinate(
                      view, logical_x, view->origin_x, view->offset_x) : 0;
}

int ki_td_screen_y(const ki_td_view *view, float logical_y)
{
    return view ? screen_coordinate(
                      view, logical_y, view->origin_y, view->offset_y) : 0;
}

float ki_td_screen_scale(const ki_td_view *view, float logical_length)
{
    float scaled;
    if (!view || !isfinite(logical_length) || !isfinite(view->scale) ||
        view->scale <= 0.0f)
        return 0.0f;
    scaled = logical_length * view->scale;
    return isfinite(scaled) ? scaled : 0.0f;
}

bool ki_td_screen_to_logical(const ki_td_view *view, float screen_x,
                             float screen_y, float *logical_x,
                             float *logical_y)
{
    float x;
    float y;
    if (!view || !logical_x || !logical_y || !isfinite(screen_x) ||
        !isfinite(screen_y) || !isfinite(view->scale) ||
        view->scale <= 0.0f)
        return false;
    x = (screen_x - (float)view->origin_x - (float)view->offset_x) /
        view->scale;
    y = (screen_y - (float)view->origin_y - (float)view->offset_y) /
        view->scale;
    if (!isfinite(x) || !isfinite(y)) return false;
    *logical_x = x;
    *logical_y = y;
    return true;
}

static int clipped_grid_edge(double logical, int cell_size, int limit,
                             bool upper)
{
    double edge = logical / (double)cell_size;
    double rounded = upper ? ceil(edge) : floor(edge);
    if (rounded <= 0.0) return 0;
    if (rounded >= (double)limit) return limit;
    return (int)rounded;
}

bool ki_td_view_visible_cells(const ki_td_view *view,
                              ki_td_rect screen_bounds,
                              float grid_origin_x, float grid_origin_y,
                              int cell_width, int cell_height,
                              int columns, int rows, int padding,
                              ki_td_cell_bounds *bounds)
{
    double screen_right;
    double screen_bottom;
    double logical_left;
    double logical_top;
    double logical_right;
    double logical_bottom;
    int first_column;
    int first_row;
    int last_column;
    int last_row;
    if (!view || !bounds || !rect_is_valid(screen_bounds) ||
        !isfinite(view->scale) || view->scale <= 0.0f ||
        !isfinite(grid_origin_x) || !isfinite(grid_origin_y) ||
        cell_width <= 0 || cell_height <= 0 || columns <= 0 || rows <= 0 ||
        padding < 0)
        return false;

    screen_right = (double)screen_bounds.x + (double)screen_bounds.width;
    screen_bottom = (double)screen_bounds.y + (double)screen_bounds.height;
    logical_left = ((double)screen_bounds.x - (double)view->origin_x -
                    (double)view->offset_x) / (double)view->scale -
                   (double)grid_origin_x;
    logical_top = ((double)screen_bounds.y - (double)view->origin_y -
                   (double)view->offset_y) / (double)view->scale -
                  (double)grid_origin_y;
    logical_right = (screen_right - (double)view->origin_x -
                     (double)view->offset_x) / (double)view->scale -
                    (double)grid_origin_x;
    logical_bottom = (screen_bottom - (double)view->origin_y -
                      (double)view->offset_y) / (double)view->scale -
                     (double)grid_origin_y;
    if (!isfinite(logical_left) || !isfinite(logical_top) ||
        !isfinite(logical_right) || !isfinite(logical_bottom))
        return false;

    first_column = clipped_grid_edge(logical_left, cell_width, columns,
                                     false);
    first_row = clipped_grid_edge(logical_top, cell_height, rows, false);
    last_column = clipped_grid_edge(logical_right, cell_width, columns, true);
    last_row = clipped_grid_edge(logical_bottom, cell_height, rows, true);
    first_column = first_column > padding ? first_column - padding : 0;
    first_row = first_row > padding ? first_row - padding : 0;
    last_column = last_column < columns - padding ?
        last_column + padding : columns;
    last_row = last_row < rows - padding ? last_row + padding : rows;
    if (last_column < first_column) last_column = first_column;
    if (last_row < first_row) last_row = first_row;
    *bounds = (ki_td_cell_bounds){
        first_column, first_row, last_column - first_column,
        last_row - first_row
    };
    return true;
}

static uint32_t visual_noise(uint32_t value)
{
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value;
}

int ki_td_shake_axis(uint32_t frame, float magnitude, uint32_t salt)
{
    int displacement;
    if (!isfinite(magnitude) || magnitude <= 0.0f) return 0;
    uint32_t bits = visual_noise(frame ^ salt);
    float unit = (float)(bits >> 8) * (1.0f / 16777216.0f);
    if (!finite_float_to_int(
            (unit - 0.5f) * magnitude, &displacement))
        return 0;
    return displacement;
}
