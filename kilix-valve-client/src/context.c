#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

uint64_t
kvalve_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000)
        + (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

bool
kvalve_copy_path(char destination[PATH_MAX], const char *source)
{
    size_t length;
    if (destination == NULL || source == NULL) {
        return false;
    }
    length = strlen(source);
    if (length == 0U || length >= (size_t)PATH_MAX) {
        return false;
    }
    (void)memcpy(destination, source, length + 1U);
    return true;
}

kvalve_client_result
kvalve_client_context_create(kvalve_client_context **out)
{
    kvalve_client_context *context;
    struct utsname system_name;
    if (out == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    *out = NULL;
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        return KVALVE_CLIENT_ERR_IO;
    }
    if (!kvalve_copy_path(context->helper, KVALVE_DEFAULT_HELPER)
            || !kvalve_copy_path(context->launcher, KVALVE_DEFAULT_LAUNCHER)
            || !kvalve_copy_path(context->policy, KVALVE_DEFAULT_POLICY)
            || !kvalve_copy_path(context->dpkg_arch, KVALVE_DEFAULT_DPKG_ARCH)
            || !kvalve_copy_path(context->dpkg_status,
                                 KVALVE_DEFAULT_DPKG_STATUS)
            || !kvalve_copy_path(context->proc_root,
                                 KVALVE_DEFAULT_PROC_ROOT)) {
        free(context);
        return KVALVE_CLIENT_ERR_INVALID;
    }
    if (uname(&system_name) == 0) {
        (void)snprintf(context->machine, sizeof(context->machine), "%.31s",
                       system_name.machine);
    } else {
        (void)snprintf(context->machine, sizeof(context->machine), "%s",
                       "unknown");
    }
    context->trusted_uid = 0U;
    context->require_root_owner = true;
    kvalve_diag_set(&context->diagnostic, KVALVE_CLIENT_OK, "context-ready",
                    "The fixed client policy is ready for a read-only probe.",
                    false);
    *out = context;
    return KVALVE_CLIENT_OK;
}

void
kvalve_client_context_free(kvalve_client_context *context)
{
    if (context != NULL) {
        (void)memset(context, 0, sizeof(*context));
        free(context);
    }
}

kvalve_client_result
kvalve_client_status_create(kvalve_client_status **out)
{
    kvalve_client_status *status;
    if (out == NULL) {
        return KVALVE_CLIENT_ERR_INVALID;
    }
    *out = NULL;
    status = calloc(1U, sizeof(*status));
    if (status == NULL) {
        return KVALVE_CLIENT_ERR_IO;
    }
    status->classification = KVALVE_CLIENT_INSTALL_UNKNOWN;
    kvalve_diag_set(&status->diagnostic, KVALVE_CLIENT_OK, "not-probed",
                    "No read-only system probe has run yet.", false);
    *out = status;
    return KVALVE_CLIENT_OK;
}

void
kvalve_client_status_free(kvalve_client_status *status)
{
    free(status);
}

kvalve_client_install_classification
kvalve_client_status_classification(const kvalve_client_status *status)
{
    return status != NULL ? status->classification
                          : KVALVE_CLIENT_INSTALL_UNKNOWN;
}

bool
kvalve_client_status_helper_verified(const kvalve_client_status *status)
{
    return status != NULL && status->helper_verified;
}

bool
kvalve_client_status_policy_verified(const kvalve_client_status *status)
{
    return status != NULL && status->policy_verified;
}

bool
kvalve_client_status_i386_enabled(const kvalve_client_status *status)
{
    return status != NULL && status->i386_enabled;
}

bool
kvalve_client_status_package_installed(const kvalve_client_status *status)
{
    return status != NULL && status->package_installed;
}

bool
kvalve_client_status_launcher_verified(const kvalve_client_status *status)
{
    return status != NULL && status->launcher_verified;
}

#ifdef KVALVE_CLIENT_TESTING
bool
kvalve_test_context_paths(kvalve_client_context *context,
                          const char *helper, const char *launcher,
                          const char *policy, const char *dpkg_arch,
                          const char *dpkg_status, const char *machine)
{
    if (context == NULL
            || !kvalve_copy_path(context->helper, helper)
            || !kvalve_copy_path(context->launcher, launcher)
            || !kvalve_copy_path(context->policy, policy)
            || !kvalve_copy_path(context->dpkg_arch, dpkg_arch)
            || !kvalve_copy_path(context->dpkg_status, dpkg_status)
            || machine == NULL || strlen(machine) >= sizeof(context->machine)) {
        return false;
    }
    (void)snprintf(context->machine, sizeof(context->machine), "%s", machine);
    context->trusted_uid = geteuid();
    context->require_root_owner = false;
    return true;
}

bool
kvalve_test_context_proc_root(kvalve_client_context *context,
                              const char *proc_root)
{
    return context != NULL && kvalve_copy_path(context->proc_root, proc_root);
}
#endif
