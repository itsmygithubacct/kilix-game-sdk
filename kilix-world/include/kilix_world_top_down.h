#ifndef KILIX_WORLD_TOP_DOWN_H
#define KILIX_WORLD_TOP_DOWN_H

#include "kilix_world.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional rectangular top-down projection adapter. It has no renderer
 * dependency: games can feed the returned rectangles and points to any
 * drawing backend.
 */

typedef struct kilix_world_td_layout {
    float origin_x;
    float origin_y;
    float cell_width;
    float cell_height;
} kilix_world_td_layout;

typedef struct kilix_world_td_rect {
    float x;
    float y;
    float width;
    float height;
} kilix_world_td_rect;

typedef struct kilix_world_td_point {
    float x;
    float y;
} kilix_world_td_point;

bool kilix_world_td_layout_init(kilix_world_td_layout *layout,
                                float origin_x, float origin_y,
                                float cell_width, float cell_height);

kilix_world_result kilix_world_td_cell_rect(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    kilix_world_cell cell, float inset, kilix_world_td_rect *rect);

kilix_world_result kilix_world_td_point_cell(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    float logical_x, float logical_y, kilix_world_cell *cell);

/*
 * Convert cells to inset overlay rectangles or cell-center path points.
 * On NO_SPACE, the output count reports the required capacity and no output
 * elements are written.
 */
kilix_world_result kilix_world_td_cell_rects(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    const kilix_world_cell *cells, size_t cell_count, float inset,
    kilix_world_td_rect *rects, size_t rect_capacity, size_t *rect_count);

kilix_world_result kilix_world_td_path_points(
    const kilix_world_grid *grid, const kilix_world_td_layout *layout,
    const kilix_world_cell *path, size_t path_count,
    kilix_world_td_point *points, size_t point_capacity,
    size_t *point_count);

#ifdef __cplusplus
}
#endif

#endif
