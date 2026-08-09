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

/*
 * Every projection input is reduced to int64 before it is combined, and the
 * extents that multiply it are bounded at init (KT_PROJECTION_MAX_EXTENT).
 * With |view| <= 2^32, |extent| <= 2^16 and zoom <= 65535, the widest
 * intermediate is below 2^60, so the arithmetic below cannot overflow int64;
 * only the final narrowing back to int32 needs a range test.
 */
static bool kt_fits_i32(int64_t value)
{
    return value >= (int64_t)INT32_MIN && value <= (int64_t)INT32_MAX;
}

/*
 * kt_projection is a public struct, and a game that restores a saved view
 * writes its fields directly rather than calling kt_projection_init(). The
 * bound checked at init is only load-bearing if the transform re-checks it,
 * so every arithmetic entry point confirms it before multiplying by an
 * extent.
 */
static bool kt_projection_extent_ok(const kt_projection *projection)
{
    return projection->tile_width > 0 && projection->tile_height > 0 &&
           projection->level_step >= 0 &&
           projection->tile_width <= KT_PROJECTION_MAX_EXTENT &&
           projection->tile_height <= KT_PROJECTION_MAX_EXTENT &&
           projection->level_step <= KT_PROJECTION_MAX_EXTENT;
}

/* Round half away from zero, matching KAT's kat_scale_projection exactly.
 * At 100 percent this is the identity on every input. */
static int64_t kt_scale_zoom(int64_t value, uint16_t zoom_percent)
{
    int64_t scaled;

    if (zoom_percent == 100u) {
        return value;
    }
    scaled = value * (int64_t)zoom_percent;
    if (scaled >= 0) {
        scaled += 50;
    } else {
        scaled -= 50;
    }
    return scaled / 100;
}

/*
 * Apply the camera origin and narrow to screen space. The origin is caller
 * data and the scaled value is unbounded in principle, so a result that does
 * not fit a screen point is reported rather than wrapped.
 */
static kt_status kt_screen_from_raw(const kt_camera *camera, int64_t raw_x,
                                    int64_t raw_y, kt_screen_point *out)
{
    int64_t x = (int64_t)camera->origin_x +
                kt_scale_zoom(raw_x, camera->zoom_percent);
    int64_t y = (int64_t)camera->origin_y +
                kt_scale_zoom(raw_y, camera->zoom_percent);

    if (!kt_fits_i32(x) || !kt_fits_i32(y)) {
        return KT_ERR_RANGE;
    }
    out->x = (int32_t)x;
    out->y = (int32_t)y;
    return KT_OK;
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
    /*
     * Bounding the extents is what keeps the projection arithmetic inside
     * int64 for the whole int32 input domain, and every real consumer is
     * three orders of magnitude below the limit (both games use 32/16 with a
     * 12 or 24 pixel level step). Without it a caller-chosen extent near
     * INT32_MAX overflows the picking diamond test and the zoom scale.
     */
    if (tile_width > KT_PROJECTION_MAX_EXTENT ||
        tile_height > KT_PROJECTION_MAX_EXTENT ||
        level_step > KT_PROJECTION_MAX_EXTENT) {
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
/*
 * Reports false when the reflected coordinate is not representable. The
 * reflection `extent - 1 - v` is exact in int64 for every int32 input, so the
 * only failure is a result outside int32, which happens for positions far
 * outside the extent -- a domain the map-free entry point deliberately
 * accepts.
 */
static bool kt_rotate_ccw(int32_t width, int32_t height, uint8_t rotation,
                          int32_t x, int32_t y, int32_t *out_x, int32_t *out_y)
{
    int64_t wide_x = (int64_t)x;
    int64_t wide_y = (int64_t)y;
    int64_t rx;
    int64_t ry;

    switch (rotation & 3u) {
    case 1u:
        rx = wide_y;
        ry = (int64_t)width - 1 - wide_x;
        break;
    case 2u:
        rx = (int64_t)width - 1 - wide_x;
        ry = (int64_t)height - 1 - wide_y;
        break;
    case 3u:
        rx = (int64_t)height - 1 - wide_y;
        ry = wide_x;
        break;
    default:
        rx = wide_x;
        ry = wide_y;
        break;
    }
    if (!kt_fits_i32(rx) || !kt_fits_i32(ry)) {
        return false;
    }
    *out_x = (int32_t)rx;
    *out_y = (int32_t)ry;
    return true;
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
    if (!kt_rotate_ccw(width, height, kt_effective_rotation(projection, camera),
                       x, y, out_x, out_y)) {
        return KT_ERR_RANGE;
    }
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
        if (!kt_rotate_ccw(height, width, (uint8_t)((4u - rotation) & 3u),
                           view_x, view_y, out_x, out_y)) {
            return KT_ERR_RANGE;
        }
    } else {
        if (!kt_rotate_ccw(width, height, (uint8_t)((4u - rotation) & 3u),
                           view_x, view_y, out_x, out_y)) {
            return KT_ERR_RANGE;
        }
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
     * over the swapped extent. A view position far enough outside the grid
     * that its reflection leaves int32 is out of range, not a broken state. */
    {
        kt_status rotated = kt_rotate_extent_inverse(
            projection, camera, map->width, map->height, view.x, view.y, &wx,
            &wy);

        if (rotated == KT_ERR_RANGE) {
            return KT_ERR_RANGE;
        }
        if (rotated != KT_OK) {
            return KT_ERR_STATE;
        }
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
    if (!kt_projection_extent_ok(projection)) {
        return KT_ERR_STATE;
    }
    half_w = projection->tile_width / 2;
    half_h = projection->tile_height / 2;
    raw_x = ((int64_t)view_x_16 - (int64_t)view_y_16) * half_w / 16;
    raw_y = ((int64_t)view_x_16 + (int64_t)view_y_16) * half_h / 16 -
            (int64_t)height_16 * (int64_t)projection->level_step / 16;
    return kt_screen_from_raw(camera, raw_x, raw_y, out);
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
    if (!kt_projection_extent_ok(projection)) {
        return KT_ERR_STATE;
    }
    half_w = projection->tile_width / 2;
    half_h = projection->tile_height / 2;
    height = (int64_t)world.z + (int64_t)cell->elevation;
    raw_x = ((int64_t)view.x - (int64_t)view.y) * half_w;
    raw_y = ((int64_t)view.x + (int64_t)view.y) * half_h -
            height * (int64_t)projection->level_step;
    return kt_screen_from_raw(camera, raw_x, raw_y, out);
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

    if (!kt_projection_extent_ok(projection)) {
        return KT_ERR_STATE;
    }
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

/*
 * Shared candidate sweep. Emits every cell whose floor diamond contains the
 * point. kt_pick_cell() ranks the emissions; kt_pick_cell_all() reports them.
 */
static kt_status kt_pick_sweep(const kt_map *map,
                               const kt_projection *projection,
                               const kt_camera *camera, kt_screen_point screen,
                               int32_t radius, kt_cell_point *out_cells,
                               size_t capacity, size_t *out_count,
                               kt_cell_point *out_best, bool *out_found)
{
    int32_t level;
    int32_t top;
    int32_t band;
    bool found = false;
    int64_t best_key = 0;
    size_t written = 0u;
    kt_cell_point best;

    if (map == NULL || projection == NULL || camera == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (camera->zoom_percent == 0u || radius < 0) {
        return KT_ERR_RANGE;
    }
    top = map->levels - 1;
    if (camera->view_level < top) {
        top = camera->view_level;
    }
    if (top < 0) {
        if (map->elevation_span == 0 || top < -map->elevation_span) {
            if (out_found != NULL) {
                *out_found = false;
            }
            if (out_count != NULL) {
                *out_count = 0u;
            }
            return KT_OK;
        }
        top = 0;
    }
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

            for (dvy = -radius; dvy <= radius; ++dvy)
            for (dvx = -radius; dvx <= radius; ++dvx) {
                kt_cell_point view;
                kt_cell_point candidate;
                kt_screen_point origin;
                const kt_cell *cell;
                int64_t key;
                int64_t dx;
                int64_t dy;
                int64_t half_w;
                int64_t half_h;
                size_t seen;

                view.x = base_view.x + dvx;
                view.y = base_view.y + dvy;
                view.z = level;
                if (kt_rotate_to_world(map, projection, camera, view,
                                       &candidate) != KT_OK) {
                    continue;
                }
                cell = kt_map_cell_const(map, candidate);
                if (cell == NULL || (int32_t)cell->elevation != offset) {
                    continue;
                }
                if (candidate.z + (int32_t)cell->elevation >
                    camera->view_level) {
                    continue;
                }
                if (kt_project(map, projection, camera, candidate, &origin) !=
                    KT_OK) {
                    continue;
                }
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
                /*
                 * The diamond test is |dx|/half_w + |dy|/half_h <= 1 cleared
                 * of division. Both distances are differences of screen
                 * points, so each is at most 2^32, and the extent bound caps
                 * the zoomed half-extents; the products below therefore stay
                 * inside int64. A point further away than the half-extent on
                 * either axis is outside the diamond by inspection, which
                 * also keeps the cheap rejection first.
                 */
                if (dx > half_w || dy > half_h) {
                    continue;
                }
                if (dx * half_h + dy * half_w > half_w * half_h) {
                    continue;
                }

                /* The sweep can reach the same cell from several probes. */
                for (seen = 0u; seen < written; ++seen) {
                    if (out_cells != NULL &&
                        kt_cell_point_equal(out_cells[seen], candidate)) {
                        break;
                    }
                }
                if (out_cells != NULL && seen < written) {
                    continue;
                }
                if (out_cells != NULL) {
                    if (written >= capacity) {
                        return KT_ERR_CAPACITY;
                    }
                    out_cells[written] = candidate;
                }
                ++written;

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
    if (out_count != NULL) {
        *out_count = written;
    }
    if (out_found != NULL) {
        *out_found = found;
    }
    if (out_best != NULL) {
        *out_best = best;
    }
    return KT_OK;
}

kt_status kt_pick_cell_all(const kt_map *map, const kt_projection *projection,
                           const kt_camera *camera, kt_screen_point screen,
                           int32_t radius, kt_cell_point *out_cells,
                           size_t capacity, size_t *out_count)
{
    if (out_cells == NULL || out_count == NULL) {
        return KT_ERR_ARGUMENT;
    }
    return kt_pick_sweep(map, projection, camera, screen, radius, out_cells,
                         capacity, out_count, NULL, NULL);
}

kt_status kt_pick_cell(const kt_map *map, const kt_projection *projection,
                       const kt_camera *camera, kt_screen_point screen,
                       kt_cell_point *out_world)
{
    kt_cell_point best;
    bool found = false;
    kt_status status;

    if (out_world == NULL) {
        return KT_ERR_ARGUMENT;
    }
    status = kt_pick_sweep(map, projection, camera, screen, 1, NULL, 0u, NULL,
                           &best, &found);
    if (status != KT_OK) {
        return status;
    }
    if (!found) {
        return KT_ERR_UNREACHABLE;
    }
    *out_world = best;
    return KT_OK;
}
