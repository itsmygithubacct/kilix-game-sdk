/*
 * map.c — stacked cell grid storage and compiled-fact lookups.
 */
#include <string.h>

#include "kilix_tactics_map.h"

kt_status kt_map_init(kt_map *map, int32_t width, int32_t height,
                      int32_t levels, kt_cell *storage, size_t cell_capacity)
{
    size_t required;

    if (map == NULL || storage == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (width <= 0 || height <= 0 || levels <= 0 ||
        width > KT_MAP_MAX_SPAN || height > KT_MAP_MAX_SPAN ||
        levels > KT_MAP_MAX_LEVELS) {
        return KT_ERR_RANGE;
    }
    required = (size_t)width * (size_t)height * (size_t)levels;
    if (required > cell_capacity) {
        return KT_ERR_CAPACITY;
    }
    map->width = width;
    map->height = height;
    map->levels = levels;
    map->cells = storage;
    map->cell_capacity = cell_capacity;
    map->elevation_span = 0;
    memset(storage, 0, required * sizeof(*storage));
    return KT_OK;
}

size_t kt_map_cell_count(const kt_map *map)
{
    if (map == NULL || map->cells == NULL) {
        return 0u;
    }
    return (size_t)map->width * (size_t)map->height * (size_t)map->levels;
}

bool kt_map_contains(const kt_map *map, kt_cell_point point)
{
    if (map == NULL || map->cells == NULL) {
        return false;
    }
    return point.x >= 0 && point.x < map->width && point.y >= 0 &&
           point.y < map->height && point.z >= 0 && point.z < map->levels;
}

size_t kt_map_index(const kt_map *map, kt_cell_point point)
{
    if (!kt_map_contains(map, point)) {
        return SIZE_MAX;
    }
    return ((size_t)point.z * (size_t)map->height + (size_t)point.y) *
               (size_t)map->width +
           (size_t)point.x;
}

bool kt_map_point_from_index(const kt_map *map, size_t index,
                             kt_cell_point *out)
{
    size_t plane;

    if (map == NULL || map->cells == NULL || out == NULL ||
        index >= kt_map_cell_count(map)) {
        return false;
    }
    plane = (size_t)map->width * (size_t)map->height;
    out->z = (int32_t)(index / plane);
    index %= plane;
    out->y = (int32_t)(index / (size_t)map->width);
    out->x = (int32_t)(index % (size_t)map->width);
    return true;
}

kt_cell *kt_map_cell(kt_map *map, kt_cell_point point)
{
    size_t index = kt_map_index(map, point);

    if (index == SIZE_MAX) {
        return NULL;
    }
    return &map->cells[index];
}

const kt_cell *kt_map_cell_const(const kt_map *map, kt_cell_point point)
{
    size_t index = kt_map_index(map, point);

    if (index == SIZE_MAX) {
        return NULL;
    }
    return &map->cells[index];
}

static uint8_t kt_channel_wall_bit(kt_channel channel)
{
    switch (channel) {
    case KT_CHANNEL_MOVE:
        return (uint8_t)KT_WALL_BLOCKS_MOVE;
    case KT_CHANNEL_SIGHT:
        return (uint8_t)KT_WALL_BLOCKS_SIGHT;
    case KT_CHANNEL_FIRE:
        return (uint8_t)KT_WALL_BLOCKS_FIRE;
    default:
        return 0u;
    }
}

static uint8_t kt_channel_occupy_bit(kt_channel channel)
{
    switch (channel) {
    case KT_CHANNEL_MOVE:
        return (uint8_t)KT_OCCUPY_BLOCKS_MOVE;
    case KT_CHANNEL_SIGHT:
        return (uint8_t)KT_OCCUPY_BLOCKS_SIGHT;
    case KT_CHANNEL_FIRE:
        return (uint8_t)KT_OCCUPY_BLOCKS_FIRE;
    default:
        return 0u;
    }
}

bool kt_map_wall_blocks(const kt_map *map, kt_cell_point point,
                        kt_wall_side side, kt_channel channel)
{
    const kt_cell *cell;
    uint8_t bit = kt_channel_wall_bit(channel);

    if (bit == 0u || (unsigned)side >= (unsigned)KT_WALL_SIDE_COUNT) {
        return true;
    }
    cell = kt_map_cell_const(map, point);
    if (cell == NULL) {
        /* Off-map boundaries block on every channel, which both games'
         * frozen edge rules require. */
        return true;
    }
    return (cell->wall[side] & bit) != 0u;
}

bool kt_map_cell_blocks(const kt_map *map, kt_cell_point point,
                        kt_channel channel)
{
    const kt_cell *cell;
    uint8_t bit = kt_channel_occupy_bit(channel);

    if (bit == 0u) {
        return true;
    }
    cell = kt_map_cell_const(map, point);
    if (cell == NULL) {
        return true;
    }
    return (cell->occupy & bit) != 0u;
}

kt_status kt_map_validate(kt_map *map)
{
    size_t count;
    size_t index;
    int32_t span = 0;

    if (map == NULL || map->cells == NULL) {
        return KT_ERR_ARGUMENT;
    }
    if (map->width <= 0 || map->height <= 0 || map->levels <= 0 ||
        map->width > KT_MAP_MAX_SPAN || map->height > KT_MAP_MAX_SPAN ||
        map->levels > KT_MAP_MAX_LEVELS) {
        return KT_ERR_RANGE;
    }
    count = kt_map_cell_count(map);
    if (count > map->cell_capacity) {
        return KT_ERR_CAPACITY;
    }
    for (index = 0u; index < count; ++index) {
        const kt_cell *cell = &map->cells[index];
        int32_t magnitude = cell->elevation < 0 ? -(int32_t)cell->elevation
                                                : (int32_t)cell->elevation;

        if (magnitude > span) {
            span = magnitude;
        }
        /* A level link that is not enterable cannot connect anything, and
         * silently ignoring it would make vertical sight and movement
         * disagree between the two games. */
        if ((cell->flags & KT_CELL_LEVEL_LINK) != 0u &&
            cell->move_cost == KT_MOVE_BLOCKED) {
            return KT_ERR_STATE;
        }
        /* Full cover subsumes half cover; overlapping bits would make the
         * reported grade depend on test order. */
        if ((cell->cover_half & cell->cover_full) != 0u) {
            return KT_ERR_STATE;
        }
    }
    map->elevation_span = span;
    return KT_OK;
}
