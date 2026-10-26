/*
 * Tests for the air ride sound synthesis, WAV validation and cue logic.
 * Usage: air_sound_test.exe [directory]  -- with a directory, also writes the three built-in
 * sounds there as WAV files for listening.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../features/air_sound_synth.h"
#include "../features/air_suspension.h"

static int failures;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static unsigned get16(const unsigned char *at) {
    return at[0] | at[1] << 8;
}

static unsigned get32(const unsigned char *at) {
    return get16(at) | get16(at + 2) << 16;
}

static void put16(unsigned char *at, unsigned value) {
    at[0] = (unsigned char)value;
    at[1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *at, unsigned value) {
    put16(at, value & 0xffff);
    put16(at + 2, value >> 16);
}

typedef struct {
    unsigned char *wav;
    uint32_t size;
} Sound;

static Sound build(AirSoundKind kind, int volume) {
    Sound sound = {0};
    uint32_t capacity = air_sound_builtin_size(kind);
    sound.wav = malloc(capacity);
    sound.size = air_sound_build(kind, volume, sound.wav, capacity);
    return sound;
}

static int16_t sample(const Sound *sound, uint32_t index) {
    return (int16_t)get16(sound->wav + AIR_SOUND_WAV_HEADER + index * 2);
}

static uint32_t samples(const Sound *sound) {
    return (sound->size - AIR_SOUND_WAV_HEADER) / 2;
}

static int peak(const Sound *sound) {
    int result = 0;
    for (uint32_t index = 0; index < samples(sound); ++index) {
        int value = abs(sample(sound, index));
        result = value > result ? value : result;
    }
    return result;
}

static double rms(const Sound *sound, uint32_t first, uint32_t count) {
    double sum = 0.0;
    for (uint32_t index = first; index < first + count; ++index) {
        sum += (double)sample(sound, index) * sample(sound, index);
    }
    return count ? sum / count : 0.0; /* mean square is enough for comparisons */
}

static unsigned crossings_per_second(const Sound *sound) {
    unsigned count = 0;
    for (uint32_t index = 1; index < samples(sound); ++index) {
        count += (sample(sound, index - 1) < 0) != (sample(sound, index) < 0);
    }
    return (unsigned)((unsigned long long)count * AIR_SOUND_RATE / samples(sound));
}

static void test_builtin(void) {
    const float loud[] = {0.0f, 0.95f, 0.55f, 0.60f};
    const unsigned lengths[] = {0, 1700, 1200, 1400};
    CHECK(air_sound_builtin_size(AIR_SOUND_NONE) == 0);
    CHECK(air_sound_builtin_size(AIR_SOUND_KIND_COUNT) == 0);
    unsigned char tiny[64];
    CHECK(air_sound_build(AIR_SOUND_DUMP, 100, tiny, sizeof(tiny)) == 0);
    for (int kind = AIR_SOUND_DUMP; kind < AIR_SOUND_KIND_COUNT; ++kind) {
        Sound full = build((AirSoundKind)kind, 100);
        CHECK(full.size == air_sound_builtin_size((AirSoundKind)kind));
        CHECK(memcmp(full.wav, "RIFF", 4) == 0 && memcmp(full.wav + 8, "WAVEfmt ", 8) == 0);
        CHECK(get32(full.wav + 4) == full.size - 8 && get32(full.wav + 40) == full.size - 44);
        CHECK(get16(full.wav + 20) == 1 && get16(full.wav + 22) == 1 && get16(full.wav + 34) == 16);
        CHECK(get32(full.wav + 24) == AIR_SOUND_RATE);
        CHECK(air_sound_duration_ms(full.wav, full.size) == lengths[kind]);
        int expected = (int)(loud[kind] * 32767.0f);
        CHECK(abs(peak(&full) - expected) <= expected / 100 + 1);
        /* Begins and ends quietly, so a restart or cut-off is not a click. */
        CHECK(abs(sample(&full, 0)) < 200 && abs(sample(&full, samples(&full) - 1)) < 200);

        Sound again = build((AirSoundKind)kind, 100);
        CHECK(again.size == full.size && memcmp(again.wav, full.wav, full.size) == 0);
        Sound half = build((AirSoundKind)kind, 50);
        CHECK(abs(peak(&half) - expected / 2) <= expected / 100 + 1);
        Sound silent = build((AirSoundKind)kind, 0);
        CHECK(silent.size == full.size && peak(&silent) == 0);
        Sound over = build((AirSoundKind)kind, 250);
        CHECK(peak(&over) == peak(&full));
        free(full.wav);
        free(again.wav);
        free(half.wav);
        free(silent.wav);
        free(over.wav);
    }

    Sound dump = build(AIR_SOUND_DUMP, 100), down = build(AIR_SOUND_DOWN, 100);
    Sound up = build(AIR_SOUND_UP, 100);
    uint32_t window = AIR_SOUND_RATE * 3 / 10;
    /* The blast decays as the pressure drops; the vent hiss holds steady. */
    CHECK(rms(&dump, 0, window) > 16.0 * rms(&dump, samples(&dump) - window, window));
    CHECK(rms(&down, window, window) < 2.0 * rms(&down, samples(&down) - 2 * window, window));
    CHECK(rms(&down, samples(&down) - 2 * window, window) <
          2.0 * rms(&down, window, window));
    /* Hiss is broadband noise; the compressor is a low buzzing tone. */
    unsigned dump_rate = crossings_per_second(&dump), down_rate = crossings_per_second(&down);
    unsigned up_rate = crossings_per_second(&up);
    printf("zero crossings per second: dump %u, down %u, up %u\n", dump_rate, down_rate, up_rate);
    CHECK(dump_rate > 2000 && down_rate > 2000);
    CHECK(up_rate < down_rate / 2);
    free(dump.wav);
    free(down.wav);
    free(up.wav);
}

/* A WAV with a LIST chunk (odd size, padded) between "fmt " and "data". */
static uint32_t make_wav(unsigned char *out, unsigned format, unsigned channels, unsigned rate,
                         unsigned bits, unsigned frames, const int16_t *values) {
    uint32_t data_bytes = frames * channels * 2;
    unsigned char *at = out;
    memcpy(at, "RIFF", 4);
    memcpy(at + 8, "WAVE", 4);
    at += 12;
    memcpy(at, "fmt ", 4);
    put32(at + 4, 16);
    put16(at + 8, format);
    put16(at + 10, channels);
    put32(at + 12, rate);
    put32(at + 16, rate * channels * bits / 8);
    put16(at + 20, channels * bits / 8);
    put16(at + 22, bits);
    at += 24;
    memcpy(at, "LIST", 4);
    put32(at + 4, 3);
    memcpy(at + 8, "abc\0", 4);
    at += 12;
    memcpy(at, "data", 4);
    put32(at + 4, data_bytes);
    for (uint32_t index = 0; index < frames * channels; ++index) {
        put16(at + 8 + index * 2, (uint16_t)values[index % 4]);
    }
    at += 8 + data_bytes;
    uint32_t size = (uint32_t)(at - out);
    put32(out + 4, size - 8);
    return size;
}

static void test_user_wav(void) {
    static unsigned char input[200000], output[200000];
    const int16_t values[4] = {1000, -1000, 32767, -32768};
    unsigned duration = 0;
    uint32_t size = make_wav(input, 1, 2, 44100, 16, 22050, values);
    uint32_t written = air_sound_prepare_wav(input, size, 50, output, &duration);
    CHECK(written == 44 + 22050 * 4 && written <= size);
    CHECK(duration == 500 && air_sound_duration_ms(output, written) == 500);
    CHECK(get16(output + 22) == 2 && get32(output + 24) == 44100 && get16(output + 32) == 4);
    CHECK((int16_t)get16(output + 44) == 500 && (int16_t)get16(output + 46) == -500);
    CHECK((int16_t)get16(output + 48) == 16383 && (int16_t)get16(output + 50) == -16384);
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == written);
    CHECK((int16_t)get16(output + 48) == 32767 && (int16_t)get16(output + 50) == -32768);

    size = make_wav(input, 1, 1, 22050, 16, 1000, values);
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 44 + 2000);

    /* Extensible format with a PCM sub-format is accepted; anything else is refused. */
    uint32_t extended = make_wav(input, 0xfffe, 1, 22050, 16, 100, values);
    CHECK(air_sound_prepare_wav(input, extended, 100, output, &duration) == 0); /* fmt too short */

    struct {
        unsigned format, channels, rate, bits;
    } refused[] = {{3, 1, 22050, 16}, {1, 1, 22050, 8}, {1, 3, 22050, 16}, {1, 1, 4000, 16},
                   {1, 1, 192000, 16}, {2, 1, 22050, 16}};
    for (unsigned index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        size = make_wav(input, refused[index].format, refused[index].channels,
                        refused[index].rate, refused[index].bits, 100, values);
        CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
    }

    size = make_wav(input, 1, 1, 22050, 16, 100, values);
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) != 0);
    CHECK(air_sound_prepare_wav(input, size - 1, 100, output, &duration) == 0); /* truncated */
    CHECK(air_sound_prepare_wav(input, 11, 100, output, &duration) == 0);
    CHECK(air_sound_prepare_wav(NULL, size, 100, output, &duration) == 0);
    CHECK(air_sound_prepare_wav(input, AIR_SOUND_MAX_FILE + 1, 100, output, &duration) == 0);
    memcpy(input, "RIFX", 4);
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
    memcpy(input, "RIFF", 4);
    put32(input + 12 + 24 + 4, 0xfffffff0u); /* LIST length overflows */
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
    put32(input + 12 + 24 + 4, 3);
    put32(input + 12 + 4, 8); /* fmt chunk too short */
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
    put32(input + 12 + 4, 16);
    memcpy(input + 12, "junk", 4); /* no fmt chunk */
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
    memcpy(input + 12, "fmt ", 4);
    memcpy(input + 12 + 24 + 12, "dat_", 4); /* no data chunk */
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
    memcpy(input + 12 + 24 + 12, "data", 4);
    put32(input + 12 + 24 + 12 + 4, 1); /* less than one frame of data */
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
    size = make_wav(input, 1, 1, 22050, 16, 101, values);
    put32(input + 12 + 24 + 12 + 4, 201); /* odd data length is cut to whole frames */
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 44 + 200);
    put32(input + 12 + 24 + 12 + 4, 203); /* more data declared than the file holds */
    CHECK(air_sound_prepare_wav(input, size, 100, output, &duration) == 0);
}

static void test_cue(void) {
    AirSoundState state;
    air_sound_state_reset(&state);
    AirSoundCue cue = air_sound_cue(&state, 0, 0.0f, 100);
    CHECK(cue.action == AIR_SOUND_CUE_KEEP);

    /* Lowering starts the vent, keeps it, restarts it shortly before it ends, then cuts it. */
    cue = air_sound_cue(&state, AIR_RIDE_EVENT_MOVING, -0.01f, 1000);
    CHECK(cue.action == AIR_SOUND_CUE_PLAY && cue.kind == AIR_SOUND_DOWN);
    air_sound_started(&state, AIR_SOUND_DOWN, 1200, 1000);
    CHECK(air_sound_cue(&state, AIR_RIDE_EVENT_MOVING, -0.01f, 1500).action ==
          AIR_SOUND_CUE_KEEP);
    cue = air_sound_cue(&state, AIR_RIDE_EVENT_MOVING, -0.01f, 2170);
    CHECK(cue.action == AIR_SOUND_CUE_PLAY && cue.kind == AIR_SOUND_DOWN);
    air_sound_started(&state, AIR_SOUND_DOWN, 1200, 2170);
    /* Reversing switches to the compressor. */
    cue = air_sound_cue(&state, AIR_RIDE_EVENT_MOVING, 0.01f, 2200);
    CHECK(cue.action == AIR_SOUND_CUE_PLAY && cue.kind == AIR_SOUND_UP);
    air_sound_started(&state, AIR_SOUND_UP, 1400, 2200);
    cue = air_sound_cue(&state, 0, 0.0f, 2300);
    CHECK(cue.action == AIR_SOUND_CUE_STOP && state.playing == AIR_SOUND_NONE);
    CHECK(air_sound_cue(&state, 0, 0.0f, 2316).action == AIR_SOUND_CUE_KEEP);

    /* An air-out plays the blast, which is not cut when the car settles. */
    cue = air_sound_cue(&state, AIR_RIDE_EVENT_AIR_OUT | AIR_RIDE_EVENT_MOVING, -0.01f, 5000);
    CHECK(cue.action == AIR_SOUND_CUE_PLAY && cue.kind == AIR_SOUND_DUMP);
    air_sound_started(&state, AIR_SOUND_DUMP, 1700, 5000);
    CHECK(air_sound_cue(&state, AIR_RIDE_EVENT_MOVING, -0.01f, 5500).action ==
          AIR_SOUND_CUE_KEEP);
    CHECK(air_sound_cue(&state, 0, 0.0f, 5600).action == AIR_SOUND_CUE_KEEP);
    CHECK(state.playing == AIR_SOUND_DUMP);
    CHECK(air_sound_cue(&state, 0, 0.0f, 6700).action == AIR_SOUND_CUE_KEEP);
    CHECK(state.playing == AIR_SOUND_NONE);
    /* Still lowering after a finished blast falls back to the vent; raising interrupts it. */
    air_sound_started(&state, AIR_SOUND_DUMP, 1700, 7000);
    cue = air_sound_cue(&state, AIR_RIDE_EVENT_MOVING, -0.01f, 8700);
    CHECK(cue.action == AIR_SOUND_CUE_PLAY && cue.kind == AIR_SOUND_DOWN);
    air_sound_started(&state, AIR_SOUND_DUMP, 1700, 9000);
    cue = air_sound_cue(&state, AIR_RIDE_EVENT_MOVING, 0.01f, 9100);
    CHECK(cue.action == AIR_SOUND_CUE_PLAY && cue.kind == AIR_SOUND_UP);
}

/* Full controller sequence: air-out, settle, air back up, settle. */
static void test_with_controller(void) {
    AirSuspensionConfig config = {.travel_rate = 0.35f, .air_out_rate = 0.8f, .step = 0.02f,
                                  .hold_rate = 0.25f, .repeat_delay_ms = 300};
    air_suspension_configure(&config);
    air_suspension_attach(0.5f, 0.5f);
    AirSoundState state;
    air_sound_state_reset(&state);
    unsigned long long clock = 1000;
    unsigned plays[AIR_SOUND_KIND_COUNT] = {0}, stops = 0;
    for (int frame = 0; frame < 400; ++frame) {
        unsigned commands = (frame == 1 || frame == 200) ? AIR_RIDE_AIR_OUT : 0;
        clock += 16;
        AirSuspensionTargets before = air_suspension_targets();
        unsigned events = air_suspension_update(commands, 0.016f, clock, 1);
        AirSuspensionTargets after = air_suspension_targets();
        AirSoundCue cue = air_sound_cue(&state, events,
                                        after.front - before.front + after.rear - before.rear,
                                        clock);
        if (cue.action == AIR_SOUND_CUE_PLAY) {
            ++plays[cue.kind];
            air_sound_started(&state, cue.kind, cue.kind == AIR_SOUND_UP ? 1400 : 1700, clock);
        }
        stops += cue.action == AIR_SOUND_CUE_STOP;
    }
    CHECK(plays[AIR_SOUND_DUMP] == 1 && plays[AIR_SOUND_DOWN] == 0);
    CHECK(plays[AIR_SOUND_UP] == 1 && stops == 1);
    CHECK(air_suspension_targets().front == 0.5f);
}

/* Full lift plays the raise sound, stock from up high plays the lower sound. */
static void test_lift_and_stock_sounds(void) {
    AirSuspensionConfig config = {.travel_rate = 0.35f, .air_out_rate = 0.8f, .step = 0.02f,
                                  .hold_rate = 0.25f, .extra_lift = 0.5f, .repeat_delay_ms = 300};
    air_suspension_configure(&config);
    air_suspension_attach(0.5f, 0.5f);
    AirSoundState state;
    air_sound_state_reset(&state);
    unsigned long long clock = 1000;
    unsigned plays[AIR_SOUND_KIND_COUNT] = {0};
    for (int frame = 0; frame < 600; ++frame) {
        unsigned commands = frame == 1 ? AIR_RIDE_LIFT : frame == 300 ? AIR_RIDE_STOCK : 0;
        clock += 16;
        AirSuspensionTargets before = air_suspension_targets();
        unsigned events = air_suspension_update(commands, 0.016f, clock, 1);
        AirSuspensionTargets after = air_suspension_targets();
        AirSoundCue cue = air_sound_cue(&state, events,
                                        after.front - before.front + after.rear - before.rear,
                                        clock);
        if (cue.action == AIR_SOUND_CUE_PLAY) {
            ++plays[cue.kind];
            air_sound_started(&state, cue.kind, 1200, clock);
        }
    }
    CHECK(plays[AIR_SOUND_UP] >= 1 && plays[AIR_SOUND_DOWN] >= 1 && plays[AIR_SOUND_DUMP] == 0);
    CHECK(air_suspension_targets().front == 0.5f);
}

static void write_previews(const char *directory) {
    const char *names[] = {0, "AirRide_AirOut_builtin.wav", "AirRide_Down_builtin.wav",
                           "AirRide_Up_builtin.wav"};
    for (int kind = AIR_SOUND_DUMP; kind < AIR_SOUND_KIND_COUNT; ++kind) {
        Sound sound = build((AirSoundKind)kind, 80);
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, names[kind]);
        FILE *file = fopen(path, "wb");
        CHECK(file != NULL);
        if (file) {
            CHECK(fwrite(sound.wav, 1, sound.size, file) == sound.size);
            fclose(file);
            printf("wrote %s\n", path);
        }
        free(sound.wav);
    }
}

int main(int argc, char **argv) {
    test_builtin();
    test_user_wav();
    test_cue();
    test_with_controller();
    test_lift_and_stock_sounds();
    if (argc > 1) {
        write_previews(argv[1]);
    }
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all air sound tests passed\n");
    return 0;
}
