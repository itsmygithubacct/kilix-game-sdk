#include "kilix_valve_client.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
print_help(FILE *stream)
{
    (void)fprintf(stream,
        "usage: kilix-valve-client COMMAND\n"
        "\n"
        "Read-only commands:\n"
        "  status        classify the packaged Steam system layer\n"
        "  doctor        show a bounded diagnostic\n"
        "  plan-install  print separate license and trust requirements\n"
        "\n"
        "Mutation command:\n"
        "  install       request the fixed mediated installation\n");
}

static void
print_diagnostic(const kvalve_client_diagnostic *diagnostic)
{
    if (diagnostic == NULL) {
        return;
    }
    (void)printf("{\"schema_version\":%u,\"result\":\"%s\","
                 "\"code\":\"%s\",\"summary\":\"%s\","
                 "\"retryable\":%s}\n",
                 diagnostic->schema_version,
                 kvalve_client_result_name(diagnostic->result),
                 diagnostic->code, diagnostic->summary,
                 diagnostic->retryable ? "true" : "false");
}

static int
status_command(kvalve_client_context *context, bool doctor)
{
    kvalve_client_status *status = NULL;
    kvalve_client_result result;
    if (kvalve_client_status_create(&status) != KVALVE_CLIENT_OK) {
        return 70;
    }
    result = kvalve_client_probe(context, status);
    if (doctor) {
        print_diagnostic(kvalve_client_status_diagnostic(status));
    } else {
        (void)printf(
            "{\"classification\":\"%s\",\"helper_verified\":%s,"
            "\"policy_verified\":%s,\"i386_enabled\":%s,"
            "\"package_installed\":%s,\"launcher_verified\":%s}\n",
            kvalve_client_install_classification_name(
                kvalve_client_status_classification(status)),
            kvalve_client_status_helper_verified(status) ? "true" : "false",
            kvalve_client_status_policy_verified(status) ? "true" : "false",
            kvalve_client_status_i386_enabled(status) ? "true" : "false",
            kvalve_client_status_package_installed(status) ? "true" : "false",
            kvalve_client_status_launcher_verified(status) ? "true" : "false");
    }
    kvalve_client_status_free(status);
    return result == KVALVE_CLIENT_OK ? 0 : 3;
}

int
main(int argc, char **argv)
{
    kvalve_client_context *context = NULL;
    kvalve_client_operation *operation = NULL;
    kvalve_client_result result;
    int exit_status;
    if (argc != 2) {
        print_help(stderr);
        return 64;
    }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(stdout);
        return 0;
    }
    if (kvalve_client_context_create(&context) != KVALVE_CLIENT_OK) {
        return 70;
    }
    if (strcmp(argv[1], "status") == 0) {
        exit_status = status_command(context, false);
    } else if (strcmp(argv[1], "doctor") == 0) {
        exit_status = status_command(context, true);
    } else if (strcmp(argv[1], "plan-install") == 0) {
        result = kvalve_client_plan_install(context, STDOUT_FILENO);
        exit_status = result == KVALVE_CLIENT_OK ? 0 : 3;
    } else if (strcmp(argv[1], "install") == 0) {
        result = kvalve_client_request_install(context, &operation);
        print_diagnostic(kvalve_client_context_diagnostic(context));
        kvalve_client_operation_free(operation);
        exit_status = result == KVALVE_CLIENT_OK ? 0 : 3;
    } else {
        print_help(stderr);
        exit_status = 64;
    }
    kvalve_client_context_free(context);
    return exit_status;
}
