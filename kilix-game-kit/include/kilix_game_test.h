#ifndef KILIX_GAME_TEST_H
#define KILIX_GAME_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kilix_test_pty {
    int master_fd;
    int slave_fd;
} kilix_test_pty;

typedef struct kilix_test_image_diff {
    size_t differing_pixels;
    uint8_t maximum_channel_delta;
} kilix_test_image_diff;

typedef int (*kilix_test_cli_fn)(void *user, int argument_count,
                                 const char *const *arguments);

typedef struct kilix_test_cli_command {
    const char *option;
    int minimum_arguments;
    int maximum_arguments;
    kilix_test_cli_fn run;
} kilix_test_cli_command;

/* Matches argv[1], validates the remaining argument count, and invokes one
 * registered command. false means no command matched. */
bool kilix_test_cli_dispatch(int argc, char **argv,
                             const kilix_test_cli_command *commands,
                             size_t command_count, void *user,
                             int *exit_code);

typedef struct kilix_test_golden_suite {
    uint64_t suite_hash;
    size_t case_count;
    size_t mismatch_count;
} kilix_test_golden_suite;

bool kilix_test_pty_open(kilix_test_pty *pty, unsigned short columns,
                         unsigned short rows, unsigned short pixel_width,
                         unsigned short pixel_height);
void kilix_test_pty_close(kilix_test_pty *pty);
bool kilix_test_write_all(int fd, const void *bytes, size_t byte_count);
ssize_t kilix_test_read_available(int fd, void *bytes, size_t capacity,
                                  int idle_timeout_ms);
bool kilix_test_contains(const void *bytes, size_t byte_count,
                         const void *needle, size_t needle_size);

uint64_t kilix_test_hash64(const void *bytes, size_t byte_count);
kilix_test_image_diff kilix_test_diff_rgba(const uint8_t *first,
                                           const uint8_t *second,
                                           size_t pixel_count,
                                           uint8_t tolerance);

void kilix_test_golden_suite_init(kilix_test_golden_suite *suite);
bool kilix_test_golden_add(kilix_test_golden_suite *suite,
                           const void *bytes, size_t byte_count,
                           uint64_t expected_hash);
bool kilix_test_golden_finish(const kilix_test_golden_suite *suite,
                              uint64_t expected_suite_hash);

/* Writes canonical P6 RGB while accepting a padded straight-RGBA source. */
bool kilix_test_write_ppm_rgba(const char *path, const uint8_t *rgba,
                               size_t width, size_t height, size_t stride);

#ifdef __cplusplus
}
#endif

#endif
