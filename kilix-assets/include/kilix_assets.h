#ifndef KILIX_ASSETS_H
#define KILIX_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILIX_ASSETS_VERSION_MAJOR 0
#define KILIX_ASSETS_VERSION_MINOR 1
#define KILIX_ASSETS_VERSION_PATCH 0

#define KILIX_ASSET_DEFAULT_MAX_FILE_BYTES (64u * 1024u * 1024u)
#define KILIX_ASSET_DEFAULT_MAX_IMAGE_BYTES (256u * 1024u * 1024u)
#define KILIX_ASSET_DEFAULT_MAX_DIMENSION 8192u

typedef enum kilix_asset_status {
    KILIX_ASSET_OK = 0,
    KILIX_ASSET_INVALID_ARGUMENT,
    KILIX_ASSET_NOT_FOUND,
    KILIX_ASSET_IO_ERROR,
    KILIX_ASSET_UNSUPPORTED,
    KILIX_ASSET_CORRUPT,
    KILIX_ASSET_LIMIT_EXCEEDED,
    KILIX_ASSET_OUT_OF_MEMORY
} kilix_asset_status;

const char *kilix_asset_status_string(kilix_asset_status status);

/* Paths accepted from manifests are relative and portable. Absolute paths,
 * empty components, backslashes, and `.`/`..` components are rejected. */
bool kilix_asset_path_is_safe(const char *relative_path);

typedef struct kilix_asset_locator {
    const char *environment_variable;
    const char *source_root;
    const char *installed_root;
} kilix_asset_locator;

void kilix_asset_locator_init(kilix_asset_locator *locator);

/* Resolves an existing file. The environment override wins, followed by the
 * source-tree root and installed root. Roots are borrowed from the caller. */
kilix_asset_status kilix_asset_resolve(const kilix_asset_locator *locator,
                                       const char *relative_path,
                                       char *destination,
                                       size_t destination_size);

typedef struct kilix_asset_limits {
    size_t max_file_bytes;
    size_t max_image_bytes;
    uint32_t max_dimension;
} kilix_asset_limits;

void kilix_asset_limits_init(kilix_asset_limits *limits);

/* Owned, straight-alpha RGBA8 pixels. */
typedef struct kilix_asset_image {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    size_t stride;
    size_t byte_count;
} kilix_asset_image;

void kilix_asset_image_clear(kilix_asset_image *image);
bool kilix_asset_image_is_valid(const kilix_asset_image *image);
kilix_asset_status kilix_asset_image_load_png(
    kilix_asset_image *image, const char *path,
    const kilix_asset_limits *limits);
kilix_asset_status kilix_asset_image_load_rgba(
    kilix_asset_image *image, const char *path, uint32_t width,
    uint32_t height, const kilix_asset_limits *limits);

typedef struct kilix_asset_region {
    const uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    size_t stride;
} kilix_asset_region;

bool kilix_asset_region_is_valid(const kilix_asset_region *region);
kilix_asset_region kilix_asset_image_region(const kilix_asset_image *image,
                                             uint32_t x, uint32_t y,
                                             uint32_t width,
                                             uint32_t height);

typedef struct kilix_asset_atlas {
    const kilix_asset_image *image;
    uint32_t columns;
    uint32_t rows;
    uint32_t cell_width;
    uint32_t cell_height;
} kilix_asset_atlas;

bool kilix_asset_atlas_init_grid(kilix_asset_atlas *atlas,
                                 const kilix_asset_image *image,
                                 uint32_t columns, uint32_t rows);
kilix_asset_region kilix_asset_atlas_cell(const kilix_asset_atlas *atlas,
                                          uint32_t column, uint32_t row);

typedef struct kilix_asset_clip {
    uint32_t first_frame;
    uint32_t frame_count;
    uint32_t ticks_per_frame;
    bool loop;
} kilix_asset_clip;

bool kilix_asset_clip_is_valid(const kilix_asset_clip *clip);
uint32_t kilix_asset_clip_frame(const kilix_asset_clip *clip, uint64_t tick);

/* The cache owns loaded images. Returned image pointers remain valid until
 * clear; duplicate canonical path/format requests return the same image. */
typedef struct kilix_asset_cache {
    void *entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t byte_count;
    size_t max_entries;
    size_t max_bytes;
} kilix_asset_cache;

bool kilix_asset_cache_init(kilix_asset_cache *cache, size_t max_entries,
                            size_t max_bytes);
void kilix_asset_cache_clear(kilix_asset_cache *cache);
kilix_asset_status kilix_asset_cache_load_png(
    kilix_asset_cache *cache, const char *path,
    const kilix_asset_limits *limits, const kilix_asset_image **image);
kilix_asset_status kilix_asset_cache_load_rgba(
    kilix_asset_cache *cache, const char *path, uint32_t width,
    uint32_t height, const kilix_asset_limits *limits,
    const kilix_asset_image **image);

typedef struct kilix_asset_manifest_atlas {
    char *id;
    char *path;
    bool alpha_required;
    uint32_t columns;
    uint32_t rows;
    uint32_t width;
    uint32_t height;
    uint32_t cell_width;
    uint32_t cell_height;
} kilix_asset_manifest_atlas;

typedef struct kilix_asset_manifest_bitmap {
    char *id;
    char *path;
    uint32_t width;
    uint32_t height;
} kilix_asset_manifest_bitmap;

/* Parser for the versioned Kilix graphics manifest schema. Unknown metadata
 * fields are skipped, while runtime atlas/bitmap records are strict. */
typedef struct kilix_asset_manifest {
    uint32_t schema_version;
    char *game;
    kilix_asset_manifest_atlas *atlases;
    size_t atlas_count;
    kilix_asset_manifest_bitmap *bitmaps;
    size_t bitmap_count;
} kilix_asset_manifest;

void kilix_asset_manifest_clear(kilix_asset_manifest *manifest);
kilix_asset_status kilix_asset_manifest_load_json(
    kilix_asset_manifest *manifest, const char *path, size_t max_file_bytes);
const kilix_asset_manifest_atlas *kilix_asset_manifest_find_atlas(
    const kilix_asset_manifest *manifest, const char *id);
const kilix_asset_manifest_bitmap *kilix_asset_manifest_find_bitmap(
    const kilix_asset_manifest *manifest, const char *id);

#ifdef __cplusplus
}
#endif

#endif
