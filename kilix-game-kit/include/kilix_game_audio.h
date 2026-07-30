#ifndef KILIX_GAME_AUDIO_H
#define KILIX_GAME_AUDIO_H

#include "pcmmix_bank.h"
#include "pcm_mixer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kilix_game_data_roots {
    const char *environment_variable;
    const char *local_root;
    const char *installed_root;
} kilix_game_data_roots;

void kilix_game_data_roots_init(kilix_game_data_roots *roots);
bool kilix_game_data_path_is_safe(const char *relative_path);
bool kilix_game_data_resolve(const kilix_game_data_roots *roots,
                             const char *relative_path, char *destination,
                             size_t destination_size);

typedef enum kilix_game_audio_bus {
    KILIX_GAME_AUDIO_BUS_MASTER = 0,
    KILIX_GAME_AUDIO_BUS_SFX,
    KILIX_GAME_AUDIO_BUS_UI,
    KILIX_GAME_AUDIO_BUS_AMBIENCE,
    KILIX_GAME_AUDIO_BUS_MUSIC,
    KILIX_GAME_AUDIO_BUS_COUNT
} kilix_game_audio_bus;

typedef struct kilix_game_audio_cue_spec {
    uint32_t cue;
    uint32_t variant;
    const char *relative_path;
    float gain;
    float pitch;
    bool required;
} kilix_game_audio_cue_spec;

typedef struct kilix_game_music_scene_spec {
    uint32_t scene;
    uint32_t cue;
    uint32_t variant;
    float gain;
    bool loop;
} kilix_game_music_scene_spec;

typedef struct kilix_game_audio_options {
    uint32_t cue_count;
    uint32_t random_seed;
    kilix_game_data_roots data;
    const kilix_game_audio_cue_spec *cues;
    size_t cue_spec_count;
    const kilix_game_music_scene_spec *scenes;
    size_t scene_count;
    pcmmix_options mixer;
    bool start_mixer;
    bool require_mixer;
} kilix_game_audio_options;

typedef struct kilix_game_audio_bus_state {
    float gain;
    float target;
    float rate_per_second;
} kilix_game_audio_bus_state;

typedef struct kilix_game_audio {
    pcmmix mixer;
    pcmmix_bank bank;
    kilix_game_audio_bus_state buses[KILIX_GAME_AUDIO_BUS_COUNT];
    const kilix_game_music_scene_spec *scenes;
    size_t scene_count;
    const pcmmix_sample *current_music;
    uint32_t current_scene;
    size_t loaded_cues;
    size_t loaded_variants;
    bool ready;
    bool mixer_started;
} kilix_game_audio;

void kilix_game_audio_options_init(kilix_game_audio_options *options);

/* audio must be zero-initialized before its first init. cue/scene tables are
 * semantic data owned by the game. Cue WAVs are loaded into the bank; the
 * scene table must remain alive until shutdown. */
bool kilix_game_audio_init(kilix_game_audio *audio,
                           const kilix_game_audio_options *options,
                           char *error, size_t error_size);
void kilix_game_audio_shutdown(kilix_game_audio *audio);
bool kilix_game_audio_is_ready(const kilix_game_audio *audio);
bool kilix_game_audio_is_running(kilix_game_audio *audio);

int kilix_game_audio_play(kilix_game_audio *audio, uint32_t cue,
                          kilix_game_audio_bus bus, float gain, float pitch);
int kilix_game_audio_loop(kilix_game_audio *audio, uint32_t cue,
                          kilix_game_audio_bus bus, float gain, float pitch);

void kilix_game_audio_set_bus(kilix_game_audio *audio,
                              kilix_game_audio_bus bus, float gain);
void kilix_game_audio_fade_bus(kilix_game_audio *audio,
                               kilix_game_audio_bus bus, float target,
                               float seconds);
float kilix_game_audio_bus_gain(const kilix_game_audio *audio,
                                kilix_game_audio_bus bus);
void kilix_game_audio_update(kilix_game_audio *audio, float seconds);

/* pcm-mixer crossfades between different scene samples. Re-selecting the
 * active scene retargets its volume without restarting it. */
bool kilix_game_audio_set_scene(kilix_game_audio *audio, uint32_t scene);
void kilix_game_audio_stop_music(kilix_game_audio *audio, float fade_seconds);

#ifdef __cplusplus
}
#endif

#endif
