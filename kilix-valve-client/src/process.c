#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

bool
kvalve_process_group_exists(pid_t process_group)
{
    if (process_group <= 1) {
        return false;
    }
    if (kill(-process_group, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

bool
kvalve_process_start_time(pid_t pid, uint64_t *start_time)
{
    char path[64];
    char contents[4096];
    char *cursor;
    char *end;
    unsigned field;
    unsigned long long value;
    if (pid <= 1 || start_time == NULL) {
        return false;
    }
    (void)snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    if (!kvalve_read_bounded(path, contents, sizeof(contents), NULL)) {
        return false;
    }
    cursor = strrchr(contents, ')');
    if (cursor == NULL || cursor[1] != ' ') {
        return false;
    }
    cursor += 2;
    for (field = 3U; field < 22U; ++field) {
        cursor = strchr(cursor, ' ');
        if (cursor == NULL) {
            return false;
        }
        while (*cursor == ' ') {
            ++cursor;
        }
    }
    errno = 0;
    value = strtoull(cursor, &end, 10);
    if (errno != 0 || end == cursor || (*end != ' ' && *end != '\n'
                                        && *end != '\0')) {
        return false;
    }
    *start_time = (uint64_t)value;
    return true;
}

bool
kvalve_process_identity_matches(pid_t pid, pid_t process_group,
                                uint64_t start_time)
{
    uint64_t observed;
    return pid > 1 && process_group > 1 && getpgid(pid) == process_group
        && (start_time == 0U
            || (kvalve_process_start_time(pid, &observed)
                && observed == start_time));
}

kvalve_client_result
kvalve_stop_process_group(pid_t pid, pid_t process_group, uint64_t start_time,
                          unsigned timeout_ms,
                          struct kvalve_diagnostic_storage *diagnostic)
{
    uint64_t deadline;
    struct timespec delay = {0, 10000000L};
    bool leader_absent;
    if (!kvalve_process_group_exists(process_group)) {
        return KVALVE_CLIENT_OK;
    }
    leader_absent = kill(pid, 0) != 0 && errno == ESRCH;
    if (!leader_absent
            && !kvalve_process_identity_matches(pid, process_group,
                                                start_time)) {
        kvalve_diag_set(diagnostic, KVALVE_CLIENT_ERR_PERMISSION,
                        "process-identity-mismatch",
                        "The recorded process identity changed; no signal was sent.",
                        false);
        return KVALVE_CLIENT_ERR_PERMISSION;
    }
    if (kill(-process_group, SIGTERM) != 0 && errno != ESRCH) {
        kvalve_diag_set(diagnostic, KVALVE_CLIENT_ERR_PERMISSION,
                        "owned-stop-refused",
                        "The owned process group could not be stopped.", true);
        return KVALVE_CLIENT_ERR_PERMISSION;
    }
    deadline = kvalve_now_ms() + (uint64_t)timeout_ms;
    while (kvalve_process_group_exists(process_group)
           && kvalve_now_ms() < deadline) {
        (void)nanosleep(&delay, NULL);
    }
    if (kvalve_process_group_exists(process_group)) {
        if (kill(-process_group, SIGKILL) != 0 && errno != ESRCH) {
            kvalve_diag_set(diagnostic, KVALVE_CLIENT_ERR_PERMISSION,
                            "owned-kill-refused",
                            "The owned process group exceeded its stop deadline.",
                            false);
            return KVALVE_CLIENT_ERR_PERMISSION;
        }
    }
    return KVALVE_CLIENT_OK;
}

static bool
all_digits(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    if (cursor == NULL || *cursor == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (*cursor < (unsigned char)'0' || *cursor > (unsigned char)'9') {
            return false;
        }
        ++cursor;
    }
    return true;
}

static bool
process_matches_launcher(pid_t pid, const char *launcher, const char *resolved)
{
    char path[64];
    char executable[PATH_MAX];
    char argument[PATH_MAX];
    char chunk[4096];
    struct stat process_info;
    ssize_t length;
    int descriptor;
    size_t argument_length = 0U;
    size_t total = 0U;
    bool argument_overflow = false;
    bool ended_at_boundary = true;
    enum { COMMAND_LINE_CAP = 1024 * 1024 };

    (void)snprintf(path, sizeof(path), "/proc/%ld", (long)pid);
    if (stat(path, &process_info) != 0 || process_info.st_uid != geteuid()) {
        return false;
    }
    (void)snprintf(path, sizeof(path), "/proc/%ld/exe", (long)pid);
    length = readlink(path, executable, sizeof(executable) - 1U);
    if (length > 0) {
        executable[(size_t)length] = '\0';
        if (strcmp(executable, resolved) == 0) {
            return true;
        }
    }
    (void)snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return kill(pid, 0) == 0 || errno == EPERM;
    }
    for (;;) {
        size_t index;
        do {
            length = read(descriptor, chunk, sizeof(chunk));
        } while (length < 0 && errno == EINTR);
        if (length < 0) {
            (void)close(descriptor);
            return true;
        }
        if (length == 0) {
            break;
        }
        total += (size_t)length;
        if (total > (size_t)COMMAND_LINE_CAP) {
            (void)close(descriptor);
            return true;
        }
        for (index = 0U; index < (size_t)length; ++index) {
            if (chunk[index] == '\0') {
                if (!argument_overflow) {
                    argument[argument_length] = '\0';
                    if (strcmp(argument, launcher) == 0
                            || strcmp(argument, resolved) == 0) {
                        (void)close(descriptor);
                        return true;
                    }
                }
                argument_length = 0U;
                argument_overflow = false;
                ended_at_boundary = true;
            } else {
                ended_at_boundary = false;
                if (argument_length + 1U < sizeof(argument)) {
                    argument[argument_length++] = chunk[index];
                } else {
                    argument_overflow = true;
                }
            }
        }
    }
    (void)close(descriptor);
    /* A live same-user process with a malformed/truncated cmdline is
     * ambiguous. Refuse concurrency rather than silently treating it as
     * unrelated-to-Steam evidence. Zombies have an empty, valid cmdline. */
    return total > 0U && !ended_at_boundary;
}

bool
kvalve_unrelated_launcher_running(const char *launcher, pid_t except_pid)
{
    DIR *processes;
    struct dirent *entry;
    char resolved[PATH_MAX];
    bool found = false;
    if (launcher == NULL || realpath(launcher, resolved) == NULL) {
        return false;
    }
    processes = opendir("/proc");
    if (processes == NULL) {
        return false;
    }
    while ((entry = readdir(processes)) != NULL) {
        char *end;
        long raw;
        if (!all_digits(entry->d_name)) {
            continue;
        }
        errno = 0;
        raw = strtol(entry->d_name, &end, 10);
        if (errno != 0 || *end != '\0' || raw <= 1L
                || raw > (long)INT_MAX || (pid_t)raw == except_pid
                || (pid_t)raw == getpid()) {
            continue;
        }
        if (process_matches_launcher((pid_t)raw, launcher, resolved)) {
            found = true;
            break;
        }
    }
    (void)closedir(processes);
    return found;
}
