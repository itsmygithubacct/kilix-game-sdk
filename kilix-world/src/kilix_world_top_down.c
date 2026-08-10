#include "kilix_world_top_down.h"

#include <limits.h>
#include <math.h>

typedef struct byte_range {
    uintptr_t begin;
    uintptr_t end;
} byte_range;

static bool byte_range_init(byte_range *range, const void *pointer,
                            size_t count, size_t element_size,
                            size_t alignment)
{
    size_t byte_count;
    uintptr_t begin;
    if (!range || element_size == 0u || alignment == 0u) return false;
    if (count == 0u) {
        range->begin = 0u;
        range->end = 0u;
        return true;
    }
    if (!pointer || (uintptr_t)pointer % alignment != 0u ||
        count > SIZE_MAX / element_size)
        return false;
    byte_count = count * element_size;
    begin = (uintptr_t)pointer;
    if (byte_count > UINTPTR_MAX - begin) return false;
    range->begin = begin;
    range->end = begin + byte_count;
    return true;
}

static bool byte_ranges_overlap(byte_range left, byte_range right)
{
    return left.begin < right.end && right.begin < left.end;
}

static bool bulk_ranges_valid(
    const void *input, size_t input_count, size_t input_size,
    size_t input_alignment, const void *output, size_t output_count,
    size_t output_size, size_t output_alignment, const void *count_output)
{
    byte_range ranges[3];
    if (!byte_range_init(&ranges[0], input, input_count,
                         input_size, input_alignment) ||
        !byte_range_init(&ranges[1], output, output_count,
                         output_size, output_alignment) ||
        !byte_range_init(&ranges[2], count_output, 1u,
                         sizeof(size_t), _Alignof(size_t)))
        return false;
    return !byte_ranges_overlap(ranges[0], ranges[1]) &&
           !byte_ranges_overlap(ranges[0], ranges[2]) &&
           !byte_ranges_overlap(ranges[1], ranges[2]);
}

static bool grid_valid(const kilix_world_grid *grid)
{
    size_t width;
    size_t height;
    if (!grid || grid->width <= 0 || grid->height <= 0) return false;
    width = (size_t)grid->width;
    height = (size_t)grid->height;
    return width <= SIZE_MAX / height;
}

static bool cell_in_bounds(const kilix_world_grid *grid,
                           kilix_world_cell cell)
{
    return cell.x >= 0 && cell.x < grid->width &&
           cell.y >= 0 && cell.y < grid->height;
}

static bool layout_valid(const kilix_world_td_layout *layout)
{
    return layout && isfinite(layout->origin_x) &&
           isfinite(layout->origin_y) && isfinite(layout->cell_width) &&
           isfinite(layout->cell_height) && layout->cell_width > 0.0f &&
           layout->cell_height > 0.0f;
}

static bool inset_valid(const kilix_world_td_layout *layout, float inset)
{
    return isfinite(inset) && inset >= 0.0f &&
           (double)inset * 2.0 < (double)layout->cell_width &&
           (double)inset * 2.0 < (double)layout->cell_height;
}

static kilix_world_result rect_for_cell(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    kilix_world_cell cell, float inset, kilix_world_td_rect *rect)
{
    kilix_world_td_rect next;
    if (!cell_in_bounds(grid, cell)) return KILIX_WORLD_OUT_OF_BOUNDS;
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

static kilix_world_result point_for_cell(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    kilix_world_cell cell, kilix_world_td_point *point)
{
    kilix_world_td_point next;
    if (!cell_in_bounds(grid, cell)) return KILIX_WORLD_OUT_OF_BOUNDS;
    next.x = layout->origin_x + (float)cell.x * layout->cell_width;
    next.y = layout->origin_y + (float)cell.y * layout->cell_height;
    next.x += layout->cell_width * 0.5f;
    next.y += layout->cell_height * 0.5f;
    if (!isfinite(next.x) || !isfinite(next.y))
        return KILIX_WORLD_OVERFLOW;
    *point = next;
    return KILIX_WORLD_OK;
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
    if (!grid_valid(grid) || !layout_valid(layout) || !rect ||
        !inset_valid(layout, inset))
        return KILIX_WORLD_INVALID_ARGUMENT;
    {
        kilix_world_result result =
            rect_for_cell(grid, layout, cell, inset, &next);
        if (result != KILIX_WORLD_OK) return result;
    }
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
    if (!grid_valid(grid) || !layout_valid(layout) || !cell ||
        !isfinite(logical_x) || !isfinite(logical_y))
        return KILIX_WORLD_INVALID_ARGUMENT;
    column = floor(((double)logical_x - (double)layout->origin_x) /
                   (double)layout->cell_width);
    row = floor(((double)logical_y - (double)layout->origin_y) /
                (double)layout->cell_height);
    if (column < (double)INT32_MIN || column > (double)INT32_MAX ||
        row < (double)INT32_MIN || row > (double)INT32_MAX)
        return KILIX_WORLD_OUT_OF_BOUNDS;
    next = (kilix_world_cell){(int32_t)column, (int32_t)row};
    if (!cell_in_bounds(grid, next)) return KILIX_WORLD_OUT_OF_BOUNDS;
    *cell = next;
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_td_cell_rects(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    const kilix_world_cell *cells, size_t cell_count, float inset,
    kilix_world_td_rect *rects, size_t rect_capacity, size_t *rect_count)
{
    kilix_world_grid grid_copy;
    kilix_world_td_layout layout_copy;
    size_t index;
    if (!grid_valid(grid) || !layout_valid(layout) || !rect_count ||
        !inset_valid(layout, inset) ||
        (cell_count > 0u && !cells) ||
        (rect_capacity > 0u && !rects))
        return KILIX_WORLD_INVALID_ARGUMENT;
    grid_copy = *grid;
    layout_copy = *layout;
    if (!bulk_ranges_valid(
            cells, cell_count, sizeof(*cells), _Alignof(kilix_world_cell),
            rects, rect_capacity < cell_count ? 0u : cell_count,
            sizeof(*rects), _Alignof(kilix_world_td_rect), rect_count))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (rect_capacity < cell_count) {
        *rect_count = cell_count;
        return KILIX_WORLD_NO_SPACE;
    }
    for (index = 0u; index < cell_count; ++index) {
        kilix_world_td_rect ignored;
        kilix_world_result result = rect_for_cell(
            &grid_copy, &layout_copy, cells[index], inset, &ignored);
        if (result != KILIX_WORLD_OK) return result;
    }
    for (index = 0u; index < cell_count; ++index)
        (void)rect_for_cell(
            &grid_copy, &layout_copy, cells[index], inset, &rects[index]);
    *rect_count = cell_count;
    return KILIX_WORLD_OK;
}

kilix_world_result kilix_world_td_path_points(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    const kilix_world_cell *path, size_t path_count,
    kilix_world_td_point *points, size_t point_capacity,
    size_t *point_count)
{
    kilix_world_grid grid_copy;
    kilix_world_td_layout layout_copy;
    size_t index;
    if (!grid_valid(grid) || !layout_valid(layout) || !point_count ||
        (path_count > 0u && !path) || (point_capacity > 0u && !points))
        return KILIX_WORLD_INVALID_ARGUMENT;
    grid_copy = *grid;
    layout_copy = *layout;
    if (!bulk_ranges_valid(
            path, path_count, sizeof(*path), _Alignof(kilix_world_cell),
            points, point_capacity < path_count ? 0u : path_count,
            sizeof(*points), _Alignof(kilix_world_td_point), point_count))
        return KILIX_WORLD_INVALID_ARGUMENT;
    if (point_capacity < path_count) {
        *point_count = path_count;
        return KILIX_WORLD_NO_SPACE;
    }
    for (index = 0u; index < path_count; ++index) {
        kilix_world_td_point ignored;
        kilix_world_result result = point_for_cell(
            &grid_copy, &layout_copy, path[index], &ignored);
        if (result != KILIX_WORLD_OK) return result;
    }
    for (index = 0u; index < path_count; ++index)
        (void)point_for_cell(
            &grid_copy, &layout_copy, path[index], &points[index]);
    *point_count = path_count;
    return KILIX_WORLD_OK;
}
