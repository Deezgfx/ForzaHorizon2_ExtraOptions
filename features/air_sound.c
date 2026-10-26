#include "air_sound.h"
#include "air_sound_synth.h"
#include "logging.h"
#include "support.h"

#define SOUND_ASYNC 0x0001u
#define SOUND_NO_DEFAULT 0x0002u
#define SOUND_MEMORY 0x0004u

typedef BOOL(WINAPI *PlaySoundFunction)(LPCWSTR sound, HMODULE module, DWORD flags);

static const wchar_t *const file_names[AIR_SOUND_KIND_COUNT] = {
    0, L"AirRide_AirOut.wav", L"AirRide_Down.wav", L"AirRide_Up.wav"};
static const char *const log_names[AIR_SOUND_KIND_COUNT] = {
    0, "AIRSOUND: using AirRide_AirOut.wav\r\n", "AIRSOUND: using AirRide_Down.wav\r\n",
    "AIRSOUND: using AirRide_Up.wav\r\n"};

static wchar_t directory[PATH_CAPACITY];
/* Scratch paths, static because they are too large for the stack; only the game thread uses them. */
static wchar_t system_directory[PATH_CAPACITY], path[PATH_CAPACITY];
static PlaySoundFunction play_sound;
static int api_state; /* 0 untried, 1 ready, -1 unavailable */
static int enabled, volume, dirty = 1;
/* Buffers are only replaced on the game thread after the current sound has been stopped,
   because PlaySoundW keeps reading an asynchronous SND_MEMORY buffer while it plays. */
static unsigned char *buffers[AIR_SOUND_KIND_COUNT];
static unsigned durations[AIR_SOUND_KIND_COUNT];
static AirSoundState state;

void air_sound_set_directory(const wchar_t *value) {
    size_t length = 0;
    while (value && value[length] && length + 1 < PATH_CAPACITY) {
        directory[length] = value[length];
        ++length;
    }
    directory[length] = 0;
    dirty = 1;
}

void air_sound_configure(int on, int level) {
    if (on != enabled || level != volume) {
        dirty = 1;
    }
    enabled = on;
    volume = level;
}

static int load_api(void) {
    if (!api_state) {
        api_state = -1;
        UINT length = GetSystemDirectoryW(system_directory, PATH_CAPACITY);
        HMODULE self = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(uintptr_t)load_api, &self);
        /* A full system path: under Azimuth this DLL is itself loaded as winmm.dll. */
        HMODULE winmm = length && length < PATH_CAPACITY &&
                                path_join(path, system_directory, L"winmm.dll")
                            ? LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)
                            : NULL;
        if (winmm && winmm != self) {
            play_sound = (PlaySoundFunction)(uintptr_t)GetProcAddress(winmm, "PlaySoundW");
        }
        if (play_sound) {
            api_state = 1;
        } else {
            log_append("AIRSOUND: the system PlaySoundW is unavailable; air ride sounds are off.\r\n");
        }
    }
    return api_state > 0;
}

static unsigned char *read_user_file(uint32_t *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER length;
    unsigned char *bytes = 0;
    if (GetFileSizeEx(file, &length) && length.QuadPart >= 12 &&
        length.QuadPart <= AIR_SOUND_MAX_FILE) {
        *size = (uint32_t)length.QuadPart;
        bytes = HeapAlloc(GetProcessHeap(), 0, *size);
        DWORD loaded = 0, count = 0;
        while (bytes && loaded < *size && ReadFile(file, bytes + loaded, *size - loaded, &count, 0) &&
               count) {
            loaded += count;
        }
        if (bytes && loaded != *size) {
            HeapFree(GetProcessHeap(), 0, bytes);
            bytes = 0;
        }
    }
    CloseHandle(file);
    return bytes;
}

static unsigned char *user_sound(AirSoundKind kind, unsigned *duration) {
    uint32_t size = 0;
    unsigned char *input =
        directory[0] && path_join(path, directory, file_names[kind]) ? read_user_file(&size) : 0;
    if (!input) {
        return 0;
    }
    unsigned char *output = HeapAlloc(GetProcessHeap(), 0, size);
    if (output && !air_sound_prepare_wav(input, size, volume, output, duration)) {
        HeapFree(GetProcessHeap(), 0, output);
        output = 0;
        log_append("AIRSOUND: a custom WAV is not 16-bit PCM mono/stereo; using the built-in "
                   "sound instead.\r\n");
    } else if (output) {
        log_append(log_names[kind]);
    }
    HeapFree(GetProcessHeap(), 0, input);
    return output;
}

static void prepare(void) {
    for (unsigned kind = AIR_SOUND_DUMP; kind < AIR_SOUND_KIND_COUNT; ++kind) {
        if (buffers[kind]) {
            HeapFree(GetProcessHeap(), 0, buffers[kind]);
            buffers[kind] = 0;
        }
        if (!enabled) {
            continue;
        }
        buffers[kind] = user_sound((AirSoundKind)kind, &durations[kind]);
        if (!buffers[kind]) {
            uint32_t size = air_sound_builtin_size((AirSoundKind)kind);
            buffers[kind] = HeapAlloc(GetProcessHeap(), 0, size);
            if (buffers[kind] && air_sound_build((AirSoundKind)kind, volume, buffers[kind], size)) {
                durations[kind] = air_sound_duration_ms(buffers[kind], size);
            } else if (buffers[kind]) {
                HeapFree(GetProcessHeap(), 0, buffers[kind]);
                buffers[kind] = 0;
            }
        }
    }
    dirty = 0;
}

#ifdef FH2_TWEAKS_TEST
/* Prepares the sounds from `folder` and plays each one synchronously through the real
   PlaySoundW. Returns a bit per sound that played, and stores each sound's length. */
__declspec(dllexport) int Fh2TestAirSound(const wchar_t *folder, int level, unsigned *lengths) {
    air_sound_set_directory(folder);
    air_sound_configure(1, level);
    air_sound_stop();
    if (!load_api()) {
        return -1;
    }
    prepare();
    int played = 0;
    for (unsigned kind = AIR_SOUND_DUMP; kind < AIR_SOUND_KIND_COUNT; ++kind) {
        lengths[kind] = durations[kind];
        if (buffers[kind] &&
            play_sound((LPCWSTR)(void *)buffers[kind], NULL, SOUND_NO_DEFAULT | SOUND_MEMORY)) {
            played |= 1 << kind;
        }
    }
    air_sound_configure(0, level);
    prepare();
    return played;
}
#endif

void air_sound_stop(void) {
    if (state.playing != AIR_SOUND_NONE && api_state > 0) {
        play_sound(NULL, NULL, 0);
    }
    air_sound_state_reset(&state);
}

void air_sound_frame(unsigned events, float rise, uint64_t now_ms) {
    if (dirty) {
        air_sound_stop();
        if (enabled && !load_api()) {
            enabled = 0;
        }
        prepare();
    }
    if (!enabled || !volume) {
        return;
    }
    AirSoundCue cue = air_sound_cue(&state, events, rise, now_ms);
    if (cue.action == AIR_SOUND_CUE_STOP) {
        play_sound(NULL, NULL, 0);
    } else if (cue.action == AIR_SOUND_CUE_PLAY && buffers[cue.kind]) {
        /* A failed start is still recorded, so it is retried once per sound length, not per frame. */
        play_sound((LPCWSTR)(void *)buffers[cue.kind], NULL,
                   SOUND_ASYNC | SOUND_NO_DEFAULT | SOUND_MEMORY);
        air_sound_started(&state, cue.kind, durations[cue.kind], now_ms);
    }
}
