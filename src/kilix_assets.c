#include "kilix_assets.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <zlib.h>

typedef struct cache_entry {
    char *key;
    kilix_asset_image image;
    uint32_t raw_width;
    uint32_t raw_height;
    bool png;
} cache_entry;

typedef struct json_reader {
    const char *bytes;
    size_t size;
    size_t cursor;
    unsigned int depth;
} json_reader;

static bool checked_image_size(uint32_t width, uint32_t height,
                               size_t *stride, size_t *byte_count)
{
    size_t row;
    if (width == 0u || height == 0u) return false;
    row = (size_t)width * 4u;
    if (row / 4u != (size_t)width) return false;
    if ((size_t)height > SIZE_MAX / row) return false;
    if (stride) *stride = row;
    if (byte_count) *byte_count = row * (size_t)height;
    return true;
}

const char *kilix_asset_status_string(kilix_asset_status status)
{
    switch (status) {
    case KILIX_ASSET_OK: return "ok";
    case KILIX_ASSET_INVALID_ARGUMENT: return "invalid argument";
    case KILIX_ASSET_NOT_FOUND: return "not found";
    case KILIX_ASSET_IO_ERROR: return "I/O error";
    case KILIX_ASSET_UNSUPPORTED: return "unsupported format";
    case KILIX_ASSET_CORRUPT: return "corrupt asset";
    case KILIX_ASSET_LIMIT_EXCEEDED: return "resource limit exceeded";
    case KILIX_ASSET_OUT_OF_MEMORY: return "out of memory";
    }
    return "unknown asset error";
}

bool kilix_asset_path_is_safe(const char *relative_path)
{
    const char *component;
    const char *cursor;
    if (!relative_path || relative_path[0] == '\0' ||
        relative_path[0] == '/') return false;
    component = relative_path;
    for (cursor = relative_path;; ++cursor) {
        unsigned char byte = (unsigned char)*cursor;
        if (byte == '\\' || (byte != '\0' && byte < 32u)) return false;
        if (byte == '/' || byte == '\0') {
            size_t length = (size_t)(cursor - component);
            if (length == 0u ||
                (length == 1u && component[0] == '.') ||
                (length == 2u && component[0] == '.' &&
                 component[1] == '.')) return false;
            if (byte == '\0') break;
            component = cursor + 1;
        }
    }
    return true;
}

void kilix_asset_locator_init(kilix_asset_locator *locator)
{
    if (!locator) return;
    locator->environment_variable = NULL;
    locator->source_root = ".";
    locator->installed_root = NULL;
}

static bool regular_file_exists(const char *path)
{
    struct stat status;
    return path && stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static bool join_path(const char *root, const char *relative, char *output,
                      size_t output_size)
{
    int count;
    size_t root_length;
    const char *separator;
    if (!root || root[0] == '\0' || !relative || !output || output_size == 0u)
        return false;
    root_length = strlen(root);
    separator = root[root_length - 1u] == '/' ? "" : "/";
    count = snprintf(output, output_size, "%s%s%s", root, separator, relative);
    return count >= 0 && (size_t)count < output_size;
}

kilix_asset_status kilix_asset_resolve(const kilix_asset_locator *locator,
                                       const char *relative_path,
                                       char *destination,
                                       size_t destination_size)
{
    const char *roots[3];
    size_t count = 0u;
    size_t index;
    if (!locator || !kilix_asset_path_is_safe(relative_path) ||
        !destination || destination_size == 0u)
        return KILIX_ASSET_INVALID_ARGUMENT;
    destination[0] = '\0';
    if (locator->environment_variable &&
        locator->environment_variable[0] != '\0') {
        const char *override = getenv(locator->environment_variable);
        if (override && override[0] != '\0') roots[count++] = override;
    }
    if (locator->source_root && locator->source_root[0] != '\0')
        roots[count++] = locator->source_root;
    if (locator->installed_root && locator->installed_root[0] != '\0')
        roots[count++] = locator->installed_root;
    for (index = 0u; index < count; ++index) {
        if (!join_path(roots[index], relative_path, destination,
                       destination_size)) {
            destination[0] = '\0';
            return KILIX_ASSET_LIMIT_EXCEEDED;
        }
        if (regular_file_exists(destination)) return KILIX_ASSET_OK;
    }
    destination[0] = '\0';
    return KILIX_ASSET_NOT_FOUND;
}

void kilix_asset_limits_init(kilix_asset_limits *limits)
{
    if (!limits) return;
    limits->max_file_bytes = KILIX_ASSET_DEFAULT_MAX_FILE_BYTES;
    limits->max_image_bytes = KILIX_ASSET_DEFAULT_MAX_IMAGE_BYTES;
    limits->max_dimension = KILIX_ASSET_DEFAULT_MAX_DIMENSION;
}

void kilix_asset_image_clear(kilix_asset_image *image)
{
    if (!image) return;
    free(image->pixels);
    *image = (kilix_asset_image){0};
}

bool kilix_asset_image_is_valid(const kilix_asset_image *image)
{
    size_t minimum_stride;
    size_t minimum_bytes;
    return image && image->pixels &&
           checked_image_size(image->width, image->height, &minimum_stride,
                              &minimum_bytes) &&
           image->stride >= minimum_stride &&
           image->stride <= SIZE_MAX / (size_t)image->height &&
           image->byte_count >= image->stride * (size_t)image->height &&
           image->byte_count >= minimum_bytes;
}

static kilix_asset_limits selected_limits(const kilix_asset_limits *limits)
{
    kilix_asset_limits result;
    kilix_asset_limits_init(&result);
    if (limits) result = *limits;
    return result;
}

static kilix_asset_status read_file(const char *path, size_t maximum,
                                    uint8_t **bytes, size_t *size)
{
    FILE *stream;
    uint8_t *contents;
    long length;
    if (!path || maximum == 0u || !bytes || !size)
        return KILIX_ASSET_INVALID_ARGUMENT;
    *bytes = NULL;
    *size = 0u;
    stream = fopen(path, "rb");
    if (!stream)
        return errno == ENOENT ? KILIX_ASSET_NOT_FOUND : KILIX_ASSET_IO_ERROR;
    if (fseek(stream, 0L, SEEK_END) != 0 || (length = ftell(stream)) < 0L ||
        fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        return KILIX_ASSET_IO_ERROR;
    }
    if ((unsigned long)length > (unsigned long)maximum) {
        (void)fclose(stream);
        return KILIX_ASSET_LIMIT_EXCEEDED;
    }
    contents = malloc((size_t)length + 1u);
    if (!contents) {
        (void)fclose(stream);
        return KILIX_ASSET_OUT_OF_MEMORY;
    }
    if ((size_t)length != 0u &&
        fread(contents, 1u, (size_t)length, stream) != (size_t)length) {
        free(contents);
        (void)fclose(stream);
        return KILIX_ASSET_IO_ERROR;
    }
    if (fclose(stream) != 0) {
        free(contents);
        return KILIX_ASSET_IO_ERROR;
    }
    contents[(size_t)length] = UINT8_C(0);
    *bytes = contents;
    *size = (size_t)length;
    return KILIX_ASSET_OK;
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | (uint32_t)bytes[3];
}

static uint8_t paeth(uint8_t left, uint8_t above, uint8_t upper_left)
{
    int prediction = (int)left + (int)above - (int)upper_left;
    int left_delta = prediction - (int)left;
    int above_delta = prediction - (int)above;
    int diagonal_delta = prediction - (int)upper_left;
    if (left_delta < 0) left_delta = -left_delta;
    if (above_delta < 0) above_delta = -above_delta;
    if (diagonal_delta < 0) diagonal_delta = -diagonal_delta;
    if (left_delta <= above_delta && left_delta <= diagonal_delta) return left;
    return above_delta <= diagonal_delta ? above : upper_left;
}

static bool unfilter(uint8_t *bytes, size_t row_bytes, uint32_t height,
                     unsigned int bytes_per_pixel)
{
    uint32_t y;
    for (y = 0u; y < height; ++y) {
        size_t source_row = (size_t)y * (row_bytes + 1u);
        size_t output_row = (size_t)y * row_bytes;
        uint8_t filter = bytes[source_row];
        size_t x;
        if (filter > 4u) return false;
        for (x = 0u; x < row_bytes; ++x) {
            uint8_t raw = bytes[source_row + 1u + x];
            uint8_t left = x >= (size_t)bytes_per_pixel ?
                bytes[output_row + x - (size_t)bytes_per_pixel] : 0u;
            uint8_t above = y != 0u ? bytes[output_row - row_bytes + x] : 0u;
            uint8_t diagonal = y != 0u && x >= (size_t)bytes_per_pixel ?
                bytes[output_row - row_bytes + x -
                      (size_t)bytes_per_pixel] : 0u;
            uint8_t predictor = 0u;
            if (filter == 1u) predictor = left;
            else if (filter == 2u) predictor = above;
            else if (filter == 3u)
                predictor = (uint8_t)(((unsigned int)left + above) / 2u);
            else if (filter == 4u) predictor = paeth(left, above, diagonal);
            bytes[output_row + x] = (uint8_t)(raw + predictor);
        }
    }
    return true;
}

kilix_asset_status kilix_asset_image_load_png(
    kilix_asset_image *image, const char *path,
    const kilix_asset_limits *limits)
{
    static const uint8_t signature[8] = {
        137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
    };
    kilix_asset_limits bounds = selected_limits(limits);
    kilix_asset_image decoded = {0};
    uint8_t *file = NULL;
    uint8_t *compressed = NULL;
    uint8_t *scanlines = NULL;
    size_t file_size = 0u;
    size_t compressed_size = 0u;
    size_t cursor = sizeof signature;
    uint32_t width = 0u;
    uint32_t height = 0u;
    unsigned int channels = 0u;
    bool header_seen = false;
    bool end_seen = false;
    kilix_asset_status result;
    if (!image || !path || bounds.max_file_bytes == 0u ||
        bounds.max_image_bytes == 0u || bounds.max_dimension == 0u)
        return KILIX_ASSET_INVALID_ARGUMENT;
    result = read_file(path, bounds.max_file_bytes, &file, &file_size);
    if (result != KILIX_ASSET_OK) return result;
    if (file_size < sizeof signature ||
        memcmp(file, signature, sizeof signature) != 0) {
        result = KILIX_ASSET_UNSUPPORTED;
        goto done;
    }
    while (cursor < file_size) {
        uint32_t chunk_length;
        size_t chunk_size;
        const uint8_t *type;
        const uint8_t *chunk;
        uint32_t expected_crc;
        uLong actual_crc;
        if (file_size - cursor < 12u) {
            result = KILIX_ASSET_CORRUPT;
            goto done;
        }
        chunk_length = read_be32(file + cursor);
        cursor += 4u;
        type = file + cursor;
        cursor += 4u;
        chunk_size = (size_t)chunk_length;
        if (chunk_size > file_size - cursor - 4u || chunk_size > UINT_MAX) {
            result = KILIX_ASSET_CORRUPT;
            goto done;
        }
        chunk = file + cursor;
        expected_crc = read_be32(chunk + chunk_size);
        actual_crc = crc32(0L, Z_NULL, 0);
        actual_crc = crc32(actual_crc, type, 4u);
        actual_crc = crc32(actual_crc, chunk, (uInt)chunk_size);
        if ((uint32_t)actual_crc != expected_crc) {
            result = KILIX_ASSET_CORRUPT;
            goto done;
        }
        cursor += chunk_size + 4u;
        if (memcmp(type, "IHDR", 4u) == 0) {
            unsigned int color_type;
            if (header_seen || chunk_size != 13u) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            width = read_be32(chunk);
            height = read_be32(chunk + 4u);
            color_type = chunk[9];
            if (width == 0u || height == 0u ||
                width > bounds.max_dimension || height > bounds.max_dimension ||
                chunk[8] != 8u || chunk[10] != 0u || chunk[11] != 0u ||
                chunk[12] != 0u) {
                result = KILIX_ASSET_UNSUPPORTED;
                goto done;
            }
            if (color_type == 0u) channels = 1u;
            else if (color_type == 2u) channels = 3u;
            else if (color_type == 4u) channels = 2u;
            else if (color_type == 6u) channels = 4u;
            else {
                result = KILIX_ASSET_UNSUPPORTED;
                goto done;
            }
            header_seen = true;
        } else if (memcmp(type, "IDAT", 4u) == 0) {
            uint8_t *next;
            if (!header_seen || chunk_size > bounds.max_file_bytes -
                compressed_size) {
                result = KILIX_ASSET_LIMIT_EXCEEDED;
                goto done;
            }
            next = realloc(compressed, compressed_size + chunk_size);
            if (!next) {
                result = KILIX_ASSET_OUT_OF_MEMORY;
                goto done;
            }
            compressed = next;
            memcpy(compressed + compressed_size, chunk, chunk_size);
            compressed_size += chunk_size;
        } else if (memcmp(type, "IEND", 4u) == 0) {
            if (chunk_size != 0u) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            end_seen = true;
            break;
        } else if (memcmp(type, "PLTE", 4u) != 0 &&
                   (type[0] & UINT8_C(0x20)) == 0u) {
            result = KILIX_ASSET_UNSUPPORTED;
            goto done;
        }
    }
    if (!header_seen || !end_seen || compressed_size == 0u ||
        cursor != file_size) {
        result = KILIX_ASSET_CORRUPT;
        goto done;
    }
    {
        size_t source_stride;
        size_t inflated_size;
        size_t rgba_stride;
        size_t rgba_size;
        uLongf actual_size;
        uint32_t y;
        if ((size_t)width > SIZE_MAX / channels) {
            result = KILIX_ASSET_LIMIT_EXCEEDED;
            goto done;
        }
        source_stride = (size_t)width * channels;
        if (source_stride == SIZE_MAX ||
            (size_t)height > SIZE_MAX / (source_stride + 1u) ||
            !checked_image_size(width, height, &rgba_stride, &rgba_size) ||
            rgba_size > bounds.max_image_bytes) {
            result = KILIX_ASSET_LIMIT_EXCEEDED;
            goto done;
        }
        inflated_size = (source_stride + 1u) * (size_t)height;
        if (inflated_size > bounds.max_image_bytes ||
            inflated_size > (size_t)ULONG_MAX ||
            compressed_size > (size_t)ULONG_MAX) {
            result = KILIX_ASSET_LIMIT_EXCEEDED;
            goto done;
        }
        scanlines = malloc(inflated_size);
        decoded.pixels = malloc(rgba_size);
        if (!scanlines || !decoded.pixels) {
            result = KILIX_ASSET_OUT_OF_MEMORY;
            goto done;
        }
        actual_size = (uLongf)inflated_size;
        if (uncompress(scanlines, &actual_size, compressed,
                       (uLong)compressed_size) != Z_OK ||
            actual_size != (uLongf)inflated_size ||
            !unfilter(scanlines, source_stride, height, channels)) {
            result = KILIX_ASSET_CORRUPT;
            goto done;
        }
        for (y = 0u; y < height; ++y) {
            const uint8_t *source = scanlines + (size_t)y * source_stride;
            uint8_t *destination = decoded.pixels + (size_t)y * rgba_stride;
            uint32_t x;
            for (x = 0u; x < width; ++x) {
                if (channels == 1u) {
                    destination[x * 4u] = source[x];
                    destination[x * 4u + 1u] = source[x];
                    destination[x * 4u + 2u] = source[x];
                    destination[x * 4u + 3u] = UINT8_MAX;
                } else if (channels == 2u) {
                    destination[x * 4u] = source[x * 2u];
                    destination[x * 4u + 1u] = source[x * 2u];
                    destination[x * 4u + 2u] = source[x * 2u];
                    destination[x * 4u + 3u] = source[x * 2u + 1u];
                } else if (channels == 3u) {
                    destination[x * 4u] = source[x * 3u];
                    destination[x * 4u + 1u] = source[x * 3u + 1u];
                    destination[x * 4u + 2u] = source[x * 3u + 2u];
                    destination[x * 4u + 3u] = UINT8_MAX;
                } else {
                    memcpy(destination + x * 4u, source + x * 4u, 4u);
                }
            }
        }
        decoded.width = width;
        decoded.height = height;
        decoded.stride = rgba_stride;
        decoded.byte_count = rgba_size;
    }
    kilix_asset_image_clear(image);
    *image = decoded;
    decoded = (kilix_asset_image){0};
    result = KILIX_ASSET_OK;
done:
    free(file);
    free(compressed);
    free(scanlines);
    kilix_asset_image_clear(&decoded);
    return result;
}

kilix_asset_status kilix_asset_image_load_rgba(
    kilix_asset_image *image, const char *path, uint32_t width,
    uint32_t height, const kilix_asset_limits *limits)
{
    kilix_asset_limits bounds = selected_limits(limits);
    kilix_asset_image loaded = {0};
    uint8_t *bytes = NULL;
    size_t size = 0u;
    size_t stride;
    size_t expected;
    kilix_asset_status result;
    if (!image || !path || width > bounds.max_dimension ||
        height > bounds.max_dimension ||
        !checked_image_size(width, height, &stride, &expected) ||
        expected > bounds.max_image_bytes || expected > bounds.max_file_bytes)
        return KILIX_ASSET_INVALID_ARGUMENT;
    result = read_file(path, bounds.max_file_bytes, &bytes, &size);
    if (result != KILIX_ASSET_OK) return result;
    if (size != expected) {
        free(bytes);
        return KILIX_ASSET_CORRUPT;
    }
    loaded.pixels = bytes;
    loaded.width = width;
    loaded.height = height;
    loaded.stride = stride;
    loaded.byte_count = expected;
    kilix_asset_image_clear(image);
    *image = loaded;
    return KILIX_ASSET_OK;
}

bool kilix_asset_region_is_valid(const kilix_asset_region *region)
{
    size_t minimum_stride;
    if (!region || !region->pixels || region->width == 0u ||
        region->height == 0u) return false;
    minimum_stride = (size_t)region->width * 4u;
    return minimum_stride / 4u == (size_t)region->width &&
           region->stride >= minimum_stride;
}

kilix_asset_region kilix_asset_image_region(const kilix_asset_image *image,
                                             uint32_t x, uint32_t y,
                                             uint32_t width,
                                             uint32_t height)
{
    kilix_asset_region region = {0};
    if (!kilix_asset_image_is_valid(image) || width == 0u || height == 0u ||
        x >= image->width || y >= image->height ||
        width > image->width - x || height > image->height - y) return region;
    region.pixels = image->pixels + (size_t)y * image->stride + (size_t)x * 4u;
    region.width = width;
    region.height = height;
    region.stride = image->stride;
    return region;
}

bool kilix_asset_atlas_init_grid(kilix_asset_atlas *atlas,
                                 const kilix_asset_image *image,
                                 uint32_t columns, uint32_t rows)
{
    if (!atlas || !kilix_asset_image_is_valid(image) || columns == 0u ||
        rows == 0u || image->width % columns != 0u ||
        image->height % rows != 0u) return false;
    atlas->image = image;
    atlas->columns = columns;
    atlas->rows = rows;
    atlas->cell_width = image->width / columns;
    atlas->cell_height = image->height / rows;
    return true;
}

kilix_asset_region kilix_asset_atlas_cell(const kilix_asset_atlas *atlas,
                                          uint32_t column, uint32_t row)
{
    if (!atlas || column >= atlas->columns || row >= atlas->rows)
        return (kilix_asset_region){0};
    return kilix_asset_image_region(atlas->image,
        column * atlas->cell_width, row * atlas->cell_height,
        atlas->cell_width, atlas->cell_height);
}

bool kilix_asset_clip_is_valid(const kilix_asset_clip *clip)
{
    return clip && clip->frame_count != 0u && clip->ticks_per_frame != 0u &&
           clip->first_frame <= UINT32_MAX - (clip->frame_count - 1u);
}

uint32_t kilix_asset_clip_frame(const kilix_asset_clip *clip, uint64_t tick)
{
    uint64_t frame;
    if (!kilix_asset_clip_is_valid(clip)) return 0u;
    frame = tick / clip->ticks_per_frame;
    if (clip->loop) frame %= clip->frame_count;
    else if (frame >= clip->frame_count) frame = clip->frame_count - 1u;
    return clip->first_frame + (uint32_t)frame;
}

bool kilix_asset_cache_init(kilix_asset_cache *cache, size_t max_entries,
                            size_t max_bytes)
{
    cache_entry *entries;
    if (!cache || max_entries == 0u || max_bytes == 0u ||
        max_entries > SIZE_MAX / sizeof *entries)
        return false;
    entries = calloc(max_entries, sizeof *entries);
    if (!entries) return false;
    *cache = (kilix_asset_cache){0};
    cache->entries = entries;
    cache->entry_capacity = max_entries;
    cache->max_entries = max_entries;
    cache->max_bytes = max_bytes;
    return true;
}

void kilix_asset_cache_clear(kilix_asset_cache *cache)
{
    cache_entry *entries;
    size_t index;
    if (!cache) return;
    entries = cache->entries;
    for (index = 0u; index < cache->entry_count; ++index) {
        free(entries[index].key);
        kilix_asset_image_clear(&entries[index].image);
    }
    free(entries);
    *cache = (kilix_asset_cache){0};
}

static kilix_asset_status cache_load(kilix_asset_cache *cache,
                                     const char *path, uint32_t width,
                                     uint32_t height, bool png,
                                     const kilix_asset_limits *limits,
                                     const kilix_asset_image **image)
{
    cache_entry *entries;
    cache_entry next = {0};
    size_t index;
    size_t key_size;
    kilix_asset_status result;
    if (!cache || !path || !image || cache->max_entries == 0u ||
        cache->max_bytes == 0u) return KILIX_ASSET_INVALID_ARGUMENT;
    *image = NULL;
    entries = cache->entries;
    for (index = 0u; index < cache->entry_count; ++index) {
        if (entries[index].png == png &&
            (png || (entries[index].raw_width == width &&
                     entries[index].raw_height == height)) &&
            strcmp(entries[index].key, path) == 0) {
            *image = &entries[index].image;
            return KILIX_ASSET_OK;
        }
    }
    if (cache->entry_count >= cache->max_entries)
        return KILIX_ASSET_LIMIT_EXCEEDED;
    result = png ? kilix_asset_image_load_png(&next.image, path, limits) :
                   kilix_asset_image_load_rgba(&next.image, path, width,
                                               height, limits);
    if (result != KILIX_ASSET_OK) return result;
    if (cache->byte_count > cache->max_bytes ||
        next.image.byte_count > cache->max_bytes - cache->byte_count) {
        kilix_asset_image_clear(&next.image);
        return KILIX_ASSET_LIMIT_EXCEEDED;
    }
    key_size = strlen(path) + 1u;
    next.key = malloc(key_size);
    if (!next.key) {
        kilix_asset_image_clear(&next.image);
        return KILIX_ASSET_OUT_OF_MEMORY;
    }
    memcpy(next.key, path, key_size);
    next.png = png;
    next.raw_width = width;
    next.raw_height = height;
    if (cache->entry_count == cache->entry_capacity) {
        size_t capacity = cache->entry_capacity == 0u ? 8u :
                          cache->entry_capacity * 2u;
        cache_entry *grown;
        if (capacity > cache->max_entries) capacity = cache->max_entries;
        if (capacity < cache->entry_count + 1u ||
            capacity > SIZE_MAX / sizeof *grown) {
            free(next.key);
            kilix_asset_image_clear(&next.image);
            return KILIX_ASSET_LIMIT_EXCEEDED;
        }
        grown = realloc(entries, capacity * sizeof *grown);
        if (!grown) {
            free(next.key);
            kilix_asset_image_clear(&next.image);
            return KILIX_ASSET_OUT_OF_MEMORY;
        }
        cache->entries = entries = grown;
        cache->entry_capacity = capacity;
    }
    entries[cache->entry_count] = next;
    cache->byte_count += next.image.byte_count;
    *image = &entries[cache->entry_count].image;
    ++cache->entry_count;
    return KILIX_ASSET_OK;
}

kilix_asset_status kilix_asset_cache_load_png(
    kilix_asset_cache *cache, const char *path,
    const kilix_asset_limits *limits, const kilix_asset_image **image)
{
    return cache_load(cache, path, 0u, 0u, true, limits, image);
}

kilix_asset_status kilix_asset_cache_load_rgba(
    kilix_asset_cache *cache, const char *path, uint32_t width,
    uint32_t height, const kilix_asset_limits *limits,
    const kilix_asset_image **image)
{
    return cache_load(cache, path, width, height, false, limits, image);
}

static void json_space(json_reader *reader)
{
    while (reader->cursor < reader->size &&
           isspace((unsigned char)reader->bytes[reader->cursor]))
        ++reader->cursor;
}

static bool json_take(json_reader *reader, char byte)
{
    json_space(reader);
    if (reader->cursor >= reader->size ||
        reader->bytes[reader->cursor] != byte) return false;
    ++reader->cursor;
    return true;
}

static bool json_string(json_reader *reader, char **output)
{
    char *value;
    size_t length = 0u;
    if (!output || !json_take(reader, '"')) return false;
    value = malloc(reader->size - reader->cursor + 1u);
    if (!value) return false;
    while (reader->cursor < reader->size) {
        unsigned char byte = (unsigned char)reader->bytes[reader->cursor++];
        if (byte == '"') {
            value[length] = '\0';
            *output = value;
            return true;
        }
        if (byte < 32u) break;
        if (byte == '\\') {
            unsigned char escape;
            if (reader->cursor >= reader->size) break;
            escape = (unsigned char)reader->bytes[reader->cursor++];
            if (escape == '"' || escape == '\\' || escape == '/') byte = escape;
            else if (escape == 'b') byte = '\b';
            else if (escape == 'f') byte = '\f';
            else if (escape == 'n') byte = '\n';
            else if (escape == 'r') byte = '\r';
            else if (escape == 't') byte = '\t';
            else if (escape == 'u') {
                unsigned int code = 0u;
                unsigned int digit;
                if (reader->size - reader->cursor < 4u) break;
                for (digit = 0u; digit < 4u; ++digit) {
                    unsigned char hex =
                        (unsigned char)reader->bytes[reader->cursor++];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') code |= hex - '0';
                    else if (hex >= 'a' && hex <= 'f') code |= hex - 'a' + 10u;
                    else if (hex >= 'A' && hex <= 'F') code |= hex - 'A' + 10u;
                    else { free(value); return false; }
                }
                byte = code >= 32u && code <= 126u ? (unsigned char)code : '?';
            } else break;
        }
        value[length++] = (char)byte;
    }
    free(value);
    return false;
}

static bool json_uint(json_reader *reader, uint32_t *output)
{
    uint64_t value = 0u;
    bool found = false;
    json_space(reader);
    if (reader->cursor < reader->size &&
        reader->bytes[reader->cursor] == '0' &&
        reader->cursor + 1u < reader->size &&
        isdigit((unsigned char)reader->bytes[reader->cursor + 1u]))
        return false;
    while (reader->cursor < reader->size) {
        unsigned char byte = (unsigned char)reader->bytes[reader->cursor];
        if (!isdigit(byte)) break;
        found = true;
        value = value * 10u + (uint64_t)(byte - '0');
        if (value > UINT32_MAX) return false;
        ++reader->cursor;
    }
    if (!found) return false;
    *output = (uint32_t)value;
    return true;
}

static bool json_literal(json_reader *reader, const char *literal)
{
    size_t size = strlen(literal);
    json_space(reader);
    if (size > reader->size - reader->cursor ||
        memcmp(reader->bytes + reader->cursor, literal, size) != 0)
        return false;
    reader->cursor += size;
    return true;
}

static bool json_bool(json_reader *reader, bool *output)
{
    if (json_literal(reader, "true")) { *output = true; return true; }
    if (json_literal(reader, "false")) { *output = false; return true; }
    return false;
}

static bool json_skip_value(json_reader *reader);

static bool json_skip_number(json_reader *reader)
{
    size_t cursor;
    json_space(reader);
    cursor = reader->cursor;
    if (cursor < reader->size && reader->bytes[cursor] == '-') ++cursor;
    if (cursor >= reader->size) return false;
    if (reader->bytes[cursor] == '0') {
        ++cursor;
        if (cursor < reader->size &&
            isdigit((unsigned char)reader->bytes[cursor])) return false;
    } else {
        if (reader->bytes[cursor] < '1' || reader->bytes[cursor] > '9')
            return false;
        do { ++cursor; }
        while (cursor < reader->size &&
               isdigit((unsigned char)reader->bytes[cursor]));
    }
    if (cursor < reader->size && reader->bytes[cursor] == '.') {
        ++cursor;
        if (cursor >= reader->size ||
            !isdigit((unsigned char)reader->bytes[cursor])) return false;
        do { ++cursor; }
        while (cursor < reader->size &&
               isdigit((unsigned char)reader->bytes[cursor]));
    }
    if (cursor < reader->size &&
        (reader->bytes[cursor] == 'e' || reader->bytes[cursor] == 'E')) {
        ++cursor;
        if (cursor < reader->size &&
            (reader->bytes[cursor] == '+' || reader->bytes[cursor] == '-'))
            ++cursor;
        if (cursor >= reader->size ||
            !isdigit((unsigned char)reader->bytes[cursor])) return false;
        do { ++cursor; }
        while (cursor < reader->size &&
               isdigit((unsigned char)reader->bytes[cursor]));
    }
    reader->cursor = cursor;
    return true;
}

static bool json_skip_sequence(json_reader *reader, char open, char close)
{
    bool object = open == '{';
    if (reader->depth >= 64u || !json_take(reader, open)) return false;
    ++reader->depth;
    json_space(reader);
    if (json_take(reader, close)) { --reader->depth; return true; }
    for (;;) {
        if (object) {
            char *key = NULL;
            if (!json_string(reader, &key)) { --reader->depth; return false; }
            free(key);
            if (!json_take(reader, ':')) { --reader->depth; return false; }
        }
        if (!json_skip_value(reader)) { --reader->depth; return false; }
        if (json_take(reader, close)) { --reader->depth; return true; }
        if (!json_take(reader, ',')) { --reader->depth; return false; }
    }
}

static bool json_skip_value(json_reader *reader)
{
    char *string = NULL;
    json_space(reader);
    if (reader->cursor >= reader->size) return false;
    if (reader->bytes[reader->cursor] == '"') {
        bool result = json_string(reader, &string);
        free(string);
        return result;
    }
    if (reader->bytes[reader->cursor] == '{')
        return json_skip_sequence(reader, '{', '}');
    if (reader->bytes[reader->cursor] == '[')
        return json_skip_sequence(reader, '[', ']');
    if (json_literal(reader, "true") || json_literal(reader, "false") ||
        json_literal(reader, "null")) return true;
    return json_skip_number(reader);
}

static void free_atlas(kilix_asset_manifest_atlas *atlas)
{
    free(atlas->id);
    free(atlas->path);
    *atlas = (kilix_asset_manifest_atlas){0};
}

static void free_bitmap(kilix_asset_manifest_bitmap *bitmap)
{
    free(bitmap->id);
    free(bitmap->path);
    *bitmap = (kilix_asset_manifest_bitmap){0};
}

void kilix_asset_manifest_clear(kilix_asset_manifest *manifest)
{
    size_t index;
    if (!manifest) return;
    free(manifest->game);
    for (index = 0u; index < manifest->atlas_count; ++index)
        free_atlas(&manifest->atlases[index]);
    for (index = 0u; index < manifest->bitmap_count; ++index)
        free_bitmap(&manifest->bitmaps[index]);
    free(manifest->atlases);
    free(manifest->bitmaps);
    *manifest = (kilix_asset_manifest){0};
}

static bool parse_grid(json_reader *reader,
                       kilix_asset_manifest_atlas *atlas)
{
    bool columns = false, rows = false, width = false, height = false;
    bool cell_width = false, cell_height = false;
    if (!json_take(reader, '{')) return false;
    if (json_take(reader, '}')) return false;
    for (;;) {
        char *key = NULL;
        bool result;
        if (!json_string(reader, &key) || !json_take(reader, ':')) {
            free(key);
            return false;
        }
        if (strcmp(key, "columns") == 0)
            result = json_uint(reader, &atlas->columns), columns = result;
        else if (strcmp(key, "rows") == 0)
            result = json_uint(reader, &atlas->rows), rows = result;
        else if (strcmp(key, "width") == 0)
            result = json_uint(reader, &atlas->width), width = result;
        else if (strcmp(key, "height") == 0)
            result = json_uint(reader, &atlas->height), height = result;
        else if (strcmp(key, "cell_width") == 0)
            result = json_uint(reader, &atlas->cell_width), cell_width = result;
        else if (strcmp(key, "cell_height") == 0)
            result = json_uint(reader, &atlas->cell_height), cell_height = result;
        else result = json_skip_value(reader);
        free(key);
        if (!result) return false;
        if (json_take(reader, '}')) break;
        if (!json_take(reader, ',')) return false;
    }
    return columns && rows && width && height && cell_width && cell_height &&
           atlas->columns != 0u && atlas->rows != 0u &&
           atlas->cell_width != 0u && atlas->cell_height != 0u &&
           atlas->columns <= UINT32_MAX / atlas->cell_width &&
           atlas->rows <= UINT32_MAX / atlas->cell_height &&
           atlas->columns * atlas->cell_width == atlas->width &&
           atlas->rows * atlas->cell_height == atlas->height;
}

static bool parse_atlas(json_reader *reader,
                        kilix_asset_manifest_atlas *atlas)
{
    bool grid = false;
    if (!json_take(reader, '{')) return false;
    if (json_take(reader, '}')) return false;
    for (;;) {
        char *key = NULL;
        bool result;
        if (!json_string(reader, &key) || !json_take(reader, ':')) {
            free(key);
            return false;
        }
        if (strcmp(key, "id") == 0) {
            free(atlas->id);
            atlas->id = NULL;
            result = json_string(reader, &atlas->id);
        } else if (strcmp(key, "path") == 0) {
            free(atlas->path);
            atlas->path = NULL;
            result = json_string(reader, &atlas->path);
        } else if (strcmp(key, "alpha_required") == 0) {
            result = json_bool(reader, &atlas->alpha_required);
        } else if (strcmp(key, "grid") == 0) {
            result = parse_grid(reader, atlas);
            grid = result;
        } else result = json_skip_value(reader);
        free(key);
        if (!result) return false;
        if (json_take(reader, '}')) break;
        if (!json_take(reader, ',')) return false;
    }
    return atlas->id && atlas->id[0] != '\0' && atlas->path &&
           kilix_asset_path_is_safe(atlas->path) && grid;
}

static bool parse_bitmap(json_reader *reader,
                         kilix_asset_manifest_bitmap *bitmap)
{
    bool width = false, height = false;
    if (!json_take(reader, '{')) return false;
    if (json_take(reader, '}')) return false;
    for (;;) {
        char *key = NULL;
        bool result;
        if (!json_string(reader, &key) || !json_take(reader, ':')) {
            free(key);
            return false;
        }
        if (strcmp(key, "id") == 0) {
            free(bitmap->id);
            bitmap->id = NULL;
            result = json_string(reader, &bitmap->id);
        } else if (strcmp(key, "png") == 0) {
            free(bitmap->path);
            bitmap->path = NULL;
            result = json_string(reader, &bitmap->path);
        } else if (strcmp(key, "width") == 0)
            result = json_uint(reader, &bitmap->width), width = result;
        else if (strcmp(key, "height") == 0)
            result = json_uint(reader, &bitmap->height), height = result;
        else result = json_skip_value(reader);
        free(key);
        if (!result) return false;
        if (json_take(reader, '}')) break;
        if (!json_take(reader, ',')) return false;
    }
    return bitmap->id && bitmap->id[0] != '\0' && bitmap->path &&
           kilix_asset_path_is_safe(bitmap->path) && width && height &&
           bitmap->width != 0u && bitmap->height != 0u;
}

static bool append_atlas(kilix_asset_manifest *manifest,
                         kilix_asset_manifest_atlas *atlas)
{
    kilix_asset_manifest_atlas *items;
    size_t index;
    for (index = 0u; index < manifest->atlas_count; ++index)
        if (strcmp(manifest->atlases[index].id, atlas->id) == 0) return false;
    if (manifest->atlas_count == SIZE_MAX / sizeof *items) return false;
    items = realloc(manifest->atlases,
                    (manifest->atlas_count + 1u) * sizeof *items);
    if (!items) return false;
    manifest->atlases = items;
    items[manifest->atlas_count++] = *atlas;
    *atlas = (kilix_asset_manifest_atlas){0};
    return true;
}

static bool append_bitmap(kilix_asset_manifest *manifest,
                          kilix_asset_manifest_bitmap *bitmap)
{
    kilix_asset_manifest_bitmap *items;
    size_t index;
    for (index = 0u; index < manifest->bitmap_count; ++index)
        if (strcmp(manifest->bitmaps[index].id, bitmap->id) == 0) return false;
    if (manifest->bitmap_count == SIZE_MAX / sizeof *items) return false;
    items = realloc(manifest->bitmaps,
                    (manifest->bitmap_count + 1u) * sizeof *items);
    if (!items) return false;
    manifest->bitmaps = items;
    items[manifest->bitmap_count++] = *bitmap;
    *bitmap = (kilix_asset_manifest_bitmap){0};
    return true;
}

static bool parse_atlas_array(json_reader *reader,
                              kilix_asset_manifest *manifest)
{
    if (!json_take(reader, '[')) return false;
    if (json_take(reader, ']')) return true;
    for (;;) {
        kilix_asset_manifest_atlas atlas = {0};
        if (!parse_atlas(reader, &atlas) || !append_atlas(manifest, &atlas)) {
            free_atlas(&atlas);
            return false;
        }
        if (json_take(reader, ']')) return true;
        if (!json_take(reader, ',')) return false;
    }
}

static bool parse_bitmap_array(json_reader *reader,
                               kilix_asset_manifest *manifest)
{
    if (!json_take(reader, '[')) return false;
    if (json_take(reader, ']')) return true;
    for (;;) {
        kilix_asset_manifest_bitmap bitmap = {0};
        if (!parse_bitmap(reader, &bitmap) ||
            !append_bitmap(manifest, &bitmap)) {
            free_bitmap(&bitmap);
            return false;
        }
        if (json_take(reader, ']')) return true;
        if (!json_take(reader, ',')) return false;
    }
}

kilix_asset_status kilix_asset_manifest_load_json(
    kilix_asset_manifest *manifest, const char *path, size_t max_file_bytes)
{
    kilix_asset_manifest parsed = {0};
    uint8_t *file = NULL;
    size_t size = 0u;
    json_reader reader;
    bool schema = false, game = false, atlases = false, bitmaps = false;
    kilix_asset_status status;
    if (!manifest || !path || max_file_bytes == 0u)
        return KILIX_ASSET_INVALID_ARGUMENT;
    status = read_file(path, max_file_bytes, &file, &size);
    if (status != KILIX_ASSET_OK) return status;
    reader = (json_reader){(const char *)file, size, 0u, 0u};
    if (!json_take(&reader, '{') || json_take(&reader, '}')) goto corrupt;
    for (;;) {
        char *key = NULL;
        bool result;
        if (!json_string(&reader, &key) || !json_take(&reader, ':')) {
            free(key);
            goto corrupt;
        }
        if (strcmp(key, "schema_version") == 0) {
            result = !schema && json_uint(&reader, &parsed.schema_version);
            schema = result;
        } else if (strcmp(key, "game") == 0) {
            result = !game && json_string(&reader, &parsed.game);
            game = result;
        } else if (strcmp(key, "atlases") == 0) {
            result = !atlases && parse_atlas_array(&reader, &parsed);
            atlases = result;
        } else if (strcmp(key, "bitmaps") == 0) {
            result = !bitmaps && parse_bitmap_array(&reader, &parsed);
            bitmaps = result;
        } else result = json_skip_value(&reader);
        free(key);
        if (!result) goto corrupt;
        if (json_take(&reader, '}')) break;
        if (!json_take(&reader, ',')) goto corrupt;
    }
    json_space(&reader);
    if (reader.cursor != reader.size || !schema || parsed.schema_version != 1u ||
        !game || !parsed.game || parsed.game[0] == '\0' || !atlases ||
        !bitmaps) goto corrupt;
    kilix_asset_manifest_clear(manifest);
    *manifest = parsed;
    free(file);
    return KILIX_ASSET_OK;
corrupt:
    kilix_asset_manifest_clear(&parsed);
    free(file);
    return KILIX_ASSET_CORRUPT;
}

const kilix_asset_manifest_atlas *kilix_asset_manifest_find_atlas(
    const kilix_asset_manifest *manifest, const char *id)
{
    size_t index;
    if (!manifest || !id) return NULL;
    for (index = 0u; index < manifest->atlas_count; ++index)
        if (strcmp(manifest->atlases[index].id, id) == 0)
            return &manifest->atlases[index];
    return NULL;
}

const kilix_asset_manifest_bitmap *kilix_asset_manifest_find_bitmap(
    const kilix_asset_manifest *manifest, const char *id)
{
    size_t index;
    if (!manifest || !id) return NULL;
    for (index = 0u; index < manifest->bitmap_count; ++index)
        if (strcmp(manifest->bitmaps[index].id, id) == 0)
            return &manifest->bitmaps[index];
    return NULL;
}
