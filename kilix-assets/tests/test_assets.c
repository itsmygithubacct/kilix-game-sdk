#include "kilix_assets.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zlib.h>

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                     \
        (void)fprintf(stderr, "%s:%d: check failed: %s\n",               \
                      __FILE__, __LINE__, #condition);                      \
        return false;                                                       \
    }                                                                       \
} while (false)

static bool write_all(FILE *stream, const void *bytes, size_t size)
{
    return size == 0u || fwrite(bytes, 1u, size, stream) == size;
}

static void be32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static bool write_chunk(FILE *stream, const char type[4],
                        const uint8_t *payload, size_t size)
{
    uint8_t encoded[4];
    uLong crc = crc32(0L, Z_NULL, 0);
    if (size > UINT32_MAX) return false;
    be32(encoded, (uint32_t)size);
    crc = crc32(crc, (const Bytef *)type, 4u);
    if (size != 0u) crc = crc32(crc, payload, (uInt)size);
    if (!write_all(stream, encoded, 4u) || !write_all(stream, type, 4u) ||
        !write_all(stream, payload, size)) return false;
    be32(encoded, (uint32_t)crc);
    return write_all(stream, encoded, 4u);
}

static bool write_test_png(const char *path)
{
    static const uint8_t signature[8] = {
        137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
    };
    static const uint8_t raw[] = {
        0u, 255u, 0u, 0u, 255u, 0u, 255u, 0u, 128u,
        1u, 1u, 2u, 3u, 4u, 4u, 5u, 6u, 251u
    };
    uint8_t header[13] = {0};
    uLongf compressed_size = compressBound(sizeof raw);
    uint8_t *compressed = malloc((size_t)compressed_size);
    FILE *stream;
    bool result;
    if (!compressed || compress2(compressed, &compressed_size, raw,
                                 sizeof raw, Z_BEST_COMPRESSION) != Z_OK) {
        free(compressed);
        return false;
    }
    be32(header, 2u);
    be32(header + 4u, 2u);
    header[8] = 8u;
    header[9] = 6u;
    stream = fopen(path, "wb");
    if (!stream) { free(compressed); return false; }
    result = write_all(stream, signature, sizeof signature) &&
             write_chunk(stream, "IHDR", header, sizeof header) &&
             write_chunk(stream, "IDAT", compressed,
                         (size_t)compressed_size) &&
             write_chunk(stream, "IEND", NULL, 0u);
    if (fclose(stream) != 0) result = false;
    free(compressed);
    return result;
}

enum png_fixture_flags {
    PNG_FIXTURE_EMPTY_IDAT = 1u << 0,
    PNG_FIXTURE_SPLIT_IDAT = 1u << 1,
    PNG_FIXTURE_INTERLEAVE_IDAT = 1u << 2,
    PNG_FIXTURE_TRAILING_ZLIB = 1u << 3,
    PNG_FIXTURE_BEFORE_IHDR = 1u << 4
};

static uint8_t fixture_paeth(uint8_t left, uint8_t above,
                             uint8_t upper_left)
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

static bool write_png_fixture(
    const char *path, uint32_t width, uint32_t height,
    unsigned int bit_depth, unsigned int color_type,
    const uint8_t *pixels, const uint8_t *filters,
    const uint8_t *palette, size_t palette_size,
    const uint8_t *transparency, size_t transparency_size,
    unsigned int flags)
{
    static const uint8_t signature[8] = {
        137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
    };
    uint8_t header[13] = {0};
    uint8_t *raw = NULL;
    uint8_t *compressed = NULL;
    size_t channels;
    size_t bits_per_pixel;
    size_t row_bytes;
    size_t raw_size;
    uLongf compressed_size;
    uint32_t y;
    FILE *stream = NULL;
    bool result = false;

    if (!path || !pixels || width == 0u || height == 0u) return false;
    if (color_type == 0u || color_type == 3u) channels = 1u;
    else if (color_type == 2u) channels = 3u;
    else if (color_type == 4u) channels = 2u;
    else if (color_type == 6u) channels = 4u;
    else return false;
    bits_per_pixel = color_type == 3u ? bit_depth : channels * bit_depth;
    if (bits_per_pixel == 0u ||
        (size_t)width > (SIZE_MAX - 7u) / bits_per_pixel)
        return false;
    row_bytes = ((size_t)width * bits_per_pixel + 7u) / 8u;
    if (row_bytes == SIZE_MAX ||
        (size_t)height > SIZE_MAX / (row_bytes + 1u)) return false;
    raw_size = (row_bytes + 1u) * (size_t)height;
    raw = malloc(raw_size);
    compressed_size = compressBound((uLong)raw_size);
    if (compressed_size == (uLongf)ULONG_MAX) goto finished;
    compressed = malloc((size_t)compressed_size + 1u);
    if (!raw || !compressed) goto finished;
    for (y = 0u; y < height; ++y) {
        unsigned int filter = filters ? filters[y] : 0u;
        size_t bytes_per_pixel = (bits_per_pixel + 7u) / 8u;
        size_t x;
        raw[(size_t)y * (row_bytes + 1u)] = (uint8_t)filter;
        for (x = 0u; x < row_bytes; ++x) {
            size_t source = (size_t)y * row_bytes + x;
            uint8_t left = x >= bytes_per_pixel ?
                pixels[source - bytes_per_pixel] : 0u;
            uint8_t above = y != 0u ? pixels[source - row_bytes] : 0u;
            uint8_t diagonal = y != 0u && x >= bytes_per_pixel ?
                pixels[source - row_bytes - bytes_per_pixel] : 0u;
            uint8_t predictor;
            if (filter == 0u) predictor = 0u;
            else if (filter == 1u) predictor = left;
            else if (filter == 2u) predictor = above;
            else if (filter == 3u)
                predictor = (uint8_t)(((unsigned int)left + above) / 2u);
            else if (filter == 4u)
                predictor = fixture_paeth(left, above, diagonal);
            else goto finished;
            raw[(size_t)y * (row_bytes + 1u) + 1u + x] =
                (uint8_t)(pixels[source] - predictor);
        }
    }
    if (compress2(compressed, &compressed_size, raw, (uLong)raw_size,
                  Z_BEST_SPEED) != Z_OK) goto finished;
    if ((flags & PNG_FIXTURE_TRAILING_ZLIB) != 0u)
        compressed[compressed_size++] = 0u;
    be32(header, width);
    be32(header + 4u, height);
    header[8] = (uint8_t)bit_depth;
    header[9] = (uint8_t)color_type;
    stream = fopen(path, "wb");
    if (!stream || !write_all(stream, signature, sizeof signature))
        goto finished;
    if ((flags & PNG_FIXTURE_BEFORE_IHDR) != 0u &&
        !write_chunk(stream, "tEXt", (const uint8_t *)"x", 1u))
        goto finished;
    if (!write_chunk(stream, "IHDR", header, sizeof header) ||
        (palette_size != 0u &&
         !write_chunk(stream, "PLTE", palette, palette_size)) ||
        (transparency_size != 0u &&
         !write_chunk(stream, "tRNS", transparency, transparency_size)) ||
        ((flags & PNG_FIXTURE_EMPTY_IDAT) != 0u &&
         !write_chunk(stream, "IDAT", NULL, 0u)))
        goto finished;
    if ((flags & (PNG_FIXTURE_SPLIT_IDAT |
                  PNG_FIXTURE_INTERLEAVE_IDAT)) != 0u) {
        size_t first = (size_t)compressed_size / 2u;
        if (!write_chunk(stream, "IDAT", compressed, first) ||
            ((flags & PNG_FIXTURE_INTERLEAVE_IDAT) != 0u &&
             !write_chunk(stream, "tEXt", (const uint8_t *)"x", 1u)) ||
            !write_chunk(stream, "IDAT", compressed + first,
                         (size_t)compressed_size - first))
            goto finished;
    } else if (!write_chunk(stream, "IDAT", compressed,
                            (size_t)compressed_size)) goto finished;
    if (!write_chunk(stream, "IEND", NULL, 0u)) goto finished;
    result = true;
finished:
    if (stream && fclose(stream) != 0) result = false;
    free(compressed);
    free(raw);
    return result;
}

static bool write_bytes(const char *path, const void *bytes, size_t size)
{
    FILE *stream = fopen(path, "wb");
    bool result;
    if (!stream) return false;
    result = write_all(stream, bytes, size);
    if (fclose(stream) != 0) result = false;
    return result;
}

static bool test_paths(const char *directory, const char *raw_path)
{
    kilix_asset_locator locator;
    char resolved[1024];
    CHECK(kilix_asset_path_is_safe("assets/graphics/a.png"));
    CHECK(!kilix_asset_path_is_safe("../secret"));
    CHECK(!kilix_asset_path_is_safe("./secret"));
    CHECK(!kilix_asset_path_is_safe("a//b"));
    CHECK(!kilix_asset_path_is_safe("a/"));
    CHECK(!kilix_asset_path_is_safe("a\\b"));
    CHECK(!kilix_asset_path_is_safe("a\nb"));
    CHECK(!kilix_asset_path_is_safe("a\177b"));
    CHECK(!kilix_asset_path_is_safe("/absolute"));
    kilix_asset_locator_init(&locator);
    locator.environment_variable = "KILIX_ASSET_TEST_ROOT";
    locator.source_root = "/does/not/exist";
    CHECK(setenv("KILIX_ASSET_TEST_ROOT", directory, 1) == 0);
    CHECK(kilix_asset_resolve(&locator, "pixels.rgba", resolved,
                              sizeof resolved) == KILIX_ASSET_OK);
    CHECK(strcmp(resolved, raw_path) == 0);
    CHECK(kilix_asset_resolve(&locator, "missing.rgba", resolved,
                              sizeof resolved) == KILIX_ASSET_NOT_FOUND);
    CHECK(unsetenv("KILIX_ASSET_TEST_ROOT") == 0);
    locator.source_root = directory;
    CHECK(kilix_asset_resolve(&locator, "pixels.rgba", resolved, 1u) ==
          KILIX_ASSET_LIMIT_EXCEEDED);
    CHECK(resolved[0] == '\0');
    return true;
}

static bool test_images(const char *png_path, const char *raw_path)
{
    kilix_asset_image png = {0};
    kilix_asset_image raw = {0};
    kilix_asset_cache cache;
    kilix_asset_atlas atlas;
    kilix_asset_region cell;
    kilix_asset_clip clip = {4u, 3u, 2u, true};
    const kilix_asset_image *first = NULL;
    const kilix_asset_image *second = NULL;
    CHECK(kilix_asset_image_load_png(&png, png_path, NULL) == KILIX_ASSET_OK);
    CHECK(png.width == 2u && png.height == 2u && png.stride == 8u);
    CHECK(png.pixels[0] == 255u && png.pixels[3] == 255u);
    CHECK(png.pixels[4] == 0u && png.pixels[5] == 255u &&
          png.pixels[7] == 128u);
    CHECK(png.pixels[8] == 1u && png.pixels[12] == 5u);
    CHECK(kilix_asset_image_load_rgba(&raw, raw_path, 2u, 2u, NULL) ==
          KILIX_ASSET_OK);
    CHECK(kilix_asset_atlas_init_grid(&atlas, &raw, 2u, 2u));
    cell = kilix_asset_atlas_cell(&atlas, 1u, 1u);
    CHECK(kilix_asset_region_is_valid(&cell));
    CHECK(cell.width == 1u && cell.height == 1u && cell.pixels[0] == 13u);
    CHECK(kilix_asset_clip_frame(&clip, 0u) == 4u);
    CHECK(kilix_asset_clip_frame(&clip, 5u) == 6u);
    CHECK(kilix_asset_clip_frame(&clip, 6u) == 4u);
    clip.loop = false;
    CHECK(kilix_asset_clip_frame(&clip, 999u) == 6u);
    CHECK(kilix_asset_cache_init(&cache, 4u, 1024u));
    CHECK(kilix_asset_cache_load_png(&cache, png_path, NULL, &first) ==
          KILIX_ASSET_OK);
    CHECK(kilix_asset_cache_load_png(&cache, png_path, NULL, &second) ==
          KILIX_ASSET_OK && first == second && cache.entry_count == 1u);
    kilix_asset_cache_clear(&cache);
    kilix_asset_image_clear(&raw);
    kilix_asset_image_clear(&png);
    return true;
}

static bool fixture_path(char *path, size_t capacity, const char *directory,
                         const char *leaf)
{
    int count = snprintf(path, capacity, "%s/%s", directory, leaf);
    return count >= 0 && (size_t)count < capacity;
}

static bool test_png_color_types(const char *directory)
{
    static const uint8_t grayscale[] = {10u, 20u};
    static const uint8_t grayscale_transparency[] = {0u, 20u};
    static const uint8_t rgb[] = {1u, 2u, 3u, 4u, 5u, 6u};
    static const uint8_t rgb_transparency[] = {0u, 4u, 0u, 5u, 0u, 6u};
    static const uint8_t gray_alpha[] = {7u, 9u};
    static const uint8_t palette[] = {
        255u, 0u, 0u, 0u, 255u, 0u,
        0u, 0u, 255u, 255u, 255u, 255u
    };
    static const uint8_t palette_alpha[] = {255u, 64u, 0u, 128u};
    static const struct {
        unsigned int depth;
        uint8_t packed[2];
        uint8_t first;
        uint8_t second;
    } indexed[] = {
        {1u, {0x40u, 0u}, 0u, 1u},
        {2u, {0x60u, 0u}, 1u, 2u},
        {4u, {0x12u, 0u}, 1u, 2u},
        {8u, {1u, 2u}, 1u, 2u}
    };
    kilix_asset_image image = {0};
    char path[1024];
    size_t index;

    CHECK(fixture_path(path, sizeof path, directory, "gray.png"));
    CHECK(write_png_fixture(path, 2u, 1u, 8u, 0u, grayscale, NULL,
                            NULL, 0u, grayscale_transparency,
                            sizeof grayscale_transparency, 0u));
    CHECK(kilix_asset_image_load_png(&image, path, NULL) == KILIX_ASSET_OK);
    CHECK(image.pixels[0] == 10u && image.pixels[3] == UINT8_MAX &&
          image.pixels[4] == 20u && image.pixels[7] == 0u);
    kilix_asset_image_clear(&image);
    CHECK(unlink(path) == 0);

    CHECK(fixture_path(path, sizeof path, directory, "rgb.png"));
    CHECK(write_png_fixture(path, 2u, 1u, 8u, 2u, rgb, NULL,
                            NULL, 0u, rgb_transparency,
                            sizeof rgb_transparency, 0u));
    CHECK(kilix_asset_image_load_png(&image, path, NULL) == KILIX_ASSET_OK);
    CHECK(memcmp(image.pixels, "\1\2\3\377\4\5\6\0", 8u) == 0);
    kilix_asset_image_clear(&image);
    CHECK(unlink(path) == 0);

    CHECK(fixture_path(path, sizeof path, directory, "gray-alpha.png"));
    CHECK(write_png_fixture(path, 1u, 1u, 8u, 4u, gray_alpha, NULL,
                            NULL, 0u, NULL, 0u, 0u));
    CHECK(kilix_asset_image_load_png(&image, path, NULL) == KILIX_ASSET_OK);
    CHECK(memcmp(image.pixels, "\7\7\7\11", 4u) == 0);
    kilix_asset_image_clear(&image);
    CHECK(unlink(path) == 0);

    for (index = 0u; index < sizeof indexed / sizeof indexed[0]; ++index) {
        const uint8_t *first = palette + (size_t)indexed[index].first * 3u;
        const uint8_t *second = palette + (size_t)indexed[index].second * 3u;
        size_t entry_count = indexed[index].depth == 1u ? 2u : 4u;
        CHECK(fixture_path(path, sizeof path, directory, "indexed.png"));
        CHECK(write_png_fixture(path, 2u, 1u, indexed[index].depth, 3u,
                                indexed[index].packed, NULL,
                                palette, entry_count * 3u, palette_alpha,
                                entry_count, 0u));
        CHECK(kilix_asset_image_load_png(&image, path, NULL) ==
              KILIX_ASSET_OK);
        CHECK(memcmp(image.pixels, first, 3u) == 0 &&
              image.pixels[3] == palette_alpha[indexed[index].first] &&
              memcmp(image.pixels + 4u, second, 3u) == 0 &&
              image.pixels[7] == palette_alpha[indexed[index].second]);
        kilix_asset_image_clear(&image);
        CHECK(unlink(path) == 0);
    }
    return true;
}

static bool test_png_filters(const char *directory)
{
    uint8_t pixels[3u * 5u * 4u];
    static const uint8_t filters[] = {0u, 1u, 2u, 3u, 4u};
    kilix_asset_image image = {0};
    char path[1024];
    size_t index;
    for (index = 0u; index < sizeof pixels; ++index)
        pixels[index] = (uint8_t)(index * 37u + 11u);
    CHECK(fixture_path(path, sizeof path, directory, "filters.png"));
    CHECK(write_png_fixture(path, 3u, 5u, 8u, 6u, pixels, filters,
                            NULL, 0u, NULL, 0u,
                            PNG_FIXTURE_EMPTY_IDAT |
                            PNG_FIXTURE_SPLIT_IDAT));
    CHECK(kilix_asset_image_load_png(&image, path, NULL) == KILIX_ASSET_OK);
    CHECK(image.byte_count == sizeof pixels &&
          memcmp(image.pixels, pixels, sizeof pixels) == 0);
    kilix_asset_image_clear(&image);
    CHECK(unlink(path) == 0);
    return true;
}

static bool test_png_structure(const char *directory, const char *good_path)
{
    static const uint8_t pixel[] = {1u, 2u, 3u, 4u};
    static const uint8_t palette[] = {0u, 0u, 0u, 255u, 255u, 255u};
    static const struct {
        const char *leaf;
        unsigned int flags;
    } malformed[] = {
        {"interleaved.png", PNG_FIXTURE_INTERLEAVE_IDAT},
        {"trailing.png", PNG_FIXTURE_TRAILING_ZLIB},
        {"before-ihdr.png", PNG_FIXTURE_BEFORE_IHDR}
    };
    kilix_asset_image image = {0};
    const uint8_t *preserved;
    uint8_t preserved_first;
    char path[1024];
    size_t index;
    kilix_asset_limits limits;

    CHECK(kilix_asset_image_load_png(&image, good_path, NULL) == KILIX_ASSET_OK);
    preserved = image.pixels;
    preserved_first = image.pixels[0];
    for (index = 0u; index < sizeof malformed / sizeof malformed[0]; ++index) {
        CHECK(fixture_path(path, sizeof path, directory, malformed[index].leaf));
        CHECK(write_png_fixture(path, 1u, 1u, 8u, 6u, pixel, NULL,
                                NULL, 0u, NULL, 0u,
                                malformed[index].flags));
        CHECK(kilix_asset_image_load_png(&image, path, NULL) ==
              KILIX_ASSET_CORRUPT);
        CHECK(image.pixels == preserved && image.pixels[0] == preserved_first);
        CHECK(unlink(path) == 0);
    }

    CHECK(fixture_path(path, sizeof path, directory, "missing-palette.png"));
    CHECK(write_png_fixture(path, 1u, 1u, 1u, 3u, pixel, NULL,
                            NULL, 0u, NULL, 0u, 0u));
    CHECK(kilix_asset_image_load_png(&image, path, NULL) ==
          KILIX_ASSET_CORRUPT);
    CHECK(unlink(path) == 0);

    CHECK(fixture_path(path, sizeof path, directory, "bad-index.png"));
    CHECK(write_png_fixture(path, 1u, 1u, 2u, 3u,
                            (const uint8_t *)"\300", NULL,
                            palette, sizeof palette, NULL, 0u, 0u));
    CHECK(kilix_asset_image_load_png(&image, path, NULL) ==
          KILIX_ASSET_CORRUPT);
    CHECK(unlink(path) == 0);

    kilix_asset_limits_init(&limits);
    limits.max_dimension = 1u;
    CHECK(kilix_asset_image_load_png(&image, good_path, &limits) ==
          KILIX_ASSET_LIMIT_EXCEEDED);
    CHECK(image.pixels == preserved);
    CHECK(kilix_asset_image_load_png(&image, "", NULL) ==
          KILIX_ASSET_INVALID_ARGUMENT);
    kilix_asset_image_clear(&image);
    return true;
}

static bool test_cache_pointer_stability(const char *directory,
                                         const uint8_t pixels[16])
{
    enum { CACHE_FIXTURES = 20 };
    kilix_asset_cache cache;
    const kilix_asset_image *first = NULL;
    const uint8_t *first_pixels = NULL;
    char paths[CACHE_FIXTURES][1024];
    size_t index;

    CHECK(kilix_asset_cache_init(&cache, CACHE_FIXTURES, 4096u));
    for (index = 0u; index < CACHE_FIXTURES; ++index) {
        const kilix_asset_image *image = NULL;
        CHECK(snprintf(paths[index], sizeof paths[index],
                       "%s/cache-%zu.rgba", directory, index) > 0);
        CHECK(write_bytes(paths[index], pixels, 16u));
        CHECK(kilix_asset_cache_load_rgba(&cache, paths[index], 2u, 2u,
                                          NULL, &image) == KILIX_ASSET_OK);
        CHECK(image && image->pixels && image->pixels[0] == pixels[0]);
        if (index == 0u) {
            first = image;
            first_pixels = image->pixels;
        }
        CHECK(first && first->pixels == first_pixels);
        CHECK(first->width == 2u && first->height == 2u);
        CHECK(first->pixels[15] == pixels[15]);
    }
    kilix_asset_cache_clear(&cache);
    for (index = 0u; index < CACHE_FIXTURES; ++index)
        CHECK(unlink(paths[index]) == 0);
    return true;
}

static bool test_cache_limits(const char *directory,
                              const uint8_t pixels[16])
{
    kilix_asset_cache cache = {0};
    const kilix_asset_image *first = NULL;
    const kilix_asset_image *second = NULL;
    char path[1024];
    CHECK(fixture_path(path, sizeof path, directory, "cache-limits.rgba"));
    CHECK(write_bytes(path, pixels, 16u));
    CHECK(kilix_asset_cache_init(&cache, 2u, 32u));
    CHECK(kilix_asset_cache_load_rgba(&cache, path, 2u, 2u, NULL, &first) ==
          KILIX_ASSET_OK);
    CHECK(kilix_asset_cache_load_rgba(&cache, path, 1u, 4u, NULL, &second) ==
          KILIX_ASSET_OK);
    CHECK(first != second && cache.entry_count == 2u &&
          cache.byte_count == 32u);
    CHECK(kilix_asset_cache_load_png(&cache, path, NULL, &second) ==
          KILIX_ASSET_LIMIT_EXCEEDED);
    CHECK(cache.entry_count == 2u);
    CHECK(unlink(path) == 0);
    CHECK(kilix_asset_cache_load_rgba(&cache, path, 2u, 2u, NULL, &second) ==
          KILIX_ASSET_OK && second == first);
    kilix_asset_cache_clear(&cache);

    CHECK(kilix_asset_cache_init(&cache, 1u, 15u));
    CHECK(write_bytes(path, pixels, 16u));
    CHECK(kilix_asset_cache_load_rgba(&cache, path, 2u, 2u, NULL, &first) ==
          KILIX_ASSET_LIMIT_EXCEEDED);
    CHECK(first == NULL && cache.entry_count == 0u && cache.byte_count == 0u);
    kilix_asset_cache_clear(&cache);
    CHECK(unlink(path) == 0);
    return true;
}

static bool test_geometry_and_animation(const uint8_t pixels[16])
{
    kilix_asset_image image = {(uint8_t *)(uintptr_t)pixels, 2u, 2u, 8u, 16u};
    kilix_asset_image padded = {(uint8_t *)(uintptr_t)pixels, 1u, 2u, 8u, 16u};
    kilix_asset_atlas atlas = {0};
    kilix_asset_region region;
    kilix_asset_clip clip;
    CHECK(strcmp(kilix_asset_status_string(KILIX_ASSET_CORRUPT),
                 "corrupt asset") == 0);
    CHECK(strcmp(kilix_asset_status_string((kilix_asset_status)999),
                 "unknown asset error") == 0);
    CHECK(kilix_asset_image_is_valid(&image));
    CHECK(kilix_asset_image_is_valid(&padded));
    padded.byte_count = 15u;
    CHECK(!kilix_asset_image_is_valid(&padded));
    region = kilix_asset_image_region(&image, 1u, 0u, 1u, 2u);
    CHECK(kilix_asset_region_is_valid(&region) && region.pixels == pixels + 4u);
    CHECK(!kilix_asset_region_is_valid(
        &(kilix_asset_region){pixels, UINT32_MAX, 1u, 1u}));
    CHECK(kilix_asset_image_region(&image, 2u, 0u, 1u, 1u).pixels == NULL);
    CHECK(kilix_asset_atlas_init_grid(&atlas, &image, 2u, 1u));
    CHECK(!kilix_asset_atlas_init_grid(&atlas, &image, 3u, 1u));
    CHECK(atlas.columns == 2u && atlas.rows == 1u);
    CHECK(kilix_asset_atlas_cell(&atlas, 1u, 0u).pixels == pixels + 4u);
    CHECK(kilix_asset_atlas_cell(&atlas, 2u, 0u).pixels == NULL);
    atlas.cell_width = UINT32_MAX;
    CHECK(kilix_asset_atlas_cell(&atlas, 0u, 0u).pixels == NULL);
    CHECK(!kilix_asset_region_is_valid(
        &(kilix_asset_region){pixels, 1u, 2u, SIZE_MAX}));
    clip = (kilix_asset_clip){UINT32_MAX - 1u, 2u, 1u, true};
    CHECK(kilix_asset_clip_is_valid(&clip));
    CHECK(kilix_asset_clip_frame(&clip, 1u) == UINT32_MAX);
    clip.frame_count = 3u;
    CHECK(!kilix_asset_clip_is_valid(&clip));
    CHECK(kilix_asset_clip_frame(&clip, 1u) == 0u);
    return true;
}

static bool test_nonregular_inputs(const char *directory)
{
    kilix_asset_image image = {0};
    kilix_asset_manifest manifest = {0};
    char path[1024];
    CHECK(fixture_path(path, sizeof path, directory, "input.fifo"));
    CHECK(mkfifo(path, 0600) == 0);
    CHECK(kilix_asset_image_load_png(&image, path, NULL) ==
          KILIX_ASSET_UNSUPPORTED);
    CHECK(kilix_asset_image_load_rgba(&image, path, 1u, 1u, NULL) ==
          KILIX_ASSET_UNSUPPORTED);
    CHECK(kilix_asset_manifest_load_json(&manifest, path, 1024u) ==
          KILIX_ASSET_UNSUPPORTED);
    CHECK(unlink(path) == 0);
    CHECK(kilix_asset_image_load_rgba(&image, directory, 1u, 1u, NULL) ==
          KILIX_ASSET_UNSUPPORTED);
    return true;
}

static bool test_manifest(const char *path)
{
    static const char json[] =
        "{\n"
        " \"schema_version\": 1, \"game\": \"fixture\",\n"
        " \"metadata\": {\"ignored\": [true, null, 2.5]},\n"
        " \"atlases\": [{\"id\":\"heroes\",\"path\":\"art/heroes.png\","
        "\"alpha_required\":true,\"grid\":{\"columns\":2,\"rows\":2,"
        "\"width\":4,\"height\":6,\"cell_width\":2,\"cell_height\":3}}],\n"
        " \"bitmaps\": [{\"id\":\"town\",\"png\":\"art/town.png\","
        "\"ppm\":\"art/town.ppm\",\"width\":320,\"height\":180}]\n"
        "}\n";
    kilix_asset_manifest manifest = {0};
    const kilix_asset_manifest_atlas *atlas;
    const kilix_asset_manifest_bitmap *bitmap;
    CHECK(write_bytes(path, json, sizeof json - 1u));
    CHECK(kilix_asset_manifest_load_json(&manifest, path, 16384u) ==
          KILIX_ASSET_OK);
    CHECK(manifest.schema_version == 1u && strcmp(manifest.game, "fixture") == 0);
    CHECK(manifest.atlas_count == 1u && manifest.bitmap_count == 1u);
    atlas = kilix_asset_manifest_find_atlas(&manifest, "heroes");
    bitmap = kilix_asset_manifest_find_bitmap(&manifest, "town");
    CHECK(atlas && atlas->columns == 2u && atlas->cell_height == 3u &&
          atlas->alpha_required);
    CHECK(bitmap && bitmap->width == 320u && bitmap->height == 180u);
    kilix_asset_manifest_clear(&manifest);
    {
        static const char grid_bitmap[] =
            "{\"schema_version\":1,\"game\":\"fixture\","
            "\"atlases\":[],\"bitmaps\":[{\"id\":\"map\","
            "\"path\":\"art/map.png\",\"grid\":{"
            "\"columns\":1,\"rows\":1,\"width\":480,\"height\":240,"
            "\"cell_width\":480,\"cell_height\":240}}]}";
        CHECK(write_bytes(path, grid_bitmap, sizeof grid_bitmap - 1u));
        CHECK(kilix_asset_manifest_load_json(&manifest, path, 16384u) ==
              KILIX_ASSET_OK);
        bitmap = kilix_asset_manifest_find_bitmap(&manifest, "map");
        CHECK(bitmap && strcmp(bitmap->path, "art/map.png") == 0 &&
              bitmap->width == 480u && bitmap->height == 240u);
        kilix_asset_manifest_clear(&manifest);
    }
    {
        static const char duplicate[] =
            "{\"schema_version\":1,\"game\":\"bad\",\"atlases\":["
            "{\"id\":\"same\",\"path\":\"a.png\",\"grid\":{"
            "\"columns\":1,\"rows\":1,\"width\":1,\"height\":1,"
            "\"cell_width\":1,\"cell_height\":1}},"
            "{\"id\":\"same\",\"path\":\"b.png\",\"grid\":{"
            "\"columns\":1,\"rows\":1,\"width\":1,\"height\":1,"
            "\"cell_width\":1,\"cell_height\":1}}],\"bitmaps\":[]}";
        CHECK(write_bytes(path, duplicate, sizeof duplicate - 1u));
        CHECK(kilix_asset_manifest_load_json(&manifest, path, 16384u) ==
              KILIX_ASSET_CORRUPT);
    }
    {
        static const char invalid_number[] =
            "{\"schema_version\":1,\"game\":\"bad\",\"metadata\":-,"
            "\"atlases\":[],\"bitmaps\":[]}";
        CHECK(write_bytes(path, invalid_number, sizeof invalid_number - 1u));
        CHECK(kilix_asset_manifest_load_json(&manifest, path, 16384u) ==
              KILIX_ASSET_CORRUPT);
    }
    return true;
}

static bool manifest_status_is(const char *path, const char *json,
                               kilix_asset_status expected)
{
    kilix_asset_manifest manifest = {0};
    kilix_asset_status status;
    if (!write_bytes(path, json, strlen(json))) return false;
    status = kilix_asset_manifest_load_json(&manifest, path, 65536u);
    kilix_asset_manifest_clear(&manifest);
    return status == expected;
}

static bool test_manifest_unicode_and_strictness(const char *path)
{
    static const char valid[] =
        "{\"schema_version\":1,"
        "\"game\":\"caf\\u00e9-\\ud83d\\ude80\","
        "\"metadata\":{\"note\":\"raw-\xc3\xa9\"},"
        "\"atlases\":[{\"id\":\"h\\u00e9ro\","
        "\"path\":\"art/h\\u00e9ro.png\",\"grid\":{"
        "\"columns\":1,\"rows\":1,\"width\":1,\"height\":1,"
        "\"cell_width\":1,\"cell_height\":1}}],\"bitmaps\":[]}";
    static const char *const invalid[] = {
        "{\"schema_version\":1,\"game\":\"x\",\"game\":\"y\","
        "\"atlases\":[],\"bitmaps\":[]}",
        "{\"schema_version\":1,\"game\":\"x\",\"atlases\":[{"
        "\"id\":\"a\",\"id\":\"b\",\"path\":\"a.png\","
        "\"grid\":{\"columns\":1,\"rows\":1,\"width\":1,"
        "\"height\":1,\"cell_width\":1,\"cell_height\":1}}],"
        "\"bitmaps\":[]}",
        "{\"schema_version\":1,\"game\":\"x\",\"atlases\":[{"
        "\"id\":\"a\",\"path\":\"a.png\",\"grid\":{"
        "\"columns\":1,\"columns\":1,\"rows\":1,\"width\":1,"
        "\"height\":1,\"cell_width\":1,\"cell_height\":1}}],"
        "\"bitmaps\":[]}",
        "{\"schema_version\":1,\"game\":\"x\",\"atlases\":[],"
        "\"bitmaps\":[{\"id\":\"a\",\"png\":\"a.png\","
        "\"width\":1,\"width\":1,\"height\":1}]}",
        "{\"schema_version\":1,\"game\":\"x\",\"atlases\":[],"
        "\"bitmaps\":[{\"id\":\"a\",\"path\":\"a.png\","
        "\"width\":1,\"height\":1}]}",
        "{\"schema_version\":1,\"game\":\"x\",\"atlases\":[],"
        "\"bitmaps\":[{\"id\":\"a\",\"png\":\"a.png\","
        "\"grid\":{\"columns\":1,\"rows\":1,\"width\":1,"
        "\"height\":1,\"cell_width\":1,\"cell_height\":1}}]}",
        "{\"schema_version\":1,\"game\":\"x\",\"atlases\":[{"
        "\"id\":\"a\",\"path\":\"a.png\",\"grid\":{"
        "\"columns\":1,\"rows\":1,\"width\":1,\"height\":1,"
        "\"cell_width\":1,\"cell_height\":1}},{\"id\":\"\\u0061\","
        "\"path\":\"b.png\",\"grid\":{\"columns\":1,\"rows\":1,"
        "\"width\":1,\"height\":1,\"cell_width\":1,"
        "\"cell_height\":1}}],\"bitmaps\":[]}",
        "{\"schema_version\":1,\"game\":\"\\ud800\","
        "\"atlases\":[],\"bitmaps\":[]}",
        "{\"schema_version\":1,\"game\":\"\\u0000\","
        "\"atlases\":[],\"bitmaps\":[]}",
        "{\v\"schema_version\":1,\"game\":\"x\","
        "\"atlases\":[],\"bitmaps\":[]}",
        "{\"schema_version\":1,\"game\":\"bad\\nname\","
        "\"atlases\":[],\"bitmaps\":[]}"
    };
    kilix_asset_manifest manifest = {0};
    char *preserved_game;
    size_t index;
    CHECK(write_bytes(path, valid, sizeof valid - 1u));
    CHECK(kilix_asset_manifest_load_json(&manifest, path, 65536u) ==
          KILIX_ASSET_OK);
    CHECK(strcmp(manifest.game, "caf\xc3\xa9-\xf0\x9f\x9a\x80") == 0);
    CHECK(manifest.atlas_count == 1u &&
          strcmp(manifest.atlases[0].id, "h\xc3\xa9ro") == 0 &&
          strcmp(manifest.atlases[0].path, "art/h\xc3\xa9ro.png") == 0);
    preserved_game = manifest.game;
    for (index = 0u; index < sizeof invalid / sizeof invalid[0]; ++index) {
        CHECK(write_bytes(path, invalid[index], strlen(invalid[index])));
        CHECK(kilix_asset_manifest_load_json(&manifest, path, 65536u) ==
              KILIX_ASSET_CORRUPT);
        CHECK(manifest.game == preserved_game &&
              strcmp(manifest.game, "caf\xc3\xa9-\xf0\x9f\x9a\x80") == 0);
    }
    kilix_asset_manifest_clear(&manifest);
    {
        static const char invalid_utf8[] =
            "{\"schema_version\":1,\"game\":\"\xc0\xaf\","
            "\"atlases\":[],\"bitmaps\":[]}";
        CHECK(write_bytes(path, invalid_utf8, sizeof invalid_utf8 - 1u));
        CHECK(kilix_asset_manifest_load_json(&manifest, path, 65536u) ==
              KILIX_ASSET_CORRUPT);
    }
    CHECK(manifest_status_is(path,
        "{\"schema_version\":1,\"game\":\"ok\","
        "\"atlases\":[],\"bitmaps\":[]}", KILIX_ASSET_OK));
    return true;
}

static bool write_checker_fixture(const char *directory)
{
    static const char manifest[] =
        "{\"schema_version\":1,\"game\":\"checker-fixture\","
        "\"atlases\":[{\"id\":\"sheet\",\"path\":\"image.png\","
        "\"alpha_required\":true,\"grid\":{\"columns\":2,\"rows\":2,"
        "\"width\":2,\"height\":2,\"cell_width\":1,"
        "\"cell_height\":1}}],\"bitmaps\":[]}";
    char png_path[1024];
    char manifest_path[1024];
    return fixture_path(png_path, sizeof png_path, directory, "image.png") &&
           fixture_path(manifest_path, sizeof manifest_path, directory,
                        "manifest.json") &&
           write_test_png(png_path) &&
           write_bytes(manifest_path, manifest, sizeof manifest - 1u);
}

int main(int argc, char **argv)
{
#define RUN_TEST(label, expression) do {                                    \
    if (!(expression)) failed = 1;                                         \
    else {                                                                 \
        ++passed;                                                          \
        (void)printf("ok %s\n", (label));                                \
    }                                                                      \
} while (false)
    static const uint8_t raw_pixels[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u
    };
    char directory[] = "/tmp/kilix-assets-test-XXXXXX";
    char png_path[1024];
    char raw_path[1024];
    char manifest_path[1024];
    int failed = 0;
    size_t passed = 0u;
    if (argc == 3 && strcmp(argv[1], "--write-checker-fixture") == 0)
        return write_checker_fixture(argv[2]) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (argc != 1) return EXIT_FAILURE;
    if (!mkdtemp(directory) ||
        snprintf(png_path, sizeof png_path, "%s/image.png", directory) < 0 ||
        snprintf(raw_path, sizeof raw_path, "%s/pixels.rgba", directory) < 0 ||
        snprintf(manifest_path, sizeof manifest_path, "%s/manifest.json",
                 directory) < 0 ||
        !write_test_png(png_path) ||
        !write_bytes(raw_path, raw_pixels, sizeof raw_pixels)) {
        (void)fprintf(stderr, "fixture setup failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    RUN_TEST("paths", test_paths(directory, raw_path));
    RUN_TEST("images", test_images(png_path, raw_path));
    RUN_TEST("PNG color types", test_png_color_types(directory));
    RUN_TEST("PNG filters and split IDAT", test_png_filters(directory));
    RUN_TEST("PNG structure and transactions",
             test_png_structure(directory, png_path));
    RUN_TEST("geometry and animation", test_geometry_and_animation(raw_pixels));
    RUN_TEST("cache pointer stability",
             test_cache_pointer_stability(directory, raw_pixels));
    RUN_TEST("cache limits", test_cache_limits(directory, raw_pixels));
    RUN_TEST("nonregular inputs", test_nonregular_inputs(directory));
    RUN_TEST("manifest v1", test_manifest(manifest_path));
    RUN_TEST("manifest Unicode and strictness",
             test_manifest_unicode_and_strictness(manifest_path));
    (void)unlink(manifest_path);
    (void)unlink(raw_path);
    (void)unlink(png_path);
    (void)rmdir(directory);
    if (failed) return EXIT_FAILURE;
    (void)printf("%zu tests passed\n", passed);
    return EXIT_SUCCESS;
#undef RUN_TEST
}
