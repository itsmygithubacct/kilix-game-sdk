/*
 * kilix_tactics_projection.h — isometric projection, rotation, zoom,
 * cutaway, and inverse picking.
 *
 * Part of libkilix-tactics-core. Standard library only.
 *
 * FROZEN CONTRACT. For the C-COM parameter set (32, 16, 24) at zoom 100 and
 * rotation 0, kt_project() must be bit-identical to that game's frozen
 * sx = (x - y) * 16; sy = (x + y) * 8 - z * 24. See DECISIONS.md T-004.
 */
#ifndef KILIX_TACTICS_PROJECTION_H
#define KILIX_TACTICS_PROJECTION_H

#include "kilix_tactics_map.h"
#include "kilix_tactics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The two consuming games number their quarter turns in opposite senses and
 * both persist that number, so the sense is explicit rather than assumed.
 * KT_ROTATE_CCW matches C-COM; KT_ROTATE_CW matches Kilix Advanced Tactics.
 * The two are related by r -> (4 - r) & 3.
 */
typedef enum kt_rotation_sense {
    KT_ROTATE_CCW = 0,
    KT_ROTATE_CW = 1
} kt_rotation_sense;

/*
 * Painter traversal order. The two are NOT interchangeable: they invert for
 * cross-level pairs whose sprites overlap, which is visible wherever a raised
 * structure stands next to a lower one.
 */
typedef enum kt_depth_order {
    /* Farther diagonal rows first, then level, then west-to-east. Matches a
     * renderer that walks the board diagonally. The default. */
    KT_DEPTH_DIAGONAL_MAJOR = 0,
    /* All of one level before any of the next. Matches a renderer whose
     * terrain pass is `for z { for x { for y } }`, which is C-COM's. */
    KT_DEPTH_LEVEL_MAJOR
} kt_depth_order;

typedef struct kt_projection {
    int32_t tile_width;      /* floor diamond width, px  (both games: 32)  */
    int32_t tile_height;     /* floor diamond height, px (both games: 16)  */
    int32_t level_step;      /* px per level (C-COM 24, KAT 12)            */
    kt_rotation_sense sense;
    kt_depth_order depth_order;
} kt_projection;

typedef struct kt_camera {
    int32_t origin_x;        /* added to every projected x                 */
    int32_t origin_y;        /* added to every projected y                 */
    uint16_t zoom_percent;   /* 100 is identity and exactly bit-preserving */
    uint8_t rotation;        /* quarter turns, 0..3, in projection->sense  */
    int32_t view_level;      /* cutaway: cells above this are suppressed   */
} kt_camera;

/*
 * Initialise a projection. Rejects non-positive or odd tile extents: the
 * frozen transform halves both, and an odd extent would silently lose a
 * pixel per cell.
 */
kt_status kt_projection_init(kt_projection *projection, int32_t tile_width,
                             int32_t tile_height, int32_t level_step,
                             kt_rotation_sense sense);

/* Identity camera: no origin, zoom 100, rotation 0, no cutaway. */
void kt_camera_init(kt_camera *camera);

/*
 * Quarter-turn remap of a world cell into the camera's view grid. World
 * coordinates never rotate; simulation, paths, sight, and saves stay in
 * world space and only presentation and its inverse use this.
 */
/*
 * Map-free quarter-turn remap over an explicit grid extent.
 *
 * Rotation depends only on the extent, never on cell contents, and a renderer
 * legitimately transforms positions that are not cells at all -- a projectile
 * in flight, a camera clamp, a position just outside the grid. Those callers
 * must not be forced to own a map or to stay in bounds, so this is pure
 * arithmetic and does not range-check.
 *
 * kt_rotate_to_view() is the bounds-checked cell wrapper around this.
 */
kt_status kt_rotate_extent(const kt_projection *projection,
                           const kt_camera *camera, int32_t width,
                           int32_t height, int32_t x, int32_t y,
                           int32_t *out_x, int32_t *out_y);
kt_status kt_rotate_extent_inverse(const kt_projection *projection,
                                   const kt_camera *camera, int32_t width,
                                   int32_t height, int32_t view_x,
                                   int32_t view_y, int32_t *out_x,
                                   int32_t *out_y);

kt_status kt_rotate_to_view(const kt_map *map, const kt_projection *projection,
                            const kt_camera *camera, kt_cell_point world,
                            kt_cell_point *out_view);
kt_status kt_rotate_to_world(const kt_map *map, const kt_projection *projection,
                             const kt_camera *camera, kt_cell_point view,
                             kt_cell_point *out_world);

/*
 * Project a world cell to screen space, applying rotation, the cell's own
 * elevation offset, zoom, and the camera origin.
 *
 * The returned point is the raw projected origin of the cell. What that
 * anchors — C-COM treats it as the top-left of the 32x40 sprite cell, KAT
 * treats it as the floor diamond centre — is the caller's convention,
 * absorbed into camera->origin_x/origin_y.
 *
 * Zoom rounds half away from zero, so zoom_percent == 100 is exactly the
 * identity on every input.
 */
kt_status kt_project(const kt_map *map, const kt_projection *projection,
                     const kt_camera *camera, kt_cell_point world,
                     kt_screen_point *out);

/*
 * Map-free projection of an already-rotated view position, in sixteenths of
 * a cell, with an explicit height in sixteenths of a level step.
 *
 * This is the path for anything between cells: a unit's walk translation, a
 * thrown object's arc, a projectile. Callers that have whole cells should
 * use kt_project(), which rotates and reads the cell elevation for them.
 */
kt_status kt_project_subcell(const kt_projection *projection,
                             const kt_camera *camera, int32_t view_x_16,
                             int32_t view_y_16, int32_t height_16,
                             kt_screen_point *out);

/*
 * Closed-form inverse for one level, ignoring per-cell elevation offsets.
 * Cells are half-open, so a point exactly on a shared edge resolves by
 * mathematical floor rather than truncation toward zero.
 */
kt_status kt_unproject_level(const kt_map *map, const kt_projection *projection,
                             const kt_camera *camera, kt_screen_point screen,
                             int32_t level, kt_cell_point *out_world);

/*
 * Topmost-first pick. Walks levels from the cutaway level downward and
 * returns the frontmost cell whose floor diamond contains the point, using
 * the same depth key as the draw queue so picking and painting agree.
 *
 * When the map has no elevation offsets this costs one closed-form inverse
 * per level. Maps that do carry offsets additionally scan the band those
 * offsets can shift a cell across, which kt_map_validate() measures.
 *
 * Returns KT_ERR_UNREACHABLE when no cell is hit.
 */
kt_status kt_pick_cell(const kt_map *map, const kt_projection *projection,
                       const kt_camera *camera, kt_screen_point screen,
                       kt_cell_point *out_world);

/*
 * Painter depth key for a world cell. Ordering is by rotated (x + y), then
 * level, then rotated x, so nearer and higher cells sort later. The last
 * term makes the key total over distinct cells, which keeps paint order
 * independent of the order a game submits its terrain. Exposed because
 * picking, the draw queue, and game-side overlays must all agree.
 */
kt_status kt_depth_key(const kt_map *map, const kt_projection *projection,
                       const kt_camera *camera, kt_cell_point world,
                       int64_t *out_key);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_PROJECTION_H */
