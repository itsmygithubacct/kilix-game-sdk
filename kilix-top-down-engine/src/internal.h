#ifndef KILIX_TOP_DOWN_INTERNAL_H
#define KILIX_TOP_DOWN_INTERNAL_H

#include "kilix_top_down_types.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static inline bool ki_td_internal_float_to_int(float value, int *result)
{
    if (!result || !isfinite(value) ||
        (double)value < (double)INT_MIN ||
        (double)value > (double)INT_MAX)
        return false;
    *result = (int)value;
    return true;
}

static inline bool ki_td_internal_view_valid(const ki_td_view *view)
{
    return view && isfinite(view->scale) && view->scale > 0.0f &&
           (double)view->scale <= (double)INT_MAX;
}

static inline bool ki_td_internal_screen_coordinate(
    const ki_td_view *view, float logical, int origin, int offset,
    int *result)
{
    float scaled;
    float rounded;
    int logical_pixels;
    int64_t screen;
    if (!result || !ki_td_internal_view_valid(view) || !isfinite(logical))
        return false;
    scaled = logical * view->scale;
    if (!isfinite(scaled)) return false;
    rounded = floorf(scaled + 0.5f);
    if (!ki_td_internal_float_to_int(rounded, &logical_pixels)) return false;
    screen = (int64_t)origin + (int64_t)offset + logical_pixels;
    if (screen < INT_MIN || screen > INT_MAX) return false;
    *result = (int)screen;
    return true;
}

static inline bool ki_td_internal_screen_x(const ki_td_view *view,
                                           float logical, int *result)
{
    return view && ki_td_internal_screen_coordinate(
        view, logical, view->origin_x, view->offset_x, result);
}

static inline bool ki_td_internal_screen_y(const ki_td_view *view,
                                           float logical, int *result)
{
    return view && ki_td_internal_screen_coordinate(
        view, logical, view->origin_y, view->offset_y, result);
}

static inline bool ki_td_internal_screen_scale(const ki_td_view *view,
                                               float logical,
                                               float *result)
{
    float scaled;
    if (!result || !ki_td_internal_view_valid(view) ||
        !isfinite(logical))
        return false;
    scaled = logical * view->scale;
    if (!isfinite(scaled) || (double)scaled < (double)INT_MIN ||
        (double)scaled > (double)INT_MAX)
        return false;
    *result = scaled;
    return true;
}

#endif /* KILIX_TOP_DOWN_INTERNAL_H */
