#ifndef FH2_SETTINGS_H
#define FH2_SETTINGS_H

#include "extra_options.h"

typedef enum { SETTING_BOOLEAN, SETTING_MSAA, SETTING_PHOTO_SAMPLES, SETTING_INTEGER } SettingType;

typedef struct {
    const char *section;
    const char *key;
    const char *log_name;
    size_t field_offset;
    SettingType type;
    /* SETTING_INTEGER bounds; decimal or 0x-prefixed hexadecimal values are accepted. */
    unsigned minimum, maximum;
} SettingDefinition;

extern const SettingDefinition setting_definitions[];
extern const size_t setting_count;

void settings_defaults(ModOptions *options);
int settings_read(const wchar_t *directory, ModOptions *options);
int settings_value(const ModOptions *options, const SettingDefinition *definition);

#endif
