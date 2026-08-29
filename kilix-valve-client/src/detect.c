#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static bool
owner_allowed(uid_t owner, uid_t trusted_uid, bool require_root_owner)
{
    return owner == 0U || (!require_root_owner && owner == trusted_uid);
}

static bool
secure_ancestry(const char *path, uid_t trusted_uid, bool require_root_owner)
{
    char copy[PATH_MAX];
    char *slash;
    struct stat info;
    if (!kvalve_copy_path(copy, path) || copy[0] != '/') {
        return false;
    }
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        return false;
    }
    if (slash == copy) {
        copy[1] = '\0';
    } else {
        *slash = '\0';
    }
    for (;;) {
        if (lstat(copy, &info) != 0 || !S_ISDIR(info.st_mode)
                || !owner_allowed(info.st_uid, trusted_uid,
                                  require_root_owner)
                || (info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            return false;
        }
        if (strcmp(copy, "/") == 0) {
            return true;
        }
        slash = strrchr(copy, '/');
        if (slash == NULL) {
            return false;
        }
        if (slash == copy) {
            copy[1] = '\0';
        } else {
            *slash = '\0';
        }
    }
}

bool
kvalve_secure_file(const char *path, mode_t kind, uid_t trusted_uid,
                   bool executable, bool require_root_owner)
{
    struct stat info;
    if (path == NULL || path[0] != '/' || lstat(path, &info) != 0
            || (info.st_mode & S_IFMT) != kind
            || !owner_allowed(info.st_uid, trusted_uid, require_root_owner)
            || (info.st_mode & (S_IWGRP | S_IWOTH)) != 0
            || (executable && (info.st_mode & S_IXUSR) == 0)) {
        return false;
    }
    return secure_ancestry(path, trusted_uid, require_root_owner);
}

bool
kvalve_secure_launcher(const kvalve_client_context *context)
{
    struct stat info;
    char resolved[PATH_MAX];
    if (context == NULL) {
        return false;
    }
    if (!context->require_root_owner) {
        return kvalve_secure_file(
            context->launcher, S_IFREG, context->trusted_uid, true, false);
    }
    if (strcmp(context->launcher, KVALVE_DEFAULT_LAUNCHER) != 0
            || lstat(context->launcher, &info) != 0) {
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        return kvalve_secure_file(context->launcher, S_IFREG, 0U, true, true);
    }
    if (!S_ISLNK(info.st_mode) || info.st_uid != 0U
            || !secure_ancestry(context->launcher, 0U, true)
            || realpath(context->launcher, resolved) == NULL
            || strcmp(resolved, KVALVE_DEFAULT_LAUNCHER_TARGET) != 0) {
        return false;
    }
    return kvalve_secure_file(
        KVALVE_DEFAULT_LAUNCHER_TARGET, S_IFREG, 0U, true, true);
}

bool
kvalve_read_bounded(const char *path, char *buffer, size_t capacity,
                    size_t *length)
{
    int descriptor;
    struct stat info;
    size_t used = 0U;
    if (path == NULL || buffer == NULL || capacity < 2U) {
        return false;
    }
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return false;
    }
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)
            || info.st_size < 0 || (uintmax_t)info.st_size >= capacity) {
        (void)close(descriptor);
        return false;
    }
    while (used + 1U < capacity) {
        ssize_t amount = read(descriptor, buffer + used, capacity - used - 1U);
        if (amount > 0) {
            used += (size_t)amount;
            continue;
        }
        if (amount < 0 && errno == EINTR) {
            continue;
        }
        if (amount < 0) {
            (void)close(descriptor);
            return false;
        }
        break;
    }
    if (used + 1U == capacity) {
        char extra;
        ssize_t amount;
        do {
            amount = read(descriptor, &extra, 1U);
        } while (amount < 0 && errno == EINTR);
        if (amount != 0) {
            (void)close(descriptor);
            return false;
        }
    }
    if (close(descriptor) != 0) {
        return false;
    }
    buffer[used] = '\0';
    if (length != NULL) {
        *length = used;
    }
    return true;
}

static bool
has_exact_line(const char *text, const char *wanted)
{
    size_t wanted_length = strlen(wanted);
    const char *cursor = text;
    while (cursor != NULL && *cursor != '\0') {
        const char *end = strchr(cursor, '\n');
        size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (length > 0U && cursor[length - 1U] == '\r') {
            --length;
        }
        if (length == wanted_length && memcmp(cursor, wanted, length) == 0) {
            return true;
        }
        cursor = end != NULL ? end + 1 : NULL;
    }
    return false;
}

static bool
consume_exact_line(const char **cursor, const char *wanted)
{
    size_t length;
    if (cursor == NULL || *cursor == NULL || wanted == NULL) {
        return false;
    }
    length = strlen(wanted);
    if (strncmp(*cursor, wanted, length) != 0 || (*cursor)[length] != '\n') {
        return false;
    }
    *cursor += length + 1U;
    return true;
}

static bool
consume_digest_line(const char **cursor, const char *prefix)
{
    size_t index;
    size_t prefix_length;
    if (cursor == NULL || *cursor == NULL || prefix == NULL) {
        return false;
    }
    prefix_length = strlen(prefix);
    if (strncmp(*cursor, prefix, prefix_length) != 0) {
        return false;
    }
    for (index = prefix_length; index < prefix_length + 64U; ++index) {
        unsigned char byte = (unsigned char)(*cursor)[index];
        if (!isdigit(byte) && (byte < (unsigned char)'a'
                               || byte > (unsigned char)'f')) {
            return false;
        }
    }
    if ((*cursor)[index] != '\n') {
        return false;
    }
    *cursor += index + 1U;
    return true;
}

static bool
policy_is_exact(const kvalve_client_context *context)
{
    char contents[KVALVE_FILE_CAP];
    const char *cursor = contents;
    if (!kvalve_secure_file(context->policy, S_IFREG, context->trusted_uid,
                            false, context->require_root_owner)
            || !kvalve_read_bounded(context->policy, contents,
                                    sizeof(contents), NULL)) {
        return false;
    }
    return consume_exact_line(
               &cursor, "schema=plebian-os.steam-policy/v1")
        && consume_exact_line(
               &cursor,
               "authorization_schema=kilix.install.authorization/v2")
        && consume_exact_line(&cursor, "architectures=amd64,i386")
        && consume_exact_line(
               &cursor,
               "packages=steam-launcher,steam-libs-amd64:amd64,steam-libs-i386:i386")
        && consume_exact_line(&cursor, "helper_modes=install,repair,verify")
        && consume_digest_line(&cursor, "archive_policy_sha256=")
        && consume_digest_line(&cursor, "key_policy_sha256=")
        && consume_digest_line(&cursor, "pin_policy_sha256=")
        && *cursor == '\0';
}

static bool
package_is_installed(const char *path)
{
    FILE *stream;
    char line[1024];
    bool package = false;
    bool installed = false;
    bool architecture = false;
    bool overflow = false;
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return false;
    }
    stream = fdopen(descriptor, "r");
    if (stream == NULL) {
        (void)close(descriptor);
        return false;
    }
    while (fgets(line, sizeof(line), stream) != NULL) {
        size_t length = strlen(line);
        if (length > 0U && line[length - 1U] == '\n') {
            line[--length] = '\0';
            if (overflow) {
                overflow = false;
                continue;
            }
            if (length > 0U && line[length - 1U] == '\r') {
                line[--length] = '\0';
            }
        } else if (!feof(stream)) {
            overflow = true;
            continue;
        }
        if (overflow) {
            continue;
        }
        if (length == 0U) {
            if (package && installed && architecture) {
                (void)fclose(stream);
                return true;
            }
            package = installed = architecture = false;
        } else if (strcmp(line, "Package: steam-launcher") == 0) {
            package = true;
        } else if (strcmp(line, "Status: install ok installed") == 0) {
            installed = true;
        } else if (strcmp(line, "Architecture: all") == 0
                   || strcmp(line, "Architecture: amd64") == 0) {
            architecture = true;
        }
    }
    if (ferror(stream)) {
        (void)fclose(stream);
        return false;
    }
    (void)fclose(stream);
    return package && installed && architecture;
}

static bool
path_exists(const char *path)
{
    struct stat info;
    return lstat(path, &info) == 0;
}

static bool
helper_verifies_system(const kvalve_client_context *context)
{
    static char *const environment[] = {
        (char *)"HOME=/nonexistent",
        (char *)"LANG=C.UTF-8",
        (char *)"LC_ALL=C.UTF-8",
        (char *)"LOGNAME=nobody",
        (char *)"PATH=/usr/sbin:/usr/bin:/sbin:/bin",
        (char *)"TZ=UTC",
        (char *)"USER=nobody",
        NULL
    };
    char *const arguments[] = {(char *)context->helper, (char *)"--verify",
                               NULL};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    struct kvalve_diagnostic_storage diagnostic;
    struct timespec delay = {0, 10000000L};
    pid_t child = -1;
    uint64_t start_time = 0U;
    uint64_t deadline;
    int child_status;
    int error = EINVAL;
    short flags = POSIX_SPAWN_SETPGROUP;
    bool actions_ready = false;
    bool attributes_ready = false;

    if (posix_spawn_file_actions_init(&actions) != 0) {
        return false;
    }
    actions_ready = true;
    if (posix_spawn_file_actions_addopen(
            &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0
            || posix_spawn_file_actions_addopen(
                &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0
            || posix_spawn_file_actions_addopen(
                &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0
            || posix_spawnattr_init(&attributes) != 0) {
        goto cleanup;
    }
    attributes_ready = true;
    if (posix_spawnattr_setflags(&attributes, flags) != 0
            || posix_spawnattr_setpgroup(&attributes, 0) != 0) {
        goto cleanup;
    }
    error = posix_spawn(&child, context->helper, &actions, &attributes,
                        arguments, environment);
    if (error != 0) {
        child = -1;
        goto cleanup;
    }
    (void)kvalve_process_start_time(child, &start_time);
    deadline = kvalve_now_ms() + UINT64_C(10000);
    for (;;) {
        pid_t result = waitpid(child, &child_status, WNOHANG);
        if (result == child) {
            child = -1;
            error = WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0
                ? 0 : ECHILD;
            break;
        }
        if (result < 0 && errno != EINTR) {
            child = -1;
            error = ECHILD;
            break;
        }
        if (kvalve_now_ms() >= deadline) {
            kvalve_diag_set(&diagnostic, KVALVE_CLIENT_ERR_TIMEOUT,
                            "verify-timeout",
                            "The fixed read-only helper exceeded its deadline.",
                            true);
            (void)kvalve_stop_process_group(
                child, child, start_time, 250U, &diagnostic);
            while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
            }
            child = -1;
            error = ETIMEDOUT;
            break;
        }
        (void)nanosleep(&delay, NULL);
    }

cleanup:
    if (child > 1) {
        (void)kill(-child, SIGKILL);
        (void)kill(child, SIGKILL);
        while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
        }
    }
    if (attributes_ready) {
        (void)posix_spawnattr_destroy(&attributes);
    }
    if (actions_ready) {
        (void)posix_spawn_file_actions_destroy(&actions);
    }
    return error == 0;
}

kvalve_client_result
kvalve_client_probe(kvalve_client_context *context,
                    kvalve_client_status *status)
{
    char architectures[4096];
    bool any;
    bool system_verified = false;
    bool unrelated_running;
    kvalve_client_result result;
    if (context == NULL || status == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    (void)memset(status, 0, sizeof(*status));
    if (strcmp(context->machine, "x86_64") != 0
            && strcmp(context->machine, "amd64") != 0) {
        status->classification =
            KVALVE_CLIENT_INSTALL_UNSUPPORTED_ARCHITECTURE;
        result = KVALVE_CLIENT_ERR_UNSUPPORTED;
        kvalve_diag_set(&status->diagnostic, result, "unsupported-architecture",
                        "Steam integration requires an amd64 system.", false);
        kvalve_diag_copy(&context->diagnostic, &status->diagnostic);
        return result;
    }
    status->helper_verified = kvalve_secure_file(
        context->helper, S_IFREG, context->trusted_uid, true,
        context->require_root_owner);
    status->launcher_verified = kvalve_secure_launcher(context);
    status->policy_verified = policy_is_exact(context);
    status->i386_enabled = kvalve_read_bounded(
        context->dpkg_arch, architectures, sizeof(architectures), NULL)
        && has_exact_line(architectures, "i386");
    status->package_installed = package_is_installed(context->dpkg_status);

    unrelated_running = kvalve_unrelated_launcher_running(
        context->launcher, 0);
    if (!unrelated_running && status->helper_verified
            && status->launcher_verified && status->policy_verified
            && status->i386_enabled && status->package_installed) {
        system_verified = helper_verifies_system(context);
    }

    if (unrelated_running) {
        status->classification = KVALVE_CLIENT_INSTALL_UNRELATED_RUNNING;
        result = KVALVE_CLIENT_ERR_UNRELATED_INSTANCE;
        kvalve_diag_set(&status->diagnostic, result,
                        "unrelated-steam-running",
                        "Steam is already running outside this Kilix tab; it was left untouched.",
                        true);
    } else if (system_verified) {
        status->classification = KVALVE_CLIENT_INSTALL_EXACT;
        result = KVALVE_CLIENT_OK;
        kvalve_diag_set(&status->diagnostic, result, "system-layer-exact",
                        "The packaged Steam system layer matches the fixed policy.",
                        false);
    } else {
        any = path_exists(context->launcher) || status->package_installed;
        if ((path_exists(context->helper) && !status->helper_verified)
                   || (path_exists(context->launcher)
                       && !status->launcher_verified)
                   || (path_exists(context->policy)
                       && !status->policy_verified)) {
            status->classification = KVALVE_CLIENT_INSTALL_CONFLICTING;
            result = KVALVE_CLIENT_ERR_CONFLICTING;
            kvalve_diag_set(&status->diagnostic, result,
                            "system-layer-conflicting",
                            "A Steam policy path exists but does not match trusted packaged state.",
                            false);
        } else if (status->helper_verified && status->launcher_verified
                   && status->policy_verified && status->i386_enabled
                   && status->package_installed) {
            status->classification = KVALVE_CLIENT_INSTALL_PARTIAL;
            result = KVALVE_CLIENT_ERR_INSTALL_REQUIRED;
            kvalve_diag_set(&status->diagnostic, result,
                            "system-layer-unverified",
                            "The fixed read-only helper did not verify the Steam transaction outcome.",
                            true);
        } else if (any
                   || status->helper_verified != status->policy_verified) {
            status->classification = KVALVE_CLIENT_INSTALL_PARTIAL;
            result = KVALVE_CLIENT_ERR_INSTALL_REQUIRED;
            kvalve_diag_set(&status->diagnostic, result,
                            "system-layer-partial",
                            "The Steam system layer is incomplete and requires fixed-policy repair.",
                            true);
        } else {
            status->classification = KVALVE_CLIENT_INSTALL_ABSENT;
            result = KVALVE_CLIENT_ERR_NOT_INSTALLED;
            kvalve_diag_set(&status->diagnostic, result, "system-layer-absent",
                            "The optional Steam system layer is not installed.",
                            true);
        }
    }
    kvalve_diag_copy(&context->diagnostic, &status->diagnostic);
    return result;
}
