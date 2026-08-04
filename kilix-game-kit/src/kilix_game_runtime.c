#include "kilix_game_runtime.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static kilix_game_signal_scope *volatile active_signal_scope;
static kittyts_session *volatile emergency_terminal;

static const int handled_signals[4] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
static const int crash_signals[5] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE,
                                     SIGILL};

static void request_stop_signal(int signal_number)
{
    kilix_game_signal_scope *scope = active_signal_scope;
    if (!scope) return;
    scope->signal_number = (sig_atomic_t)signal_number;
    scope->stop_requested = 1;
}

static void request_suspend_signal(int signal_number)
{
    kilix_game_signal_scope *scope = active_signal_scope;

    (void)signal_number;
    if (!scope) return;
    scope->suspend_requested = 1;
}

/* Async-signal-safe by construction: kittyts_emergency_restore performs only
 * write(2)/tcsetattr(2) behind a test-and-set claim, and signal(2)/raise(2)
 * are on the POSIX async-signal-safe list. Re-raising with the default
 * disposition keeps the fatal signal (and any core dump) intact. */
static void crash_restore_signal(int signal_number)
{
    kittyts_session *terminal = emergency_terminal;

    if (terminal) kittyts_emergency_restore(terminal);
    (void)signal(signal_number, SIG_DFL);
    (void)raise(signal_number);
}

void kilix_game_signals_set_emergency_terminal(kittyts_session *terminal)
{
    emergency_terminal = terminal;
}

bool kilix_game_signals_install(kilix_game_signal_scope *scope)
{
    struct sigaction action;
    struct sigaction ignore;
    struct sigaction suspend;
    struct sigaction crash;
    size_t installed = 0u;
    size_t crashes = 0u;
    if (!scope || active_signal_scope) {
        errno = EBUSY;
        return false;
    }
    *scope = (kilix_game_signal_scope){0};
    active_signal_scope = scope;
    memset(&action, 0, sizeof action);
    action.sa_handler = request_stop_signal;
    if (sigemptyset(&action.sa_mask) != 0) {
        active_signal_scope = NULL;
        return false;
    }
    for (installed = 0u; installed < 4u; ++installed) {
        if (sigaction(handled_signals[installed], &action,
                      &scope->previous[installed]) != 0)
            goto rollback;
    }
    memset(&ignore, 0, sizeof ignore);
    ignore.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore.sa_mask) != 0 ||
        sigaction(SIGPIPE, &ignore, &scope->previous_pipe) != 0)
        goto rollback;
    scope->pipe_installed = true;
    memset(&suspend, 0, sizeof suspend);
    suspend.sa_handler = request_suspend_signal;
    if (sigemptyset(&suspend.sa_mask) != 0 ||
        sigaction(SIGTSTP, &suspend, &scope->previous_suspend) != 0)
        goto rollback;
    scope->suspend_installed = true;
    memset(&crash, 0, sizeof crash);
    crash.sa_handler = crash_restore_signal;
    if (sigemptyset(&crash.sa_mask) != 0)
        goto rollback;
    for (crashes = 0u; crashes < 5u; ++crashes) {
        if (sigaction(crash_signals[crashes], &crash,
                      &scope->previous_crash[crashes]) != 0)
            goto rollback;
    }
    scope->crash_installed = true;
    scope->installed = true;
    return true;
rollback:
    while (crashes > 0u) {
        --crashes;
        (void)sigaction(crash_signals[crashes],
                        &scope->previous_crash[crashes], NULL);
    }
    if (scope->suspend_installed)
        (void)sigaction(SIGTSTP, &scope->previous_suspend, NULL);
    if (scope->pipe_installed)
        (void)sigaction(SIGPIPE, &scope->previous_pipe, NULL);
    while (installed > 0u) {
        --installed;
        (void)sigaction(handled_signals[installed],
                        &scope->previous[installed], NULL);
    }
    if (active_signal_scope == scope) active_signal_scope = NULL;
    *scope = (kilix_game_signal_scope){0};
    return false;
}

void kilix_game_signals_restore(kilix_game_signal_scope *scope)
{
    size_t index;
    if (!scope || !scope->installed) return;
    if (active_signal_scope == scope) active_signal_scope = NULL;
    if (scope->crash_installed)
        for (index = 0u; index < 5u; ++index)
            (void)sigaction(crash_signals[index],
                            &scope->previous_crash[index], NULL);
    if (scope->suspend_installed)
        (void)sigaction(SIGTSTP, &scope->previous_suspend, NULL);
    if (scope->pipe_installed)
        (void)sigaction(SIGPIPE, &scope->previous_pipe, NULL);
    for (index = 0u; index < 4u; ++index)
        (void)sigaction(handled_signals[index], &scope->previous[index], NULL);
    *scope = (kilix_game_signal_scope){0};
}

bool kilix_game_signals_requested(const kilix_game_signal_scope *scope)
{
    return scope && scope->stop_requested != 0;
}

bool kilix_game_signals_suspend_requested(
    const kilix_game_signal_scope *scope)
{
    return scope && scope->suspend_requested != 0;
}

int kilix_game_signals_number(const kilix_game_signal_scope *scope)
{
    return scope ? (int)scope->signal_number : 0;
}

void kilix_game_host_options_init(kilix_game_host_options *options)
{
    if (!options) return;
    memset(options, 0, sizeof *options);
    options->input_fd = STDIN_FILENO;
    options->output_fd = STDOUT_FILENO;
    kittyts_options_init(&options->terminal);
    kilix_game_clock_options_init(&options->clock);
    options->idle_sleep_ns = INT64_C(2000000);
    options->max_frames = 0u;
    options->headless = false;
    options->install_signals = true;
}

void kilix_game_host_request_stop(kilix_game_host *host)
{
    if (host) host->stop_requested = true;
}

kittyts_session *kilix_game_host_terminal(kilix_game_host *host)
{
    return host ? &host->terminal : NULL;
}

bool kilix_game_event_letter(const kittyin_event *event, char lower_letter)
{
    unsigned char lower = (unsigned char)lower_letter;
    unsigned char upper;
    if (!event || event->kind != KITTYIN_EVENT_KEY ||
        lower < (unsigned char)'a' || lower > (unsigned char)'z') return false;
    upper = (unsigned char)(lower - (unsigned char)'a' + (unsigned char)'A');
    return kittykb_event_matches_key(&event->data.key, (uint32_t)lower) ||
           kittykb_event_matches_key(&event->data.key, (uint32_t)upper);
}

int kilix_game_host_run(kilix_game_host *host,
                        const kilix_game_host_options *options,
                        const kilix_game_host_callbacks *callbacks,
                        void *user)
{
    kilix_game_host_options defaults;
    const kilix_game_host_options *selected = options;
    bool callback_started = false;
    bool failed = false;
    int64_t now;
    if (!host || !callbacks) return EXIT_FAILURE;
    if (!selected) {
        kilix_game_host_options_init(&defaults);
        selected = &defaults;
    }
    if (selected->input_fd < 0 || selected->output_fd < 0 ||
        selected->idle_sleep_ns < 0 ||
        !kilix_game_clock_init(&host->clock, &selected->clock))
        return EXIT_FAILURE;
    *host = (kilix_game_host){0};
    kittyts_session_init(&host->terminal);
    if (!kilix_game_clock_init(&host->clock, &selected->clock))
        return EXIT_FAILURE;
    if (selected->install_signals &&
        !kilix_game_signals_install(&host->signals)) return EXIT_FAILURE;
    if (!selected->headless) {
        if (kittyts_start(&host->terminal, selected->input_fd,
                          selected->output_fd, &selected->terminal) != 0) {
            host->terminal_errno = errno;
            failed = true;
            goto done;
        }
        host->terminal_started = true;
        if (selected->install_signals)
            kilix_game_signals_set_emergency_terminal(&host->terminal);
    }
    callback_started = true;
    if (callbacks->start && !callbacks->start(host, user)) {
        failed = true;
        goto done;
    }
    now = kilix_game_monotonic_ns();
    if (now < 0) {
        failed = true;
        goto done;
    }
    kilix_game_clock_reset(&host->clock, now);
    host->running = true;
    while (!host->stop_requested &&
           !kilix_game_signals_requested(&host->signals)) {
        kilix_game_frame frame;
        uint32_t step;
        if (kilix_game_signals_suspend_requested(&host->signals)) {
            host->signals.suspend_requested = 0;
            if (host->terminal_started) {
                kilix_game_signals_set_emergency_terminal(NULL);
                kittyts_suspend(&host->terminal);
                host->terminal_started = false;
            }
            if (raise(SIGSTOP) != 0) {
                failed = true;
                break;
            }
            ++host->suspension_count;
            if (!selected->headless) {
                if (kittyts_start(
                        &host->terminal, selected->input_fd,
                        selected->output_fd, &selected->terminal) != 0) {
                    host->terminal_errno = errno;
                    failed = true;
                    break;
                }
                host->terminal_started = true;
                if (selected->install_signals)
                    kilix_game_signals_set_emergency_terminal(&host->terminal);
            }
            now = kilix_game_monotonic_ns();
            if (now < 0) {
                failed = true;
                break;
            }
            kilix_game_clock_reset(&host->clock, now);
        }
        if (!selected->headless) {
            kittyin_event event;
            if (kittyts_read_input(&host->terminal) < 0) {
                if (errno == EINTR ||
                    kilix_game_signals_requested(&host->signals)) continue;
                failed = true;
                break;
            }
            while (kittyts_next_event(&host->terminal, &event))
                if (callbacks->event) callbacks->event(host, user, &event);
            if (host->stop_requested ||
                kilix_game_signals_requested(&host->signals)) break;
        }
        now = kilix_game_monotonic_ns();
        if (now < 0) {
            failed = true;
            break;
        }
        frame = kilix_game_clock_advance(&host->clock, now);
        for (step = 0u; step < frame.steps; ++step) {
            if (callbacks->step &&
                !callbacks->step(host, user,
                                 kilix_game_clock_step_seconds(&host->clock))) {
                failed = true;
                break;
            }
        }
        if (failed) break;
        if (callbacks->render &&
            !callbacks->render(host, user, frame.alpha)) {
            failed = true;
            break;
        }
        ++host->frame_count;
        if (selected->max_frames != 0u &&
            host->frame_count >= selected->max_frames) break;
        if (selected->idle_sleep_ns > 0 &&
            (now > INT64_MAX - selected->idle_sleep_ns ||
             !kilix_game_sleep_until_ns(now + selected->idle_sleep_ns))) {
            failed = true;
            break;
        }
    }
done:
    host->running = false;
    if (callback_started && callbacks->stop) callbacks->stop(host, user);
    if (host->terminal_started ||
        host->terminal.framebuffer_suspended) {
        kittyts_stop(&host->terminal);
        host->terminal_started = false;
    }
    kilix_game_signals_set_emergency_terminal(NULL);
    kilix_game_signals_restore(&host->signals);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
