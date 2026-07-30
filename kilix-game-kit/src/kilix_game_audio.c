#include "kilix_game_audio.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define KILIX_GAME_AUDIO_PATH_CAPACITY 4096u
#define SET_ERROR(destination, destination_size, ...) do {                   \
    if ((destination) && (destination_size) != 0u)                           \
        (void)snprintf((destination), (destination_size), __VA_ARGS__);      \
} while (false)

void kilix_game_data_roots_init(kilix_game_data_roots *roots)
{
    if (!roots) return;
    roots->environment_variable = NULL;
    roots->local_root = ".";
    roots->installed_root = NULL;
}

bool kilix_game_data_path_is_safe(const char *relative_path)
{
    const char *component;
    const char *cursor;
    if (!relative_path || relative_path[0] == '\0' ||
        relative_path[0] == '/') return false;
    component = relative_path;
    for (cursor = relative_path;; ++cursor) {
        unsigned char byte = (unsigned char)*cursor;
        if (byte == '\\' || (byte != '\0' && byte < 32u)) return false;
        if (byte == '/' || byte == '\0') {
            size_t length = (size_t)(cursor - component);
            if (length == 0u ||
                (length == 1u && component[0] == '.') ||
                (length == 2u && component[0] == '.' &&
                 component[1] == '.')) return false;
            if (byte == '\0') break;
            component = cursor + 1;
        }
    }
    return true;
}

static bool join_existing(const char *root, const char *relative,
                          char *destination, size_t destination_size)
{
    struct stat status;
    size_t root_length;
    const char *separator;
    int written;
    if (!root || root[0] == '\0') return false;
    root_length = strlen(root);
    separator = root[root_length - 1u] == '/' ? "" : "/";
    written = snprintf(destination, destination_size, "%s%s%s", root,
                       separator, relative);
    return written >= 0 && (size_t)written < destination_size &&
           stat(destination, &status) == 0 && S_ISREG(status.st_mode);
}

bool kilix_game_data_resolve(const kilix_game_data_roots *roots,
                             const char *relative_path, char *destination,
                             size_t destination_size)
{
    const char *override = NULL;
    if (!roots || !kilix_game_data_path_is_safe(relative_path) ||
        !destination || destination_size == 0u) return false;
    destination[0] = '\0';
    if (roots->environment_variable && roots->environment_variable[0] != '\0')
        override = getenv(roots->environment_variable);
    if (override && override[0] != '\0' &&
        join_existing(override, relative_path, destination, destination_size))
        return true;
    if (join_existing(roots->local_root, relative_path, destination,
                      destination_size)) return true;
    if (join_existing(roots->installed_root, relative_path, destination,
                      destination_size)) return true;
    destination[0] = '\0';
    return false;
}

static float clamped_gain(float gain)
{
    if (!isfinite(gain) || gain <= 0.0f) return 0.0f;
    return gain > 2.0f ? 2.0f : gain;
}

void kilix_game_audio_options_init(kilix_game_audio_options *options)
{
    if (!options) return;
    memset(options, 0, sizeof *options);
    options->cue_count = PCMMIX_BANK_CUES_MAX;
    options->random_seed = UINT32_C(0x4b494c58);
    kilix_game_data_roots_init(&options->data);
    pcmmix_options_init(&options->mixer);
    options->start_mixer = true;
    options->require_mixer = false;
}

void kilix_game_audio_shutdown(kilix_game_audio *audio)
{
    if (!audio) return;
    if (audio->mixer_started) pcmmix_stop(&audio->mixer);
    pcmmix_bank_clear(&audio->bank);
    *audio = (kilix_game_audio){0};
}

bool kilix_game_audio_init(kilix_game_audio *audio,
                           const kilix_game_audio_options *options,
                           char *error, size_t error_size)
{
    kilix_game_audio_options defaults;
    const kilix_game_audio_options *selected = options;
    size_t index;
    bool cue_seen[PCMMIX_BANK_CUES_MAX] = {false};
    if (error && error_size != 0u) error[0] = '\0';
    if (!audio) return false;
    if (!selected) {
        kilix_game_audio_options_init(&defaults);
        selected = &defaults;
    }
    if (selected->cue_count == 0u ||
        selected->cue_count > PCMMIX_BANK_CUES_MAX ||
        (selected->cue_spec_count != 0u && !selected->cues) ||
        (selected->scene_count != 0u && !selected->scenes)) {
        SET_ERROR(error, error_size, "invalid audio options");
        return false;
    }
    *audio = (kilix_game_audio){0};
    audio->current_scene = UINT32_MAX;
    for (index = 0u; index < KILIX_GAME_AUDIO_BUS_COUNT; ++index) {
        audio->buses[index].gain = 1.0f;
        audio->buses[index].target = 1.0f;
    }
    if (!pcmmix_bank_init(&audio->bank, selected->cue_count,
                          selected->random_seed)) {
        SET_ERROR(error, error_size, "cannot initialize cue bank");
        return false;
    }
    for (index = 0u; index < selected->cue_spec_count; ++index) {
        const kilix_game_audio_cue_spec *spec = &selected->cues[index];
        char path[KILIX_GAME_AUDIO_PATH_CAPACITY];
        char wav_error[160] = "load failed";
        if (spec->cue >= selected->cue_count ||
            spec->variant >= PCMMIX_BANK_VARIANTS_MAX ||
            !spec->relative_path ||
            !kilix_game_data_path_is_safe(spec->relative_path) ||
            !isfinite(spec->gain) || !isfinite(spec->pitch)) {
            SET_ERROR(error, error_size, "invalid cue spec %zu", index);
            goto fail;
        }
        {
            size_t previous;
            for (previous = 0u; previous < index; ++previous) {
                if (selected->cues[previous].cue == spec->cue &&
                    selected->cues[previous].variant == spec->variant) {
                    SET_ERROR(error, error_size,
                              "duplicate cue %u variant %u", spec->cue,
                              spec->variant);
                    goto fail;
                }
            }
        }
        if (!kilix_game_data_resolve(&selected->data, spec->relative_path,
                                     path, sizeof path)) {
            if (!spec->required) continue;
            SET_ERROR(error, error_size, "missing required cue: %s",
                      spec->relative_path);
            goto fail;
        }
        if (!pcmmix_bank_load_wav(&audio->bank, spec->cue, spec->variant,
                                  path, spec->gain, spec->pitch, wav_error,
                                  sizeof wav_error)) {
            if (!spec->required) continue;
            SET_ERROR(error, error_size, "cannot load %s: %s",
                      spec->relative_path, wav_error);
            goto fail;
        }
        cue_seen[spec->cue] = true;
        ++audio->loaded_variants;
    }
    for (index = 0u; index < selected->cue_count; ++index)
        if (cue_seen[index]) ++audio->loaded_cues;
    for (index = 0u; index < selected->scene_count; ++index) {
        const kilix_game_music_scene_spec *scene = &selected->scenes[index];
        size_t previous;
        if (scene->cue >= selected->cue_count ||
            scene->variant >= PCMMIX_BANK_VARIANTS_MAX ||
            !isfinite(scene->gain)) {
            SET_ERROR(error, error_size, "invalid music scene %zu", index);
            goto fail;
        }
        for (previous = 0u; previous < index; ++previous) {
            if (selected->scenes[previous].scene == scene->scene) {
                SET_ERROR(error, error_size, "duplicate music scene %u",
                          scene->scene);
                goto fail;
            }
        }
    }
    audio->scenes = selected->scenes;
    audio->scene_count = selected->scene_count;
    if (selected->start_mixer) {
        audio->mixer_started = pcmmix_start(&audio->mixer, &selected->mixer);
        if (!audio->mixer_started && selected->require_mixer) {
            SET_ERROR(error, error_size, "audio mixer could not start");
            goto fail;
        }
    }
    audio->ready = true;
    return true;
fail:
    kilix_game_audio_shutdown(audio);
    return false;
}

bool kilix_game_audio_is_ready(const kilix_game_audio *audio)
{
    return audio && audio->ready;
}

bool kilix_game_audio_is_running(kilix_game_audio *audio)
{
    return audio && audio->mixer_started && pcmmix_is_running(&audio->mixer);
}

static bool valid_bus(kilix_game_audio_bus bus)
{
    return bus >= KILIX_GAME_AUDIO_BUS_MASTER &&
           bus < KILIX_GAME_AUDIO_BUS_COUNT;
}

static float effective_gain(const kilix_game_audio *audio,
                            kilix_game_audio_bus bus, float gain)
{
    float result = clamped_gain(gain) *
                   audio->buses[KILIX_GAME_AUDIO_BUS_MASTER].gain;
    if (bus != KILIX_GAME_AUDIO_BUS_MASTER) result *= audio->buses[bus].gain;
    return result;
}

int kilix_game_audio_play(kilix_game_audio *audio, uint32_t cue,
                          kilix_game_audio_bus bus, float gain, float pitch)
{
    if (!audio || !audio->ready || !audio->mixer_started || !valid_bus(bus))
        return -1;
    return pcmmix_bank_play(&audio->mixer, &audio->bank, cue,
                            effective_gain(audio, bus, gain), pitch);
}

int kilix_game_audio_loop(kilix_game_audio *audio, uint32_t cue,
                          kilix_game_audio_bus bus, float gain, float pitch)
{
    if (!audio || !audio->ready || !audio->mixer_started || !valid_bus(bus))
        return -1;
    return pcmmix_bank_loop(&audio->mixer, &audio->bank, cue,
                            effective_gain(audio, bus, gain), pitch);
}

static void refresh_music_gain(kilix_game_audio *audio)
{
    size_t index;
    if (!audio || !audio->mixer_started || !audio->current_music) return;
    for (index = 0u; index < audio->scene_count; ++index) {
        const kilix_game_music_scene_spec *scene = &audio->scenes[index];
        if (scene->scene == audio->current_scene) {
            pcmmix_music_play(&audio->mixer, audio->current_music,
                effective_gain(audio, KILIX_GAME_AUDIO_BUS_MUSIC,
                               scene->gain), scene->loop);
            return;
        }
    }
}

void kilix_game_audio_set_bus(kilix_game_audio *audio,
                              kilix_game_audio_bus bus, float gain)
{
    if (!audio || !valid_bus(bus)) return;
    audio->buses[bus].gain = clamped_gain(gain);
    audio->buses[bus].target = audio->buses[bus].gain;
    audio->buses[bus].rate_per_second = 0.0f;
    if (bus == KILIX_GAME_AUDIO_BUS_MASTER ||
        bus == KILIX_GAME_AUDIO_BUS_MUSIC) refresh_music_gain(audio);
}

void kilix_game_audio_fade_bus(kilix_game_audio *audio,
                               kilix_game_audio_bus bus, float target,
                               float seconds)
{
    float selected;
    if (!audio || !valid_bus(bus)) return;
    selected = clamped_gain(target);
    if (!isfinite(seconds) || seconds <= 0.0f) {
        kilix_game_audio_set_bus(audio, bus, selected);
        return;
    }
    audio->buses[bus].target = selected;
    audio->buses[bus].rate_per_second =
        fabsf(selected - audio->buses[bus].gain) / seconds;
}

float kilix_game_audio_bus_gain(const kilix_game_audio *audio,
                                kilix_game_audio_bus bus)
{
    return audio && valid_bus(bus) ? audio->buses[bus].gain : 0.0f;
}

void kilix_game_audio_update(kilix_game_audio *audio, float seconds)
{
    size_t index;
    bool music_changed = false;
    if (!audio || !audio->ready || !isfinite(seconds) || seconds <= 0.0f)
        return;
    for (index = 0u; index < KILIX_GAME_AUDIO_BUS_COUNT; ++index) {
        kilix_game_audio_bus_state *bus = &audio->buses[index];
        float step;
        if (bus->rate_per_second <= 0.0f || bus->gain == bus->target) continue;
        step = bus->rate_per_second * seconds;
        if (bus->gain < bus->target) {
            bus->gain += step;
            if (bus->gain >= bus->target) bus->gain = bus->target;
        } else {
            bus->gain -= step;
            if (bus->gain <= bus->target) bus->gain = bus->target;
        }
        if (bus->gain == bus->target) bus->rate_per_second = 0.0f;
        if (index == KILIX_GAME_AUDIO_BUS_MASTER ||
            index == KILIX_GAME_AUDIO_BUS_MUSIC) music_changed = true;
    }
    if (music_changed) refresh_music_gain(audio);
}

bool kilix_game_audio_set_scene(kilix_game_audio *audio, uint32_t scene_id)
{
    size_t index;
    if (!audio || !audio->ready || !audio->mixer_started) return false;
    for (index = 0u; index < audio->scene_count; ++index) {
        const kilix_game_music_scene_spec *scene = &audio->scenes[index];
        const pcmmix_sample *sample;
        if (scene->scene != scene_id) continue;
        sample = pcmmix_bank_sample_at(&audio->bank, scene->cue,
                                      scene->variant);
        if (!sample) return false;
        audio->current_scene = scene_id;
        audio->current_music = sample;
        pcmmix_music_play(&audio->mixer, sample,
            effective_gain(audio, KILIX_GAME_AUDIO_BUS_MUSIC, scene->gain),
            scene->loop);
        return true;
    }
    return false;
}

void kilix_game_audio_stop_music(kilix_game_audio *audio, float fade_seconds)
{
    if (!audio) return;
    if (audio->mixer_started)
        pcmmix_music_stop(&audio->mixer, fade_seconds);
    audio->current_scene = UINT32_MAX;
    audio->current_music = NULL;
}
