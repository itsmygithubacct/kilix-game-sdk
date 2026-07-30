#include "kilix_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_transparency(const kilix_asset_image *image)
{
    uint32_t y;
    if (!kilix_asset_image_is_valid(image)) return false;
    for (y = 0u; y < image->height; ++y) {
        const uint8_t *row = image->pixels + (size_t)y * image->stride;
        uint32_t x;
        for (x = 0u; x < image->width; ++x)
            if (row[(size_t)x * 4u + 3u] != UINT8_MAX) return true;
    }
    return false;
}

static bool check_image(kilix_asset_cache *cache,
                        const kilix_asset_locator *locator,
                        const char *id, const char *relative_path,
                        uint32_t width, uint32_t height,
                        bool alpha_required,
                        const kilix_asset_image **loaded)
{
    char path[4096];
    kilix_asset_status status = kilix_asset_resolve(
        locator, relative_path, path, sizeof path);
    if (status != KILIX_ASSET_OK) {
        (void)fprintf(stderr, "%s: %s: %s\n", id, relative_path,
                      kilix_asset_status_string(status));
        return false;
    }
    status = kilix_asset_cache_load_png(cache, path, NULL, loaded);
    if (status != KILIX_ASSET_OK) {
        (void)fprintf(stderr, "%s: %s\n", id,
                      kilix_asset_status_string(status));
        return false;
    }
    if ((*loaded)->width != width || (*loaded)->height != height) {
        (void)fprintf(stderr, "%s: expected %ux%u, got %ux%u\n", id,
                      width, height, (*loaded)->width, (*loaded)->height);
        return false;
    }
    if (alpha_required && !has_transparency(*loaded)) {
        (void)fprintf(stderr, "%s: manifest requires transparent pixels\n",
                      id);
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    kilix_asset_manifest manifest = {0};
    kilix_asset_locator locator;
    kilix_asset_cache cache;
    kilix_asset_status status;
    size_t index;
    bool valid = true;
    if (argc != 3) {
        (void)fprintf(stderr, "usage: %s MANIFEST GAME_ROOT\n", argv[0]);
        return EXIT_FAILURE;
    }
    status = kilix_asset_manifest_load_json(&manifest, argv[1],
                                             4u * 1024u * 1024u);
    if (status != KILIX_ASSET_OK) {
        (void)fprintf(stderr, "%s: %s\n", argv[1],
                      kilix_asset_status_string(status));
        return EXIT_FAILURE;
    }
    kilix_asset_locator_init(&locator);
    locator.source_root = argv[2];
    locator.installed_root = NULL;
    if (!kilix_asset_cache_init(&cache,
            manifest.atlas_count + manifest.bitmap_count + 1u,
            512u * 1024u * 1024u)) {
        kilix_asset_manifest_clear(&manifest);
        return EXIT_FAILURE;
    }
    for (index = 0u; index < manifest.atlas_count; ++index) {
        const kilix_asset_manifest_atlas *entry = &manifest.atlases[index];
        const kilix_asset_image *image = NULL;
        kilix_asset_atlas atlas;
        if (!check_image(&cache, &locator, entry->id, entry->path,
                         entry->width, entry->height, entry->alpha_required,
                         &image) ||
            !kilix_asset_atlas_init_grid(&atlas, image, entry->columns,
                                         entry->rows)) {
            (void)fprintf(stderr, "%s: invalid atlas grid\n", entry->id);
            valid = false;
        }
    }
    for (index = 0u; index < manifest.bitmap_count; ++index) {
        const kilix_asset_manifest_bitmap *entry = &manifest.bitmaps[index];
        const kilix_asset_image *image = NULL;
        if (!check_image(&cache, &locator, entry->id, entry->path,
                         entry->width, entry->height, false, &image))
            valid = false;
    }
    if (valid)
        (void)printf("PASS kilix-assets game=%s atlases=%zu bitmaps=%zu "
                     "decoded-bytes=%zu\n", manifest.game,
                     manifest.atlas_count, manifest.bitmap_count,
                     cache.byte_count);
    kilix_asset_cache_clear(&cache);
    kilix_asset_manifest_clear(&manifest);
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
