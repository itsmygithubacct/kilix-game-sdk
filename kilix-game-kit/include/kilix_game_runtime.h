#ifndef KILIX_GAME_RUNTIME_H
#define KILIX_GAME_RUNTIME_H

#include "kilix_game_loop.h"
#include "kitty_terminal_session.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kilix_game_signal_scope {
    struct sigaction previous[4];
    struct sigaction previous_pipe;
    struct sigaction previous_suspend;
    struct sigaction previous_crash[5];
    volatile sig_atomic_t stop_requested;
    volatile sig_atomic_t suspend_requested;
    volatile sig_atomic_t signal_number;
    bool installed;
    bool pipe_installed;
    bool suspend_installed;
    bool crash_installed;
} kilix_game_signal_scope;

/* One signal scope may be active per process. SIGINT, SIGTERM, SIGHUP, and
 * SIGQUIT request an orderly stop; SIGTSTP requests a terminal-safe
 * suspension; SIGPIPE is ignored while installed. Crash-class signals
 * (SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL) emergency-restore the registered
 * terminal with async-signal-safe calls only, then re-raise with the default
 * disposition so the process still dies with the right signal and can dump
 * core — without this, a crash in game code leaves the tty in raw mode and
 * the alternate screen. */
bool kilix_game_signals_install(kilix_game_signal_scope *scope);
void kilix_game_signals_restore(kilix_game_signal_scope *scope);
bool kilix_game_signals_requested(const kilix_game_signal_scope *scope);
bool kilix_game_signals_suspend_requested(
    const kilix_game_signal_scope *scope);
int kilix_game_signals_number(const kilix_game_signal_scope *scope);

/* Register (or clear, with NULL) the terminal the crash handler restores.
 * kilix_game_host_run maintains this automatically around terminal start,
 * suspend, and stop; call it directly only when driving a kittyts_session
 * without the host. */
void kilix_game_signals_set_emergency_terminal(kittyts_session *terminal);

typedef struct kilix_game_host kilix_game_host;

typedef struct kilix_game_host_options {
    int input_fd;
    int output_fd;
    kittyts_options terminal;
    kilix_game_clock_options clock;
    int64_t idle_sleep_ns;
    uint64_t max_frames;
    bool headless;
    bool install_signals;
} kilix_game_host_options;

typedef struct kilix_game_host_callbacks {
    bool (*start)(kilix_game_host *host, void *user);
    void (*event)(kilix_game_host *host, void *user,
                  const kittyin_event *event);
    bool (*step)(kilix_game_host *host, void *user, double step_seconds);
    bool (*render)(kilix_game_host *host, void *user, double alpha);
    void (*stop)(kilix_game_host *host, void *user);
} kilix_game_host_callbacks;

struct kilix_game_host {
    kittyts_session terminal;
    kilix_game_signal_scope signals;
    kilix_game_clock clock;
    uint64_t frame_count;
    uint64_t suspension_count;
    int terminal_errno;   /* errno of a failed terminal start, else 0 */
    bool terminal_started;
    bool running;
    bool stop_requested;
};

void kilix_game_host_options_init(kilix_game_host_options *options);

/* Runs a fixed-step terminal host and always unwinds callbacks, terminal
 * modes, and signal handlers in reverse order. Returns EXIT_SUCCESS/FAILURE. */
int kilix_game_host_run(kilix_game_host *host,
                        const kilix_game_host_options *options,
                        const kilix_game_host_callbacks *callbacks,
                        void *user);
void kilix_game_host_request_stop(kilix_game_host *host);
kittyts_session *kilix_game_host_terminal(kilix_game_host *host);

/* Case-insensitive ASCII shortcut matching for a complete input event. */
bool kilix_game_event_letter(const kittyin_event *event, char lower_letter);

#ifdef __cplusplus
}
#endif

#endif
