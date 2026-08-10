#include "kilix_assets.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zlib.h>

typedef struct cache_entry {
    char *key;
    kilix_asset_image image;
    uint32_t raw_width;
    uint32_t raw_height;
    uint64_t hash;
    bool png;
} cache_entry;

typedef struct cache_state {
    cache_entry **items;
    size_t item_capacity;
    size_t *buckets;
    size_t bucket_count;
} cache_state;

typedef struct json_reader {
    const char *bytes;
    size_t size;
    size_t cursor;
    unsigned int depth;
    kilix_asset_status failure;
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
        if (byte == '\\' || (byte != '\0' && (byte < 32u || byte == 127u)))
            return false;
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

static bool manifest_text_is_safe(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    if (!cursor || *cursor == '\0') return false;
    for (; *cursor != '\0'; ++cursor)
        if (*cursor < 32u || *cursor == 127u) return false;
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
    struct stat before;
    struct stat after;
    uint8_t *contents = NULL;
    size_t length;
    size_t offset = 0u;
    int descriptor;
    int saved_errno;
    if (!path || path[0] == '\0' || maximum == 0u || !bytes || !size)
        return KILIX_ASSET_INVALID_ARGUMENT;
    *bytes = NULL;
    *size = 0u;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0)
        return errno == ENOENT ? KILIX_ASSET_NOT_FOUND : KILIX_ASSET_IO_ERROR;
    if (fstat(descriptor, &before) != 0) {
        (void)close(descriptor);
        return KILIX_ASSET_IO_ERROR;
    }
    if (!S_ISREG(before.st_mode)) {
        (void)close(descriptor);
        return KILIX_ASSET_UNSUPPORTED;
    }
    if (before.st_size < 0 || (uintmax_t)before.st_size > (uintmax_t)maximum ||
        (uintmax_t)before.st_size > (uintmax_t)(SIZE_MAX - 1u)) {
        (void)close(descriptor);
        return KILIX_ASSET_LIMIT_EXCEEDED;
    }
    length = (size_t)before.st_size;
    contents = malloc(length + 1u);
    if (!contents) {
        (void)close(descriptor);
        return KILIX_ASSET_OUT_OF_MEMORY;
    }
    while (offset < length) {
        size_t request = length - offset;
        ssize_t count;
        if (request > (size_t)SSIZE_MAX) request = (size_t)SSIZE_MAX;
        count = read(descriptor, contents + offset, request);
        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        saved_errno = count < 0 ? errno : EIO;
        free(contents);
        (void)close(descriptor);
        errno = saved_errno;
        return KILIX_ASSET_IO_ERROR;
    }
    for (;;) {
        uint8_t extra;
        ssize_t count = read(descriptor, &extra, 1u);
        if (count == 0) break;
        if (count < 0 && errno == EINTR) continue;
        saved_errno = count < 0 ? errno : EIO;
        free(contents);
        (void)close(descriptor);
        errno = saved_errno;
        return KILIX_ASSET_IO_ERROR;
    }
    if (fstat(descriptor, &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
        (void)close(descriptor);
        free(contents);
        return KILIX_ASSET_IO_ERROR;
    }
    if (close(descriptor) != 0) {
        free(contents);
        return KILIX_ASSET_IO_ERROR;
    }
    contents[length] = UINT8_C(0);
    *bytes = contents;
    *size = length;
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
        const uint8_t *source = bytes + source_row + 1u;
        uint8_t *output = bytes + output_row;
        const uint8_t *above = y != 0u ? output - row_bytes : NULL;
        uint8_t filter = bytes[source_row];
        size_t x;
        if (filter > 4u) return false;
        if (filter == 0u) {
            (void)memmove(output, source, row_bytes);
        } else if (filter == 1u) {
            for (x = 0u; x < row_bytes; ++x) {
                uint8_t left = x >= (size_t)bytes_per_pixel ?
                    output[x - (size_t)bytes_per_pixel] : 0u;
                output[x] = (uint8_t)(source[x] + left);
            }
        } else if (filter == 2u) {
            if (!above) {
                (void)memmove(output, source, row_bytes);
            } else {
                for (x = 0u; x < row_bytes; ++x)
                    output[x] = (uint8_t)(source[x] + above[x]);
            }
        } else if (filter == 3u) {
            for (x = 0u; x < row_bytes; ++x) {
                uint8_t left = x >= (size_t)bytes_per_pixel ?
                    output[x - (size_t)bytes_per_pixel] : 0u;
                uint8_t upper = above ? above[x] : 0u;
                output[x] = (uint8_t)(source[x] +
                    (uint8_t)(((unsigned int)left + upper) / 2u));
            }
        } else {
            for (x = 0u; x < row_bytes; ++x) {
                uint8_t left = x >= (size_t)bytes_per_pixel ?
                    output[x - (size_t)bytes_per_pixel] : 0u;
                uint8_t upper = above ? above[x] : 0u;
                uint8_t diagonal = above && x >= (size_t)bytes_per_pixel ?
                    above[x - (size_t)bytes_per_pixel] : 0u;
                output[x] = (uint8_t)(source[x] +
                                      paeth(left, upper, diagonal));
            }
        }
    }
    return true;
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] << 8 | (uint16_t)bytes[1]);
}

static bool png_chunk_type_is_valid(const uint8_t type[4])
{
    size_t index;
    for (index = 0u; index < 4u; ++index)
        if (!((type[index] >= (uint8_t)'A' && type[index] <= (uint8_t)'Z') ||
              (type[index] >= (uint8_t)'a' && type[index] <= (uint8_t)'z')))
            return false;
    return (type[2] & UINT8_C(0x20)) == 0u;
}

static kilix_asset_status png_inflate_chunk(
    z_stream *stream, const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size, size_t *produced, bool *ended)
{
    if (*ended) return input_size == 0u ? KILIX_ASSET_OK :
                                         KILIX_ASSET_CORRUPT;
    stream->next_in = (Bytef *)(uintptr_t)input;
    stream->avail_in = (uInt)input_size;
    while (stream->avail_in != 0u) {
        uint8_t overflow;
        size_t remaining = output_size - *produced;
        size_t capacity = remaining;
        uInt before_input = stream->avail_in;
        uInt before_output;
        int code;
        if (capacity > (size_t)UINT_MAX) capacity = (size_t)UINT_MAX;
        stream->next_out = capacity != 0u ? output + *produced : &overflow;
        stream->avail_out = capacity != 0u ? (uInt)capacity : 1u;
        before_output = stream->avail_out;
        code = inflate(stream, Z_NO_FLUSH);
        if (capacity == 0u) {
            if (stream->avail_out != before_output)
                return KILIX_ASSET_CORRUPT;
        } else {
            *produced += (size_t)(before_output - stream->avail_out);
        }
        if (code == Z_STREAM_END) {
            if (stream->avail_in != 0u) return KILIX_ASSET_CORRUPT;
            *ended = true;
            break;
        }
        if (code == Z_MEM_ERROR) return KILIX_ASSET_OUT_OF_MEMORY;
        if (code != Z_OK ||
            (stream->avail_in == before_input &&
             stream->avail_out == before_output))
            return KILIX_ASSET_CORRUPT;
    }
    return KILIX_ASSET_OK;
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
    z_stream inflater = {0};
    uint8_t palette[256u * 4u];
    uint8_t *file = NULL;
    uint8_t *scanlines = NULL;
    size_t file_size = 0u;
    size_t cursor = sizeof signature;
    size_t source_stride = 0u;
    size_t inflated_size = 0u;
    size_t inflated_bytes = 0u;
    size_t rgba_stride = 0u;
    size_t rgba_size = 0u;
    size_t palette_entries = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint16_t transparent_gray = 0u;
    uint16_t transparent_red = 0u;
    uint16_t transparent_green = 0u;
    uint16_t transparent_blue = 0u;
    unsigned int bit_depth = 0u;
    unsigned int color_type = 0u;
    unsigned int channels = 0u;
    unsigned int filter_bytes = 0u;
    bool header_seen = false;
    bool palette_seen = false;
    bool transparency_seen = false;
    bool idat_seen = false;
    bool idat_closed = false;
    bool inflater_ready = false;
    bool inflater_ended = false;
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
    (void)memset(palette, UINT8_MAX, sizeof palette);
    while (cursor < file_size) {
        uint32_t chunk_length;
        size_t chunk_size;
        const uint8_t *type;
        const uint8_t *chunk;
        uint32_t expected_crc;
        uLong actual_crc;
        bool is_idat;
        if (file_size - cursor < 12u) {
            result = KILIX_ASSET_CORRUPT;
            goto done;
        }
        chunk_length = read_be32(file + cursor);
        cursor += 4u;
        type = file + cursor;
        cursor += 4u;
        chunk_size = (size_t)chunk_length;
        if (chunk_size > file_size - cursor - 4u || chunk_size > UINT_MAX ||
            !png_chunk_type_is_valid(type)) {
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
        is_idat = memcmp(type, "IDAT", 4u) == 0;
        if (idat_seen && !is_idat) idat_closed = true;
        if (memcmp(type, "IHDR", 4u) == 0) {
            size_t row_bits;
            if (header_seen || cursor - chunk_size - 12u != sizeof signature ||
                chunk_size != 13u) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            width = read_be32(chunk);
            height = read_be32(chunk + 4u);
            bit_depth = chunk[8];
            color_type = chunk[9];
            if (width == 0u || height == 0u) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            if (width > bounds.max_dimension || height > bounds.max_dimension) {
                result = KILIX_ASSET_LIMIT_EXCEEDED;
                goto done;
            }
            if (chunk[10] != 0u || chunk[11] != 0u || chunk[12] != 0u) {
                result = KILIX_ASSET_UNSUPPORTED;
                goto done;
            }
            if (color_type == 0u && bit_depth == 8u) channels = 1u;
            else if (color_type == 2u && bit_depth == 8u) channels = 3u;
            else if (color_type == 3u &&
                     (bit_depth == 1u || bit_depth == 2u ||
                      bit_depth == 4u || bit_depth == 8u)) channels = 1u;
            else if (color_type == 4u && bit_depth == 8u) channels = 2u;
            else if (color_type == 6u && bit_depth == 8u) channels = 4u;
            else {
                result = KILIX_ASSET_UNSUPPORTED;
                goto done;
            }
            if ((size_t)width > (SIZE_MAX - 7u) /
                                    (color_type == 3u ? bit_depth :
                                                       channels * 8u)) {
                result = KILIX_ASSET_LIMIT_EXCEEDED;
                goto done;
            }
            row_bits = (size_t)width *
                       (color_type == 3u ? bit_depth : channels * 8u);
            source_stride = (row_bits + 7u) / 8u;
            filter_bytes = color_type == 3u ? 1u : channels;
            if (source_stride == SIZE_MAX ||
                (size_t)height > SIZE_MAX / (source_stride + 1u) ||
                !checked_image_size(width, height, &rgba_stride, &rgba_size) ||
                rgba_size > bounds.max_image_bytes) {
                result = KILIX_ASSET_LIMIT_EXCEEDED;
                goto done;
            }
            inflated_size = (source_stride + 1u) * (size_t)height;
            if (inflated_size > bounds.max_image_bytes) {
                result = KILIX_ASSET_LIMIT_EXCEEDED;
                goto done;
            }
            header_seen = true;
        } else if (memcmp(type, "PLTE", 4u) == 0) {
            size_t index;
            if (!header_seen || idat_seen || palette_seen ||
                transparency_seen ||
                color_type == 0u || color_type == 4u || chunk_size == 0u ||
                chunk_size > 256u * 3u || chunk_size % 3u != 0u) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            palette_entries = chunk_size / 3u;
            if (color_type == 3u &&
                palette_entries > ((size_t)1u << bit_depth)) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            for (index = 0u; index < palette_entries; ++index) {
                palette[index * 4u] = chunk[index * 3u];
                palette[index * 4u + 1u] = chunk[index * 3u + 1u];
                palette[index * 4u + 2u] = chunk[index * 3u + 2u];
                palette[index * 4u + 3u] = UINT8_MAX;
            }
            palette_seen = true;
        } else if (memcmp(type, "tRNS", 4u) == 0) {
            if (!header_seen || idat_seen || transparency_seen) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            if (color_type == 0u && chunk_size == 2u) {
                transparent_gray = read_be16(chunk);
                if (transparent_gray > UINT8_MAX) {
                    result = KILIX_ASSET_CORRUPT;
                    goto done;
                }
            } else if (color_type == 2u && chunk_size == 6u) {
                transparent_red = read_be16(chunk);
                transparent_green = read_be16(chunk + 2u);
                transparent_blue = read_be16(chunk + 4u);
                if (transparent_red > UINT8_MAX ||
                    transparent_green > UINT8_MAX ||
                    transparent_blue > UINT8_MAX) {
                    result = KILIX_ASSET_CORRUPT;
                    goto done;
                }
            } else if (color_type == 3u && palette_seen && chunk_size != 0u &&
                       chunk_size <= palette_entries) {
                size_t index;
                for (index = 0u; index < chunk_size; ++index)
                    palette[index * 4u + 3u] = chunk[index];
            } else {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            transparency_seen = true;
        } else if (is_idat) {
            if (!header_seen || idat_closed ||
                (color_type == 3u && !palette_seen)) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            if (!inflater_ready) {
                int code;
                scanlines = malloc(inflated_size);
                if (!scanlines) {
                    result = KILIX_ASSET_OUT_OF_MEMORY;
                    goto done;
                }
                code = inflateInit(&inflater);
                if (code != Z_OK) {
                    result = code == Z_MEM_ERROR ? KILIX_ASSET_OUT_OF_MEMORY :
                                                  KILIX_ASSET_CORRUPT;
                    goto done;
                }
                inflater_ready = true;
            }
            idat_seen = true;
            result = png_inflate_chunk(&inflater, chunk, chunk_size,
                                       scanlines, inflated_size,
                                       &inflated_bytes, &inflater_ended);
            if (result != KILIX_ASSET_OK) goto done;
        } else if (memcmp(type, "IEND", 4u) == 0) {
            if (!header_seen || !idat_seen || chunk_size != 0u ||
                !inflater_ended || inflated_bytes != inflated_size) {
                result = KILIX_ASSET_CORRUPT;
                goto done;
            }
            end_seen = true;
            break;
        } else if ((type[0] & UINT8_C(0x20)) == 0u) {
            result = KILIX_ASSET_UNSUPPORTED;
            goto done;
        }
    }
    if (!end_seen || cursor != file_size ||
        !unfilter(scanlines, source_stride, height, filter_bytes)) {
        result = KILIX_ASSET_CORRUPT;
        goto done;
    }
    if (color_type == 6u) {
        decoded.pixels = scanlines;
        scanlines = NULL;
    } else {
        uint32_t y;
        decoded.pixels = malloc(rgba_size);
        if (!decoded.pixels) {
            result = KILIX_ASSET_OUT_OF_MEMORY;
            goto done;
        }
        for (y = 0u; y < height; ++y) {
            const uint8_t *source = scanlines + (size_t)y * source_stride;
            uint8_t *destination = decoded.pixels + (size_t)y * rgba_stride;
            uint32_t x;
            if (color_type == 0u) {
                for (x = 0u; x < width; ++x) {
                    uint8_t value = source[x];
                    destination[x * 4u] = value;
                    destination[x * 4u + 1u] = value;
                    destination[x * 4u + 2u] = value;
                    destination[x * 4u + 3u] =
                        transparency_seen && value == transparent_gray ?
                            0u : UINT8_MAX;
                }
            } else if (color_type == 2u) {
                for (x = 0u; x < width; ++x) {
                    const uint8_t *pixel = source + (size_t)x * 3u;
                    uint8_t *target = destination + (size_t)x * 4u;
                    target[0] = pixel[0];
                    target[1] = pixel[1];
                    target[2] = pixel[2];
                    target[3] = transparency_seen &&
                        pixel[0] == transparent_red &&
                        pixel[1] == transparent_green &&
                        pixel[2] == transparent_blue ? 0u : UINT8_MAX;
                }
            } else if (color_type == 3u) {
                unsigned int mask = (1u << bit_depth) - 1u;
                for (x = 0u; x < width; ++x) {
                    size_t bit = (size_t)x * bit_depth;
                    unsigned int shift = 8u - bit_depth -
                                         (unsigned int)(bit % 8u);
                    unsigned int entry =
                        ((unsigned int)source[bit / 8u] >> shift) & mask;
                    if ((size_t)entry >= palette_entries) {
                        result = KILIX_ASSET_CORRUPT;
                        goto done;
                    }
                    (void)memcpy(destination + (size_t)x * 4u,
                                 palette + (size_t)entry * 4u, 4u);
                }
            } else {
                for (x = 0u; x < width; ++x) {
                    uint8_t value = source[(size_t)x * 2u];
                    uint8_t *target = destination + (size_t)x * 4u;
                    target[0] = value;
                    target[1] = value;
                    target[2] = value;
                    target[3] = source[(size_t)x * 2u + 1u];
                }
            }
        }
    }
    decoded.width = width;
    decoded.height = height;
    decoded.stride = rgba_stride;
    decoded.byte_count = rgba_size;
    kilix_asset_image_clear(image);
    *image = decoded;
    decoded = (kilix_asset_image){0};
    result = KILIX_ASSET_OK;
done:
    if (inflater_ready) (void)inflateEnd(&inflater);
    free(file);
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
           region->stride >= minimum_stride &&
           (size_t)region->height <= SIZE_MAX / region->stride;
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
    if (!atlas || !kilix_asset_image_is_valid(atlas->image) ||
        atlas->columns == 0u || atlas->rows == 0u ||
        atlas->cell_width == 0u || atlas->cell_height == 0u ||
        atlas->columns > UINT32_MAX / atlas->cell_width ||
        atlas->rows > UINT32_MAX / atlas->cell_height ||
        atlas->columns * atlas->cell_width != atlas->image->width ||
        atlas->rows * atlas->cell_height != atlas->image->height ||
        column >= atlas->columns || row >= atlas->rows)
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
    cache_state *state;
    size_t initial_capacity;
    if (!cache || max_entries == 0u || max_bytes == 0u ||
        max_entries > SIZE_MAX / sizeof *state->items)
        return false;
    state = calloc(1u, sizeof *state);
    if (!state) return false;
    initial_capacity = max_entries < 8u ? max_entries : 8u;
    state->items = calloc(initial_capacity, sizeof *state->items);
    state->bucket_count = 16u;
    state->buckets = calloc(state->bucket_count, sizeof *state->buckets);
    if (!state->items || !state->buckets) {
        free(state->items);
        free(state->buckets);
        free(state);
        return false;
    }
    state->item_capacity = initial_capacity;
    *cache = (kilix_asset_cache){0};
    cache->entries = state;
    cache->entry_capacity = initial_capacity;
    cache->max_entries = max_entries;
    cache->max_bytes = max_bytes;
    return true;
}

void kilix_asset_cache_clear(kilix_asset_cache *cache)
{
    cache_state *state;
    size_t index;
    if (!cache) return;
    state = cache->entries;
    for (index = 0u; index < cache->entry_count; ++index) {
        cache_entry *entry = state->items[index];
        free(entry->key);
        kilix_asset_image_clear(&entry->image);
        free(entry);
    }
    if (state) {
        free(state->items);
        free(state->buckets);
    }
    free(state);
    *cache = (kilix_asset_cache){0};
}

static uint64_t cache_key_hash(const char *path, uint32_t width,
                               uint32_t height, bool png)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *cursor = (const unsigned char *)path;
    unsigned int shift;
    while (*cursor != '\0') {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    hash ^= png ? UINT64_C(1) : UINT64_C(0);
    hash *= UINT64_C(1099511628211);
    for (shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (uint8_t)(width >> shift);
        hash *= UINT64_C(1099511628211);
        hash ^= (uint8_t)(height >> shift);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool cache_entry_matches(const cache_entry *entry, const char *path,
                                uint32_t width, uint32_t height, bool png,
                                uint64_t hash)
{
    return entry->hash == hash && entry->png == png &&
           (png || (entry->raw_width == width &&
                    entry->raw_height == height)) &&
           strcmp(entry->key, path) == 0;
}

static cache_entry *cache_find(const cache_state *state, const char *path,
                               uint32_t width, uint32_t height, bool png,
                               uint64_t hash)
{
    size_t bucket = (size_t)hash & (state->bucket_count - 1u);
    for (;;) {
        size_t stored = state->buckets[bucket];
        cache_entry *entry;
        if (stored == 0u) return NULL;
        entry = state->items[stored - 1u];
        if (cache_entry_matches(entry, path, width, height, png, hash))
            return entry;
        bucket = (bucket + 1u) & (state->bucket_count - 1u);
    }
}

static void cache_index_entry(cache_state *state, size_t index)
{
    size_t bucket = (size_t)state->items[index]->hash &
                    (state->bucket_count - 1u);
    while (state->buckets[bucket] != 0u)
        bucket = (bucket + 1u) & (state->bucket_count - 1u);
    state->buckets[bucket] = index + 1u;
}

static kilix_asset_status cache_rehash(cache_state *state,
                                       size_t entry_count,
                                       size_t bucket_count)
{
    size_t *buckets;
    size_t index;
    if (bucket_count < 16u ||
        (bucket_count & (bucket_count - 1u)) != 0u ||
        bucket_count > SIZE_MAX / sizeof *buckets)
        return KILIX_ASSET_LIMIT_EXCEEDED;
    buckets = calloc(bucket_count, sizeof *buckets);
    if (!buckets) return KILIX_ASSET_OUT_OF_MEMORY;
    free(state->buckets);
    state->buckets = buckets;
    state->bucket_count = bucket_count;
    for (index = 0u; index < entry_count; ++index)
        cache_index_entry(state, index);
    return KILIX_ASSET_OK;
}

static kilix_asset_status cache_reserve(kilix_asset_cache *cache,
                                        cache_state *state)
{
    if (cache->entry_count == state->item_capacity) {
        size_t capacity = state->item_capacity * 2u;
        cache_entry **items;
        if (capacity < state->item_capacity || capacity > cache->max_entries)
            capacity = cache->max_entries;
        if (capacity <= state->item_capacity ||
            capacity > SIZE_MAX / sizeof *items)
            return KILIX_ASSET_LIMIT_EXCEEDED;
        items = realloc(state->items, capacity * sizeof *items);
        if (!items) return KILIX_ASSET_OUT_OF_MEMORY;
        state->items = items;
        state->item_capacity = capacity;
        cache->entry_capacity = capacity;
    }
    if (cache->entry_count + 1u > state->bucket_count / 4u * 3u) {
        size_t buckets;
        if (state->bucket_count > SIZE_MAX / 2u)
            return KILIX_ASSET_LIMIT_EXCEEDED;
        buckets = state->bucket_count * 2u;
        return cache_rehash(state, cache->entry_count, buckets);
    }
    return KILIX_ASSET_OK;
}

static kilix_asset_status cache_load(kilix_asset_cache *cache,
                                     const char *path, uint32_t width,
                                     uint32_t height, bool png,
                                     const kilix_asset_limits *limits,
                                     const kilix_asset_image **image)
{
    cache_state *state;
    cache_entry *entry;
    kilix_asset_image loaded = {0};
    size_t key_size;
    uint64_t hash;
    kilix_asset_status result;
    if (!cache || !path || !image || cache->max_entries == 0u ||
        cache->max_bytes == 0u || !cache->entries)
        return KILIX_ASSET_INVALID_ARGUMENT;
    *image = NULL;
    state = cache->entries;
    if (state->item_capacity != cache->entry_capacity ||
        cache->entry_count > state->item_capacity ||
        cache->entry_count > cache->max_entries ||
        state->bucket_count < 16u ||
        (state->bucket_count & (state->bucket_count - 1u)) != 0u)
        return KILIX_ASSET_INVALID_ARGUMENT;
    hash = cache_key_hash(path, width, height, png);
    entry = cache_find(state, path, width, height, png, hash);
    if (entry) {
        *image = &entry->image;
        return KILIX_ASSET_OK;
    }
    if (cache->entry_count >= cache->max_entries)
        return KILIX_ASSET_LIMIT_EXCEEDED;
    result = cache_reserve(cache, state);
    if (result != KILIX_ASSET_OK) return result;
    result = png ? kilix_asset_image_load_png(&loaded, path, limits) :
                   kilix_asset_image_load_rgba(&loaded, path, width,
                                               height, limits);
    if (result != KILIX_ASSET_OK) return result;
    if (cache->byte_count > cache->max_bytes ||
        loaded.byte_count > cache->max_bytes - cache->byte_count) {
        kilix_asset_image_clear(&loaded);
        return KILIX_ASSET_LIMIT_EXCEEDED;
    }
    key_size = strlen(path);
    if (key_size == SIZE_MAX) {
        kilix_asset_image_clear(&loaded);
        return KILIX_ASSET_LIMIT_EXCEEDED;
    }
    ++key_size;
    entry = calloc(1u, sizeof *entry);
    if (entry) entry->key = malloc(key_size);
    if (!entry || !entry->key) {
        if (entry) free(entry->key);
        free(entry);
        kilix_asset_image_clear(&loaded);
        return KILIX_ASSET_OUT_OF_MEMORY;
    }
    (void)memcpy(entry->key, path, key_size);
    entry->image = loaded;
    entry->png = png;
    entry->raw_width = width;
    entry->raw_height = height;
    entry->hash = hash;
    state->items[cache->entry_count] = entry;
    cache_index_entry(state, cache->entry_count);
    cache->byte_count += loaded.byte_count;
    *image = &entry->image;
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
    while (reader->cursor < reader->size) {
        char byte = reader->bytes[reader->cursor];
        if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n')
            break;
        ++reader->cursor;
    }
}

static bool json_take(json_reader *reader, char byte)
{
    json_space(reader);
    if (reader->cursor >= reader->size ||
        reader->bytes[reader->cursor] != byte) return false;
    ++reader->cursor;
    return true;
}

static bool json_hex_quad(const char *bytes, size_t size, size_t cursor,
                          uint16_t *value)
{
    uint16_t result = 0u;
    size_t index;
    if (!value || cursor > size || size - cursor < 4u) return false;
    for (index = 0u; index < 4u; ++index) {
        unsigned char byte = (unsigned char)bytes[cursor + index];
        result = (uint16_t)(result << 4);
        if (byte >= (unsigned char)'0' && byte <= (unsigned char)'9')
            result = (uint16_t)(result | (uint16_t)(byte - (unsigned char)'0'));
        else if (byte >= (unsigned char)'a' && byte <= (unsigned char)'f')
            result = (uint16_t)(result |
                (uint16_t)(byte - (unsigned char)'a' + 10u));
        else if (byte >= (unsigned char)'A' && byte <= (unsigned char)'F')
            result = (uint16_t)(result |
                (uint16_t)(byte - (unsigned char)'A' + 10u));
        else return false;
    }
    *value = result;
    return true;
}

static size_t json_raw_utf8_width(const char *bytes, size_t size,
                                  size_t cursor)
{
    const unsigned char *data = (const unsigned char *)bytes;
    unsigned char first;
    if (cursor >= size) return 0u;
    first = data[cursor];
    if (first < UINT8_C(0x80)) return 1u;
    if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
        if (size - cursor < 2u || data[cursor + 1u] < UINT8_C(0x80) ||
            data[cursor + 1u] > UINT8_C(0xbf)) return 0u;
        return 2u;
    }
    if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
        unsigned char second;
        if (size - cursor < 3u) return 0u;
        second = data[cursor + 1u];
        if (data[cursor + 2u] < UINT8_C(0x80) ||
            data[cursor + 2u] > UINT8_C(0xbf) ||
            (first == UINT8_C(0xe0) && second < UINT8_C(0xa0)) ||
            (first == UINT8_C(0xed) && second > UINT8_C(0x9f)) ||
            second < UINT8_C(0x80) || second > UINT8_C(0xbf)) return 0u;
        return 3u;
    }
    if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
        unsigned char second;
        if (size - cursor < 4u) return 0u;
        second = data[cursor + 1u];
        if (data[cursor + 2u] < UINT8_C(0x80) ||
            data[cursor + 2u] > UINT8_C(0xbf) ||
            data[cursor + 3u] < UINT8_C(0x80) ||
            data[cursor + 3u] > UINT8_C(0xbf) ||
            (first == UINT8_C(0xf0) && second < UINT8_C(0x90)) ||
            (first == UINT8_C(0xf4) && second > UINT8_C(0x8f)) ||
            second < UINT8_C(0x80) || second > UINT8_C(0xbf)) return 0u;
        return 4u;
    }
    return 0u;
}

static size_t json_encode_utf8(uint32_t code, char output[4])
{
    if (code == 0u || code > UINT32_C(0x10ffff) ||
        (code >= UINT32_C(0xd800) && code <= UINT32_C(0xdfff))) return 0u;
    if (code <= UINT32_C(0x7f)) {
        if (output) output[0] = (char)code;
        return 1u;
    }
    if (code <= UINT32_C(0x7ff)) {
        if (output) {
            output[0] = (char)(UINT32_C(0xc0) | code >> 6);
            output[1] = (char)(UINT32_C(0x80) | (code & UINT32_C(0x3f)));
        }
        return 2u;
    }
    if (code <= UINT32_C(0xffff)) {
        if (output) {
            output[0] = (char)(UINT32_C(0xe0) | code >> 12);
            output[1] = (char)(UINT32_C(0x80) |
                               (code >> 6 & UINT32_C(0x3f)));
            output[2] = (char)(UINT32_C(0x80) | (code & UINT32_C(0x3f)));
        }
        return 3u;
    }
    if (output) {
        output[0] = (char)(UINT32_C(0xf0) | code >> 18);
        output[1] = (char)(UINT32_C(0x80) |
                           (code >> 12 & UINT32_C(0x3f)));
        output[2] = (char)(UINT32_C(0x80) |
                           (code >> 6 & UINT32_C(0x3f)));
        output[3] = (char)(UINT32_C(0x80) | (code & UINT32_C(0x3f)));
    }
    return 4u;
}

static bool json_string_pass(const json_reader *reader, size_t start,
                             char *output, size_t *output_length,
                             size_t *end)
{
    size_t cursor = start;
    size_t length = 0u;
    while (cursor < reader->size) {
        unsigned char byte = (unsigned char)reader->bytes[cursor];
        if (byte == (unsigned char)'"') {
            if (output_length) *output_length = length;
            if (end) *end = cursor + 1u;
            return true;
        }
        if (byte < 32u) return false;
        if (byte == (unsigned char)'\\') {
            unsigned char escape;
            uint32_t code;
            char encoded[4];
            size_t width;
            ++cursor;
            if (cursor >= reader->size) return false;
            escape = (unsigned char)reader->bytes[cursor++];
            if (escape == (unsigned char)'"' ||
                escape == (unsigned char)'\\' ||
                escape == (unsigned char)'/') code = escape;
            else if (escape == (unsigned char)'b') code = (unsigned char)'\b';
            else if (escape == (unsigned char)'f') code = (unsigned char)'\f';
            else if (escape == (unsigned char)'n') code = (unsigned char)'\n';
            else if (escape == (unsigned char)'r') code = (unsigned char)'\r';
            else if (escape == (unsigned char)'t') code = (unsigned char)'\t';
            else if (escape == (unsigned char)'u') {
                uint16_t first;
                if (!json_hex_quad(reader->bytes, reader->size, cursor,
                                   &first)) return false;
                cursor += 4u;
                code = first;
                if (first >= UINT16_C(0xd800) &&
                    first <= UINT16_C(0xdbff)) {
                    uint16_t second;
                    if (reader->size - cursor < 6u ||
                        reader->bytes[cursor] != '\\' ||
                        reader->bytes[cursor + 1u] != 'u' ||
                        !json_hex_quad(reader->bytes, reader->size,
                                       cursor + 2u, &second) ||
                        second < UINT16_C(0xdc00) ||
                        second > UINT16_C(0xdfff)) return false;
                    cursor += 6u;
                    code = UINT32_C(0x10000) +
                        ((uint32_t)(first - UINT16_C(0xd800)) << 10) +
                        (uint32_t)(second - UINT16_C(0xdc00));
                } else if (first >= UINT16_C(0xdc00) &&
                           first <= UINT16_C(0xdfff)) return false;
            } else return false;
            width = json_encode_utf8(code, encoded);
            if (width == 0u || length > SIZE_MAX - width) return false;
            if (output) (void)memcpy(output + length, encoded, width);
            length += width;
        } else if (byte < UINT8_C(0x80)) {
            if (output) output[length] = (char)byte;
            ++length;
            ++cursor;
        } else {
            size_t width = json_raw_utf8_width(reader->bytes, reader->size,
                                               cursor);
            if (width == 0u || length > SIZE_MAX - width) return false;
            if (output)
                (void)memcpy(output + length, reader->bytes + cursor, width);
            length += width;
            cursor += width;
        }
    }
    return false;
}

static bool json_string(json_reader *reader, char **output)
{
    char *value;
    size_t start;
    size_t length;
    size_t end;
    size_t written;
    size_t confirmed_end;
    if (!output) return false;
    *output = NULL;
    json_space(reader);
    if (reader->cursor >= reader->size ||
        reader->bytes[reader->cursor] != '"') return false;
    start = reader->cursor + 1u;
    if (!json_string_pass(reader, start, NULL, &length, &end) ||
        length == SIZE_MAX) return false;
    value = malloc(length + 1u);
    if (!value) {
        reader->failure = KILIX_ASSET_OUT_OF_MEMORY;
        return false;
    }
    if (!json_string_pass(reader, start, value, &written, &confirmed_end) ||
        written != length || confirmed_end != end) {
        free(value);
        return false;
    }
    value[length] = '\0';
    reader->cursor = end;
    *output = value;
    return true;
}

static bool json_skip_string(json_reader *reader)
{
    size_t end;
    json_space(reader);
    if (reader->cursor >= reader->size ||
        reader->bytes[reader->cursor] != '"' ||
        !json_string_pass(reader, reader->cursor + 1u, NULL, NULL, &end))
        return false;
    reader->cursor = end;
    return true;
}

static bool json_is_digit(unsigned char byte)
{
    return byte >= (unsigned char)'0' && byte <= (unsigned char)'9';
}

static bool json_uint(json_reader *reader, uint32_t *output)
{
    uint64_t value = 0u;
    bool found = false;
    json_space(reader);
    if (reader->cursor < reader->size &&
        reader->bytes[reader->cursor] == '0' &&
        reader->cursor + 1u < reader->size &&
        json_is_digit((unsigned char)reader->bytes[reader->cursor + 1u]))
        return false;
    while (reader->cursor < reader->size) {
        unsigned char byte = (unsigned char)reader->bytes[reader->cursor];
        if (!json_is_digit(byte)) break;
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
            json_is_digit((unsigned char)reader->bytes[cursor])) return false;
    } else {
        if (reader->bytes[cursor] < '1' || reader->bytes[cursor] > '9')
            return false;
        do { ++cursor; }
        while (cursor < reader->size &&
               json_is_digit((unsigned char)reader->bytes[cursor]));
    }
    if (cursor < reader->size && reader->bytes[cursor] == '.') {
        ++cursor;
        if (cursor >= reader->size ||
            !json_is_digit((unsigned char)reader->bytes[cursor])) return false;
        do { ++cursor; }
        while (cursor < reader->size &&
               json_is_digit((unsigned char)reader->bytes[cursor]));
    }
    if (cursor < reader->size &&
        (reader->bytes[cursor] == 'e' || reader->bytes[cursor] == 'E')) {
        ++cursor;
        if (cursor < reader->size &&
            (reader->bytes[cursor] == '+' || reader->bytes[cursor] == '-'))
            ++cursor;
        if (cursor >= reader->size ||
            !json_is_digit((unsigned char)reader->bytes[cursor])) return false;
        do { ++cursor; }
        while (cursor < reader->size &&
               json_is_digit((unsigned char)reader->bytes[cursor]));
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
            if (!json_skip_string(reader)) {
                --reader->depth;
                return false;
            }
            if (!json_take(reader, ':')) { --reader->depth; return false; }
        }
        if (!json_skip_value(reader)) { --reader->depth; return false; }
        if (json_take(reader, close)) { --reader->depth; return true; }
        if (!json_take(reader, ',')) { --reader->depth; return false; }
    }
}

static bool json_skip_value(json_reader *reader)
{
    json_space(reader);
    if (reader->cursor >= reader->size) return false;
    if (reader->bytes[reader->cursor] == '"') return json_skip_string(reader);
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
        if (strcmp(key, "columns") == 0) {
            result = !columns && json_uint(reader, &atlas->columns);
            if (result) columns = true;
        } else if (strcmp(key, "rows") == 0) {
            result = !rows && json_uint(reader, &atlas->rows);
            if (result) rows = true;
        } else if (strcmp(key, "width") == 0) {
            result = !width && json_uint(reader, &atlas->width);
            if (result) width = true;
        } else if (strcmp(key, "height") == 0) {
            result = !height && json_uint(reader, &atlas->height);
            if (result) height = true;
        } else if (strcmp(key, "cell_width") == 0) {
            result = !cell_width && json_uint(reader, &atlas->cell_width);
            if (result) cell_width = true;
        } else if (strcmp(key, "cell_height") == 0) {
            result = !cell_height && json_uint(reader, &atlas->cell_height);
            if (result) cell_height = true;
        } else result = json_skip_value(reader);
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
    bool id = false, path = false, alpha = false, grid = false;
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
            result = !id && json_string(reader, &atlas->id);
            if (result) id = true;
        } else if (strcmp(key, "path") == 0) {
            result = !path && json_string(reader, &atlas->path);
            if (result) path = true;
        } else if (strcmp(key, "alpha_required") == 0) {
            result = !alpha && json_bool(reader, &atlas->alpha_required);
            if (result) alpha = true;
        } else if (strcmp(key, "grid") == 0) {
            result = !grid && parse_grid(reader, atlas);
            if (result) grid = true;
        } else result = json_skip_value(reader);
        free(key);
        if (!result) return false;
        if (json_take(reader, '}')) break;
        if (!json_take(reader, ',')) return false;
    }
    return id && manifest_text_is_safe(atlas->id) && path && atlas->path &&
           kilix_asset_path_is_safe(atlas->path) && grid;
}

static bool parse_bitmap(json_reader *reader,
                         kilix_asset_manifest_bitmap *bitmap)
{
    bool id = false, path = false, width = false, height = false;
    bool grid = false, png_path = false, grid_path = false;
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
            result = !id && json_string(reader, &bitmap->id);
            if (result) id = true;
        } else if (strcmp(key, "png") == 0) {
            result = !path && json_string(reader, &bitmap->path);
            if (result) {
                path = true;
                png_path = true;
            }
        } else if (strcmp(key, "path") == 0) {
            result = !path && json_string(reader, &bitmap->path);
            if (result) {
                path = true;
                grid_path = true;
            }
        } else if (strcmp(key, "width") == 0) {
            result = !width && json_uint(reader, &bitmap->width);
            if (result) width = true;
        } else if (strcmp(key, "height") == 0) {
            result = !height && json_uint(reader, &bitmap->height);
            if (result) height = true;
        } else if (strcmp(key, "grid") == 0) {
            kilix_asset_manifest_atlas dimensions = {0};
            result = !grid && !width && !height &&
                     parse_grid(reader, &dimensions);
            if (result) {
                bitmap->width = dimensions.width;
                bitmap->height = dimensions.height;
                width = true;
                height = true;
                grid = true;
            }
        } else result = json_skip_value(reader);
        free(key);
        if (!result) return false;
        if (json_take(reader, '}')) break;
        if (!json_take(reader, ',')) return false;
    }
    return id && manifest_text_is_safe(bitmap->id) && path && bitmap->path &&
           kilix_asset_path_is_safe(bitmap->path) && width && height &&
           bitmap->width != 0u && bitmap->height != 0u &&
           ((png_path && !grid) || (grid_path && grid));
}

static kilix_asset_status append_atlas(kilix_asset_manifest *manifest,
                                       size_t *capacity,
                                       kilix_asset_manifest_atlas *atlas)
{
    kilix_asset_manifest_atlas *items;
    if (manifest->atlas_count == *capacity) {
        size_t next = *capacity == 0u ? 8u : *capacity * 2u;
        if (next < *capacity || next > SIZE_MAX / sizeof *items)
            return KILIX_ASSET_LIMIT_EXCEEDED;
        items = realloc(manifest->atlases, next * sizeof *items);
        if (!items) return KILIX_ASSET_OUT_OF_MEMORY;
        manifest->atlases = items;
        *capacity = next;
    } else items = manifest->atlases;
    items[manifest->atlas_count++] = *atlas;
    *atlas = (kilix_asset_manifest_atlas){0};
    return KILIX_ASSET_OK;
}

static kilix_asset_status append_bitmap(kilix_asset_manifest *manifest,
                                        size_t *capacity,
                                        kilix_asset_manifest_bitmap *bitmap)
{
    kilix_asset_manifest_bitmap *items;
    if (manifest->bitmap_count == *capacity) {
        size_t next = *capacity == 0u ? 8u : *capacity * 2u;
        if (next < *capacity || next > SIZE_MAX / sizeof *items)
            return KILIX_ASSET_LIMIT_EXCEEDED;
        items = realloc(manifest->bitmaps, next * sizeof *items);
        if (!items) return KILIX_ASSET_OUT_OF_MEMORY;
        manifest->bitmaps = items;
        *capacity = next;
    } else items = manifest->bitmaps;
    items[manifest->bitmap_count++] = *bitmap;
    *bitmap = (kilix_asset_manifest_bitmap){0};
    return KILIX_ASSET_OK;
}

static bool parse_atlas_array(json_reader *reader,
                              kilix_asset_manifest *manifest,
                              size_t *capacity)
{
    if (!json_take(reader, '[')) return false;
    if (json_take(reader, ']')) return true;
    for (;;) {
        kilix_asset_manifest_atlas atlas = {0};
        kilix_asset_status status;
        if (!parse_atlas(reader, &atlas)) {
            free_atlas(&atlas);
            return false;
        }
        status = append_atlas(manifest, capacity, &atlas);
        if (status != KILIX_ASSET_OK) {
            reader->failure = status;
            free_atlas(&atlas);
            return false;
        }
        if (json_take(reader, ']')) return true;
        if (!json_take(reader, ',')) return false;
    }
}

static bool parse_bitmap_array(json_reader *reader,
                               kilix_asset_manifest *manifest,
                               size_t *capacity)
{
    if (!json_take(reader, '[')) return false;
    if (json_take(reader, ']')) return true;
    for (;;) {
        kilix_asset_manifest_bitmap bitmap = {0};
        kilix_asset_status status;
        if (!parse_bitmap(reader, &bitmap)) {
            free_bitmap(&bitmap);
            return false;
        }
        status = append_bitmap(manifest, capacity, &bitmap);
        if (status != KILIX_ASSET_OK) {
            reader->failure = status;
            free_bitmap(&bitmap);
            return false;
        }
        if (json_take(reader, ']')) return true;
        if (!json_take(reader, ',')) return false;
    }
}

static int compare_text_pointers(const void *left, const void *right)
{
    const char *const *left_text = left;
    const char *const *right_text = right;
    return strcmp(*left_text, *right_text);
}

static kilix_asset_status manifest_ids_are_unique(
    const kilix_asset_manifest *manifest)
{
    size_t maximum = manifest->atlas_count > manifest->bitmap_count ?
        manifest->atlas_count : manifest->bitmap_count;
    const char **ids;
    size_t count;
    size_t index;
    if (maximum < 2u) return KILIX_ASSET_OK;
    if (maximum > SIZE_MAX / sizeof *ids)
        return KILIX_ASSET_LIMIT_EXCEEDED;
    ids = malloc(maximum * sizeof *ids);
    if (!ids) return KILIX_ASSET_OUT_OF_MEMORY;
    for (count = 0u; count < 2u; ++count) {
        size_t item_count = count == 0u ? manifest->atlas_count :
                                         manifest->bitmap_count;
        for (index = 0u; index < item_count; ++index)
            ids[index] = count == 0u ? manifest->atlases[index].id :
                                      manifest->bitmaps[index].id;
        qsort(ids, item_count, sizeof *ids, compare_text_pointers);
        for (index = 1u; index < item_count; ++index) {
            if (strcmp(ids[index - 1u], ids[index]) == 0) {
                free(ids);
                return KILIX_ASSET_CORRUPT;
            }
        }
    }
    free(ids);
    return KILIX_ASSET_OK;
}

kilix_asset_status kilix_asset_manifest_load_json(
    kilix_asset_manifest *manifest, const char *path, size_t max_file_bytes)
{
    kilix_asset_manifest parsed = {0};
    uint8_t *file = NULL;
    size_t size = 0u;
    size_t atlas_capacity = 0u;
    size_t bitmap_capacity = 0u;
    json_reader reader;
    bool schema = false, game = false, atlases = false, bitmaps = false;
    kilix_asset_status status;
    if (!manifest || !path || max_file_bytes == 0u)
        return KILIX_ASSET_INVALID_ARGUMENT;
    status = read_file(path, max_file_bytes, &file, &size);
    if (status != KILIX_ASSET_OK) return status;
    reader = (json_reader){(const char *)file, size, 0u, 0u,
                           KILIX_ASSET_OK};
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
            result = !atlases && parse_atlas_array(
                &reader, &parsed, &atlas_capacity);
            atlases = result;
        } else if (strcmp(key, "bitmaps") == 0) {
            result = !bitmaps && parse_bitmap_array(
                &reader, &parsed, &bitmap_capacity);
            bitmaps = result;
        } else result = json_skip_value(&reader);
        free(key);
        if (!result) goto corrupt;
        if (json_take(&reader, '}')) break;
        if (!json_take(&reader, ',')) goto corrupt;
    }
    json_space(&reader);
    if (reader.cursor != reader.size || !schema || parsed.schema_version != 1u ||
        !game || !manifest_text_is_safe(parsed.game) || !atlases ||
        !bitmaps) goto corrupt;
    status = manifest_ids_are_unique(&parsed);
    if (status != KILIX_ASSET_OK) {
        reader.failure = status;
        goto corrupt;
    }
    kilix_asset_manifest_clear(manifest);
    *manifest = parsed;
    free(file);
    return KILIX_ASSET_OK;
corrupt:
    kilix_asset_manifest_clear(&parsed);
    free(file);
    return reader.failure == KILIX_ASSET_OK ? KILIX_ASSET_CORRUPT :
                                              reader.failure;
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
