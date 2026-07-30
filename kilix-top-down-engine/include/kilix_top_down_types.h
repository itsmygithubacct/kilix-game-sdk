#ifndef KILIX_TOP_DOWN_TYPES_H
#define KILIX_TOP_DOWN_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KI_TD_VERSION_MAJOR 0
#define KI_TD_VERSION_MINOR 3
#define KI_TD_VERSION_PATCH 1

typedef struct ki_td_rect {
    int x;
    int y;
    int width;
    int height;
} ki_td_rect;

typedef enum ki_td_scale_policy {
    KI_TD_SCALE_FRACTIONAL = 0,
    KI_TD_SCALE_INTEGER = 1,
    KI_TD_SCALE_PIXEL_ART = 2
} ki_td_scale_policy;

typedef struct ki_td_fit_spec {
    int logical_width;
    int logical_height;
    ki_td_rect fit_bounds;
    ki_td_rect align_bounds;
    ki_td_scale_policy scale_policy;
    float minimum_scale;
    float integer_scale_threshold;
    bool clamp_origin_x;
    bool clamp_origin_y;
} ki_td_fit_spec;

typedef struct ki_td_view {
    int logical_width;
    int logical_height;
    float scale;
    int origin_x;
    int origin_y;
    int offset_x;
    int offset_y;
} ki_td_view;

typedef struct ki_td_cell_bounds {
    int first_column;
    int first_row;
    int column_count;
    int row_count;
} ki_td_cell_bounds;

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TOP_DOWN_TYPES_H */
