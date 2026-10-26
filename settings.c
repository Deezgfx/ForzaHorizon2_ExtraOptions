#include "settings.h"
#include "features.h"
#include "support.h"

#define INI_MAX_SIZE 16384
#define SETTING(section, key, field, type) {section, key, key, offsetof(ModOptions, field), type, 0, 0}
#define AIR_RIDE_FLAG(key, field) \
    {"AirRide", key, "AirRide" key, offsetof(ModOptions, field), SETTING_BOOLEAN, 0, 0}
#define AIR_RIDE_VALUE(key, field, minimum, maximum) \
    {"AirRide", key, "AirRide" key, offsetof(ModOptions, field), SETTING_INTEGER, minimum, maximum}
#define AIR_RIDE_KEY(key, index) AIR_RIDE_VALUE("Key" key, air_ride_keys[index], 0, 255)
#define AIR_RIDE_PAD(key, index) AIR_RIDE_VALUE("Pad" key, air_ride_pad[index], 0, 0xffff)
#define AIR_RIDE_PRESET(number, index)                                              \
    AIR_RIDE_VALUE("Preset" number "Front", air_ride_preset_front[index], 0, 100), \
        AIR_RIDE_VALUE("Preset" number "Rear", air_ride_preset_rear[index], 0, 100)

const SettingDefinition setting_definitions[] = {
    SETTING("Global", "Enabled", enabled, SETTING_BOOLEAN),
    SETTING("Global", "KeepRunningInBackground", keep_running_in_background, SETTING_BOOLEAN),
    SETTING("Content", "UnlockDLC", unlock_dlc, SETTING_BOOLEAN),
    SETTING("Content", "OfflineVIP", offline_vip, SETTING_BOOLEAN),
    SETTING("Frontend", "SkipIntro", skip_intro, SETTING_BOOLEAN),
    SETTING("Frontend", "AllRimsCategory", all_rims_category, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableMarketplace", enable_marketplace, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableSocial", enable_social, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableClubs", enable_clubs, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableRivals", enable_rivals, SETTING_BOOLEAN),
    SETTING("Frontend", "GetInCarFromPauseMenu", garage_from_pause, SETTING_BOOLEAN),
    SETTING("Content", "UnlockStormIslandCars", unlock_storm_island_cars, SETTING_BOOLEAN),
    SETTING("Content", "UnlockTreasureMap", unlock_treasure_map, SETTING_BOOLEAN),
    SETTING("Compatibility", "DisableParentalLocks", disable_parental_locks, SETTING_BOOLEAN),
    SETTING("Compatibility", "DisableAzimuthSaveChecksums", disable_azimuth_save_checksums,
            SETTING_BOOLEAN),
    SETTING("Profile", "CustomGamerpic", custom_gamerpic, SETTING_BOOLEAN),
    SETTING("PhotoMode", "FreeCamera", free_photo_camera, SETTING_BOOLEAN),
    SETTING("Files", "EnableMediaOverrides", media_overrides, SETTING_BOOLEAN),
    {"Autodrive", "Enabled", "AutodriveEnabled", offsetof(ModOptions, autodrive), SETTING_BOOLEAN,
     0, 0},
    AIR_RIDE_FLAG("Enabled", air_ride),
    AIR_RIDE_FLAG("FreeRoamOnly", air_ride_freeroam_only),
    AIR_RIDE_FLAG("RememberStance", air_ride_remember),
    AIR_RIDE_VALUE("MaxAdjustSpeedKmh", air_ride_max_speed_kmh, 0, 400),
    AIR_RIDE_VALUE("TravelSpeed", air_ride_travel_speed, 1, 1000),
    AIR_RIDE_VALUE("AirOutSpeed", air_ride_air_out_speed, 1, 1000),
    AIR_RIDE_VALUE("StepPercent", air_ride_step, 1, 50),
    AIR_RIDE_VALUE("HoldSpeed", air_ride_hold_speed, 1, 1000),
    AIR_RIDE_VALUE("ExtraDropPercent", air_ride_extra_drop, 0, 100),
    AIR_RIDE_VALUE("ExtraLiftPercent", air_ride_extra_lift, 0, 100),
    AIR_RIDE_VALUE("ApplyIntervalMs", air_ride_interval_ms, 16, 1000),
    AIR_RIDE_VALUE("PresetCount", air_ride_preset_count, 0, AIR_RIDE_PRESET_SLOTS),
    AIR_RIDE_FLAG("Sounds", air_ride_sounds),
    AIR_RIDE_VALUE("SoundVolume", air_ride_sound_volume, 0, 100),
    AIR_RIDE_KEY("FrontUp", 0),
    AIR_RIDE_KEY("FrontDown", 1),
    AIR_RIDE_KEY("RearUp", 2),
    AIR_RIDE_KEY("RearDown", 3),
    AIR_RIDE_KEY("BothUp", 4),
    AIR_RIDE_KEY("BothDown", 5),
    AIR_RIDE_KEY("AirOut", 6),
    AIR_RIDE_KEY("Stock", 7),
    AIR_RIDE_KEY("PresetNext", 8),
    AIR_RIDE_KEY("PresetPrevious", 9),
    AIR_RIDE_KEY("Lift", 10),
    AIR_RIDE_VALUE("PadModifier", air_ride_pad_modifier, 0, 0xffff),
    AIR_RIDE_PAD("FrontUp", 0),
    AIR_RIDE_PAD("FrontDown", 1),
    AIR_RIDE_PAD("RearUp", 2),
    AIR_RIDE_PAD("RearDown", 3),
    AIR_RIDE_PAD("BothUp", 4),
    AIR_RIDE_PAD("BothDown", 5),
    AIR_RIDE_PAD("AirOut", 6),
    AIR_RIDE_PAD("Stock", 7),
    AIR_RIDE_PAD("PresetNext", 8),
    AIR_RIDE_PAD("PresetPrevious", 9),
    AIR_RIDE_PAD("Lift", 10),
    AIR_RIDE_VALUE("PadDoubleTapLower", air_ride_pad_lower, 0, 0xffff),
    AIR_RIDE_VALUE("PadDoubleTapRaise", air_ride_pad_raise, 0, 0xffff),
    AIR_RIDE_VALUE("PadDoubleTapStock", air_ride_pad_stock_tap, 0, 0xffff),
    AIR_RIDE_VALUE("DoubleTapMs", air_ride_double_tap_ms, 150, 1000),
    AIR_RIDE_PRESET("1", 0),
    AIR_RIDE_PRESET("2", 1),
    AIR_RIDE_PRESET("3", 2),
    AIR_RIDE_PRESET("4", 3),
    AIR_RIDE_PRESET("5", 4),
};

static const SettingDefinition legacy_settings[] = {
    SETTING("Livery", "Enable4K", enable_4k_liveries, SETTING_BOOLEAN),
    SETTING("Livery", "IntermediateMSAA", livery_msaa, SETTING_MSAA),
    SETTING("Livery", "ExtraSupersampling", livery_supersampling, SETTING_BOOLEAN),
    SETTING("Frontend", "SnapToRoadOnCarChange", snap_car_to_road, SETTING_BOOLEAN),
    SETTING("Frontend", "SkipCarMeetOnStartup", skip_car_meet_on_startup, SETTING_BOOLEAN),
    SETTING("Frontend", "SpawnAtFestival", spawn_at_festival, SETTING_BOOLEAN),
    SETTING("Frontend", "SkipIneligibleCarDialogue", skip_ineligible_car_dialogue, SETTING_BOOLEAN),
    SETTING("Graphics", "SmoothSunShadows", smooth_sun_shadows, SETTING_BOOLEAN),
    SETTING("HUD", "MinimapFollowsCamera", minimap_follows_camera, SETTING_BOOLEAN),
    SETTING("PhotoMode", "Samples", photo_mode_samples, SETTING_PHOTO_SAMPLES),
    {"Autodrive", "Cinematic", "AutodriveCinematic", offsetof(ModOptions, autodrive_cinematic),
     SETTING_BOOLEAN, 0, 0},
};

const size_t setting_count = ARRAY_COUNT(setting_definitions);
static char ini_buffer[INI_MAX_SIZE + 1];
static wchar_t ini_path[PATH_CAPACITY];

void settings_defaults(ModOptions *options) {
    *options = (ModOptions){
        .enabled = 1,
        .unlock_dlc = 1,
        .offline_vip = 1,
        .skip_intro = 1,
        .enable_marketplace = 1,
        .enable_4k_liveries = 0,
        .livery_msaa = 2,
        .livery_supersampling = 0,
        .garage_from_pause = 1,
        .snap_car_to_road = 1,
        .unlock_storm_island_cars = 1,
        .disable_parental_locks = 1,
        .custom_gamerpic = 1,
        .free_photo_camera = 1,
        .unlock_treasure_map = 0,
        .autodrive = 0,
        .autodrive_cinematic = 1,
        .disable_azimuth_save_checksums = 1,
        .smooth_sun_shadows = -1,
        .skip_ineligible_car_dialogue = 0,
        .minimap_follows_camera = 1,
        .media_overrides = 0,
        .spawn_at_festival = 0,
        .skip_car_meet_on_startup = 0,
        .all_rims_category = 0,
        .drift_camera = 1,
        .enable_social = 1,
        .enable_clubs = 1,
        .enable_rivals = 1,
        .photo_mode_samples = 0,
        .air_ride = 1,
        .air_ride_freeroam_only = 1,
        .air_ride_remember = 1,
        .air_ride_max_speed_kmh = 30,
        .air_ride_travel_speed = 35,
        .air_ride_air_out_speed = 80,
        .air_ride_step = 2,
        .air_ride_hold_speed = 25,
        .air_ride_extra_drop = 50,
        .air_ride_extra_lift = 50,
        .air_ride_interval_ms = 50,
        .air_ride_preset_count = 5,
        .air_ride_sounds = 1,
        .air_ride_sound_volume = 80,
        /* Home/End front, Page Up/Down rear, Insert/Delete both, F5 air-out, F6 stock,
           F8/F7 presets, F9 full lift. */
        .air_ride_keys = {0x24, 0x23, 0x21, 0x22, 0x2d, 0x2e, 0x74, 0x75, 0x77, 0x76, 0x78},
        /* Controller: double-tap LB toggles air-out and full lift, double-tap and hold LB returns
           to the car's own tune. RB is left alone (it changes the camera in game). The
           hold-a-modifier combos are unbound by default because the game still sees those
           buttons. */
        .air_ride_pad_modifier = 0x0100,
        .air_ride_pad_lower = 0x0100,
        .air_ride_pad_raise = 0x0100,
        .air_ride_pad_stock_tap = 0,
        .air_ride_double_tap_ms = 300,
        .air_ride_preset_front = {0, 15, 30, 50, 100},
        .air_ride_preset_rear = {0, 30, 30, 50, 100},
    };
}

int settings_value(const ModOptions *options, const SettingDefinition *definition) {
    const unsigned char *fields = (const unsigned char *)options;
    return *(const int *)(fields + definition->field_offset);
}

static char *trim_whitespace(char *text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    size_t length = text_length(text);
    while (length &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r')) {
        text[--length] = 0;
    }
    return text;
}

static int known_section(const char *name) {
    if (text_equal_ignore_case(name, "Camera") || text_equal_ignore_case(name, "Graphics") ||
        text_equal_ignore_case(name, "Livery") || text_equal_ignore_case(name, "HUD") ||
        text_equal_ignore_case(name, "General") || text_equal_ignore_case(name, "Language")) {
        return 1;
    }
    for (size_t index = 0; index < setting_count; ++index) {
        if (text_equal_ignore_case(name, setting_definitions[index].section)) {
            return 1;
        }
    }
    return 0;
}

static const SettingDefinition *find_setting(const char *section, const char *key) {
    if (text_equal_ignore_case(section, "General")) {
        section = "Global";
    }
    for (size_t index = 0; index < setting_count; ++index) {
        const SettingDefinition *definition = &setting_definitions[index];
        if (text_equal_ignore_case(section, definition->section) &&
            text_equal_ignore_case(key, definition->key)) {
            return definition;
        }
    }
    for (unsigned index = 0; index < ARRAY_COUNT(legacy_settings); ++index) {
        const SettingDefinition *definition = &legacy_settings[index];
        if (text_equal_ignore_case(section, definition->section) &&
            text_equal_ignore_case(key, definition->key)) {
            return definition;
        }
    }
    return NULL;
}

static int parse_integer(const SettingDefinition *definition, const char *text, int *value) {
    unsigned base = 10, number = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (!*text) {
        return 0;
    }
    for (const char *digit = text; *digit; ++digit) {
        unsigned next;
        if (*digit >= '0' && *digit <= '9') {
            next = (unsigned)(*digit - '0');
        } else if (base == 16 && *digit >= 'a' && *digit <= 'f') {
            next = (unsigned)(*digit - 'a' + 10);
        } else if (base == 16 && *digit >= 'A' && *digit <= 'F') {
            next = (unsigned)(*digit - 'A' + 10);
        } else {
            return 0;
        }
        if (number > (definition->maximum - next) / base) {
            return 0;
        }
        number = number * base + next;
    }
    if (number < definition->minimum || number > definition->maximum) {
        return 0;
    }
    *value = (int)number;
    return 1;
}

static int parse_value(const SettingDefinition *definition, const char *text, int *value) {
    if (definition->type == SETTING_INTEGER) {
        return parse_integer(definition, text, value);
    }
    if (definition->type == SETTING_PHOTO_SAMPLES) {
        unsigned samples = 0;
        if (!*text) {
            return 0;
        }
        for (const char *digit = text; *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || samples > 4096) {
                return 0;
            }
            samples = samples * 10 + (unsigned)(*digit - '0');
        }
        if (samples > 4096 || (samples && samples < 16)) {
            return 0;
        }
        *value = (int)samples;
        return 1;
    }
    if (definition->type == SETTING_MSAA) {
        if (text_equal_ignore_case(text, "2")) {
            *value = 2;
            return 1;
        }
        if (text_equal_ignore_case(text, "4")) {
            *value = 4;
            return 1;
        }
        return 0;
    }
    if (text_equal_ignore_case(text, "0") || text_equal_ignore_case(text, "1")) {
        *value = text[0] - '0';
        return 1;
    }
    return 0;
}

static int parse_ini(char *text, ModOptions *options) {
    char section[32] = {0};
    if ((unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
        (unsigned char)text[2] == 0xbf) {
        text += 3;
    }

    while (*text) {
        char *line = text;
        while (*text && *text != '\n') {
            ++text;
        }
        if (*text) {
            *text++ = 0;
        }
        for (char *cursor = line; *cursor; ++cursor) {
            if (*cursor == ';' || *cursor == '#') {
                *cursor = 0;
                break;
            }
        }
        line = trim_whitespace(line);
        if (!*line) {
            continue;
        }

        if (*line == '[') {
            char *closing = line;
            while (*closing && *closing != ']') {
                ++closing;
            }
            if (*closing != ']' || *trim_whitespace(closing + 1)) {
                goto invalid;
            }
            *closing = 0;
            char *name = trim_whitespace(line + 1);
            size_t length = text_length(name);
            if (length >= sizeof(section) || !known_section(name)) {
                goto invalid;
            }
            memory_copy(section, name, length + 1);
            continue;
        }

        char *separator = line;
        while (*separator && *separator != '=') {
            ++separator;
        }
        if (!*separator) {
            goto invalid;
        }
        *separator = 0;
        char *key = trim_whitespace(line);
        char *value = trim_whitespace(separator + 1);

        if (text_equal_ignore_case(section, "Frontend") &&
            text_equal_ignore_case(key, "HideOnlineTabs")) {
            if (!text_equal_ignore_case(value, "0") && !text_equal_ignore_case(value, "1")) {
                goto invalid;
            }
            continue;
        }

        // Retired keys are ignored so an older INI still loads.
        if ((text_equal_ignore_case(section, "Language") &&
             (text_equal_ignore_case(key, "Text") || text_equal_ignore_case(key, "Audio"))) ||
            (text_equal_ignore_case(section, "Graphics") &&
             (text_equal_ignore_case(key, "ForceAnisoFiltering") ||
              text_equal_ignore_case(key, "MotionBlurSamples")))) {
            continue;
        }

        if (text_equal_ignore_case(section, "Camera") &&
            (text_equal_ignore_case(key, "DriftCamera") ||
             text_equal_ignore_case(key, "DriftCameraStiffness") ||
             text_equal_ignore_case(key, "FirstPersonDriftCameraStiffness"))) {
            continue;
        }

        const SettingDefinition *definition = find_setting(section, key);
        int parsed;
        if (!definition || !parse_value(definition, value, &parsed)) {
            goto invalid;
        }
        int *field = (int *)((unsigned char *)options + definition->field_offset);
        *field = parsed;
    }
    return 1;

invalid:
    status_set("REFUSED: invalid INI section, option, or value. See the supplied "
               "FH2_ExtraOptions.ini.");
    return 0;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestOptionOffset(const char *log_name) {
    for (size_t index = 0; index < setting_count; ++index) {
        const char *left = setting_definitions[index].log_name, *right = log_name;
        while (*left && *left == *right) {
            ++left;
            ++right;
        }
        if (!*left && !*right) {
            return (int)setting_definitions[index].field_offset;
        }
    }
    return -1;
}

__declspec(dllexport) unsigned Fh2TestOptionsSize(void) {
    return (unsigned)sizeof(ModOptions);
}
#endif

int settings_read(const wchar_t *directory, ModOptions *options) {
    settings_defaults(options);
    if (!path_join(ini_path, directory, MOD_INI_NAME)) {
        status_set("REFUSED: INI path is too long.");
        return 0;
    }
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return 1;
        }
        status_set("REFUSED: cannot read FH2_ExtraOptions.ini.");
        return 0;
    }
    LARGE_INTEGER size;
    DWORD bytes_read;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > INI_MAX_SIZE ||
        !ReadFile(file, ini_buffer, (DWORD)size.QuadPart, &bytes_read, NULL) ||
        bytes_read != size.QuadPart) {
        CloseHandle(file);
        status_set("REFUSED: INI read failed or file exceeds 16384 bytes.");
        return 0;
    }
    CloseHandle(file);
    ini_buffer[bytes_read] = 0;
    for (unsigned index = 0; index < bytes_read; ++index) {
        if (!ini_buffer[index]) {
            status_set("REFUSED: INI must use UTF-8 or ASCII, not UTF-16.");
            return 0;
        }
    }
    return parse_ini(ini_buffer, options);
}
