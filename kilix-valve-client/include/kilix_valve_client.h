#ifndef KILIX_VALVE_CLIENT_H
#define KILIX_VALVE_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#define KVALVE_CLIENT_VERSION_MAJOR 0
#define KVALVE_CLIENT_VERSION_MINOR 1
#define KVALVE_CLIENT_VERSION_PATCH 0
#define KVALVE_CLIENT_DIAGNOSTIC_SCHEMA 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kvalve_client_context kvalve_client_context;
typedef struct kvalve_client_operation kvalve_client_operation;
typedef struct kvalve_client_session kvalve_client_session;
typedef struct kvalve_client_status kvalve_client_status;

typedef enum {
    KVALVE_CLIENT_OK = 0,
    KVALVE_CLIENT_ERR_NOT_INSTALLED,
    KVALVE_CLIENT_ERR_INSTALL_REQUIRED,
    KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED,
    KVALVE_CLIENT_ERR_UNSUPPORTED,
    KVALVE_CLIENT_ERR_CONFLICTING,
    KVALVE_CLIENT_ERR_BUSY,
    KVALVE_CLIENT_ERR_UNRELATED_INSTANCE,
    KVALVE_CLIENT_ERR_PERMISSION,
    KVALVE_CLIENT_ERR_DISPLAY,
    KVALVE_CLIENT_ERR_RUNTIME,
    KVALVE_CLIENT_ERR_TIMEOUT,
    KVALVE_CLIENT_ERR_IO,
    KVALVE_CLIENT_ERR_INVALID
} kvalve_client_result;

typedef enum {
    KVALVE_CLIENT_INSTALL_UNKNOWN = 0,
    KVALVE_CLIENT_INSTALL_ABSENT,
    KVALVE_CLIENT_INSTALL_EXACT,
    KVALVE_CLIENT_INSTALL_PARTIAL,
    KVALVE_CLIENT_INSTALL_CONFLICTING,
    KVALVE_CLIENT_INSTALL_UNSUPPORTED_ARCHITECTURE,
    KVALVE_CLIENT_INSTALL_UNRELATED_RUNNING
} kvalve_client_install_classification;

typedef enum {
    KVALVE_CLIENT_OPERATION_PENDING = 0,
    KVALVE_CLIENT_OPERATION_RUNNING,
    KVALVE_CLIENT_OPERATION_RECOVERING,
    KVALVE_CLIENT_OPERATION_SUCCEEDED,
    KVALVE_CLIENT_OPERATION_CANCELLED,
    KVALVE_CLIENT_OPERATION_FAILED
} kvalve_client_operation_state;

typedef enum {
    KVALVE_CLIENT_INSTALL_STATE_PROBED = 0,
    KVALVE_CLIENT_INSTALL_STATE_PLAN_FROZEN,
    KVALVE_CLIENT_INSTALL_STATE_LICENSE_BOUND,
    KVALVE_CLIENT_INSTALL_STATE_TRUST_BOUND,
    KVALVE_CLIENT_INSTALL_STATE_PRIVILEGE_GRANTED,
    KVALVE_CLIENT_INSTALL_STATE_SYSTEM_MUTATING,
    KVALVE_CLIENT_INSTALL_STATE_SYSTEM_VERIFIED,
    KVALVE_CLIENT_INSTALL_STATE_CLIENT_BOOTSTRAPPING,
    KVALVE_CLIENT_INSTALL_STATE_CLIENT_READY
} kvalve_client_install_state;

typedef enum {
    KVALVE_CLIENT_INSTALL_OUTCOME_DEFERRED_LICENSE = 0,
    KVALVE_CLIENT_INSTALL_OUTCOME_DEFERRED_TRUST,
    KVALVE_CLIENT_INSTALL_OUTCOME_UNSUPPORTED,
    KVALVE_CLIENT_INSTALL_OUTCOME_CONFLICTING_SYSTEM_STATE,
    KVALVE_CLIENT_INSTALL_OUTCOME_RECOVERED_NO_CHANGE,
    KVALVE_CLIENT_INSTALL_OUTCOME_REPAIR_REQUIRED
} kvalve_client_install_outcome;

typedef enum {
    KVALVE_CLIENT_UPDATE_STARTING = 0,
    KVALVE_CLIENT_UPDATE_CHECKING,
    KVALVE_CLIENT_UPDATE_DOWNLOADING,
    KVALVE_CLIENT_UPDATE_APPLYING,
    KVALVE_CLIENT_UPDATE_REEXECUTING,
    KVALVE_CLIENT_UPDATE_MAPPING,
    KVALVE_CLIENT_UPDATE_READY
} kvalve_client_update_state;

typedef enum {
    KVALVE_CLIENT_SESSION_TAB_ALLOCATED = 0,
    KVALVE_CLIENT_SESSION_DISPLAY_STARTING,
    KVALVE_CLIENT_SESSION_DISPLAY_READY,
    KVALVE_CLIENT_SESSION_CLIENT_STARTING,
    KVALVE_CLIENT_SESSION_CLIENT_UPDATING,
    KVALVE_CLIENT_SESSION_LIBRARY_READY,
    KVALVE_CLIENT_SESSION_GAME_ACTIVE,
    KVALVE_CLIENT_SESSION_STOPPING,
    KVALVE_CLIENT_SESSION_CLOSED,
    KVALVE_CLIENT_SESSION_FAILED
} kvalve_client_session_state;

typedef struct {
    uint32_t schema_version;
    kvalve_client_result result;
    const char *code;
    const char *summary;
    bool retryable;
} kvalve_client_diagnostic;

kvalve_client_result kvalve_client_context_create(kvalve_client_context **out);
void kvalve_client_context_free(kvalve_client_context *context);
const kvalve_client_diagnostic *kvalve_client_context_diagnostic(
    const kvalve_client_context *context);

kvalve_client_result kvalve_client_status_create(kvalve_client_status **out);
void kvalve_client_status_free(kvalve_client_status *status);
kvalve_client_result kvalve_client_probe(kvalve_client_context *context,
                                         kvalve_client_status *status);
kvalve_client_install_classification kvalve_client_status_classification(
    const kvalve_client_status *status);
bool kvalve_client_status_helper_verified(const kvalve_client_status *status);
bool kvalve_client_status_policy_verified(const kvalve_client_status *status);
bool kvalve_client_status_i386_enabled(const kvalve_client_status *status);
bool kvalve_client_status_package_installed(const kvalve_client_status *status);
bool kvalve_client_status_launcher_verified(const kvalve_client_status *status);
const kvalve_client_diagnostic *kvalve_client_status_diagnostic(
    const kvalve_client_status *status);

kvalve_client_result kvalve_client_plan_install(kvalve_client_context *context,
                                                int output_fd);
kvalve_client_result kvalve_client_request_install(
    kvalve_client_context *context, kvalve_client_operation **out);
int kvalve_client_operation_fd(const kvalve_client_operation *operation);
kvalve_client_result kvalve_client_operation_poll(
    kvalve_client_operation *operation, kvalve_client_operation_state *state);
kvalve_client_result kvalve_client_operation_cancel(
    kvalve_client_operation *operation);
const kvalve_client_diagnostic *kvalve_client_operation_diagnostic(
    const kvalve_client_operation *operation);
void kvalve_client_operation_free(kvalve_client_operation *operation);

kvalve_client_result kvalve_client_session_start(
    kvalve_client_context *context, const char *display_endpoint,
    kvalve_client_session **out);
kvalve_client_result kvalve_client_session_poll(
    kvalve_client_session *session, kvalve_client_session_state *state);
kvalve_client_result kvalve_client_session_stop(kvalve_client_session *session,
                                                unsigned timeout_ms);
const kvalve_client_diagnostic *kvalve_client_session_diagnostic(
    const kvalve_client_session *session);
void kvalve_client_session_free(kvalve_client_session *session);

const char *kvalve_client_result_name(kvalve_client_result result);
const char *kvalve_client_install_classification_name(
    kvalve_client_install_classification classification);
const char *kvalve_client_install_state_name(kvalve_client_install_state state);
const char *kvalve_client_install_outcome_name(
    kvalve_client_install_outcome outcome);
const char *kvalve_client_update_state_name(kvalve_client_update_state state);
const char *kvalve_client_session_state_name(kvalve_client_session_state state);

#ifdef __cplusplus
}
#endif

#endif
