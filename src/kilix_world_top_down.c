#include "kilix_world_top_down.h"

#include <limits.h>
#include <math.h>

static bool layout_valid(const kilix_world_td_layout *layout)
{
    return layout && isfinite(layout->origin_x) &&
           isfinite(layout->origin_y) && isfinite(layout->cell_width) &&
           isfinite(layout->cell_height) && layout->cell_width > 0.0f &&
           layout->cell_height > 0.0f;
}

bool kilix_world_td_layout_init(kilix_world_td_layout *layout,
                                float origin_x, float origin_y,
                                float cell_width, float cell_height)
{
    kilix_world_td_layout next = {
        origin_x, origin_y, cell_width, cell_height
    };
    if (!layout || !layout_valid(&next)) return false;
    *layout = next;
    return true;
}

kilix_world_result kilix_world_td_cell_rect(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    kilix_world_cell cell, float inset, kilix_world_td_rect *rect)
{
    kilix_world_td_rect next;
    if (!grid || !layout_valid(layout) || !rect || !isfinite(inset) ||
        inset < 0.0f || inset * 2.0f >= layout->cell_width ||
        inset * 2.0f >= layout->cell_height)
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (!kilix_world_in_bounds(grid, cell)) return KILIX_WORLD_OUT_OF_BOUNDS;
    next.x = layout->origin_x + (float)cell.x * layout->cell_width + inset;
    next.y = layout->origin_y + (float)cell.y * layout->cell_height + inset;
    next.width = layout->cell_width - inset * 2.0f;
    next.height = layout->cell_height - inset * 2.0f;
    if (!isfinite(next.x) || !isfinite(next.y) || !isfinite(next.width) ||
        !isfinite(next.height))
        return KILIX_WORLD_OVERFLOW;
    *rect = next;
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_td_point_cell(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    float logical_x, float logical_y, kilix_world_cell *cell)
{
    double column;
    double row;
    kilix_world_cell next;
    if (!grid || !layout_valid(layout) || !cell || !isfinite(logical_x) ||
        !isfinite(logical_y))
        return KILIX_WORLD_INVALID_ARGUMENT;
    column = floor(((double)logical_x - (double)layout->origin_x) /
                   (double)layout->cell_width);
    row = floor(((double)logical_y - (double)layout->origin_y) /
                (double)layout->cell_height);
    if (column < (double)INT32_MIN || column > (double)INT32_MAX ||
        row < (double)INT32_MIN || row > (double)INT32_MAX)
        return KILIX_WORLD_OUT_OF_BOUNDS;
    next = (kilix_world_cell){(int32_t)column, (int32_t)row};
    if (!kilix_world_in_bounds(grid, next)) return KILIX_WORLD_OUT_OF_BOUNDS;
    *cell = next;
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_td_cell_rects(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    const kilix_world_cell *cells, size_t cell_count, float inset,
    kilix_world_td_rect *rects, size_t rect_capacity, size_t *rect_count)
{
    size_t index;
    if (!grid || !layout_valid(layout) || !rect_count ||
        !isfinite(inset) || inset < 0.0f ||
        inset * 2.0f >= layout->cell_width ||
        inset * 2.0f >= layout->cell_height ||
        (cell_count > 0u && !cells) ||
        (rect_capacity > 0u && !rects))
        return KILIX_WORLD_INVALID_ARGUMENT;
    *rect_count = cell_count;
    if (rect_capacity < cell_count) return KILIX_WORLD_NO_SPACE;
    for (index = 0u; index < cell_count; ++index) {
        kilix_world_result result = kilix_world_td_cell_rect(
            grid, layout, cells[index], inset, &rects[index]);
        if (result != KILIX_WORLD_OK) return result;
    }
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_td_path_points(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    const kilix_world_cell *path, size_t path_count,
    kilix_world_td_point *points, size_t point_capacity,
    size_t *point_count)
{
    size_t index;
    if (!grid || !layout_valid(layout) || !point_count ||
        (path_count > 0u && !path) || (point_capacity > 0u && !points))
        return KILIX_WORLD_INVALID_ARGUMENT;
    *point_count = path_count;
    if (point_capacity < path_count) return KILIX_WORLD_NO_SPACE;
    for (index = 0u; index < path_count; ++index) {
        kilix_world_td_rect rect;
        kilix_world_result result = kilix_world_td_cell_rect(
            grid, layout, path[index], 0.0f, &rect);
        if (result != KILIX_WORLD_OK) return result;
        points[index] = (kilix_world_td_point){
            rect.x + rect.width * 0.5f,
            rect.y + rect.height * 0.5f
        };
    }
    return KILIX_WORLD_OK;
}
