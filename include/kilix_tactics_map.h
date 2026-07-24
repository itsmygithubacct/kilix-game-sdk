/*
 * kilix_tactics_map.h — stacked tactical cell grid and its runtime builder.
 *
 * Part of libkilix-tactics-core. Standard library only.
 *
 * The grid is width x height x levels with levels >= 1. C-COM instantiates
 * 40 x 40 x 4 and uses all four wall/occupancy facts; Kilix Advanced Tactics
 * instantiates w x h x 1 and carries terrain height in the elevation offset.
 *
 * A cell stores compiled spatial FACTS, never content indices. Games compile
 * their own definition tables down into these fields when a map is built and
 * rewrite affected cells when terrain changes. See DECISIONS.md T-002/T-003.
 */
#ifndef KILIX_TACTICS_MAP_H
#define KILIX_TACTICS_MAP_H

#include "kilix_tactics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KT_MOVE_BLOCKED = 0u,       /* move_cost sentinel: cell not enterable */
    KT_MAP_MAX_LEVELS = 32,
    KT_MAP_MAX_SPAN = 1024
};

/* Bits in kt_cell.wall[side]. */
enum {
    KT_WALL_BLOCKS_MOVE = 1u << 0,
    KT_WALL_BLOCKS_SIGHT = 1u << 1,
    KT_WALL_BLOCKS_FIRE = 1u << 2
};

/* Bits in kt_cell.occupy — facts about the cell's own contents. */
enum {
    KT_OCCUPY_BLOCKS_MOVE = 1u << 0,
    KT_OCCUPY_BLOCKS_SIGHT = 1u << 1,
    KT_OCCUPY_BLOCKS_FIRE = 1u << 2
};

/* Bits in kt_cell.flags. */
enum {
    KT_CELL_HAS_FLOOR = 1u << 0,    /* an intact floor seals this level    */
    KT_CELL_LEVEL_LINK = 1u << 1    /* gravlift/ladder: sight and movement
                                       may pass vertically here            */
};

typedef struct kt_cell {
    /*
     * Height above this cell's own level, counted in whole level steps.
     * C-COM leaves this 0 and expresses everything in the level index; KAT
     * runs one level and carries its terrain height here. Sub-step draw
     * nudges (C-COM's terrain_level and y_offset) are a game-side sprite
     * concern and deliberately do not live in the projection.
     */
    int16_t elevation;
    uint8_t move_cost;    /* KT_MOVE_BLOCKED (0) means not enterable       */
    uint8_t wall[KT_WALL_SIDE_COUNT];
    uint8_t occupy;
    uint8_t cover_half;   /* 8-direction mask, facing the attacker         */
    uint8_t cover_full;   /* 8-direction mask; full wins over half         */
    uint8_t flags;
    uint8_t reserved;
} kt_cell;

typedef struct kt_map {
    int32_t width;
    int32_t height;
    int32_t levels;
    kt_cell *cells;        /* caller storage, width*height*levels entries  */
    size_t cell_capacity;
    /*
     * Derived: the largest absolute cell elevation in the grid, measured by
     * kt_map_validate(). Picking uses it to size its search band, so a map
     * whose elevations changed must be revalidated before the next pick.
     * Zero on a flat map, which is the O(levels) fast path.
     */
    int32_t elevation_span;
} kt_map;

/*
 * Bind caller storage and zero it. Every cell starts impassable, opaque to
 * nothing, and floorless; the game then writes its compiled facts. Returns
 * KT_ERR_CAPACITY when cell_capacity is too small for the requested extent.
 */
kt_status kt_map_init(kt_map *map, int32_t width, int32_t height,
                      int32_t levels, kt_cell *storage, size_t cell_capacity);

size_t kt_map_cell_count(const kt_map *map);
bool kt_map_contains(const kt_map *map, kt_cell_point point);

/* Row-major index: ((z * height) + y) * width + x. Returns SIZE_MAX when
 * the point is outside the grid. */
size_t kt_map_index(const kt_map *map, kt_cell_point point);
bool kt_map_point_from_index(const kt_map *map, size_t index,
                             kt_cell_point *out);

kt_cell *kt_map_cell(kt_map *map, kt_cell_point point);
const kt_cell *kt_map_cell_const(const kt_map *map, kt_cell_point point);

/*
 * Wall fact lookup honouring the shared-boundary rule: the west wall of
 * (x,y,z) is the same boundary as the east wall of (x-1,y,z). Off-map
 * lookups report blocked on every channel, which is what both games'
 * frozen edge rules require.
 */
bool kt_map_wall_blocks(const kt_map *map, kt_cell_point point,
                        kt_wall_side side, kt_channel channel);
bool kt_map_cell_blocks(const kt_map *map, kt_cell_point point,
                        kt_channel channel);

/*
 * Structural validation: extent, storage, and level-link consistency. Also
 * measures elevation_span, so this must be re-run after bulk terrain edits
 * that change cell elevations.
 */
kt_status kt_map_validate(kt_map *map);

#ifdef __cplusplus
}
#endif

#endif /* KILIX_TACTICS_MAP_H */
