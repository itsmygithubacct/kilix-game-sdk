#include "internal.h"

#include <stdlib.h>

/*
 * F123's OD-24-approved v1 host is compositor-wide.  It does not expose the
 * tab-private Xwayland/Xauthority identity or owned descendant scope required
 * by F102.  Keep the public surface present for consumers, but keep every
 * lifecycle action inert until the owner returns the named steam-v1 contract.
 * In particular, this file deliberately contains no shared-display fallback,
 * launcher exec, process-name signal, or guessed scope implementation.
 */

kvalve_client_result
kvalve_client_session_start(kvalve_client_context *context,
                            const char *display_endpoint,
                            kvalve_client_session **out)
{
    if (context == NULL || display_endpoint == NULL || out == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    *out = NULL;
    kvalve_diag_set(&context->diagnostic, KVALVE_CLIENT_ERR_DISPLAY,
                    "steam-session-profile-unavailable",
                    "The required steam-v1 provider is 0/1; no launcher ran.",
                    true);
    return KVALVE_CLIENT_ERR_DISPLAY;
}

kvalve_client_result
kvalve_client_session_poll(kvalve_client_session *session,
                           kvalve_client_session_state *state)
{
    if (session == NULL || state == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    *state = session->state;
    return session->state == KVALVE_CLIENT_SESSION_FAILED
        ? session->diagnostic.view.result : KVALVE_CLIENT_OK;
}

kvalve_client_result
kvalve_client_session_stop(kvalve_client_session *session,
                           unsigned timeout_ms)
{
    (void)timeout_ms;
    if (session == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    if (session->state == KVALVE_CLIENT_SESSION_CLOSED) {
        return KVALVE_CLIENT_OK;
    }
    session->state = KVALVE_CLIENT_SESSION_FAILED;
    kvalve_diag_set(&session->diagnostic, KVALVE_CLIENT_ERR_DISPLAY,
                    "steam-session-profile-unavailable",
                    "The required steam-v1 provider is 0/1; no scope was stopped.",
                    true);
    return KVALVE_CLIENT_ERR_DISPLAY;
}

void
kvalve_client_session_free(kvalve_client_session *session)
{
    if (session != NULL) {
        free(session);
    }
}
