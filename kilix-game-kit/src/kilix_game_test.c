#include "kilix_game_test.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <pty.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

bool kilix_test_cli_dispatch(int argc, char **argv,
                             const kilix_test_cli_command *commands,
                             size_t command_count, void *user,
                             int *exit_code)
{
    size_t index;
    int argument_count;
    if (!exit_code || argc < 0 || (argc != 0 && !argv) ||
        (command_count != 0u && !commands)) return false;
    if (argc < 2 || !argv[1]) return false;
    argument_count = argc - 2;
    for (index = 0u; index < command_count; ++index) {
        const kilix_test_cli_command *command = &commands[index];
        if (!command->option || !command->run ||
            strcmp(argv[1], command->option) != 0) continue;
        if (argument_count < command->minimum_arguments ||
            (command->maximum_arguments >= 0 &&
             argument_count > command->maximum_arguments)) {
            *exit_code = 1;
            return true;
        }
        *exit_code = command->run(user, argument_count,
                                  (const char *const *)(argv + 2));
        return true;
    }
    return false;
}

bool kilix_test_pty_open(kilix_test_pty *pty, unsigned short columns,
                         unsigned short rows, unsigned short pixel_width,
                         unsigned short pixel_height)
{
    struct winsize window;

    if (pty == NULL || columns == 0u || rows == 0u) {
        errno = EINVAL;
        return false;
    }
    pty->master_fd = -1;
    pty->slave_fd = -1;
    (void)memset(&window, 0, sizeof window);
    window.ws_col = columns;
    window.ws_row = rows;
    window.ws_xpixel = pixel_width;
    window.ws_ypixel = pixel_height;
    if (openpty(&pty->master_fd, &pty->slave_fd, NULL, NULL, &window) != 0)
        return false;
    return true;
}

void kilix_test_pty_close(kilix_test_pty *pty)
{
    if (pty == NULL) return;
    if (pty->slave_fd >= 0) (void)close(pty->slave_fd);
    if (pty->master_fd >= 0) (void)close(pty->master_fd);
    pty->slave_fd = -1;
    pty->master_fd = -1;
}

bool kilix_test_write_all(int fd, const void *bytes, size_t byte_count)
{
    const unsigned char *source = bytes;
    size_t offset = 0u;

    if (fd < 0 || (bytes == NULL && byte_count != 0u)) {
        errno = EINVAL;
        return false;
    }
    while (offset < byte_count) {
        const ssize_t count = write(fd, source + offset, byte_count - offset);
        if (count > 0) offset += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

ssize_t kilix_test_read_available(int fd, void *bytes, size_t capacity,
                                  int idle_timeout_ms)
{
    unsigned char *destination = bytes;
    size_t total = 0u;

    if (fd < 0 || (bytes == NULL && capacity != 0u) || idle_timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }
    while (total < capacity) {
        struct pollfd descriptor;
        int result;
        ssize_t count;

        descriptor.fd = fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        do {
            result = poll(&descriptor, 1u, idle_timeout_ms);
        } while (result < 0 && errno == EINTR);
        if (result < 0) return -1;
        if (result == 0) break;
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            errno = EIO;
            return -1;
        }
        if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) break;
        count = read(fd, destination + total, capacity - total);
        if (count > 0) total += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else if (count < 0) return -1;
        else break;
    }
    return (ssize_t)total;
}

bool kilix_test_contains(const void *bytes, size_t byte_count,
                         const void *needle, size_t needle_size)
{
    const unsigned char *haystack = bytes;
    const unsigned char *match = needle;

    if ((bytes == NULL && byte_count != 0u) ||
        (needle == NULL && needle_size != 0u)) return false;
    if (needle_size == 0u) return true;
    if (needle_size > byte_count) return false;
    for (size_t index = 0u; index + needle_size <= byte_count; ++index)
        if (memcmp(haystack + index, match, needle_size) == 0) return true;
    return false;
}

uint64_t kilix_test_hash64(const void *bytes, size_t byte_count)
{
    const unsigned char *source = bytes;
    uint64_t hash = UINT64_C(14695981039346656037);

    if (bytes == NULL && byte_count != 0u) return 0u;
    for (size_t index = 0u; index < byte_count; ++index) {
        hash ^= source[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

kilix_test_image_diff kilix_test_diff_rgba(const uint8_t *first,
                                           const uint8_t *second,
                                           size_t pixel_count,
                                           uint8_t tolerance)
{
    kilix_test_image_diff result = {0};

    if ((first == NULL || second == NULL) && pixel_count != 0u) {
        result.differing_pixels = pixel_count;
        result.maximum_channel_delta = UINT8_MAX;
        return result;
    }
    for (size_t pixel = 0u; pixel < pixel_count; ++pixel) {
        bool differs = false;

        for (size_t channel = 0u; channel < 4u; ++channel) {
            const size_t offset = pixel * 4u + channel;
            const unsigned int a = first[offset];
            const unsigned int b = second[offset];
            const unsigned int delta = a >= b ? a - b : b - a;

            if (delta > result.maximum_channel_delta)
                result.maximum_channel_delta = (uint8_t)delta;
            if (delta > tolerance) differs = true;
        }
        if (differs) ++result.differing_pixels;
    }
    return result;
}

void kilix_test_golden_suite_init(kilix_test_golden_suite *suite)
{
    if (!suite) return;
    suite->suite_hash = UINT64_C(14695981039346656037);
    suite->case_count = 0u;
    suite->mismatch_count = 0u;
}

bool kilix_test_golden_add(kilix_test_golden_suite *suite,
                           const void *bytes, size_t byte_count,
                           uint64_t expected_hash)
{
    uint64_t hash;
    unsigned int shift;
    if (!suite || (!bytes && byte_count != 0u)) return false;
    hash = kilix_test_hash64(bytes, byte_count);
    for (shift = 0u; shift < 64u; shift += 8u) {
        suite->suite_hash ^= (uint8_t)(hash >> shift);
        suite->suite_hash *= UINT64_C(1099511628211);
    }
    ++suite->case_count;
    if (hash != expected_hash) {
        ++suite->mismatch_count;
        return false;
    }
    return true;
}

bool kilix_test_golden_finish(const kilix_test_golden_suite *suite,
                              uint64_t expected_suite_hash)
{
    return suite && suite->case_count != 0u && suite->mismatch_count == 0u &&
           suite->suite_hash == expected_suite_hash;
}

bool kilix_test_write_ppm_rgba(const char *path, const uint8_t *rgba,
                               size_t width, size_t height, size_t stride)
{
    FILE *stream;
    size_t row;
    if (!path || !rgba || width == 0u || height == 0u ||
        width > SIZE_MAX / 4u || stride < width * 4u ||
        height > SIZE_MAX / stride) return false;
    stream = fopen(path, "wb");
    if (!stream) return false;
    if (fprintf(stream, "P6\n%zu %zu\n255\n", width, height) < 0) {
        (void)fclose(stream);
        return false;
    }
    for (row = 0u; row < height; ++row) {
        const uint8_t *source = rgba + row * stride;
        size_t column;
        for (column = 0u; column < width; ++column) {
            if (fwrite(source + column * 4u, 1u, 3u, stream) != 3u) {
                (void)fclose(stream);
                return false;
            }
        }
    }
    return fclose(stream) == 0;
}
