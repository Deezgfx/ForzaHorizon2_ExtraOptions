#ifndef FH2_AIR_SOUND_SYNTH_H
#define FH2_AIR_SOUND_SYNTH_H

/*
 * Platform-independent part of the air ride sounds: procedural synthesis of the built-in
 * sounds into in-memory WAV files, validation and volume scaling of user-supplied WAV files,
 * and the cue logic that decides which sound a controller frame should play. No Windows API is
 * used here, so tests/air_sound_test.c can exercise all of it natively.
 */

#include <stdint.h>

#define AIR_SOUND_RATE 32000u
#define AIR_SOUND_WAV_HEADER 44u
#define AIR_SOUND_MAX_FILE (4u * 1024u * 1024u)

typedef enum {
    AIR_SOUND_NONE = 0,
    AIR_SOUND_DUMP, /* one-shot air-out blast */
    AIR_SOUND_DOWN, /* valve venting while the car lowers, repeated while it keeps moving */
    AIR_SOUND_UP,   /* compressor while the car rises, repeated while it keeps moving */
    AIR_SOUND_KIND_COUNT
} AirSoundKind;

typedef enum {
    AIR_SOUND_CUE_KEEP = 0, /* leave whatever is playing alone */
    AIR_SOUND_CUE_PLAY,     /* start (or restart) `kind` from the beginning */
    AIR_SOUND_CUE_STOP      /* cut the current sound off: the valve closed */
} AirSoundAction;

typedef struct {
    AirSoundAction action;
    AirSoundKind kind;
} AirSoundCue;

typedef struct {
    AirSoundKind playing;
    unsigned long long started_ms;
    unsigned duration_ms;
} AirSoundState;

/* Bytes needed for the built-in sound `kind`, header included. 0 for an invalid kind. */
uint32_t air_sound_builtin_size(AirSoundKind kind);
/*
 * Synthesize built-in sound `kind` as a mono 16-bit PCM WAV at AIR_SOUND_RATE. `volume` is
 * 0..100 and scales the normalized peak. Deterministic for equal arguments. Returns the bytes
 * written, 0 when `capacity` is too small or the kind is invalid.
 */
uint32_t air_sound_build(AirSoundKind kind, int volume, unsigned char *wav, uint32_t capacity);
/*
 * Validate a user WAV (RIFF/WAVE, PCM 16-bit, 1 or 2 channels, 8000..96000 Hz, extra chunks
 * allowed) and copy it into `output` as a minimal 44-byte-header WAV with samples scaled by
 * `volume`. `output` needs at least `size` bytes. Returns the bytes written (0 = rejected) and
 * stores the play length in milliseconds.
 */
uint32_t air_sound_prepare_wav(const unsigned char *input, uint32_t size, int volume,
                               unsigned char *output, unsigned *duration_ms);
/* Play length of a WAV produced by air_sound_build or air_sound_prepare_wav. */
unsigned air_sound_duration_ms(const unsigned char *wav, uint32_t size);

void air_sound_state_reset(AirSoundState *state);
/*
 * Decide what to play for one controller frame. `events` are AIR_RIDE_EVENT_* flags from
 * air_suspension_update, `rise` is the summed front+rear position change this frame (positive
 * up). Repeating sounds restart shortly before they end while the car is still moving. A PLAY
 * cue must be followed by air_sound_started() once the sound actually starts.
 */
AirSoundCue air_sound_cue(AirSoundState *state, unsigned events, float rise,
                          unsigned long long now_ms);
void air_sound_started(AirSoundState *state, AirSoundKind kind, unsigned duration_ms,
                       unsigned long long now_ms);

#endif
