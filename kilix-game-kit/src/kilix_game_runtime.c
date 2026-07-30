#include "kilix_game_runtime.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static kilix_game_signal_scope *volatile active_signal_scope;

static const int handled_signals[4] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};

static void request_stop_signal(int signal_number)
{
    kilix_game_signal_scope *scope = active_signal_scope;
    if (!scope) return;
    scope->signal_number = (sig_atomic_t)signal_number;
    scope->stop_requested = 1;
}

bool kilix_game_signals_install(kilix_game_signal_scope *scope)
{
    struct sigaction action;
    struct sigaction ignore;
    size_t installed = 0u;
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
    scope->installed = true;
    return true;
rollback:
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
            failed = true;
            goto done;
        }
        host->terminal_started = true;
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
    if (host->terminal_started) {
        kittyts_stop(&host->terminal);
        host->terminal_started = false;
    }
    kilix_game_signals_restore(&host->signals);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
