#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char INSTALL_REQUIREMENTS[] =
    "{\n"
    "  \"schema\": \"kilix.valve.install-requirements/v1\",\n"
    "  \"mutation_authorized\": false,\n"
    "  \"helper_modes\": [\"install\", \"repair\", \"verify\"],\n"
    "  \"system_scope\": {\n"
    "    \"vendor\": \"Valve\",\n"
    "    \"standing_root_package_authority\": true,\n"
    "    \"dedicated_archive_and_keyring\": true,\n"
    "    \"package_pin_and_allowlist\": true,\n"
    "    \"foreign_architecture\": \"i386\",\n"
    "    \"future_system_updates_in_scope\": true,\n"
    "    \"i386_removal_may_be_blocked_by_other_packages\": true\n"
    "  },\n"
    "  \"decision_moments\": [\n"
    "    {\"purpose\": \"valve-terms\", \"schema\": \"kilix.install.license/v1\"},\n"
    "    {\"purpose\": \"valve-archive-and-i386\", \"schema\": \"kilix.install.authorization/v2\"}\n"
    "  ],\n"
    "  \"combined_confirmation_allowed\": false,\n"
    "  \"normal_states\": [\"PROBED\", \"PLAN_FROZEN\", \"LICENSE_BOUND\", \"TRUST_BOUND\", \"PRIVILEGE_GRANTED\", \"SYSTEM_MUTATING\", \"SYSTEM_VERIFIED\", \"CLIENT_BOOTSTRAPPING\", \"CLIENT_READY\"],\n"
    "  \"terminal_outcomes\": [\"DEFERRED_LICENSE\", \"DEFERRED_TRUST\", \"UNSUPPORTED\", \"CONFLICTING_SYSTEM_STATE\", \"RECOVERED_NO_CHANGE\", \"REPAIR_REQUIRED\"],\n"
    "  \"update_layers\": {\"system_launcher\": \"mediated-apt\", \"client_runtime\": \"unprivileged-valve-updater\", \"games_and_workshop_integration_owned\": false},\n"
    "  \"client_update_states\": [\"STARTING\", \"CHECKING\", \"DOWNLOADING\", \"APPLYING\", \"REEXECUTING\", \"MAPPING\", \"READY\"],\n"
    "  \"authority_state\": \"awaiting-f100-pre-mutation-mediator-contract\"\n"
    "}\n";

static bool
write_all(int descriptor, const char *bytes, size_t length)
{
    size_t written = 0U;
    while (written < length) {
        ssize_t amount = write(descriptor, bytes + written, length - written);
        if (amount > 0) {
            written += (size_t)amount;
        } else if (amount < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

kvalve_client_result
kvalve_client_plan_install(kvalve_client_context *context, int output_fd)
{
    if (context == NULL || output_fd < 0) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    if (!write_all(output_fd, INSTALL_REQUIREMENTS,
                   sizeof(INSTALL_REQUIREMENTS) - 1U)) {
        kvalve_diag_set(&context->diagnostic, KVALVE_CLIENT_ERR_IO,
                        "requirements-write-failed",
                        "The bounded install requirements could not be written.",
                        true);
        return KVALVE_CLIENT_ERR_IO;
    }
    kvalve_diag_set(&context->diagnostic,
                    KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED,
                    "trust-authorization-required",
                    "Valve archive and i386 authorization is a separate required decision.",
                    true);
    return KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED;
}

kvalve_client_result
kvalve_client_request_install(kvalve_client_context *context,
                              kvalve_client_operation **out)
{
    if (context == NULL || out == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    *out = NULL;
    kvalve_diag_set(&context->diagnostic,
                    KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED,
                    "pre-mutation-authority-unavailable",
                    "No validated pre-mutation authorization-v2 handle is available; no helper ran.",
                    true);
    return KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED;
}

int
kvalve_client_operation_fd(const kvalve_client_operation *operation)
{
    return operation != NULL ? operation->event_fd : -1;
}

kvalve_client_result
kvalve_client_operation_poll(kvalve_client_operation *operation,
                             kvalve_client_operation_state *state)
{
    int child_status;
    pid_t result;
    char discard[64];
    if (operation == NULL || state == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    if (operation->event_fd >= 0) {
        while (read(operation->event_fd, discard, sizeof(discard)) > 0) {
        }
    }
    if (!operation->reaped && operation->pid > 0) {
        result = waitpid(operation->pid, &child_status, WNOHANG);
        if (result == operation->pid) {
            operation->reaped = true;
            if (operation->state != KVALVE_CLIENT_OPERATION_CANCELLED) {
                if (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0) {
                    operation->state = KVALVE_CLIENT_OPERATION_SUCCEEDED;
                    kvalve_diag_set(&operation->diagnostic, KVALVE_CLIENT_OK,
                                    "operation-complete",
                                    "The owned operation completed.", false);
                } else {
                    operation->state = KVALVE_CLIENT_OPERATION_FAILED;
                    kvalve_diag_set(&operation->diagnostic,
                                    KVALVE_CLIENT_ERR_RUNTIME,
                                    "operation-failed",
                                    "The owned operation exited without success.",
                                    true);
                }
            }
        } else if (result < 0 && errno != EINTR) {
            operation->state = KVALVE_CLIENT_OPERATION_FAILED;
            operation->reaped = true;
            kvalve_diag_set(&operation->diagnostic, KVALVE_CLIENT_ERR_RUNTIME,
                            "operation-lost",
                            "The owned operation could no longer be supervised.",
                            false);
        }
    }
    if (!operation->reaped && operation->deadline_ms != 0U
            && kvalve_now_ms() >= operation->deadline_ms) {
        (void)kvalve_stop_process_group(
            operation->pid, operation->process_group, 0U, 250U,
            &operation->diagnostic);
        (void)waitpid(operation->pid, &child_status, 0);
        operation->reaped = true;
        operation->state = KVALVE_CLIENT_OPERATION_FAILED;
        kvalve_diag_set(&operation->diagnostic, KVALVE_CLIENT_ERR_TIMEOUT,
                        "operation-deadline",
                        "The owned operation reached its fixed deadline.", true);
        *state = operation->state;
        return KVALVE_CLIENT_ERR_TIMEOUT;
    }
    *state = operation->state;
    return operation->state == KVALVE_CLIENT_OPERATION_FAILED
        ? operation->diagnostic.view.result : KVALVE_CLIENT_OK;
}

kvalve_client_result
kvalve_client_operation_cancel(kvalve_client_operation *operation)
{
    int child_status;
    kvalve_client_result result;
    if (operation == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    if (operation->reaped
            || operation->state == KVALVE_CLIENT_OPERATION_SUCCEEDED
            || operation->state == KVALVE_CLIENT_OPERATION_CANCELLED) {
        return KVALVE_CLIENT_OK;
    }
    result = kvalve_stop_process_group(
        operation->pid, operation->process_group, 0U, 500U,
        &operation->diagnostic);
    if (result != KVALVE_CLIENT_OK) {
        return result;
    }
    while (waitpid(operation->pid, &child_status, 0) < 0 && errno == EINTR) {
    }
    operation->reaped = true;
    operation->state = KVALVE_CLIENT_OPERATION_CANCELLED;
    kvalve_diag_set(&operation->diagnostic, KVALVE_CLIENT_OK,
                    "operation-cancelled",
                    "The owned operation was cancelled.", false);
    return KVALVE_CLIENT_OK;
}

void
kvalve_client_operation_free(kvalve_client_operation *operation)
{
    if (operation == NULL) {
        return;
    }
    if (!operation->reaped) {
        (void)kvalve_client_operation_cancel(operation);
    }
    if (operation->event_fd >= 0) {
        (void)close(operation->event_fd);
    }
    free(operation);
}

#ifdef KVALVE_CLIENT_TESTING
kvalve_client_result
kvalve_test_operation_start(unsigned runtime_ms, unsigned deadline_ms,
                            kvalve_client_operation **out)
{
    int events[2];
    pid_t child;
    kvalve_client_operation *operation;
    if (out == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    *out = NULL;
    if (pipe2(events, O_CLOEXEC | O_NONBLOCK) != 0) {
        return KVALVE_CLIENT_ERR_IO;
    }
    child = fork();
    if (child < 0) {
        (void)close(events[0]);
        (void)close(events[1]);
        return KVALVE_CLIENT_ERR_IO;
    }
    if (child == 0) {
        struct timespec delay;
        char ready = 'R';
        (void)close(events[0]);
        if (setsid() < 0) {
            _exit(70);
        }
        delay.tv_sec = (time_t)(runtime_ms / 1000U);
        delay.tv_nsec = (long)(runtime_ms % 1000U) * 1000000L;
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        }
        (void)write(events[1], &ready, 1U);
        _exit(0);
    }
    (void)close(events[1]);
    operation = calloc(1U, sizeof(*operation));
    if (operation == NULL) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        (void)close(events[0]);
        return KVALVE_CLIENT_ERR_IO;
    }
    operation->pid = child;
    operation->process_group = child;
    operation->event_fd = events[0];
    operation->deadline_ms = kvalve_now_ms() + (uint64_t)deadline_ms;
    operation->state = KVALVE_CLIENT_OPERATION_RUNNING;
    kvalve_diag_set(&operation->diagnostic, KVALVE_CLIENT_OK,
                    "operation-running", "The owned operation is running.",
                    false);
    *out = operation;
    return KVALVE_CLIENT_OK;
}
#endif
