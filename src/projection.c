/*
 * projection.c — isometric transform, quarter-turn remap, zoom, cutaway,
 * and inverse picking.
 *
 * FROZEN. See DECISIONS.md T-004: at (32, 16, 24), zoom 100, rotation 0,
 * kt_project() reproduces C-COM's sx = (x - y) * 16, sy = (x + y) * 8 - z * 24
 * bit for bit, and at (32, 16, 12) it reproduces KAT's projection including
 * that game's round-half-away-from-zero zoom.
 */
#include "kilix_tactics_projection.h"

/* Round half away from zero, matching KAT's kat_scale_projection exactly.
 * At 100 percent this is the identity on every input. */
static int32_t kt_scale_zoom(int64_t value, uint16_t zoom_percent)
{
    int64_t scaled;

    if (zoom_percent == 100u) {
        return (int32_t)value;
    }
    scaled = value * (int64_t)zoom_percent;
    if (scaled >= 0) {
        scaled += 50;
    } else {
        scaled -= 50;
    }
    return (int32_t)(scaled / 100);
}

/* Floor division; C truncates toward zero, which would fold the two cells
 * either side of the origin into one. */
static int64_t kt_floor_div(int64_t numerator, int64_t denominator)
{
    int64_t quotient = numerator / denominator;

    if ((numerator % denominator != 0) &&
        ((numerator < 0) != (denominator < 0))) {
        quotient -= 1;
    }
    return quotient;
}

kt_status kt_projection_init(kt_projection *projection, int32_t tile_width,
                             int32_t tile_height, int32_t level_step,
                             kt_rotation_sense sense)
{
    if (projection == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (sense != KT_ROTATE_CCW && sense != KT_ROTATE_CW) {
        return KT_ERR_ARGUMENT;
    }
    if (tile_width <= 0 || tile_height <= 0 || level_step < 0) {
        return KT_ERR_RANGE;
    }
    /* The transform halves both extents; an odd extent would silently drop
     * a pixel per cell and desynchronise picking from painting. */
    if ((tile_width % 2) != 0 || (tile_height % 2) != 0) {
        return KT_ERR_RANGE;
    }
    projection->tile_width = tile_width;
    projection->tile_height = tile_height;
    projection->level_step = level_step;
    projection->sense = sense;
    projection->depth_order = KT_DEPTH_DIAGONAL_MAJOR;
    return KT_OK;
}

void kt_camera_init(kt_camera *camera)
{
    if (camera == NULL) {
        return;
    }
    camera->origin_x = 0;
    camera->origin_y = 0;
    camera->zoom_percent = 100u;
    camera->rotation = 0u;
    camera->view_level = INT32_MAX;
}

/*
 * Counterclockwise quarter turns, which is C-COM's sense:
 *   r1: (y, W-1-x)   r2: (W-1-x, H-1-y)   r3: (H-1-y, x)
 * The clockwise sense used by KAT is the same table read as (4 - r) & 3.
 */
static void kt_rotate_ccw(int32_t width, int32_t height, uint8_t rotation,
                          int32_t x, int32_t y, int32_t *out_x, int32_t *out_y)
{
    switch (rotation & 3u) {
    case 1u:
        *out_x = y;
        *out_y = width - 1 - x;
        break;
    case 2u:
        *out_x = width - 1 - x;
        *out_y = height - 1 - y;
        break;
    case 3u:
        *out_x = height - 1 - y;
        *out_y = x;
        break;
    default:
        *out_x = x;
        *out_y = y;
        break;
    }
}

static uint8_t kt_effective_rotation(const kt_projection *projection,
                                     const kt_camera *camera)
{
    uint8_t rotation = (uint8_t)(camera->rotation & 3u);

    if (projection->sense == KT_ROTATE_CW) {
        rotation = (uint8_t)((4u - rotation) & 3u);
    }
    return rotation;
}

kt_status kt_rotate_extent(const kt_projection *projection,
                           const kt_camera *camera, int32_t width,
                           int32_t height, int32_t x, int32_t y,
                           int32_t *out_x, int32_t *out_y)
{
    if (projection == NULL || camera == NULL || out_x == NULL ||
        out_y == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (width <= 0 || height <= 0) {
        return KT_ERR_RANGE;
    }
    kt_rotate_ccw(width, height, kt_effective_rotation(projection, camera), x,
                  y, out_x, out_y);
    return KT_OK;
}

kt_status kt_rotate_extent_inverse(const kt_projection *projection,
                                   const kt_camera *camera, int32_t width,
                                   int32_t height, int32_t view_x,
                                   int32_t view_y, int32_t *out_x,
                                   int32_t *out_y)
{
    uint8_t rotation;

    if (projection == NULL || camera == NULL || out_x == NULL ||
        out_y == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (width <= 0 || height <= 0) {
        return KT_ERR_RANGE;
    }
    /* The inverse of a quarter turn is the complementary turn taken over the
     * swapped extent. */
    rotation = kt_effective_rotation(projection, camera);
    if ((rotation & 1u) != 0u) {
        kt_rotate_ccw(height, width, (uint8_t)((4u - rotation) & 3u), view_x,
                      view_y, out_x, out_y);
    } else {
        kt_rotate_ccw(width, height, (uint8_t)((4u - rotation) & 3u), view_x,
                      view_y, out_x, out_y);
    }
    return KT_OK;
}

kt_status kt_rotate_to_view(const kt_map *map, const kt_projection *projection,
                            const kt_camera *camera, kt_cell_point world,
                            kt_cell_point *out_view)
{
    int32_t vx;
    int32_t vy;

    if (map == NULL || projection == NULL || camera == NULL ||
        out_view == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (!kt_map_contains(map, world)) {
        return KT_ERR_RANGE;
    }
    if (kt_rotate_extent(projection, camera, map->width, map->height, world.x,
                         world.y, &vx, &vy) != KT_OK) {
        return KT_ERR_STATE;
    }
    out_view->x = vx;
    out_view->y = vy;
    out_view->z = world.z;
    return KT_OK;
}

kt_status kt_rotate_to_world(const kt_map *map, const kt_projection *projection,
                             const kt_camera *camera, kt_cell_point view,
                             kt_cell_point *out_world)
{
    int32_t wx;
    int32_t wy;

    if (map == NULL || projection == NULL || camera == NULL ||
        out_world == NULL) {
        return KT_ERR_ARGUMENT;
    }
    /* The inverse of a quarter turn is the complementary quarter turn taken
     * over the swapped extent. */
    if (kt_rotate_extent_inverse(projection, camera, map->width, map->height,
                                 view.x, view.y, &wx, &wy) != KT_OK) {
        return KT_ERR_STATE;
    }
    out_world->x = wx;
    out_world->y = wy;
    out_world->z = view.z;
    if (!kt_map_contains(map, *out_world)) {
        return KT_ERR_RANGE;
    }
    return KT_OK;
}

kt_status kt_project_subcell(const kt_projection *projection,
                             const kt_camera *camera, int32_t view_x_16,
                             int32_t view_y_16, int32_t height_16,
                             kt_screen_point *out)
{
    int64_t half_w;
    int64_t half_h;
    int64_t raw_x;
    int64_t raw_y;

    if (projection == NULL || camera == NULL || out == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (camera->zoom_percent == 0u) {
        return KT_ERR_RANGE;
    }
    half_w = projection->tile_width / 2;
    half_h = projection->tile_height / 2;
    raw_x = ((int64_t)view_x_16 - (int64_t)view_y_16) * half_w / 16;
    raw_y = ((int64_t)view_x_16 + (int64_t)view_y_16) * half_h / 16 -
            (int64_t)height_16 * (int64_t)projection->level_step / 16;
    out->x = camera->origin_x + kt_scale_zoom(raw_x, camera->zoom_percent);
    out->y = camera->origin_y + kt_scale_zoom(raw_y, camera->zoom_percent);
    return KT_OK;
}

kt_status kt_project(const kt_map *map, const kt_projection *projection,
                     const kt_camera *camera, kt_cell_point world,
                     kt_screen_point *out)
{
    const kt_cell *cell;
    kt_cell_point view;
    kt_status status;
    int64_t half_w;
    int64_t half_h;
    int64_t raw_x;
    int64_t raw_y;
    int64_t height;

    if (map == NULL || projection == NULL || camera == NULL || out == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (camera->zoom_percent == 0u) {
        return KT_ERR_RANGE;
    }
    status = kt_rotate_to_view(map, projection, camera, world, &view);
    if (status != KT_OK) {
        return status;
    }
    cell = kt_map_cell_const(map, world);
    if (cell == NULL) {
        return KT_ERR_RANGE;
    }
    half_w = projection->tile_width / 2;
    half_h = projection->tile_height / 2;
    height = (int64_t)world.z + (int64_t)cell->elevation;
    raw_x = ((int64_t)view.x - (int64_t)view.y) * half_w;
    raw_y = ((int64_t)view.x + (int64_t)view.y) * half_h -
            height * (int64_t)projection->level_step;
    out->x = camera->origin_x + kt_scale_zoom(raw_x, camera->zoom_percent);
    out->y = camera->origin_y + kt_scale_zoom(raw_y, camera->zoom_percent);
    return KT_OK;
}

/*
 * Shared inverse. vertical_steps is how much vertical offset to undo, which
 * is the cell's level plus its elevation; the caller decides separately what
 * level the resulting cell belongs to.
 */
static kt_status kt_unproject_view(const kt_projection *projection,
                                   const kt_camera *camera,
                                   kt_screen_point screen,
                                   int32_t vertical_steps, int32_t *out_view_x,
                                   int32_t *out_view_y)
{
    int64_t half_w;
    int64_t half_h;
    int64_t local_x;
    int64_t local_y;
    int64_t sum;
    int64_t difference;

    half_w = projection->tile_width / 2;
    half_h = projection->tile_height / 2;
    if (half_w == 0 || half_h == 0) {
        return KT_ERR_STATE;
    }

    /* Undo origin, then zoom. Zoom is applied last on the way out, so it is
     * undone first on the way back in. */
    local_x = (int64_t)screen.x - (int64_t)camera->origin_x;
    local_y = (int64_t)screen.y - (int64_t)camera->origin_y;
    if (camera->zoom_percent != 100u) {
        local_x = kt_floor_div(local_x * 100, (int64_t)camera->zoom_percent);
        local_y = kt_floor_div(local_y * 100, (int64_t)camera->zoom_percent);
    }
    local_y += (int64_t)vertical_steps * (int64_t)projection->level_step;

    /*
     * local_x = (vx - vy) * half_w and local_y = (vx + vy) * half_h, so
     * difference and sum recover the view coordinates. Floor division keeps
     * cells half-open across the origin.
     */
    difference = kt_floor_div(local_x, half_w);
    sum = kt_floor_div(local_y, half_h);
    *out_view_x = (int32_t)kt_floor_div(sum + difference, 2);
    *out_view_y = (int32_t)kt_floor_div(sum - difference, 2);
    return KT_OK;
}

kt_status kt_unproject_level(const kt_map *map, const kt_projection *projection,
                             const kt_camera *camera, kt_screen_point screen,
                             int32_t level, kt_cell_point *out_world)
{
    kt_cell_point view;
    kt_status status;

    if (map == NULL || projection == NULL || camera == NULL ||
        out_world == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (camera->zoom_percent == 0u) {
        return KT_ERR_RANGE;
    }
    status = kt_unproject_view(projection, camera, screen, level, &view.x,
                               &view.y);
    if (status != KT_OK) {
        return status;
    }
    view.z = level;
    return kt_rotate_to_world(map, projection, camera, view, out_world);
}

kt_status kt_depth_key(const kt_map *map, const kt_projection *projection,
                       const kt_camera *camera, kt_cell_point world,
                       int64_t *out_key)
{
    kt_cell_point view;
    kt_status status;

    if (out_key == NULL) {
        return KT_ERR_ARGUMENT;
    }
    status = kt_rotate_to_view(map, projection, camera, world, &view);
    if (status != KT_OK) {
        return status;
    }
    /*
     * Painter order: farther diagonal rows first, then lower levels, then
     * west to east across the row.
     *
     * The trailing view.x term is what makes the key TOTAL over distinct
     * cells. Cells sharing a diagonal do not overlap, so visually any order
     * would do; but without it their relative order would fall through to
     * submission order, and the queue hash would stop being a usable golden.
     * Every field is bounded by KT_MAP_MAX_SPAN / KT_MAP_MAX_LEVELS, so the
     * radix packing cannot carry between fields.
     */
    if (projection->depth_order == KT_DEPTH_LEVEL_MAJOR) {
        /*
         * A literal transcription of a `for z { for vx { for vy } }` terrain
         * pass: level, then view.x, then view.y.
         *
         * An earlier version used the diagonal row as the middle term. That
         * was wrong, and not harmlessly so: over a 40x40x4 grid it inverted
         * against the emission order 156 times per frame, once at every
         * vx-loop boundary. The inversions are reachable content, not ties --
         * a 2x2 unit plate is 64 px wide and a walking unit is offset up to
         * 16 px, so two cells the diagonal argument treats as too far apart to
         * overlap can and do overlap.
         *
         * Radix: view.x and view.y are both < KT_MAP_MAX_SPAN, so
         * view.x * SPAN + view.y < SPAN * SPAN and the level term cannot
         * carry into them.
         */
        *out_key = (int64_t)world.z * ((int64_t)KT_MAP_MAX_SPAN *
                                       (int64_t)KT_MAP_MAX_SPAN) +
                   (int64_t)view.x * (int64_t)KT_MAP_MAX_SPAN +
                   (int64_t)view.y;
        return KT_OK;
    }
    *out_key = ((int64_t)view.x + (int64_t)view.y) *
                   ((int64_t)KT_MAP_MAX_LEVELS * (int64_t)KT_MAP_MAX_SPAN) +
               (int64_t)world.z * (int64_t)KT_MAP_MAX_SPAN + (int64_t)view.x;
    return KT_OK;
}

kt_status kt_pick_cell(const kt_map *map, const kt_projection *projection,
                       const kt_camera *camera, kt_screen_point screen,
                       kt_cell_point *out_world)
{
    int32_t level;
    int32_t top;
    int32_t band;
    bool found = false;
    int64_t best_key = 0;
    kt_cell_point best;

    if (map == NULL || projection == NULL || camera == NULL ||
        out_world == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (camera->zoom_percent == 0u) {
        return KT_ERR_RANGE;
    }
    top = map->levels - 1;
    if (camera->view_level < top) {
        top = camera->view_level;
    }
    /*
     * Widen by the elevation span before giving up. A cell whose elevation is
     * negative sits at or below a cutaway that its LEVEL index is above, and
     * on a single-level grid any negative view_level would otherwise return
     * immediately -- which is precisely the game that carries all its height
     * in elevation.
     */
    if (top < 0) {
        if (map->elevation_span == 0 || top < -map->elevation_span) {
            return KT_ERR_UNREACHABLE;
        }
        top = 0;
    }

    /*
     * A cell drawn with a positive elevation appears where a cell that many
     * steps nearer would sit on a flat map, so the closed-form inverse can
     * miss it. Sweep the band those offsets span; on a flat map the band is
     * a single probe and this stays O(levels).
     */
    band = map->elevation_span;
    best.x = 0;
    best.y = 0;
    best.z = 0;

    for (level = top; level >= 0; --level) {
        int32_t offset;

        for (offset = -band; offset <= band; ++offset) {
            kt_cell_point base_view;
            int32_t dvx;
            int32_t dvy;

            if (kt_unproject_view(projection, camera, screen, level + offset,
                                  &base_view.x, &base_view.y) != KT_OK) {
                continue;
            }
            base_view.z = level;

            /*
             * The closed-form inverse inverts the UNROUNDED transform, but the
             * forward transform rounds when zoom is not 100, so the exact
             * candidate can land one cell off: at zoom 80 that missed 256 of
             * 400 projected cell centres. Sweep the immediate view
             * neighbourhood and let the diamond test below decide, which is
             * exact at every zoom. At zoom 100 the centre probe always wins,
             * so this costs nothing where the inverse is already exact.
             */
            for (dvy = -1; dvy <= 1; ++dvy)
            for (dvx = -1; dvx <= 1; ++dvx) {
            kt_cell_point view;
            kt_cell_point candidate;
            kt_screen_point origin;
            const kt_cell *cell;
            int64_t key;
            int64_t dx;
            int64_t dy;
            int64_t half_w;
            int64_t half_h;

            view.x = base_view.x + dvx;
            view.y = base_view.y + dvy;
            view.z = level;
            if (kt_rotate_to_world(map, projection, camera, view, &candidate) !=
                KT_OK) {
                continue;
            }
            cell = kt_map_cell_const(map, candidate);
            if (cell == NULL || (int32_t)cell->elevation != offset) {
                continue;
            }
            /*
             * Cutaway is elevation-aware: a cell raised above the viewing
             * level is suppressed even when its level index is not. A no-op
             * for a game that leaves elevation 0 and expresses height purely
             * as a level index.
             */
            if (candidate.z + (int32_t)cell->elevation > camera->view_level) {
                continue;
            }
            if (kt_project(map, projection, camera, candidate, &origin) !=
                KT_OK) {
                continue;
            }

            /*
             * Confirm the point really is inside this cell's floor diamond.
             * The projected point is the cell's raw origin, and the diamond
             * is the tile_width x tile_height rhombus centred half a tile
             * to its right and half a tile down.
             */
            half_w = kt_scale_zoom(projection->tile_width / 2,
                                   camera->zoom_percent);
            half_h = kt_scale_zoom(projection->tile_height / 2,
                                   camera->zoom_percent);
            if (half_w <= 0 || half_h <= 0) {
                continue;
            }
            dx = (int64_t)screen.x - (int64_t)origin.x;
            dy = (int64_t)screen.y - (int64_t)origin.y;
            if (dx < 0) {
                dx = -dx;
            }
            if (dy < 0) {
                dy = -dy;
            }
            if (dx * half_h + dy * half_w > half_w * half_h) {
                continue;
            }
            if (kt_depth_key(map, projection, camera, candidate, &key) !=
                KT_OK) {
                continue;
            }
            if (!found || key > best_key) {
                found = true;
                best_key = key;
                best = candidate;
            }
            }
        }
    }
    if (!found) {
        return KT_ERR_UNREACHABLE;
    }
    *out_world = best;
    return KT_OK;
}
