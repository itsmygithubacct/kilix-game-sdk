#include "kilix_game_loop.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

void kilix_game_clock_options_init(kilix_game_clock_options *options)
{
    if (options == NULL) return;
    options->step_ns = KILIX_GAME_NANOSECONDS_PER_SECOND / 60;
    options->max_frame_ns = KILIX_GAME_NANOSECONDS_PER_SECOND / 4;
    options->max_steps_per_frame = 8u;
}

bool kilix_game_clock_init(kilix_game_clock *clock,
                           const kilix_game_clock_options *options)
{
    kilix_game_clock_options defaults;
    const kilix_game_clock_options *selected = options;

    if (clock == NULL) return false;
    if (selected == NULL) {
        kilix_game_clock_options_init(&defaults);
        selected = &defaults;
    }
    (void)memset(clock, 0, sizeof *clock);
    if (selected->step_ns <= 0 ||
        selected->max_frame_ns < selected->step_ns ||
        selected->max_steps_per_frame == 0u ||
        selected->max_steps_per_frame > 64u) return false;
    clock->step_ns = selected->step_ns;
    clock->max_frame_ns = selected->max_frame_ns;
    clock->max_steps_per_frame = selected->max_steps_per_frame;
    return true;
}

void kilix_game_clock_reset(kilix_game_clock *clock, int64_t now_ns)
{
    if (clock == NULL || clock->step_ns <= 0) return;
    clock->last_ns = now_ns;
    clock->accumulator_ns = 0;
    clock->started = true;
}

static int64_t saturated_add(int64_t first, int64_t second)
{
    return second > INT64_MAX - first ? INT64_MAX : first + second;
}

kilix_game_frame kilix_game_clock_advance(kilix_game_clock *clock,
                                          int64_t now_ns)
{
    kilix_game_frame frame = {0};
    int64_t elapsed;
    int64_t available_steps;

    if (clock == NULL || clock->step_ns <= 0) return frame;
    if (!clock->started) {
        kilix_game_clock_reset(clock, now_ns);
        return frame;
    }
    if (now_ns < clock->last_ns) {
        clock->last_ns = now_ns;
        return frame;
    }
    elapsed = now_ns - clock->last_ns;
    clock->last_ns = now_ns;
    if (elapsed > clock->max_frame_ns) {
        frame.dropped_ns = elapsed - clock->max_frame_ns;
        elapsed = clock->max_frame_ns;
    }
    frame.frame_ns = elapsed;
    clock->accumulator_ns = saturated_add(clock->accumulator_ns, elapsed);
    available_steps = clock->accumulator_ns / clock->step_ns;
    if (available_steps > (int64_t)clock->max_steps_per_frame) {
        const int64_t excess = available_steps -
                               (int64_t)clock->max_steps_per_frame;
        const int64_t dropped = excess > INT64_MAX / clock->step_ns ?
                                INT64_MAX : excess * clock->step_ns;

        frame.dropped_ns = saturated_add(frame.dropped_ns, dropped);
        clock->accumulator_ns -= dropped;
        available_steps = (int64_t)clock->max_steps_per_frame;
    }
    frame.steps = (uint32_t)available_steps;
    clock->accumulator_ns -= available_steps * clock->step_ns;
    clock->dropped_ns = saturated_add(clock->dropped_ns, frame.dropped_ns);
    if (UINT64_MAX - clock->total_steps < frame.steps)
        clock->total_steps = UINT64_MAX;
    else
        clock->total_steps += frame.steps;
    frame.alpha = (double)clock->accumulator_ns / (double)clock->step_ns;
    return frame;
}

double kilix_game_clock_step_seconds(const kilix_game_clock *clock)
{
    return clock != NULL && clock->step_ns > 0 ?
           (double)clock->step_ns /
               (double)KILIX_GAME_NANOSECONDS_PER_SECOND : 0.0;
}

uint64_t kilix_game_clock_total_steps(const kilix_game_clock *clock)
{
    return clock != NULL ? clock->total_steps : 0u;
}

int64_t kilix_game_clock_dropped_ns(const kilix_game_clock *clock)
{
    return clock != NULL ? clock->dropped_ns : 0;
}

int64_t kilix_game_monotonic_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    if (now.tv_sec > INT64_MAX / KILIX_GAME_NANOSECONDS_PER_SECOND)
        return INT64_MAX;
    return (int64_t)now.tv_sec * KILIX_GAME_NANOSECONDS_PER_SECOND +
           (int64_t)now.tv_nsec;
}

bool kilix_game_sleep_until_ns(int64_t deadline_ns)
{
    struct timespec deadline;
    int result;

    if (deadline_ns < 0) {
        errno = EINVAL;
        return false;
    }
    deadline.tv_sec = (time_t)(deadline_ns /
                               KILIX_GAME_NANOSECONDS_PER_SECOND);
    deadline.tv_nsec = (long)(deadline_ns %
                              KILIX_GAME_NANOSECONDS_PER_SECOND);
    do {
        result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                 &deadline, NULL);
    } while (result == EINTR);
    if (result != 0) {
        errno = result;
        return false;
    }
    return true;
}
