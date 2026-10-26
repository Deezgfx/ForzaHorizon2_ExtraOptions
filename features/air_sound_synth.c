#include "air_sound_synth.h"
#include "air_suspension.h"

/*
 * The built-in sounds are shaped noise and a buzzing tone, rendered with one-pole filters so no
 * math library is needed. Each sound is rendered twice with the same seed: once to find its
 * peak, once to write it normalized to that sound's loudness.
 */

#define TWO_PI 6.2831853f
#define RESTART_LEAD_MS 40u

static const float durations[AIR_SOUND_KIND_COUNT] = {0.0f, 1.7f, 1.2f, 1.4f};
static const float loudness[AIR_SOUND_KIND_COUNT] = {0.0f, 0.95f, 0.55f, 0.60f};

typedef struct {
    uint32_t seed;
} Noise;

typedef struct {
    float state;
} Pole;

static float noise_next(Noise *noise) {
    noise->seed ^= noise->seed << 13;
    noise->seed ^= noise->seed >> 17;
    noise->seed ^= noise->seed << 5;
    return (float)(int32_t)noise->seed * (1.0f / 2147483648.0f);
}

static float coefficient(float cutoff) {
    float w = TWO_PI * cutoff / (float)AIR_SOUND_RATE;
    return w / (1.0f + w);
}

static float lowpass(Pole *pole, float input, float cutoff) {
    pole->state += coefficient(cutoff) * (input - pole->state);
    return pole->state;
}

static float highpass(Pole *pole, float input, float cutoff) {
    return input - lowpass(pole, input, cutoff);
}

/* Per-sample multiplier for an exponential decay with the given time constant in seconds. */
static float decay(float seconds) {
    return 1.0f - 1.0f / (seconds * (float)AIR_SOUND_RATE);
}

static float edge_fade(uint32_t index, uint32_t count, float in_seconds, float out_seconds) {
    float rate = (float)AIR_SOUND_RATE;
    float in = (float)index / (in_seconds * rate);
    float out = (float)(count - index) / (out_seconds * rate);
    float fade = in < 1.0f ? in : 1.0f;
    return out < fade ? out : fade;
}

typedef struct {
    Noise noise;
    Pole high, low1, low2, flutter, thump, air_high, air_low, rattle_high;
    float burst, pop, spin, phase;
} Voice;

static float flutter(Voice *voice, float raw, float depth) {
    float slow = lowpass(&voice->flutter, raw, 12.0f);
    float amount = 1.0f + depth * slow;
    return amount < 0.0f ? 0.0f : amount;
}

/* Air-out: a valve pop, then a bright blast whose level and brightness fall with the pressure. */
static float dump_sample(Voice *voice, uint32_t index, uint32_t count) {
    float remaining = 1.0f - (float)index / (float)count;
    float raw = noise_next(&voice->noise);
    float cutoff = 3000.0f + 6000.0f * remaining * remaining;
    float hiss = highpass(&voice->high, raw, 700.0f);
    hiss = lowpass(&voice->low2, lowpass(&voice->low1, hiss, cutoff), cutoff);
    voice->burst *= decay(0.12f);
    voice->pop *= decay(0.008f);
    float level = remaining * remaining * (1.0f + 0.6f * voice->burst);
    float thump = lowpass(&voice->thump, raw, 250.0f) * 6.0f * voice->pop;
    float sample = hiss * level * flutter(voice, raw, 15.0f) + thump;
    return sample * edge_fade(index, count, 0.006f, 0.03f);
}

/* Lowering: a steady vent hiss with a slightly louder valve opening. */
static float down_sample(Voice *voice, uint32_t index, uint32_t count) {
    float raw = noise_next(&voice->noise);
    float hiss = highpass(&voice->high, raw, 500.0f);
    hiss = lowpass(&voice->low2, lowpass(&voice->low1, hiss, 5000.0f), 5000.0f);
    voice->burst *= decay(0.05f);
    float sample = hiss * (1.0f + 0.5f * voice->burst) * flutter(voice, raw, 10.0f);
    return sample * edge_fade(index, count, 0.008f, 0.05f);
}

/* Raising: a compressor motor spinning up to a buzzing drone, piston rattle and some airflow. */
static float up_sample(Voice *voice, uint32_t index, uint32_t count) {
    float raw = noise_next(&voice->noise);
    voice->spin *= decay(0.06f);
    float frequency = 120.0f - 65.0f * voice->spin;
    voice->phase += frequency / (float)AIR_SOUND_RATE;
    if (voice->phase >= 1.0f) {
        voice->phase -= 1.0f;
    }
    float doubled = voice->phase * 2.0f;
    doubled -= doubled >= 1.0f ? 1.0f : 0.0f;
    float tone = 0.6f * (2.0f * voice->phase - 1.0f) + 0.25f * (doubled < 0.5f ? 1.0f : -1.0f);
    tone = lowpass(&voice->low2, lowpass(&voice->low1, tone, 1500.0f), 1500.0f);
    float rattle = highpass(&voice->rattle_high, raw, 2000.0f) * (voice->phase < 0.15f ? 0.25f : 0.0f);
    float air = lowpass(&voice->air_low, highpass(&voice->air_high, raw, 800.0f), 4000.0f) * 0.12f;
    return (tone + rattle + air) * edge_fade(index, count, 0.015f, 0.05f);
}

static uint32_t sample_count(AirSoundKind kind) {
    return (uint32_t)(durations[kind] * (float)AIR_SOUND_RATE);
}

/* Renders `kind`; with `output` NULL only returns the peak. */
static float render(AirSoundKind kind, float gain, int16_t *output) {
    Voice voice = {.noise = {0x9e3779b9u + (uint32_t)kind}, .burst = 1.0f, .pop = 1.0f,
                   .spin = 1.0f};
    uint32_t count = sample_count(kind);
    float peak = 0.0f;
    for (uint32_t index = 0; index < count; ++index) {
        float sample = kind == AIR_SOUND_DUMP   ? dump_sample(&voice, index, count)
                       : kind == AIR_SOUND_DOWN ? down_sample(&voice, index, count)
                                                : up_sample(&voice, index, count);
        float magnitude = sample < 0.0f ? -sample : sample;
        peak = magnitude > peak ? magnitude : peak;
        if (output) {
            float scaled = sample * gain * 32767.0f;
            scaled = scaled > 32767.0f ? 32767.0f : scaled < -32767.0f ? -32767.0f : scaled;
            output[index] = (int16_t)(scaled + (scaled < 0.0f ? -0.5f : 0.5f));
        }
    }
    return peak;
}

static void put16(unsigned char *at, uint32_t value) {
    at[0] = (unsigned char)value;
    at[1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *at, uint32_t value) {
    put16(at, value & 0xffffu);
    put16(at + 2, value >> 16);
}

static uint32_t get16(const unsigned char *at) {
    return (uint32_t)at[0] | (uint32_t)at[1] << 8;
}

static uint32_t get32(const unsigned char *at) {
    return get16(at) | get16(at + 2) << 16;
}

static int tag_is(const unsigned char *at, const char *tag) {
    return at[0] == (unsigned char)tag[0] && at[1] == (unsigned char)tag[1] &&
           at[2] == (unsigned char)tag[2] && at[3] == (unsigned char)tag[3];
}

static void put_tag(unsigned char *at, const char *tag) {
    for (unsigned index = 0; index < 4; ++index) {
        at[index] = (unsigned char)tag[index];
    }
}

static void write_header(unsigned char *wav, uint32_t channels, uint32_t rate, uint32_t bytes) {
    put_tag(wav, "RIFF");
    put32(wav + 4, 36u + bytes);
    put_tag(wav + 8, "WAVE");
    put_tag(wav + 12, "fmt ");
    put32(wav + 16, 16);
    put16(wav + 20, 1);
    put16(wav + 22, channels);
    put32(wav + 24, rate);
    put32(wav + 28, rate * channels * 2u);
    put16(wav + 32, channels * 2u);
    put16(wav + 34, 16);
    put_tag(wav + 36, "data");
    put32(wav + 40, bytes);
}

static int valid_kind(AirSoundKind kind) {
    return kind > AIR_SOUND_NONE && kind < AIR_SOUND_KIND_COUNT;
}

static float volume_scale(int volume) {
    volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    return (float)volume * 0.01f;
}

uint32_t air_sound_builtin_size(AirSoundKind kind) {
    return valid_kind(kind) ? AIR_SOUND_WAV_HEADER + sample_count(kind) * 2u : 0;
}

uint32_t air_sound_build(AirSoundKind kind, int volume, unsigned char *wav, uint32_t capacity) {
    uint32_t size = air_sound_builtin_size(kind);
    if (!size || capacity < size || !wav) {
        return 0;
    }
    float peak = render(kind, 0.0f, 0);
    float gain = peak > 0.0f ? loudness[kind] * volume_scale(volume) / peak : 0.0f;
    write_header(wav, 1, AIR_SOUND_RATE, size - AIR_SOUND_WAV_HEADER);
    /* The data starts at offset 44, so the int16 samples are 2-byte aligned in the buffer. */
    render(kind, gain, (int16_t *)(void *)(wav + AIR_SOUND_WAV_HEADER));
    return size;
}

uint32_t air_sound_prepare_wav(const unsigned char *input, uint32_t size, int volume,
                               unsigned char *output, unsigned *duration_ms) {
    *duration_ms = 0;
    if (!input || !output || size < 12 || size > AIR_SOUND_MAX_FILE || !tag_is(input, "RIFF") ||
        !tag_is(input + 8, "WAVE")) {
        return 0;
    }
    uint32_t channels = 0, rate = 0, data_offset = 0, data_size = 0;
    for (uint32_t cursor = 12; cursor + 8 <= size;) {
        uint32_t length = get32(input + cursor + 4);
        uint32_t body = cursor + 8;
        if (length > size - body) {
            return 0;
        }
        if (tag_is(input + cursor, "fmt ")) {
            if (length < 16) {
                return 0;
            }
            uint32_t format = get16(input + body);
            if (format == 0xfffeu && length >= 26) {
                format = get16(input + body + 24); /* WAVE_FORMAT_EXTENSIBLE sub-format */
            }
            channels = get16(input + body + 2);
            rate = get32(input + body + 4);
            if (format != 1 || channels < 1 || channels > 2 || rate < 8000 || rate > 96000 ||
                get16(input + body + 12) != channels * 2u || get16(input + body + 14) != 16) {
                return 0;
            }
        } else if (tag_is(input + cursor, "data") && !data_offset) {
            data_offset = body;
            data_size = length;
        }
        uint32_t next = body + length + (length & 1u);
        if (next < body) {
            return 0;
        }
        cursor = next;
    }
    if (!channels || !data_offset) {
        return 0;
    }
    data_size -= data_size % (channels * 2u);
    /* Both chunk headers precede or follow the data, so header + data always fits in `size`. */
    if (!data_size || AIR_SOUND_WAV_HEADER + data_size > size) {
        return 0;
    }
    write_header(output, channels, rate, data_size);
    int32_t scale = (int32_t)(volume_scale(volume) * 256.0f + 0.5f);
    for (uint32_t offset = 0; offset < data_size; offset += 2) {
        int32_t sample = (int16_t)get16(input + data_offset + offset);
        put16(output + AIR_SOUND_WAV_HEADER + offset, (uint32_t)(uint16_t)(sample * scale / 256));
    }
    *duration_ms = (unsigned)((uint64_t)data_size * 1000u / (rate * channels * 2u));
    return AIR_SOUND_WAV_HEADER + data_size;
}

unsigned air_sound_duration_ms(const unsigned char *wav, uint32_t size) {
    if (!wav || size < AIR_SOUND_WAV_HEADER) {
        return 0;
    }
    uint32_t bytes_per_second = get32(wav + 28);
    return bytes_per_second ? (unsigned)((uint64_t)get32(wav + 40) * 1000u / bytes_per_second) : 0;
}

void air_sound_state_reset(AirSoundState *state) {
    state->playing = AIR_SOUND_NONE;
    state->started_ms = 0;
    state->duration_ms = 0;
}

static int finished(const AirSoundState *state, unsigned long long now_ms, unsigned lead) {
    unsigned end = state->duration_ms > lead ? state->duration_ms - lead : 0;
    return now_ms - state->started_ms >= end;
}

AirSoundCue air_sound_cue(AirSoundState *state, unsigned events, float rise,
                          unsigned long long now_ms) {
    AirSoundCue keep = {AIR_SOUND_CUE_KEEP, AIR_SOUND_NONE};
    if (events & AIR_RIDE_EVENT_AIR_OUT) {
        return (AirSoundCue){AIR_SOUND_CUE_PLAY, AIR_SOUND_DUMP};
    }
    if (!(events & AIR_RIDE_EVENT_MOVING)) {
        if (state->playing == AIR_SOUND_UP || state->playing == AIR_SOUND_DOWN) {
            state->playing = AIR_SOUND_NONE;
            return (AirSoundCue){AIR_SOUND_CUE_STOP, AIR_SOUND_NONE};
        }
        if (state->playing == AIR_SOUND_DUMP && finished(state, now_ms, 0)) {
            state->playing = AIR_SOUND_NONE; /* the dump tail rang out on its own */
        }
        return keep;
    }
    AirSoundKind wanted = rise > 0.0f ? AIR_SOUND_UP : AIR_SOUND_DOWN;
    if (state->playing == AIR_SOUND_DUMP && wanted == AIR_SOUND_DOWN &&
        !finished(state, now_ms, 0)) {
        return keep; /* still dumping */
    }
    if (state->playing != wanted || finished(state, now_ms, RESTART_LEAD_MS)) {
        return (AirSoundCue){AIR_SOUND_CUE_PLAY, wanted};
    }
    return keep;
}

void air_sound_started(AirSoundState *state, AirSoundKind kind, unsigned duration_ms,
                       unsigned long long now_ms) {
    state->playing = kind;
    state->started_ms = now_ms;
    state->duration_ms = duration_ms;
}
