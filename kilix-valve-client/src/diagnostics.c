#include "internal.h"

#include <stdio.h>
#include <string.h>

void
kvalve_diag_set(struct kvalve_diagnostic_storage *storage,
                kvalve_client_result result, const char *code,
                const char *summary, bool retryable)
{
    if (storage == NULL) {
        return;
    }
    (void)snprintf(storage->code, sizeof(storage->code), "%s",
                   code != NULL ? code : "invalid-diagnostic");
    (void)snprintf(storage->summary, sizeof(storage->summary), "%s",
                   summary != NULL ? summary : "No diagnostic is available.");
    storage->view.schema_version = KVALVE_CLIENT_DIAGNOSTIC_SCHEMA;
    storage->view.result = result;
    storage->view.code = storage->code;
    storage->view.summary = storage->summary;
    storage->view.retryable = retryable;
}

void
kvalve_diag_copy(struct kvalve_diagnostic_storage *destination,
                 const struct kvalve_diagnostic_storage *source)
{
    if (destination == NULL || source == NULL) {
        return;
    }
    kvalve_diag_set(destination, source->view.result, source->code,
                    source->summary, source->view.retryable);
}

const char *
kvalve_client_result_name(kvalve_client_result result)
{
    switch (result) {
    case KVALVE_CLIENT_OK: return "ok";
    case KVALVE_CLIENT_ERR_NOT_INSTALLED: return "not-installed";
    case KVALVE_CLIENT_ERR_INSTALL_REQUIRED: return "install-required";
    case KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED:
        return "authorization-required";
    case KVALVE_CLIENT_ERR_UNSUPPORTED: return "unsupported";
    case KVALVE_CLIENT_ERR_CONFLICTING: return "conflicting";
    case KVALVE_CLIENT_ERR_BUSY: return "busy";
    case KVALVE_CLIENT_ERR_UNRELATED_INSTANCE: return "unrelated-instance";
    case KVALVE_CLIENT_ERR_PERMISSION: return "permission";
    case KVALVE_CLIENT_ERR_DISPLAY: return "display";
    case KVALVE_CLIENT_ERR_RUNTIME: return "runtime";
    case KVALVE_CLIENT_ERR_TIMEOUT: return "timeout";
    case KVALVE_CLIENT_ERR_IO: return "io";
    case KVALVE_CLIENT_ERR_INVALID: return "invalid";
    }
    return "invalid";
}

const char *
kvalve_client_install_classification_name(
    kvalve_client_install_classification classification)
{
    switch (classification) {
    case KVALVE_CLIENT_INSTALL_UNKNOWN: return "unknown";
    case KVALVE_CLIENT_INSTALL_ABSENT: return "absent";
    case KVALVE_CLIENT_INSTALL_EXACT: return "exact";
    case KVALVE_CLIENT_INSTALL_PARTIAL: return "partial";
    case KVALVE_CLIENT_INSTALL_CONFLICTING: return "conflicting";
    case KVALVE_CLIENT_INSTALL_UNSUPPORTED_ARCHITECTURE:
        return "unsupported-architecture";
    case KVALVE_CLIENT_INSTALL_UNRELATED_RUNNING:
        return "unrelated-running";
    }
    return "unknown";
}

const char *
kvalve_client_install_state_name(kvalve_client_install_state state)
{
    switch (state) {
    case KVALVE_CLIENT_INSTALL_STATE_PROBED: return "PROBED";
    case KVALVE_CLIENT_INSTALL_STATE_PLAN_FROZEN: return "PLAN_FROZEN";
    case KVALVE_CLIENT_INSTALL_STATE_LICENSE_BOUND: return "LICENSE_BOUND";
    case KVALVE_CLIENT_INSTALL_STATE_TRUST_BOUND: return "TRUST_BOUND";
    case KVALVE_CLIENT_INSTALL_STATE_PRIVILEGE_GRANTED:
        return "PRIVILEGE_GRANTED";
    case KVALVE_CLIENT_INSTALL_STATE_SYSTEM_MUTATING: return "SYSTEM_MUTATING";
    case KVALVE_CLIENT_INSTALL_STATE_SYSTEM_VERIFIED: return "SYSTEM_VERIFIED";
    case KVALVE_CLIENT_INSTALL_STATE_CLIENT_BOOTSTRAPPING:
        return "CLIENT_BOOTSTRAPPING";
    case KVALVE_CLIENT_INSTALL_STATE_CLIENT_READY: return "CLIENT_READY";
    }
    return "UNKNOWN";
}

const char *
kvalve_client_install_outcome_name(kvalve_client_install_outcome outcome)
{
    switch (outcome) {
    case KVALVE_CLIENT_INSTALL_OUTCOME_DEFERRED_LICENSE:
        return "DEFERRED_LICENSE";
    case KVALVE_CLIENT_INSTALL_OUTCOME_DEFERRED_TRUST:
        return "DEFERRED_TRUST";
    case KVALVE_CLIENT_INSTALL_OUTCOME_UNSUPPORTED: return "UNSUPPORTED";
    case KVALVE_CLIENT_INSTALL_OUTCOME_CONFLICTING_SYSTEM_STATE:
        return "CONFLICTING_SYSTEM_STATE";
    case KVALVE_CLIENT_INSTALL_OUTCOME_RECOVERED_NO_CHANGE:
        return "RECOVERED_NO_CHANGE";
    case KVALVE_CLIENT_INSTALL_OUTCOME_REPAIR_REQUIRED:
        return "REPAIR_REQUIRED";
    }
    return "UNKNOWN";
}

const char *
kvalve_client_update_state_name(kvalve_client_update_state state)
{
    switch (state) {
    case KVALVE_CLIENT_UPDATE_STARTING: return "STARTING";
    case KVALVE_CLIENT_UPDATE_CHECKING: return "CHECKING";
    case KVALVE_CLIENT_UPDATE_DOWNLOADING: return "DOWNLOADING";
    case KVALVE_CLIENT_UPDATE_APPLYING: return "APPLYING";
    case KVALVE_CLIENT_UPDATE_REEXECUTING: return "REEXECUTING";
    case KVALVE_CLIENT_UPDATE_MAPPING: return "MAPPING";
    case KVALVE_CLIENT_UPDATE_READY: return "READY";
    }
    return "UNKNOWN";
}

const char *
kvalve_client_session_state_name(kvalve_client_session_state state)
{
    switch (state) {
    case KVALVE_CLIENT_SESSION_TAB_ALLOCATED: return "TAB_ALLOCATED";
    case KVALVE_CLIENT_SESSION_DISPLAY_STARTING: return "DISPLAY_STARTING";
    case KVALVE_CLIENT_SESSION_DISPLAY_READY: return "DISPLAY_READY";
    case KVALVE_CLIENT_SESSION_CLIENT_STARTING: return "CLIENT_STARTING";
    case KVALVE_CLIENT_SESSION_CLIENT_UPDATING: return "CLIENT_UPDATING";
    case KVALVE_CLIENT_SESSION_LIBRARY_READY: return "LIBRARY_READY";
    case KVALVE_CLIENT_SESSION_GAME_ACTIVE: return "GAME_ACTIVE";
    case KVALVE_CLIENT_SESSION_STOPPING: return "STOPPING";
    case KVALVE_CLIENT_SESSION_CLOSED: return "CLOSED";
    case KVALVE_CLIENT_SESSION_FAILED: return "FAILED";
    }
    return "UNKNOWN";
}

const kvalve_client_diagnostic *
kvalve_client_context_diagnostic(const kvalve_client_context *context)
{
    return context != NULL ? &context->diagnostic.view : NULL;
}

const kvalve_client_diagnostic *
kvalve_client_status_diagnostic(const kvalve_client_status *status)
{
    return status != NULL ? &status->diagnostic.view : NULL;
}

const kvalve_client_diagnostic *
kvalve_client_operation_diagnostic(const kvalve_client_operation *operation)
{
    return operation != NULL ? &operation->diagnostic.view : NULL;
}

const kvalve_client_diagnostic *
kvalve_client_session_diagnostic(const kvalve_client_session *session)
{
    return session != NULL ? &session->diagnostic.view : NULL;
}
