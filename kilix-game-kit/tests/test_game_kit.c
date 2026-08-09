#include "kilix_game_loop.h"
#include "kilix_game_runtime.h"
#include "kilix_game_audio.h"
#include "kilix_game_test.h"
#include "kilix_state_codec.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n",             \
                          __FILE__, __LINE__, #condition);                     \
            return false;                                                     \
        }                                                                     \
    } while (false)

static bool test_fixed_step(void)
{
    kilix_game_clock_options options;
    kilix_game_clock clock;
    kilix_game_frame frame;

    kilix_game_clock_options_init(&options);
    options.step_ns = 10;
    options.max_frame_ns = 100;
    options.max_steps_per_frame = 3u;
    CHECK(kilix_game_clock_init(&clock, &options));
    frame = kilix_game_clock_advance(&clock, 1000);
    CHECK(frame.steps == 0u && frame.alpha == 0.0);
    frame = kilix_game_clock_advance(&clock, 1025);
    CHECK(frame.steps == 2u);
    CHECK(frame.alpha > 0.49 && frame.alpha < 0.51);
    frame = kilix_game_clock_advance(&clock, 1040);
    CHECK(frame.steps == 2u && frame.alpha == 0.0);
    frame = kilix_game_clock_advance(&clock, 1240);
    CHECK(frame.steps == 3u);
    CHECK(frame.frame_ns == 100);
    CHECK(frame.dropped_ns == 170);
    CHECK(kilix_game_clock_dropped_ns(&clock) == 170);
    CHECK(kilix_game_clock_total_steps(&clock) == 7u);
    CHECK(kilix_game_clock_step_seconds(&clock) == 1.0e-8);

    /* Backward clocks resynchronize without a negative frame. */
    frame = kilix_game_clock_advance(&clock, 1200);
    CHECK(frame.steps == 0u && frame.frame_ns == 0);
    frame = kilix_game_clock_advance(&clock, 1210);
    CHECK(frame.steps == 1u);

    /* Arbitrary caller-supplied timestamps must saturate rather than overflow
     * before the ordinary frame and catch-up clamps run. */
    options.step_ns = 1;
    options.max_frame_ns = INT64_MAX;
    options.max_steps_per_frame = 1u;
    CHECK(kilix_game_clock_init(&clock, &options));
    kilix_game_clock_reset(&clock, INT64_MIN);
    frame = kilix_game_clock_advance(&clock, INT64_MAX);
    CHECK(frame.frame_ns == INT64_MAX);
    CHECK(frame.steps == 1u && frame.alpha == 0.0);
    CHECK(frame.dropped_ns == INT64_MAX - 1);
    return true;
}

static bool test_clock_validation_and_sleep(void)
{
    kilix_game_clock_options options;
    kilix_game_clock clock;
    int64_t now;

    kilix_game_clock_options_init(&options);
    options.step_ns = 0;
    CHECK(!kilix_game_clock_init(&clock, &options));
    options.step_ns = 20;
    options.max_frame_ns = 10;
    CHECK(!kilix_game_clock_init(&clock, &options));
    options.max_frame_ns = 20;
    options.max_steps_per_frame = 65u;
    CHECK(!kilix_game_clock_init(&clock, &options));
    CHECK(!kilix_game_clock_init(NULL, NULL));
    CHECK(kilix_game_clock_step_seconds(NULL) == 0.0);
    CHECK(kilix_game_clock_total_steps(NULL) == 0u);
    CHECK(kilix_game_clock_dropped_ns(NULL) == 0);
    now = kilix_game_monotonic_ns();
    CHECK(now >= 0);
    CHECK(kilix_game_sleep_until_ns(now));
    CHECK(!kilix_game_sleep_until_ns(-1));
    return true;
}

static bool test_state_codec_embedding(void)
{
    uint8_t payload[8];
    kilixstate_writer writer;
    kilixstate_reader reader;
    uint32_t version;
    int32_t score;

    kilixstate_writer_init(&writer, payload, sizeof payload);
    CHECK(kilixstate_write_u32(&writer, UINT32_C(3)));
    CHECK(kilixstate_write_i32(&writer, INT32_C(-42)));
    CHECK(kilixstate_writer_size(&writer) == sizeof payload);
    kilixstate_reader_init(&reader, payload, sizeof payload);
    CHECK(kilixstate_read_u32(&reader, &version) && version == 3u);
    CHECK(kilixstate_read_i32(&reader, &score) && score == -42);
    CHECK(kilixstate_reader_require_finished(&reader));
    return true;
}

static bool test_pty_and_golden_helpers(void)
{
    static const char message[] = "ordered terminal bytes";
    static const uint8_t first[] = {0u, 10u, 20u, 255u, 4u, 5u, 6u, 7u};
    static const uint8_t second[] = {0u, 12u, 20u, 255u, 4u, 5u, 9u, 7u};
    kilix_test_pty pty;
    kilix_test_image_diff difference;
    char received[64] = "";
    ssize_t count;

    CHECK(kilix_test_pty_open(&pty, 80u, 24u, 640u, 480u));
    CHECK(kilix_test_write_all(pty.slave_fd, message, sizeof message - 1u));
    count = kilix_test_read_available(pty.master_fd, received,
                                      sizeof received, 20);
    CHECK(count == (ssize_t)(sizeof message - 1u));
    CHECK(memcmp(received, message, sizeof message - 1u) == 0);
    CHECK(kilix_test_contains(received, (size_t)count, "terminal", 8u));
    CHECK(!kilix_test_contains(received, (size_t)count, "mouse", 5u));
    CHECK(kilix_test_contains(NULL, 0u, NULL, 0u));
    CHECK(!kilix_test_contains(NULL, 1u, "x", 1u));
    CHECK(!kilix_test_contains("x", 1u, "xy", 2u));
    kilix_test_pty_close(&pty);
    kilix_test_pty_close(&pty);
    errno = 0;
    CHECK(!kilix_test_pty_open(NULL, 80u, 24u, 0u, 0u) && errno == EINVAL);
    CHECK(!kilix_test_write_all(-1, NULL, 0u) && errno == EINVAL);
    CHECK(kilix_test_write_all(STDOUT_FILENO, NULL, 0u));
    CHECK(kilix_test_read_available(-1, NULL, 0u, 0) == -1 &&
          errno == EINVAL);

    CHECK(kilix_test_hash64("hello", 5u) == UINT64_C(0xa430d84680aabd0b));
    difference = kilix_test_diff_rgba(first, second, 2u, 1u);
    CHECK(difference.differing_pixels == 2u);
    CHECK(difference.maximum_channel_delta == 3u);
    difference = kilix_test_diff_rgba(first, second, 2u, 3u);
    CHECK(difference.differing_pixels == 0u);
    difference = kilix_test_diff_rgba(first, second, SIZE_MAX / 4u + 1u, 0u);
    CHECK(difference.differing_pixels == SIZE_MAX / 4u + 1u);
    CHECK(difference.maximum_channel_delta == UINT8_MAX);
    return true;
}

typedef struct host_probe {
    int starts;
    int steps;
    int renders;
    int stops;
    bool fail_start;
    bool fail_step;
    bool fail_render;
    bool raise_term;
} host_probe;

static bool host_start(kilix_game_host *host, void *user)
{
    host_probe *probe = user;
    CHECK(host != NULL);
    ++probe->starts;
    return !probe->fail_start;
}

static bool host_step(kilix_game_host *host, void *user, double step_seconds)
{
    host_probe *probe = user;
    CHECK(host != NULL && step_seconds > 0.0);
    ++probe->steps;
    return !probe->fail_step;
}

static bool host_render(kilix_game_host *host, void *user, double alpha)
{
    host_probe *probe = user;
    CHECK(alpha >= 0.0 && alpha < 1.0);
    ++probe->renders;
    if (probe->raise_term && probe->renders == 1) CHECK(raise(SIGTERM) == 0);
    if (probe->fail_render) return false;
    if (probe->renders == 2) kilix_game_host_request_stop(host);
    return true;
}

static void host_stop(kilix_game_host *host, void *user)
{
    host_probe *probe = user;
    (void)host;
    ++probe->stops;
}

static bool test_runtime_host_and_signals(void)
{
    kilix_game_signal_scope signals;
    kilix_game_signal_scope second_scope;
    kilix_game_host host;
    kilix_game_host_options options;
    kilix_game_host_callbacks callbacks = {0};
    host_probe probe = {0};
    kittyin_event event = {0};
    errno = 0;
    CHECK(!kilix_game_signals_install(NULL) && errno == EINVAL);
    CHECK(kilix_game_signals_install(&signals));
    errno = 0;
    CHECK(!kilix_game_signals_install(&second_scope) && errno == EBUSY);
    CHECK(raise(SIGTERM) == 0);
    CHECK(kilix_game_signals_requested(&signals));
    CHECK(kilix_game_signals_number(&signals) == SIGTERM);
    kilix_game_signals_restore(&signals);
    CHECK(kilix_game_signals_install(&signals));
    CHECK(raise(SIGTSTP) == 0);
    CHECK(kilix_game_signals_suspend_requested(&signals));
    CHECK(!kilix_game_signals_requested(&signals));
    kilix_game_signals_restore(&signals);

    event.kind = KITTYIN_EVENT_KEY;
    event.data.key.key = (uint32_t)'Q';
    CHECK(kilix_game_event_letter(&event, 'q'));
    CHECK(!kilix_game_event_letter(&event, 'x'));

    kilix_game_host_options_init(&options);
    options.headless = true;
    options.install_signals = false;
    options.idle_sleep_ns = 0;
    options.max_frames = 8u;
    callbacks.start = host_start;
    callbacks.render = host_render;
    callbacks.stop = host_stop;
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_SUCCESS);
    CHECK(probe.starts == 1 && probe.renders == 2 && probe.stops == 1);
    CHECK(host.frame_count == 2u && !host.running && !host.terminal_started);
    CHECK(host.suspension_count == 0u);
    return true;
}

static bool test_runtime_failure_paths(void)
{
    kilix_game_host host;
    kilix_game_host original;
    kilix_game_host_options options;
    kilix_game_host_callbacks callbacks = {0};
    host_probe probe = {0};
    kilix_test_pty pty;
    int null_fd;

    callbacks.start = host_start;
    callbacks.step = host_step;
    callbacks.render = host_render;
    callbacks.stop = host_stop;
    kilix_game_host_options_init(&options);
    options.headless = true;
    options.install_signals = false;
    options.idle_sleep_ns = 0;
    options.clock.step_ns = 0;
    (void)memset(&host, 0xa5, sizeof host);
    original = host;
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_FAILURE);
    CHECK(memcmp(&host, &original, sizeof host) == 0);

    kilix_game_host_options_init(&options);
    options.headless = true;
    options.install_signals = false;
    options.idle_sleep_ns = 0;
    options.max_frames = 4u;
    probe = (host_probe){0};
    probe.fail_start = true;
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_FAILURE);
    CHECK(probe.starts == 1 && probe.stops == 1 && probe.renders == 0);

    probe = (host_probe){0};
    probe.fail_render = true;
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_FAILURE);
    CHECK(probe.starts == 1 && probe.renders == 1 && probe.stops == 1);

    options.clock.step_ns = 1;
    options.clock.max_frame_ns = 1000000;
    options.clock.max_steps_per_frame = 2u;
    probe = (host_probe){0};
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_SUCCESS);
    CHECK(probe.starts == 1 && probe.steps > 0 && probe.renders == 2 &&
          probe.stops == 1);

    kilix_game_host_options_init(&options);
    options.headless = true;
    options.idle_sleep_ns = 0;
    options.max_frames = 4u;
    probe = (host_probe){0};
    probe.raise_term = true;
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_SUCCESS);
    CHECK(probe.renders == 1 && probe.stops == 1);

    null_fd = open("/dev/null", O_RDWR);
    CHECK(null_fd >= 0);
    kilix_game_host_options_init(&options);
    options.input_fd = null_fd;
    options.output_fd = null_fd;
    options.install_signals = false;
    options.idle_sleep_ns = 0;
    options.max_frames = 1u;
    probe = (host_probe){0};
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_FAILURE);
    CHECK(host.terminal_errno != 0 && probe.starts == 0 && probe.stops == 0);
    CHECK(close(null_fd) == 0);

    CHECK(kilix_test_pty_open(&pty, 80u, 24u, 640u, 480u));
    kilix_game_host_options_init(&options);
    options.input_fd = pty.slave_fd;
    options.output_fd = pty.slave_fd;
    options.install_signals = false;
    options.idle_sleep_ns = 0;
    options.max_frames = 1u;
    options.terminal.framebuffer.probe_graphics = false;
    options.terminal.framebuffer.install_winch_handler = false;
    options.terminal.framebuffer.transport = KITTYFB_TRANSPORT_INLINE;
    options.terminal.framebuffer.min_width = 16u;
    options.terminal.framebuffer.max_width = 16u;
    options.terminal.framebuffer.min_height = 8u;
    options.terminal.framebuffer.max_height = 8u;
    options.terminal.keyboard.probe_timeout_ms = 0;
    probe = (host_probe){0};
    CHECK(kilix_game_host_run(&host, &options, &callbacks, &probe) ==
          EXIT_SUCCESS);
    CHECK(host.frame_count == 1u && !host.terminal_started);
    CHECK(probe.starts == 1 && probe.renders == 1 && probe.stops == 1);
    kilix_test_pty_close(&pty);
    return true;
}

static bool same_signal_handler(const struct sigaction *first,
                                const struct sigaction *second)
{
    if ((first->sa_flags & SA_SIGINFO) != (second->sa_flags & SA_SIGINFO))
        return false;
    if ((first->sa_flags & SA_SIGINFO) != 0)
        return first->sa_sigaction == second->sa_sigaction;
    return first->sa_handler == second->sa_handler;
}

static bool test_crash_signals(void)
{
    kilix_game_signal_scope signals;
    struct sigaction previous;
    struct sigaction current;
    pid_t child;
    int child_status = 0;

    /* Installing the scope must claim the crash-class signals and restoring
     * it must hand back the previous dispositions. */
    CHECK(sigaction(SIGSEGV, NULL, &previous) == 0);
    CHECK(kilix_game_signals_install(&signals));
    CHECK(sigaction(SIGSEGV, NULL, &current) == 0 &&
          !same_signal_handler(&current, &previous));
    CHECK(sigaction(SIGFPE, NULL, &current) == 0 &&
          current.sa_handler != SIG_DFL);
    kilix_game_signals_restore(&signals);
    CHECK(sigaction(SIGSEGV, NULL, &current) == 0 &&
          same_signal_handler(&current, &previous));

    /* The handler must re-raise with the default disposition, so a crashing
     * process still dies with the original signal. No terminal is registered
     * here; the restore step is a no-op in this child. */
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        if (!kilix_game_signals_install(&signals)) _exit(90);
        (void)raise(SIGSEGV);
        _exit(91);   /* unreachable when the handler re-raises correctly */
    }
    CHECK(waitpid(child, &child_status, 0) == child);
    CHECK(WIFSIGNALED(child_status) && WTERMSIG(child_status) == SIGSEGV);
    return true;
}

static bool test_data_root_from_executable(void)
{
    char root[512];
    char expected[512];
    char executable[256];
    ssize_t length;
    char *slash;

    /* The environment override wins unconditionally. */
    CHECK(setenv("KILIX_KIT_TEST_DATA", "/nonexistent/override", 1) == 0);
    CHECK(kilix_game_data_root_from_executable(
        "KILIX_KIT_TEST_DATA", "assets", NULL, root, sizeof root));
    CHECK(strcmp(root, "/nonexistent/override") == 0);
    CHECK(unsetenv("KILIX_KIT_TEST_DATA") == 0);

    /* A directory beside the executable is found from any cwd. */
    length = readlink("/proc/self/exe", executable, sizeof executable - 1u);
    CHECK(length > 0);
    executable[length] = '\0';
    slash = strrchr(executable, '/');
    CHECK(slash != NULL);
    *slash = '\0';
    CHECK(snprintf(expected, sizeof expected, "%s/kit-root-probe",
                   executable) < (int)sizeof expected);
    (void)rmdir(expected);
    CHECK(mkdir(expected, 0755) == 0);
    CHECK(kilix_game_data_root_from_executable(
        NULL, "kit-root-probe", NULL, root, sizeof root));
    CHECK(strcmp(root, expected) == 0);
    CHECK(rmdir(expected) == 0);

    /* With nothing found, the local subdirectory is the cwd-relative
     * fallback. */
    CHECK(kilix_game_data_root_from_executable(
        NULL, "no-such-dir-anywhere", "also-missing", root, sizeof root));
    CHECK(strcmp(root, "no-such-dir-anywhere") == 0);
    CHECK(!kilix_game_data_root_from_executable(NULL, NULL, NULL, root,
                                                sizeof root));
    CHECK(!kilix_game_data_root_from_executable(NULL, "assets", NULL, root,
                                                1u));
    return true;
}

static bool test_audio_validation(void)
{
    kilix_game_audio audio = {0};
    kilix_game_audio_options options;
    kilix_game_audio_cue_spec cues[2] = {
        {0u, 0u, "missing.wav", 1.0f, 1.0f, false},
        {0u, 0u, "also-missing.wav", 1.0f, 1.0f, false}
    };
    kilix_game_music_scene_spec scenes[2] = {
        {7u, 0u, 0u, 1.0f, true},
        {7u, 0u, 0u, 1.0f, false}
    };
    char error[160];

    CHECK(kilix_game_data_path_is_safe("audio/.hidden.wav"));
    CHECK(!kilix_game_data_path_is_safe(NULL));
    CHECK(!kilix_game_data_path_is_safe(""));
    CHECK(!kilix_game_data_path_is_safe("/absolute"));
    CHECK(!kilix_game_data_path_is_safe("."));
    CHECK(!kilix_game_data_path_is_safe("a//b"));
    CHECK(!kilix_game_data_path_is_safe("a/./b"));
    CHECK(!kilix_game_data_path_is_safe("a/../b"));
    CHECK(!kilix_game_data_path_is_safe("a\\b"));

    kilix_game_audio_options_init(&options);
    options.cue_count = 1u;
    options.cues = cues;
    options.cue_spec_count = 1u;
    options.start_mixer = false;
    CHECK(kilix_game_audio_init(&audio, &options, error, sizeof error));
    CHECK(kilix_game_audio_is_ready(&audio));
    CHECK(!kilix_game_audio_is_running(&audio));
    CHECK(audio.loaded_cues == 0u && audio.loaded_variants == 0u);
    CHECK(kilix_game_audio_play(&audio, 0u, KILIX_GAME_AUDIO_BUS_SFX,
                                1.0f, 1.0f) == -1);
    CHECK(!kilix_game_audio_set_scene(&audio, 7u));
    kilix_game_audio_set_bus(&audio, KILIX_GAME_AUDIO_BUS_MASTER, 9.0f);
    CHECK(kilix_game_audio_bus_gain(&audio, KILIX_GAME_AUDIO_BUS_MASTER) ==
          2.0f);
    kilix_game_audio_fade_bus(&audio, KILIX_GAME_AUDIO_BUS_MASTER, 1.0f,
                              0.0f);
    CHECK(kilix_game_audio_bus_gain(&audio, KILIX_GAME_AUDIO_BUS_MASTER) ==
          1.0f);
    kilix_game_audio_shutdown(&audio);

    options.cue_count = 0u;
    CHECK(!kilix_game_audio_init(&audio, &options, error, sizeof error));
    options.cue_count = 1u;
    options.cue_spec_count = PCMMIX_BANK_VARIANTS_MAX + 1u;
    CHECK(!kilix_game_audio_init(&audio, &options, error, sizeof error));
    options.cue_spec_count = 2u;
    CHECK(!kilix_game_audio_init(&audio, &options, error, sizeof error));
    options.cue_spec_count = 0u;
    options.cues = NULL;
    options.scenes = scenes;
    options.scene_count = 2u;
    CHECK(!kilix_game_audio_init(&audio, &options, error, sizeof error));
    return true;
}

static void little16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void little32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool write_test_wav(const char *path)
{
    static const int16_t samples[4] = {12000, -6000, 3000, 0};
    uint8_t wav[52] = {0};
    FILE *stream;
    memcpy(wav, "RIFF", 4u);
    little32(wav + 4u, 44u);
    memcpy(wav + 8u, "WAVEfmt ", 8u);
    little32(wav + 16u, 16u);
    little16(wav + 20u, 1u);
    little16(wav + 22u, 1u);
    little32(wav + 24u, 44100u);
    little32(wav + 28u, 88200u);
    little16(wav + 32u, 2u);
    little16(wav + 34u, 16u);
    memcpy(wav + 36u, "data", 4u);
    little32(wav + 40u, 8u);
    little16(wav + 44u, (uint16_t)samples[0]);
    little16(wav + 46u, (uint16_t)samples[1]);
    little16(wav + 48u, (uint16_t)samples[2]);
    little16(wav + 50u, (uint16_t)samples[3]);
    stream = fopen(path, "wb");
    if (!stream) return false;
    return fwrite(wav, 1u, sizeof wav, stream) == sizeof wav &&
           fclose(stream) == 0;
}

static int cli_probe(void *user, int argument_count,
                     const char *const *arguments)
{
    int *calls = user;
    if (argument_count != 1 || strcmp(arguments[0], "fixture") != 0) return 7;
    ++*calls;
    return 3;
}

static bool test_audio_cli_and_golden(void)
{
    char directory[] = "/tmp/kilix-game-kit-test-XXXXXX";
    char wav_path[1024];
    char ppm_path[1024];
    char error[200];
    kilix_game_audio audio = {0};
    kilix_game_audio_options options;
    kilix_game_audio_cue_spec cue = {0u, 0u, "cue.wav", 1.0f, 1.0f, true};
    kilix_game_music_scene_spec scene = {7u, 0u, 0u, 0.8f, true};
    int16_t mixed[8] = {0};
    char *arguments[] = {(char *)"game", (char *)"--fixture",
                         (char *)"fixture", NULL};
    kilix_test_cli_command command = {"--fixture", 1, 1, cli_probe};
    int cli_calls = 0;
    int exit_code = 0;
    static const uint8_t rgba[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u
    };
    kilix_test_golden_suite suite;
    uint64_t suite_hash;
    static const uint8_t expected_ppm[] = {
        'P', '6', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
        255u, 0u, 0u, 0u, 255u, 0u,
        0u, 0u, 255u, 255u, 255u, 255u
    };
    uint8_t ppm[sizeof expected_ppm];
    FILE *stream;
    CHECK(mkdtemp(directory) != NULL);
    CHECK(snprintf(wav_path, sizeof wav_path, "%s/cue.wav", directory) > 0);
    CHECK(snprintf(ppm_path, sizeof ppm_path, "%s/frame.ppm", directory) > 0);
    CHECK(write_test_wav(wav_path));
    CHECK(kilix_game_data_path_is_safe("audio/cue.wav"));
    CHECK(!kilix_game_data_path_is_safe("../cue.wav"));

    kilix_game_audio_options_init(&options);
    options.cue_count = 2u;
    options.data.local_root = directory;
    options.cues = &cue;
    options.cue_spec_count = 1u;
    options.scenes = &scene;
    options.scene_count = 1u;
    options.mixer.offline = true;
    options.require_mixer = true;
    CHECK(kilix_game_audio_init(&audio, &options, error, sizeof error));
    CHECK(kilix_game_audio_is_ready(&audio) &&
          kilix_game_audio_is_running(&audio));
    CHECK(audio.loaded_cues == 1u && audio.loaded_variants == 1u);
    CHECK(kilix_game_audio_play(&audio, 0u, KILIX_GAME_AUDIO_BUS_SFX,
                                1.0f, 1.0f) > 0);
    pcmmix_mix_block(&audio.mixer, mixed, 8u);
    CHECK(mixed[0] != 0 || mixed[1] != 0);
    CHECK(kilix_game_audio_set_scene(&audio, 7u));
    CHECK(pcmmix_music_current(&audio.mixer) != NULL);
    kilix_game_audio_fade_bus(&audio, KILIX_GAME_AUDIO_BUS_MUSIC, 0.0f, 1.0f);
    kilix_game_audio_update(&audio, 0.25f);
    CHECK(kilix_game_audio_bus_gain(&audio, KILIX_GAME_AUDIO_BUS_MUSIC) > 0.74f &&
          kilix_game_audio_bus_gain(&audio, KILIX_GAME_AUDIO_BUS_MUSIC) < 0.76f);
    kilix_game_audio_stop_music(&audio, 0.1f);
    kilix_game_audio_shutdown(&audio);

    CHECK(kilix_test_cli_dispatch(3, arguments, &command, 1u, &cli_calls,
                                  &exit_code));
    CHECK(cli_calls == 1 && exit_code == 3);
    kilix_test_golden_suite_init(&suite);
    CHECK(kilix_test_golden_add(&suite, "hello", 5u,
                                UINT64_C(0xa430d84680aabd0b)));
    suite_hash = suite.suite_hash;
    CHECK(kilix_test_golden_finish(&suite, suite_hash));
    CHECK(kilix_test_write_ppm_rgba(ppm_path, rgba, 2u, 2u, 8u));
    stream = fopen(ppm_path, "rb");
    CHECK(stream != NULL);
    CHECK(fread(ppm, 1u, sizeof ppm, stream) == sizeof ppm);
    CHECK(fgetc(stream) == EOF);
    CHECK(fclose(stream) == 0);
    CHECK(memcmp(ppm, expected_ppm, sizeof ppm) == 0);
    CHECK(!kilix_test_write_ppm_rgba(ppm_path, rgba, 2u, 2u, 7u));
    CHECK(unlink(ppm_path) == 0);
    CHECK(unlink(wav_path) == 0);
    CHECK(rmdir(directory) == 0);
    return true;
}

int main(void)
{
    if (!test_fixed_step() || !test_clock_validation_and_sleep() ||
        !test_state_codec_embedding() ||
        !test_pty_and_golden_helpers() ||
        !test_runtime_host_and_signals() ||
        !test_runtime_failure_paths() ||
        !test_crash_signals() ||
        !test_data_root_from_executable() ||
        !test_audio_validation() ||
        !test_audio_cli_and_golden()) return EXIT_FAILURE;
    (void)puts("ok: kilix-game-kit");
    return EXIT_SUCCESS;
}
