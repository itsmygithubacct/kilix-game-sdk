#ifndef KILIX_GAME_LOOP_H
#define KILIX_GAME_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILIX_GAME_NANOSECONDS_PER_SECOND INT64_C(1000000000)

typedef struct kilix_game_clock_options {
    int64_t step_ns;
    int64_t max_frame_ns;
    uint32_t max_steps_per_frame;
} kilix_game_clock_options;

typedef struct kilix_game_frame {
    uint32_t steps;
    int64_t frame_ns;
    int64_t dropped_ns;
    double alpha;
} kilix_game_frame;

typedef struct kilix_game_clock {
    int64_t step_ns;
    int64_t max_frame_ns;
    int64_t last_ns;
    int64_t accumulator_ns;
    int64_t dropped_ns;
    uint64_t total_steps;
    uint32_t max_steps_per_frame;
    bool started;
} kilix_game_clock;

void kilix_game_clock_options_init(kilix_game_clock_options *options);
bool kilix_game_clock_init(kilix_game_clock *clock,
                           const kilix_game_clock_options *options);
void kilix_game_clock_reset(kilix_game_clock *clock, int64_t now_ns);
kilix_game_frame kilix_game_clock_advance(kilix_game_clock *clock,
                                          int64_t now_ns);
double kilix_game_clock_step_seconds(const kilix_game_clock *clock);
uint64_t kilix_game_clock_total_steps(const kilix_game_clock *clock);
int64_t kilix_game_clock_dropped_ns(const kilix_game_clock *clock);

int64_t kilix_game_monotonic_ns(void);
bool kilix_game_sleep_until_ns(int64_t deadline_ns);

#ifdef __cplusplus
}
#endif

#endif
