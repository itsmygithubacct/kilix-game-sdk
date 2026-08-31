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
/* Wide enough that a realistic filesystem path fits INSIDE a sentence about
 * it. The untrusted-path diagnostic names the offending directory, and a
 * directory long enough to be truncated is exactly the one an operator cannot
 * identify from a prefix. The public view exposes const char *, so this bound
 * is internal. */
#define KVALVE_DIAGNOSTIC_SUMMARY_CAP 512
#define KVALVE_DIAGNOSTIC_PATH_SHOWN_CAP 256
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

/* Why a path was refused as trusted input.
 *
 * kvalve_secure_file used to answer only true or false, so every caller that
 * wanted to say something about a refusal had to guess. The probe guessed
 * "conflicting": a Steam path exists but does not match trusted packaged
 * state. That is right when the bytes differ and wrong when the bytes are
 * perfect and the DIRECTORY above them is world-writable -- the file was never
 * read, nothing was compared, and there is no conflict to report. Callers can
 * now distinguish the two, and name the component that failed.
 */
enum kvalve_trust_reason {
    KVALVE_TRUST_OK = 0,
    KVALVE_TRUST_PATH_INVALID,
    KVALVE_TRUST_ABSENT,
    KVALVE_TRUST_WRONG_TYPE,
    KVALVE_TRUST_OWNER,
    KVALVE_TRUST_WRITABLE,
    KVALVE_TRUST_NOT_EXECUTABLE,
    KVALVE_TRUST_ANCESTOR_UNREADABLE,
    KVALVE_TRUST_ANCESTOR_NOT_DIRECTORY,
    KVALVE_TRUST_ANCESTOR_OWNER,
    KVALVE_TRUST_ANCESTOR_WRITABLE
};

struct kvalve_trust_report {
    enum kvalve_trust_reason reason;
    /* The exact component that failed the rule: the subject path itself for a
     * KVALVE_TRUST_* reason, or the offending directory for an ANCESTOR one. */
    char path[PATH_MAX];
    unsigned mode;
    uid_t owner;
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
bool kvalve_secure_file_reported(const char *path, mode_t kind,
                                 uid_t trusted_uid, bool executable,
                                 bool require_root_owner,
                                 struct kvalve_trust_report *report);
bool kvalve_secure_launcher(const kvalve_client_context *context);
bool kvalve_secure_launcher_reported(const kvalve_client_context *context,
                                     struct kvalve_trust_report *report);
bool kvalve_trust_reason_is_ancestry(enum kvalve_trust_reason reason);
const char *kvalve_trust_reason_name(enum kvalve_trust_reason reason);
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
