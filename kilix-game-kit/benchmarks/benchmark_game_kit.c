#include "kilix_game_loop.h"
#include "kilix_game_test.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t monotonic_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

int main(void)
{
    enum { width = 640, height = 360, ppm_rounds = 12 };
    const size_t byte_count = (size_t)width * (size_t)height * 4u;
    uint8_t *pixels = malloc(byte_count);
    kilix_game_clock_options options;
    kilix_game_clock clock;
    uint64_t checksum = 0u;
    uint64_t started;
    uint64_t elapsed;

    if (!pixels) return EXIT_FAILURE;
    for (size_t index = 0u; index < byte_count; ++index)
        pixels[index] = (uint8_t)(index * 17u + index / 7u);

    kilix_game_clock_options_init(&options);
    if (!kilix_game_clock_init(&clock, &options)) {
        free(pixels);
        return EXIT_FAILURE;
    }
    started = monotonic_ns();
    for (uint64_t index = 0u; index < UINT64_C(10000000); ++index) {
        const kilix_game_frame frame = kilix_game_clock_advance(
            &clock, (int64_t)(index * UINT64_C(1000000)));
        checksum += frame.steps + (uint64_t)frame.frame_ns;
    }
    elapsed = monotonic_ns() - started;
    (void)printf("clock_ns_per_call=%.3f checksum=%" PRIu64 "\n",
                 (double)elapsed / 10000000.0, checksum);

    started = monotonic_ns();
    for (unsigned int round = 0u; round < ppm_rounds; ++round) {
        if (!kilix_test_write_ppm_rgba("/dev/null", pixels, width, height,
                                      (size_t)width * 4u)) {
            free(pixels);
            return EXIT_FAILURE;
        }
    }
    elapsed = monotonic_ns() - started;
    (void)printf("ppm_ns_per_frame=%.3f pixels=%u rounds=%u\n",
                 (double)elapsed / (double)ppm_rounds,
                 width * height, ppm_rounds);
    free(pixels);
    return EXIT_SUCCESS;
}
