#include "kilix_valve_client.h"
#include "../src/internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef KVALVE_TEST_FAKE_STEAM
#error KVALVE_TEST_FAKE_STEAM must name the fixture executable
#endif

static unsigned checks;
static unsigned passed;

#define CHECK(expression) do { \
    ++checks; \
    if (expression) { \
        ++passed; \
    } else { \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
                      #expression); \
    } \
} while (0)

struct fixture {
    char root[PATH_MAX];
    char helper[PATH_MAX];
    char policy[PATH_MAX];
    char architectures[PATH_MAX];
    char package_status[PATH_MAX];
    char large_package_status[PATH_MAX];
    char missing[PATH_MAX];
    char session_home[PATH_MAX];
    char runtime_dir[PATH_MAX];
    char endpoint[PATH_MAX];
};

static void
pause_ms(unsigned milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

static bool
path_join(char output[PATH_MAX], const char *parent, const char *name)
{
    int amount = snprintf(output, PATH_MAX, "%s/%s", parent, name);
    return amount >= 0 && (size_t)amount < (size_t)PATH_MAX;
}

static bool
write_fixture(const char *path, const char *contents, mode_t mode)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    size_t length = strlen(contents);
    size_t written = 0U;
    if (descriptor < 0) {
        return false;
    }
    while (written < length) {
        ssize_t amount = write(descriptor, contents + written, length - written);
        if (amount > 0) {
            written += (size_t)amount;
        } else if (amount < 0 && errno == EINTR) {
            continue;
        } else {
            (void)close(descriptor);
            return false;
        }
    }
    return close(descriptor) == 0;
}

static bool
write_large_package_status(const char *path)
{
    static const char filler[] =
        "Package: unrelated-package\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Description: bounded fixture padding for production-sized status parsing\n\n";
    static const char wanted[] =
        "Package: steam-launcher\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n\n";
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    unsigned index;
    if (descriptor < 0) {
        return false;
    }
    for (index = 0U; index < 400U; ++index) {
        if (write(descriptor, filler, sizeof(filler) - 1U)
                != (ssize_t)(sizeof(filler) - 1U)) {
            (void)close(descriptor);
            return false;
        }
    }
    if (write(descriptor, wanted, sizeof(wanted) - 1U)
            != (ssize_t)(sizeof(wanted) - 1U)) {
        (void)close(descriptor);
        return false;
    }
    return close(descriptor) == 0;
}

/* Where the private fixture tree is created.
 *
 * TMPDIR wins when it is set to an absolute path, so an operator can still
 * place the fixture deliberately.  It is not REQUIRED: an unset TMPDIR used to
 * abort the whole binary with "could not create private test fixture", which
 * is the POSIX default state and told the reader nothing about what was wrong.
 *
 * The fallback is the directory that already holds the fixture executable.
 * KVALVE_TEST_FAKE_STEAM is an absolute path supplied by the build, and the
 * library's own ancestry rule has to hold for it anyway or every launcher
 * check in this file would fail -- so a checkout that can run this test at all
 * can host the fixture.  /tmp deliberately is NOT the fallback: it is mode
 * 1777 on a stock system, and kvalve_secure_file rejects any ancestor that is
 * group- or other-writable, so defaulting there would trade one opaque failure
 * for another.
 */
static bool
fixture_parent(char output[PATH_MAX])
{
    static const char fake_steam[] = KVALVE_TEST_FAKE_STEAM;
    const char *temporary = getenv("TMPDIR");
    const char *separator;
    size_t length;
    int amount;
    if (temporary != NULL && temporary[0] == '/') {
        amount = snprintf(output, PATH_MAX, "%s", temporary);
        return amount >= 0 && (size_t)amount < (size_t)PATH_MAX;
    }
    separator = strrchr(fake_steam, '/');
    if (separator == NULL || separator == fake_steam) {
        return false;
    }
    length = (size_t)(separator - fake_steam);
    if (length >= (size_t)PATH_MAX) {
        return false;
    }
    (void)memcpy(output, fake_steam, length);
    output[length] = '\0';
    return true;
}

/* Report an environment the library's own rules reject, naming the path and
 * the rule, instead of leaving the reader with a bare "could not create". */
static void
explain_insecure(const char *label, const char *path)
{
    char copy[PATH_MAX];
    char *slash;
    struct stat info;
    int amount = snprintf(copy, sizeof(copy), "%s", path);
    (void)fprintf(stderr, "%s is not usable: %s\n", label, path);
    if (amount < 0 || (size_t)amount >= sizeof(copy)) {
        return;
    }
    for (;;) {
        if (lstat(copy, &info) == 0
                && (info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            (void)fprintf(stderr,
                          "  %s is group- or other-writable (mode %04o); "
                          "kvalve_secure_file refuses any such ancestor\n",
                          copy, (unsigned)(info.st_mode & 07777));
            return;
        }
        slash = strrchr(copy, '/');
        if (slash == NULL) {
            return;
        }
        if (slash == copy) {
            if (copy[1] == '\0') {
                return;
            }
            copy[1] = '\0';
        } else {
            *slash = '\0';
        }
    }
}

static bool
create_fixture(struct fixture *fixture)
{
    static const char policy[] =
        "schema=plebian-os.steam-policy/v1\n"
        "authorization_schema=kilix.install.authorization/v2\n"
        "architectures=amd64,i386\n"
        "packages=steam-launcher,steam-libs-amd64:amd64,steam-libs-i386:i386\n"
        "helper_modes=install,repair,verify\n"
        "archive_policy_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "key_policy_sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
        "pin_policy_sha256=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n";
    static const char package_status[] =
        "Package: unrelated\nStatus: install ok installed\nArchitecture: amd64\n\n"
        "Package: steam-launcher\nStatus: install ok installed\nArchitecture: all\n";
    char parent[PATH_MAX];
    int amount;
    (void)memset(fixture, 0, sizeof(*fixture));
    if (!fixture_parent(parent)) {
        (void)fprintf(stderr,
                      "no usable fixture directory: TMPDIR is not an absolute "
                      "path and %s has no directory component\n",
                      KVALVE_TEST_FAKE_STEAM);
        return false;
    }
    amount = snprintf(fixture->root, sizeof(fixture->root),
                      "%s/kvalve-test-XXXXXX", parent);
    if (amount < 0 || (size_t)amount >= sizeof(fixture->root)) {
        (void)fprintf(stderr, "fixture path under %s exceeds PATH_MAX\n",
                      parent);
        return false;
    }
    if (mkdtemp(fixture->root) == NULL) {
        (void)fprintf(stderr, "could not create a fixture directory under %s: "
                      "%s\n", parent, strerror(errno));
        return false;
    }
    if (chmod(fixture->root, 0700) != 0
            || !path_join(fixture->helper, fixture->root, "helper")
            || !path_join(fixture->policy, fixture->root, "policy")
            || !path_join(fixture->architectures, fixture->root, "arch")
            || !path_join(fixture->package_status, fixture->root, "status")
            || !path_join(fixture->large_package_status, fixture->root,
                          "large-status")
            || !path_join(fixture->missing, fixture->root, "missing")
            || !path_join(fixture->session_home, fixture->root, "session")
            || !write_fixture(
                fixture->helper,
                "#!/bin/sh\n[ \"${1:-}\" = --verify ]\n", 0700)
            || !write_fixture(fixture->policy, policy, 0600)
            || !write_fixture(fixture->architectures, "i386\n", 0600)
            || !write_fixture(fixture->package_status, package_status, 0600)
            || !write_large_package_status(fixture->large_package_status)
            || mkdir(fixture->session_home, 0700) != 0
            || !path_join(fixture->runtime_dir, fixture->session_home,
                          "shared-gpu-abcdef")
            || mkdir(fixture->runtime_dir, 0700) != 0
            || !path_join(fixture->endpoint, fixture->runtime_dir,
                          "wayland-kilix-shared-123")) {
        return false;
    }
    return true;
}

static void
remove_fixture(struct fixture *fixture)
{
    char path[PATH_MAX];
    (void)unlink(fixture->endpoint);
    if (path_join(path, fixture->session_home, "valve-client/steam.lock")) {
        (void)unlink(path);
    }
    if (path_join(path, fixture->session_home, "valve-client")) {
        (void)rmdir(path);
    }
    (void)rmdir(fixture->runtime_dir);
    (void)rmdir(fixture->session_home);
    (void)unlink(fixture->helper);
    (void)unlink(fixture->policy);
    (void)unlink(fixture->architectures);
    (void)unlink(fixture->package_status);
    (void)unlink(fixture->large_package_status);
    (void)rmdir(fixture->root);
}

static kvalve_client_context *
configured_context(const struct fixture *fixture, const char *helper,
                   const char *launcher, const char *policy,
                   const char *architectures, const char *package_status,
                   const char *machine)
{
    kvalve_client_context *context = NULL;
    if (kvalve_client_context_create(&context) != KVALVE_CLIENT_OK
            || !kvalve_test_context_paths(
                context, helper, launcher, policy, architectures,
                package_status, machine)) {
        kvalve_client_context_free(context);
        return NULL;
    }
    (void)fixture;
    return context;
}

static void
test_probe(const struct fixture *fixture)
{
    static const char conflicting_policy[] =
        "schema=plebian-os.steam-policy/v1\n"
        "authorization_schema=kilix.install.authorization/v2\n"
        "architectures=amd64,i386\n"
        "packages=steam-launcher,steam-libs-amd64:amd64,steam-libs-i386:i386\n"
        "helper_modes=install,repair,verify\n"
        "archive_policy_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "key_policy_sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
        "pin_policy_sha256=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n"
        "unexpected=scope-expansion\n";
    static const char truncated_digest_policy[] =
        "schema=plebian-os.steam-policy/v1\n"
        "authorization_schema=kilix.install.authorization/v2\n"
        "architectures=amd64,i386\n"
        "packages=steam-launcher,steam-libs-amd64:amd64,steam-libs-i386:i386\n"
        "helper_modes=install,repair,verify\n"
        "archive_policy_sha256=a\n"
        "key_policy_sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
        "pin_policy_sha256=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n";
    char conflicting_path[PATH_MAX];
    char test_executable[PATH_MAX] = {0};
    char ready_byte;
    int ready_pipe[2] = {-1, -1};
    int child_status;
    int pipe_result;
    pid_t unrelated_pid = -1;
    kvalve_client_context *context;
    kvalve_client_status *status = NULL;
    kvalve_client_result result;
    context = configured_context(
        fixture, fixture->helper, KVALVE_TEST_FAKE_STEAM, fixture->policy,
        fixture->architectures, fixture->package_status, "x86_64");
    CHECK(context != NULL);
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    result = kvalve_client_probe(context, status);
    CHECK(result == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_EXACT);
    CHECK(kvalve_client_status_helper_verified(status));
    CHECK(kvalve_client_status_policy_verified(status));
    CHECK(kvalve_client_status_i386_enabled(status));
    CHECK(kvalve_client_status_package_installed(status));
    CHECK(kvalve_client_status_launcher_verified(status));
    CHECK(strcmp(kvalve_client_status_diagnostic(status)->code,
                 "system-layer-exact") == 0);
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);

    CHECK(realpath("/proc/self/exe", test_executable) != NULL);
    pipe_result = pipe(ready_pipe);
    CHECK(pipe_result == 0);
    if (pipe_result == 0) {
        unrelated_pid = fork();
    }
    CHECK(unrelated_pid >= 0);
    if (unrelated_pid == 0) {
        (void)close(ready_pipe[0]);
        (void)write(ready_pipe[1], "R", 1U);
        (void)close(ready_pipe[1]);
        for (;;) {
            (void)pause();
        }
    }
    if (unrelated_pid > 0) {
        (void)close(ready_pipe[1]);
        CHECK(read(ready_pipe[0], &ready_byte, 1U) == 1);
        (void)close(ready_pipe[0]);
        context = configured_context(
            fixture, fixture->helper, test_executable, fixture->policy,
            fixture->architectures, fixture->package_status, "x86_64");
        CHECK(context != NULL);
        CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
        CHECK(kvalve_client_probe(context, status)
              == KVALVE_CLIENT_ERR_UNRELATED_INSTANCE);
        CHECK(kvalve_client_status_classification(status)
              == KVALVE_CLIENT_INSTALL_UNRELATED_RUNNING);
        CHECK(strcmp(kvalve_client_status_diagnostic(status)->code,
                     "unrelated-steam-running") == 0);
        kvalve_client_status_free(status);
        kvalve_client_context_free(context);
        CHECK(kill(unrelated_pid, SIGTERM) == 0);
        CHECK(waitpid(unrelated_pid, &child_status, 0) == unrelated_pid);
        CHECK(WIFSIGNALED(child_status));
    } else {
        (void)close(ready_pipe[0]);
        (void)close(ready_pipe[1]);
    }

    context = configured_context(
        fixture, fixture->helper, KVALVE_TEST_FAKE_STEAM, fixture->policy,
        fixture->architectures, fixture->package_status, "x86_64");
    CHECK(context != NULL);
    CHECK(kvalve_test_context_proc_root(context, fixture->missing));
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status) == KVALVE_CLIENT_ERR_PERMISSION);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_PARTIAL);
    CHECK(strcmp(kvalve_client_status_diagnostic(status)->code,
                 "runtime-process-scan-unavailable") == 0);
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);

    CHECK(path_join(conflicting_path, fixture->root, "conflicting-policy"));
    CHECK(write_fixture(conflicting_path, conflicting_policy, 0600));
    context = configured_context(
        fixture, fixture->helper, KVALVE_TEST_FAKE_STEAM, conflicting_path,
        fixture->architectures, fixture->package_status, "x86_64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status) == KVALVE_CLIENT_ERR_CONFLICTING);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_CONFLICTING);
    CHECK(!kvalve_client_status_policy_verified(status));
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);
    (void)unlink(conflicting_path);

    CHECK(write_fixture(conflicting_path, truncated_digest_policy, 0600));
    context = configured_context(
        fixture, fixture->helper, KVALVE_TEST_FAKE_STEAM, conflicting_path,
        fixture->architectures, fixture->package_status, "x86_64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status) == KVALVE_CLIENT_ERR_CONFLICTING);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_CONFLICTING);
    CHECK(!kvalve_client_status_policy_verified(status));
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);
    (void)unlink(conflicting_path);

    context = configured_context(
        fixture, fixture->helper, KVALVE_TEST_FAKE_STEAM, fixture->policy,
        fixture->architectures, fixture->large_package_status, "x86_64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_status_package_installed(status));
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);

    context = configured_context(
        fixture, fixture->helper, fixture->missing, fixture->policy,
        fixture->missing, fixture->missing, "x86_64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status)
          == KVALVE_CLIENT_ERR_NOT_INSTALLED);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_ABSENT);
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);

    context = configured_context(
        fixture, fixture->missing, fixture->missing, fixture->missing,
        fixture->missing, fixture->missing, "x86_64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status)
          == KVALVE_CLIENT_ERR_NOT_INSTALLED);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_ABSENT);
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);

    context = configured_context(
        fixture, fixture->helper, fixture->missing, fixture->missing,
        fixture->missing, fixture->missing, "x86_64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status)
          == KVALVE_CLIENT_ERR_INSTALL_REQUIRED);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_PARTIAL);
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);

    context = configured_context(
        fixture, fixture->helper, fixture->missing, fixture->helper,
        fixture->missing, fixture->missing, "x86_64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status)
          == KVALVE_CLIENT_ERR_CONFLICTING);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_CONFLICTING);
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);

    context = configured_context(
        fixture, fixture->missing, fixture->missing, fixture->missing,
        fixture->missing, fixture->missing, "aarch64");
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status)
          == KVALVE_CLIENT_ERR_UNSUPPORTED);
    CHECK(kvalve_client_status_classification(status)
          == KVALVE_CLIENT_INSTALL_UNSUPPORTED_ARCHITECTURE);
    kvalve_client_status_free(status);
    kvalve_client_context_free(context);
}

static void
test_install_boundary(const struct fixture *fixture)
{
    kvalve_client_context *context;
    kvalve_client_operation *operation = (kvalve_client_operation *)fixture;
    char output[4096];
    int descriptors[2] = {-1, -1};
    int pipe_result;
    ssize_t length;
    context = configured_context(
        fixture, fixture->helper, KVALVE_TEST_FAKE_STEAM, fixture->policy,
        fixture->architectures, fixture->package_status, "x86_64");
    pipe_result = pipe(descriptors);
    CHECK(pipe_result == 0);
    if (pipe_result != 0) {
        kvalve_client_context_free(context);
        return;
    }
    CHECK(kvalve_client_plan_install(context, descriptors[1])
          == KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED);
    (void)close(descriptors[1]);
    length = read(descriptors[0], output, sizeof(output) - 1U);
    CHECK(length > 0);
    if (length > 0) {
        output[(size_t)length] = '\0';
        CHECK(strstr(output, "kilix.install.license/v1") != NULL);
        CHECK(strstr(output, "kilix.install.authorization/v2") != NULL);
        CHECK(strstr(output, "combined_confirmation_allowed\": false") != NULL);
        CHECK(strstr(output, "mutation_authorized\": false") != NULL);
        CHECK(strstr(output, "\"normal_states\": [\"PROBED\"") != NULL);
        CHECK(strstr(output, "\"terminal_outcomes\": [\"DEFERRED_LICENSE\"")
              != NULL);
        CHECK(strstr(output, "\"client_update_states\": [\"STARTING\"")
              != NULL);
    }
    (void)close(descriptors[0]);
    CHECK(kvalve_client_request_install(context, &operation)
          == KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED);
    CHECK(operation == NULL);
    CHECK(strcmp(kvalve_client_context_diagnostic(context)->code,
                 "pre-mutation-authority-unavailable") == 0);
    kvalve_client_context_free(context);
}

static void
test_helper_descriptor_boundary(const struct fixture *fixture)
{
    char helper[PATH_MAX];
    char script[256];
    int descriptors[2] = {-1, -1};
    int amount;
    int pipe_result;
    kvalve_client_context *context = NULL;
    kvalve_client_status *status = NULL;

    pipe_result = pipe(descriptors);
    CHECK(pipe_result == 0);
    if (pipe_result != 0) {
        return;
    }
    CHECK(path_join(helper, fixture->root, "descriptor-helper"));
    amount = snprintf(
        script, sizeof(script),
        "#!/bin/sh\n[ \"${1:-}\" = --verify ] && "
        "[ ! -e /proc/self/fd/%d ]\n",
        descriptors[0]);
    CHECK(amount >= 0 && (size_t)amount < sizeof(script));
    CHECK(write_fixture(helper, script, 0700));
    context = configured_context(
        fixture, helper, KVALVE_TEST_FAKE_STEAM, fixture->policy,
        fixture->architectures, fixture->package_status, "x86_64");
    CHECK(context != NULL);
    CHECK(kvalve_client_status_create(&status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_probe(context, status) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_status_helper_verified(status));

    kvalve_client_status_free(status);
    kvalve_client_context_free(context);
    (void)close(descriptors[0]);
    (void)close(descriptors[1]);
    (void)unlink(helper);
}

static void
test_state_names(void)
{
    static const char *const install_states[] = {
        "PROBED", "PLAN_FROZEN", "LICENSE_BOUND", "TRUST_BOUND",
        "PRIVILEGE_GRANTED", "SYSTEM_MUTATING", "SYSTEM_VERIFIED",
        "CLIENT_BOOTSTRAPPING", "CLIENT_READY"
    };
    static const char *const outcomes[] = {
        "DEFERRED_LICENSE", "DEFERRED_TRUST", "UNSUPPORTED",
        "CONFLICTING_SYSTEM_STATE", "RECOVERED_NO_CHANGE", "REPAIR_REQUIRED"
    };
    static const char *const updates[] = {
        "STARTING", "CHECKING", "DOWNLOADING", "APPLYING", "REEXECUTING",
        "MAPPING", "READY"
    };
    static const char *const sessions[] = {
        "TAB_ALLOCATED", "DISPLAY_STARTING", "DISPLAY_READY",
        "CLIENT_STARTING", "CLIENT_UPDATING", "LIBRARY_READY", "GAME_ACTIVE",
        "STOPPING", "CLOSED", "FAILED"
    };
    size_t index;
    for (index = 0U; index < sizeof(install_states) / sizeof(install_states[0]);
         ++index) {
        CHECK(strcmp(kvalve_client_install_state_name(
                         (kvalve_client_install_state)index),
                     install_states[index]) == 0);
    }
    for (index = 0U; index < sizeof(outcomes) / sizeof(outcomes[0]); ++index) {
        CHECK(strcmp(kvalve_client_install_outcome_name(
                         (kvalve_client_install_outcome)index),
                     outcomes[index]) == 0);
    }
    for (index = 0U; index < sizeof(updates) / sizeof(updates[0]); ++index) {
        CHECK(strcmp(kvalve_client_update_state_name(
                         (kvalve_client_update_state)index),
                     updates[index]) == 0);
    }
    for (index = 0U; index < sizeof(sessions) / sizeof(sessions[0]); ++index) {
        CHECK(strcmp(kvalve_client_session_state_name(
                         (kvalve_client_session_state)index),
                     sessions[index]) == 0);
    }
}

static void
test_operations(void)
{
    kvalve_client_operation *operation = NULL;
    kvalve_client_operation_state state = KVALVE_CLIENT_OPERATION_PENDING;
    kvalve_client_result poll_result = KVALVE_CLIENT_OK;
    struct pollfd readiness;
    unsigned attempts;
    CHECK(kvalve_test_operation_start(25U, 1000U, &operation)
          == KVALVE_CLIENT_OK);
    readiness.fd = kvalve_client_operation_fd(operation);
    readiness.events = POLLIN;
    readiness.revents = 0;
    CHECK(readiness.fd >= 0 && poll(&readiness, 1U, 2000) > 0);
    for (attempts = 0U; attempts < 50U; ++attempts) {
        poll_result = kvalve_client_operation_poll(operation, &state);
        if (poll_result != KVALVE_CLIENT_OK
                || state == KVALVE_CLIENT_OPERATION_SUCCEEDED) {
            break;
        }
        pause_ms(2U);
    }
    CHECK(poll_result == KVALVE_CLIENT_OK);
    CHECK(state == KVALVE_CLIENT_OPERATION_SUCCEEDED);
    kvalve_client_operation_free(operation);

    operation = NULL;
    CHECK(kvalve_test_operation_start(1000U, 2000U, &operation)
          == KVALVE_CLIENT_OK);
    CHECK(operation != NULL && operation->pid > 1
          && operation->process_group == operation->pid
          && getpgid(operation->pid) == operation->process_group);
    CHECK(kvalve_client_operation_cancel(operation) == KVALVE_CLIENT_OK);
    CHECK(kvalve_client_operation_poll(operation, &state) == KVALVE_CLIENT_OK);
    CHECK(state == KVALVE_CLIENT_OPERATION_CANCELLED);
    kvalve_client_operation_free(operation);

    operation = NULL;
    CHECK(kvalve_test_operation_start(1000U, 20U, &operation)
          == KVALVE_CLIENT_OK);
    pause_ms(30U);
    CHECK(kvalve_client_operation_poll(operation, &state)
          == KVALVE_CLIENT_ERR_TIMEOUT);
    CHECK(state == KVALVE_CLIENT_OPERATION_FAILED);
    kvalve_client_operation_free(operation);
}

static void
test_sessions(struct fixture *fixture)
{
    kvalve_client_context *context;
    kvalve_client_session *session = (kvalve_client_session *)fixture;
    char state_directory[PATH_MAX];
    context = configured_context(
        fixture, fixture->helper, KVALVE_TEST_FAKE_STEAM, fixture->policy,
        fixture->architectures, fixture->package_status, "x86_64");
    CHECK(context != NULL);
    CHECK(path_join(state_directory, fixture->session_home, "valve-client"));
    CHECK(kvalve_client_session_start(context, fixture->endpoint, &session)
          == KVALVE_CLIENT_ERR_DISPLAY);
    CHECK(session == NULL);
    CHECK(strcmp(kvalve_client_context_diagnostic(context)->code,
                 "steam-session-profile-unavailable") == 0);
    CHECK(access(state_directory, F_OK) != 0 && errno == ENOENT);
    kvalve_client_context_free(context);
}

int
main(void)
{
    struct fixture fixture;
    if (!create_fixture(&fixture)) {
        (void)fprintf(stderr, "could not create private test fixture\n");
        return 70;
    }
    /* Both of these are checked with the library's own rule rather than a
     * restatement of it, so the test and kvalve_secure_file cannot disagree
     * about what "secure" means.  Checking them here turns an environment
     * problem into one named line instead of nineteen failed assertions
     * scattered across the probe, install and session suites. */
    if (!kvalve_secure_file(fixture.root, S_IFDIR, geteuid(), false, false)) {
        explain_insecure("the fixture directory", fixture.root);
        remove_fixture(&fixture);
        return 70;
    }
    if (!kvalve_secure_file(KVALVE_TEST_FAKE_STEAM, S_IFREG, geteuid(), true,
                            false)) {
        explain_insecure("the checkout holding the fixture executable",
                         KVALVE_TEST_FAKE_STEAM);
        remove_fixture(&fixture);
        return 70;
    }
    test_probe(&fixture);
    test_helper_descriptor_boundary(&fixture);
    test_install_boundary(&fixture);
    test_state_names();
    test_operations();
    test_sessions(&fixture);
    remove_fixture(&fixture);
    (void)printf("kilix-valve-client: passed %u/%u checks\n", passed, checks);
    return passed == checks ? 0 : 1;
}
