#ifndef KVALVE_CLIENT_INTERNAL_H
#define KVALVE_CLIENT_INTERNAL_H

#include "kilix_valve_client.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define KVALVE_DIAGNOSTIC_CODE_CAP 64
#define KVALVE_DIAGNOSTIC_SUMMARY_CAP 256
#define KVALVE_FILE_CAP 32768
#define KVALVE_DEFAULT_HELPER "/usr/libexec/plebian-os-steam-setup"
#define KVALVE_DEFAULT_LAUNCHER "/usr/bin/steam"
#define KVALVE_DEFAULT_LAUNCHER_TARGET "/usr/lib/steam/bin_steam.sh"
#define KVALVE_DEFAULT_POLICY "/usr/share/plebian-os/steam/policy-v1.manifest"
#define KVALVE_DEFAULT_DPKG_ARCH "/var/lib/dpkg/arch"
#define KVALVE_DEFAULT_DPKG_STATUS "/var/lib/dpkg/status"
#define KVALVE_DEFAULT_PROC_ROOT "/proc"

enum kvalve_process_scan_result {
    KVALVE_PROCESS_SCAN_CLEAR = 0,
    KVALVE_PROCESS_SCAN_FOUND,
    KVALVE_PROCESS_SCAN_UNAVAILABLE
};

struct kvalve_diagnostic_storage {
    kvalve_client_diagnostic view;
    char code[KVALVE_DIAGNOSTIC_CODE_CAP];
    char summary[KVALVE_DIAGNOSTIC_SUMMARY_CAP];
};

struct kvalve_client_context {
    char helper[PATH_MAX];
    char launcher[PATH_MAX];
    char policy[PATH_MAX];
    char dpkg_arch[PATH_MAX];
    char dpkg_status[PATH_MAX];
    char proc_root[PATH_MAX];
    char machine[32];
    uid_t trusted_uid;
    bool require_root_owner;
    struct kvalve_diagnostic_storage diagnostic;
};

struct kvalve_client_status {
    kvalve_client_install_classification classification;
    bool helper_verified;
    bool policy_verified;
    bool i386_enabled;
    bool package_installed;
    bool launcher_verified;
    struct kvalve_diagnostic_storage diagnostic;
};

struct kvalve_client_operation {
    pid_t pid;
    pid_t process_group;
    int event_fd;
    uint64_t deadline_ms;
    kvalve_client_operation_state state;
    bool reaped;
    struct kvalve_diagnostic_storage diagnostic;
};

struct kvalve_client_session {
    pid_t pid;
    pid_t process_group;
    uint64_t leader_start_time;
    int lock_fd;
    kvalve_client_session_state state;
    bool reaped;
    struct kvalve_diagnostic_storage diagnostic;
};

void kvalve_diag_set(struct kvalve_diagnostic_storage *storage,
                     kvalve_client_result result, const char *code,
                     const char *summary, bool retryable);
void kvalve_diag_copy(struct kvalve_diagnostic_storage *destination,
                      const struct kvalve_diagnostic_storage *source);
uint64_t kvalve_now_ms(void);
bool kvalve_copy_path(char destination[PATH_MAX], const char *source);
bool kvalve_read_bounded(const char *path, char *buffer, size_t capacity,
                         size_t *length);
bool kvalve_secure_file(const char *path, mode_t kind, uid_t trusted_uid,
                        bool executable, bool require_root_owner);
bool kvalve_secure_launcher(const kvalve_client_context *context);
bool kvalve_process_group_exists(pid_t process_group);
bool kvalve_process_identity_matches(pid_t pid, pid_t process_group,
                                     uint64_t start_time);
bool kvalve_process_start_time(pid_t pid, uint64_t *start_time);
kvalve_client_result kvalve_stop_process_group(
    pid_t pid, pid_t process_group, uint64_t start_time, unsigned timeout_ms,
    struct kvalve_diagnostic_storage *diagnostic);
enum kvalve_process_scan_result kvalve_scan_unrelated_launcher(
    const char *proc_root, const char *launcher, pid_t except_pid);

#ifdef KVALVE_CLIENT_TESTING
bool kvalve_test_context_paths(kvalve_client_context *context,
                               const char *helper, const char *launcher,
                               const char *policy, const char *dpkg_arch,
                               const char *dpkg_status, const char *machine);
bool kvalve_test_context_proc_root(kvalve_client_context *context,
                                   const char *proc_root);
kvalve_client_result kvalve_test_operation_start(
    unsigned runtime_ms, unsigned deadline_ms,
    kvalve_client_operation **out);
#endif

#endif
