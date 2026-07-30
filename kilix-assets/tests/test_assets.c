#include "kilix_assets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
             write_chunk(stream, "IEND", NULL, 0u) && fclose(stream) == 0;
    free(compressed);
    return result;
}

static bool write_bytes(const char *path, const void *bytes, size_t size)
{
    FILE *stream = fopen(path, "wb");
    bool result;
    if (!stream) return false;
    result = write_all(stream, bytes, size) && fclose(stream) == 0;
    return result;
}

static bool test_paths(const char *directory, const char *raw_path)
{
    kilix_asset_locator locator;
    char resolved[1024];
    CHECK(kilix_asset_path_is_safe("assets/graphics/a.png"));
    CHECK(!kilix_asset_path_is_safe("../secret"));
    CHECK(!kilix_asset_path_is_safe("a//b"));
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

static bool test_cache_pointer_stability(const char *directory,
                                         const uint8_t pixels[16])
{
    kilix_asset_cache cache;
    const kilix_asset_image *first = NULL;
    const uint8_t *first_pixels = NULL;
    char paths[10][1024];
    size_t index;

    CHECK(kilix_asset_cache_init(&cache, 10u, 4096u));
    for (index = 0u; index < 10u; ++index) {
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
    for (index = 0u; index < 10u; ++index)
        CHECK(unlink(paths[index]) == 0);
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

int main(void)
{
    static const uint8_t raw_pixels[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u
    };
    char directory[] = "/tmp/kilix-assets-test-XXXXXX";
    char png_path[1024];
    char raw_path[1024];
    char manifest_path[1024];
    int failed = 0;
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
    if (!test_paths(directory, raw_path)) failed = 1;
    if (!failed && !test_images(png_path, raw_path)) failed = 1;
    if (!failed &&
        !test_cache_pointer_stability(directory, raw_pixels))
        failed = 1;
    if (!failed && !test_manifest(manifest_path)) failed = 1;
    (void)unlink(manifest_path);
    (void)unlink(raw_path);
    (void)unlink(png_path);
    (void)rmdir(directory);
    if (failed) return EXIT_FAILURE;
    (void)printf("PASS kilix-assets png=rgba8 cache=1 manifest=v1 atlas=grid clip=ticks\n");
    return EXIT_SUCCESS;
}
