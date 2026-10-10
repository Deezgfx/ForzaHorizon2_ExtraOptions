#ifndef FH2_FEATURES_H
#define FH2_FEATURES_H
#include "extra_options.h"
extern PatchDefinition garage_hook_patch;
extern PatchDefinition language_hook_patch;
extern PatchDefinition road_hook_patch;
extern PatchDefinition gamerpic_hook_patch;
extern PatchDefinition treasure_map_hook_patch;
extern const char *const language_codes[LANGUAGE_COUNT];
void garage_prepare_hook(unsigned char *game_image);
void *garage_callback_address(void);
void language_prepare_hook(unsigned char *game_image, const ModOptions *options);
int language_prepare_existing_objects(unsigned char *game_image, const ModOptions *options);
unsigned language_count_changes(void);
void language_write_changes(int rollback);
void language_commit_options(const ModOptions *options);
const char *language_diagnostics_get(void);
int language_supports_audio(int index);
int language_parse_setting(const char *value, int audio, int *selection);
void road_prepare_hook(unsigned char *game_image);
void gamerpic_prepare_hook(unsigned char *game_image);
void treasure_map_prepare_hook(unsigned char *game_image);
void gamerpic_set_directory(const wchar_t *directory);
#endif
