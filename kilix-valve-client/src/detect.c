#include "internal.h"

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

static void
trust_note(struct kvalve_trust_report *report, enum kvalve_trust_reason reason,
           const char *path, const struct stat *info)
{
    if (report == NULL) {
        return;
    }
    report->reason = reason;
    (void)snprintf(report->path, sizeof(report->path), "%s",
                   path != NULL ? path : "");
    report->mode = info != NULL ? (unsigned)(info->st_mode & 07777) : 0U;
    report->owner = info != NULL ? info->st_uid : (uid_t)-1;
}

bool
kvalve_trust_reason_is_ancestry(enum kvalve_trust_reason reason)
{
    return reason == KVALVE_TRUST_ANCESTOR_UNREADABLE
        || reason == KVALVE_TRUST_ANCESTOR_NOT_DIRECTORY
        || reason == KVALVE_TRUST_ANCESTOR_OWNER
        || reason == KVALVE_TRUST_ANCESTOR_WRITABLE;
}

static bool
secure_ancestry(const char *path, uid_t trusted_uid, bool require_root_owner,
                struct kvalve_trust_report *report)
{
    char copy[PATH_MAX];
    char *slash;
    struct stat info;
    if (!kvalve_copy_path(copy, path) || copy[0] != '/') {
        trust_note(report, KVALVE_TRUST_PATH_INVALID, path, NULL);
        return false;
    }
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        trust_note(report, KVALVE_TRUST_PATH_INVALID, path, NULL);
        return false;
    }
    if (slash == copy) {
        copy[1] = '\0';
    } else {
        *slash = '\0';
    }
    for (;;) {
        if (lstat(copy, &info) != 0) {
            trust_note(report, KVALVE_TRUST_ANCESTOR_UNREADABLE, copy, NULL);
            return false;
        }
        if (!S_ISDIR(info.st_mode)) {
            trust_note(report, KVALVE_TRUST_ANCESTOR_NOT_DIRECTORY, copy,
                       &info);
            return false;
        }
        if (!owner_allowed(info.st_uid, trusted_uid, require_root_owner)) {
            trust_note(report, KVALVE_TRUST_ANCESTOR_OWNER, copy, &info);
            return false;
        }
        if ((info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            trust_note(report, KVALVE_TRUST_ANCESTOR_WRITABLE, copy, &info);
            return false;
        }
        if (strcmp(copy, "/") == 0) {
            return true;
        }
        slash = strrchr(copy, '/');
        if (slash == NULL) {
            trust_note(report, KVALVE_TRUST_PATH_INVALID, copy, NULL);
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
kvalve_secure_file_reported(const char *path, mode_t kind, uid_t trusted_uid,
                            bool executable, bool require_root_owner,
                            struct kvalve_trust_report *report)
{
    struct stat info;
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
        report->reason = KVALVE_TRUST_OK;
        report->owner = (uid_t)-1;
    }
    if (path == NULL || path[0] != '/') {
        trust_note(report, KVALVE_TRUST_PATH_INVALID, path, NULL);
        return false;
    }
    if (lstat(path, &info) != 0) {
        trust_note(report, KVALVE_TRUST_ABSENT, path, NULL);
        return false;
    }
    if ((info.st_mode & S_IFMT) != kind) {
        trust_note(report, KVALVE_TRUST_WRONG_TYPE, path, &info);
        return false;
    }
    if (!owner_allowed(info.st_uid, trusted_uid, require_root_owner)) {
        trust_note(report, KVALVE_TRUST_OWNER, path, &info);
        return false;
    }
    if ((info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        trust_note(report, KVALVE_TRUST_WRITABLE, path, &info);
        return false;
    }
    if (executable && (info.st_mode & S_IXUSR) == 0) {
        trust_note(report, KVALVE_TRUST_NOT_EXECUTABLE, path, &info);
        return false;
    }
    return secure_ancestry(path, trusted_uid, require_root_owner, report);
}

bool
kvalve_secure_file(const char *path, mode_t kind, uid_t trusted_uid,
                   bool executable, bool require_root_owner)
{
    return kvalve_secure_file_reported(path, kind, trusted_uid, executable,
                                       require_root_owner, NULL);
}

bool
kvalve_secure_launcher_reported(const kvalve_client_context *context,
                                struct kvalve_trust_report *report)
{
    struct stat info;
    char resolved[PATH_MAX];
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
        report->reason = KVALVE_TRUST_OK;
        report->owner = (uid_t)-1;
    }
    if (context == NULL) {
        trust_note(report, KVALVE_TRUST_PATH_INVALID, NULL, NULL);
        return false;
    }
    if (!context->require_root_owner) {
        return kvalve_secure_file_reported(
            context->launcher, S_IFREG, context->trusted_uid, true, false,
            report);
    }
    if (strcmp(context->launcher, KVALVE_DEFAULT_LAUNCHER) != 0) {
        trust_note(report, KVALVE_TRUST_PATH_INVALID, context->launcher, NULL);
        return false;
    }
    if (lstat(context->launcher, &info) != 0) {
        trust_note(report, KVALVE_TRUST_ABSENT, context->launcher, NULL);
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        return kvalve_secure_file_reported(context->launcher, S_IFREG, 0U,
                                           true, true, report);
    }
    if (!S_ISLNK(info.st_mode)) {
        trust_note(report, KVALVE_TRUST_WRONG_TYPE, context->launcher, &info);
        return false;
    }
    if (info.st_uid != 0U) {
        trust_note(report, KVALVE_TRUST_OWNER, context->launcher, &info);
        return false;
    }
    if (!secure_ancestry(context->launcher, 0U, true, report)) {
        return false;
    }
    if (realpath(context->launcher, resolved) == NULL
            || strcmp(resolved, KVALVE_DEFAULT_LAUNCHER_TARGET) != 0) {
        trust_note(report, KVALVE_TRUST_WRONG_TYPE, context->launcher, &info);
        return false;
    }
    return kvalve_secure_file_reported(
        KVALVE_DEFAULT_LAUNCHER_TARGET, S_IFREG, 0U, true, true, report);
}

bool
kvalve_secure_launcher(const kvalve_client_context *context)
{
    return kvalve_secure_launcher_reported(context, NULL);
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
    const char *newline;
    size_t index;
    size_t prefix_length;
    if (cursor == NULL || *cursor == NULL || prefix == NULL) {
        return false;
    }
    prefix_length = strlen(prefix);
    newline = strchr(*cursor, '\n');
    if (newline == NULL
            || (size_t)(newline - *cursor) != prefix_length + 64U
            || strncmp(*cursor, prefix, prefix_length) != 0) {
        return false;
    }
    for (index = prefix_length; index < prefix_length + 64U; ++index) {
        unsigned char byte = (unsigned char)(*cursor)[index];
        if (!((byte >= (unsigned char)'0' && byte <= (unsigned char)'9')
                || (byte >= (unsigned char)'a'
                    && byte <= (unsigned char)'f'))) {
            return false;
        }
    }
    *cursor = newline + 1;
    return true;
}

static bool
policy_is_exact(const kvalve_client_context *context,
                struct kvalve_trust_report *report)
{
    char contents[KVALVE_FILE_CAP];
    const char *cursor = contents;
    if (!kvalve_secure_file_reported(context->policy, S_IFREG,
                                     context->trusted_uid, false,
                                     context->require_root_owner, report)) {
        return false;
    }
    /* Trusted, so a false from here on is a CONTENT answer and the report is
     * deliberately left at KVALVE_TRUST_OK: the caller uses that to tell a
     * policy that disagrees from a policy it was never allowed to read. */
    if (!kvalve_read_bounded(context->policy, contents, sizeof(contents),
                             NULL)) {
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
            || posix_spawn_file_actions_addclosefrom_np(
                &actions, STDERR_FILENO + 1) != 0
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
    enum kvalve_process_scan_result process_scan = KVALVE_PROCESS_SCAN_CLEAR;
    kvalve_client_result result;
    struct kvalve_trust_report helper_trust;
    struct kvalve_trust_report launcher_trust;
    struct kvalve_trust_report policy_trust;
    const struct kvalve_trust_report *untrusted = NULL;
    const char *untrusted_subject = NULL;
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
    status->helper_verified = kvalve_secure_file_reported(
        context->helper, S_IFREG, context->trusted_uid, true,
        context->require_root_owner, &helper_trust);
    status->launcher_verified = kvalve_secure_launcher_reported(
        context, &launcher_trust);
    status->policy_verified = policy_is_exact(context, &policy_trust);

    /* An ancestor no one else may write to is a precondition for reading any
     * of the three, and failing it is not evidence about the install. Pick the
     * first subject whose refusal was an ancestry refusal, in a fixed order so
     * the answer does not depend on which check ran first. */
    if (kvalve_trust_reason_is_ancestry(policy_trust.reason)) {
        untrusted = &policy_trust;
        untrusted_subject = "policy";
    } else if (kvalve_trust_reason_is_ancestry(helper_trust.reason)) {
        untrusted = &helper_trust;
        untrusted_subject = "helper";
    } else if (kvalve_trust_reason_is_ancestry(launcher_trust.reason)) {
        untrusted = &launcher_trust;
        untrusted_subject = "launcher";
    }
    status->i386_enabled = kvalve_read_bounded(
        context->dpkg_arch, architectures, sizeof(architectures), NULL)
        && has_exact_line(architectures, "i386");
    status->package_installed = package_is_installed(context->dpkg_status);

    if (status->launcher_verified) {
        process_scan = kvalve_scan_unrelated_launcher(
            context->proc_root, context->launcher, 0);
    }
    if (process_scan == KVALVE_PROCESS_SCAN_CLEAR && status->helper_verified
            && status->launcher_verified && status->policy_verified
            && status->i386_enabled && status->package_installed) {
        system_verified = helper_verifies_system(context);
    }

    if (untrusted != NULL) {
        /* Deliberately UNKNOWN rather than PARTIAL or CONFLICTING. The
         * classifier did not classify: it refused to read its own inputs, and
         * saying anything about the install would be asserting a fact it never
         * established. This is the case that used to arrive as
         * "system-layer-conflicting" -- a sentence about Steam, for a problem
         * about a directory. */
        char summary[KVALVE_DIAGNOSTIC_SUMMARY_CAP];
        char shown[KVALVE_DIAGNOSTIC_PATH_SHOWN_CAP + 4];
        const char *clause;
        status->classification = KVALVE_CLIENT_INSTALL_UNKNOWN;
        result = KVALVE_CLIENT_ERR_PERMISSION;
        switch (untrusted->reason) {
        case KVALVE_TRUST_ANCESTOR_WRITABLE:
            clause = "is group- or other-writable";
            break;
        case KVALVE_TRUST_ANCESTOR_OWNER:
            clause = "is owned by neither root nor this user";
            break;
        case KVALVE_TRUST_ANCESTOR_NOT_DIRECTORY:
            clause = "is not a directory";
            break;
        default:
            clause = "could not be examined";
            break;
        }
        /* The offending ancestor is a prefix directory and is normally short.
         * A path long enough to need truncating is marked as truncated rather
         * than silently shortened, because a prefix of a long path is exactly
         * what an operator cannot act on. Bounding it here also keeps the
         * worst case provably inside KVALVE_DIAGNOSTIC_SUMMARY_CAP. */
        if (strlen(untrusted->path) > (size_t)KVALVE_DIAGNOSTIC_PATH_SHOWN_CAP) {
            (void)snprintf(shown, sizeof(shown), "%.*s...",
                           KVALVE_DIAGNOSTIC_PATH_SHOWN_CAP, untrusted->path);
        } else {
            (void)snprintf(shown, sizeof(shown), "%s", untrusted->path);
        }
        if (untrusted->reason == KVALVE_TRUST_ANCESTOR_UNREADABLE) {
            (void)snprintf(summary, sizeof(summary),
                           "The Steam %s is not trusted: its ancestor %s "
                           "%s. No Steam state was read.",
                           untrusted_subject, shown, clause);
        } else {
            (void)snprintf(summary, sizeof(summary),
                           "The Steam %s is not trusted: its ancestor %s "
                           "%s (mode %04o, uid %ld). No Steam state was read.",
                           untrusted_subject, shown, clause,
                           untrusted->mode, (long)untrusted->owner);
        }
        kvalve_diag_set(&status->diagnostic, result,
                        "system-layer-untrusted-path", summary, false);
    } else if (process_scan == KVALVE_PROCESS_SCAN_UNAVAILABLE) {
        status->classification = KVALVE_CLIENT_INSTALL_PARTIAL;
        result = KVALVE_CLIENT_ERR_PERMISSION;
        kvalve_diag_set(&status->diagnostic, result,
                        "runtime-process-scan-unavailable",
                        "The process table could not be inspected safely; "
                        "no Steam action is permitted.",
                        true);
    } else if (process_scan == KVALVE_PROCESS_SCAN_FOUND) {
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
