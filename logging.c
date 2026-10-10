#include "logging.h"
#include "features.h"
#include "settings.h"
#include "support.h"

static wchar_t runtime_log_path[PATH_CAPACITY];
static wchar_t startup_log_path[PATH_CAPACITY];

void log_reset(void) {
    runtime_log_path[0] = 0;
}

void log_set_directory(const wchar_t *directory) {
    if (!path_join(runtime_log_path, directory, MOD_LOG_NAME)) {
        runtime_log_path[0] = 0;
    }
}

void log_append(const char *message) {
    if (!runtime_log_path[0]) {
        return;
    }
    HANDLE file =
        CreateFileW(runtime_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD bytes_written;
    WriteFile(file, message, (DWORD)text_length(message), &bytes_written, NULL);
    CloseHandle(file);
}

static void write_text(HANDLE file, const char *text) {
    DWORD bytes_written;
    WriteFile(file, text, (DWORD)text_length(text), &bytes_written, NULL);
}

void log_start(const wchar_t *directory, const ModOptions *options) {
    if (!path_join(startup_log_path, directory, MOD_LOG_NAME)) {
        return;
    }
    HANDLE file = CreateFileW(startup_log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
#ifdef FH2_AZIMUTH
    write_text(file, MOD_NAME " " MOD_VERSION " - Azimuth TLS adapter\r\n");
#else
    write_text(file, MOD_NAME " " MOD_VERSION " - xg.dll hook\r\n");
#endif
    write_text(file, status_get());
    for (size_t index = 0; index < setting_count; ++index) {
        const SettingDefinition *definition = &setting_definitions[index];
        int value = settings_value(options, definition);
        write_text(file, "\r\n");
        write_text(file, definition->log_name);
        write_text(file, "=");
        if (definition->type == SETTING_TEXT_LANGUAGE ||
            definition->type == SETTING_AUDIO_LANGUAGE) {
            write_text(file, value >= 0 && value < LANGUAGE_COUNT ? language_codes[value] : "Auto");
        } else {
            char number[] = {(char)('0' + value), 0};
            write_text(file, number);
        }
    }
    write_text(file, "\r\n");
    CloseHandle(file);
}
