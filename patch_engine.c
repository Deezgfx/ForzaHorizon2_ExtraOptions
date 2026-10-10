#include "patch_engine.h"
#include "features.h"
#include "patches_generated.h"
#include "support.h"

#define CALLBACK_PATCH_COUNT 11
#define ALL_PATCH_COUNT (FIXED_PATCH_COUNT + CALLBACK_PATCH_COUNT)
#define MAX_PATCH_PAGES 64

typedef struct {
    unsigned char *game_image;
    const ModOptions *options;
    SIZE_T page_size;
    uintptr_t pages[MAX_PATCH_PAGES];
    DWORD original_protection[MAX_PATCH_PAGES];
    unsigned page_count;
    unsigned writable_page_count;
    unsigned changed_count;
} PatchTransaction;

static unsigned char original_bytes[ALL_PATCH_COUNT][MAX_PATCH_SIZE];
static const PatchDefinition *const callback_patches[CALLBACK_PATCH_COUNT] = {
    &garage_hook_patch, &language_hook_patch, &road_hook_patch, &gamerpic_hook_patch,
    &treasure_map_hook_patch, &autodrive_hook_patches[0], &autodrive_hook_patches[1],
    &autodrive_hook_patches[2], &autodrive_hook_patches[3], &autodrive_hook_patches[4],
    &autodrive_hook_patches[5]};

#ifdef FH2_TWEAKS_TEST
static int protect_failure_at = -1;
static int protect_call_count;
#endif

static const PatchDefinition *patch_at(unsigned index) {
    if (index < FIXED_PATCH_COUNT) {
        return &fixed_patches[index];
    }
    return callback_patches[index - FIXED_PATCH_COUNT];
}

static const unsigned char *patch_target(const PatchDefinition *patch, const ModOptions *options) {
    int enabled = 0;
    switch (patch->group) {
        case GROUP_AUTODRIVE:
            enabled = options->autodrive;
            break;
        case GROUP_TREASURE_MAP:
            enabled = options->unlock_treasure_map;
            break;
        case GROUP_PHOTO_CAMERA:
            enabled = options->free_photo_camera;
            break;
        case GROUP_GAMERPIC:
            enabled = options->custom_gamerpic;
            break;
        case GROUP_PARENTAL:
            enabled = options->disable_parental_locks;
            break;
        case GROUP_ISLAND_CARS:
            enabled = options->unlock_storm_island_cars;
            break;
        case GROUP_ROAD:
            enabled = options->garage_from_pause && options->snap_car_to_road;
            break;
        case GROUP_TEXT_RELOAD:
            enabled = options->text_language >= 0;
            break;
        case GROUP_TEXT_INIT:
            return options->text_language < 0 ? patch->original
                                              : language_text_init_targets[options->text_language];
        case GROUP_TEXT_LOCK:
            enabled = options->text_language >= 0;
            break;
        case GROUP_AUDIO_LANGUAGE:
            return options->audio_language < 0
                       ? patch->original
                       : language_audio_init_targets[options->audio_language];
        case GROUP_DLC:
            enabled = options->unlock_dlc;
            break;
        case GROUP_VIP:
            enabled = options->offline_vip;
            break;
        case GROUP_INTRO:
            enabled = options->skip_intro;
            break;
        case GROUP_MARKETPLACE:
            enabled = options->enable_marketplace;
            break;
        case GROUP_LIVERY:
            enabled = options->enable_4k_liveries;
            break;
        case GROUP_MSAA:
            enabled = options->enable_4k_liveries && options->livery_msaa == 4;
            break;
        case GROUP_PAUSE_GARAGE:
            enabled = options->garage_from_pause;
            break;
    }
    if (!enabled) {
        return patch->original;
    }
    return patch->group == GROUP_LIVERY && options->livery_supersampling ? patch->supersampled
                                                                         : patch->standard;
}

static int supported_image(const unsigned char *game_image) {
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)game_image;
    if (!game_image || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 ||
        dos->e_lfanew > 4096) {
        return 0;
    }
    const IMAGE_NT_HEADERS64 *headers = (const IMAGE_NT_HEADERS64 *)(game_image + dos->e_lfanew);
    return headers->Signature == IMAGE_NT_SIGNATURE &&
           headers->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           headers->FileHeader.TimeDateStamp == GAME_TIMESTAMP &&
           headers->OptionalHeader.SizeOfImage == GAME_IMAGE_SIZE;
}

static int validate_patch_sites(PatchTransaction *transaction) {
    for (unsigned index = 0; index < ALL_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = patch_at(index);
        const unsigned char *loaded_bytes = transaction->game_image + patch->rva;
        int recognized = 0;
        for (unsigned variant = 0; variant < patch->accepted_count; ++variant) {
            if (memory_equal(loaded_bytes, patch->accepted[variant], patch->size)) {
                recognized = 1;
                break;
            }
        }
        if (!recognized) {
            status_set("REFUSED: unexpected loaded bytes at ");
            status_append(patch->name);
            status_append(". No patches applied.");
            return 0;
        }
        memory_copy(original_bytes[index], loaded_bytes, patch->size);
        if (!memory_equal(loaded_bytes, patch_target(patch, transaction->options), patch->size)) {
            ++transaction->changed_count;
        }
    }
    return 1;
}

static int pin_callback_module(const ModOptions *options) {
    if (!options->garage_from_pause && options->text_language < 0 && !options->custom_gamerpic &&
        !options->unlock_treasure_map && !options->autodrive) {
        return 1;
    }
    HMODULE pinned_module;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCWSTR)(uintptr_t)garage_callback_address(), &pinned_module)) {
        return 1;
    }
    status_set("REFUSED: cannot keep the callback DLL resident. No patches applied.");
    return 0;
}

static int collect_patch_pages(PatchTransaction *transaction) {
    for (unsigned index = 0; index < ALL_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = patch_at(index);
        unsigned char *address = transaction->game_image + patch->rva;
        if (memory_equal(address, patch_target(patch, transaction->options), patch->size)) {
            continue;
        }
        uintptr_t first_page = (uintptr_t)address / transaction->page_size * transaction->page_size;
        uintptr_t last_page = (uintptr_t)(address + patch->size - 1) / transaction->page_size *
                              transaction->page_size;
        for (uintptr_t page = first_page; page <= last_page; page += transaction->page_size) {
            unsigned existing = 0;
            while (existing < transaction->page_count && transaction->pages[existing] != page) {
                ++existing;
            }
            if (existing != transaction->page_count) {
                continue;
            }
            if (transaction->page_count == MAX_PATCH_PAGES) {
                status_set("REFUSED: too many patch pages.");
                return 0;
            }
            transaction->pages[transaction->page_count++] = page;
        }
    }
    return 1;
}

static BOOL make_writable(void *address, SIZE_T size, DWORD *previous_protection) {
#ifdef FH2_TWEAKS_TEST
    if (protect_call_count++ == protect_failure_at) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
#endif
    return VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, previous_protection);
}

static int restore_page_protection(const PatchTransaction *transaction) {
    int restored = 1;
    for (unsigned index = 0; index < transaction->writable_page_count; ++index) {
        DWORD ignored;
        if (!VirtualProtect((void *)transaction->pages[index], transaction->page_size,
                            transaction->original_protection[index], &ignored)) {
            restored = 0;
        }
    }
    return restored;
}

static int make_patch_pages_writable(PatchTransaction *transaction) {
    while (transaction->writable_page_count < transaction->page_count) {
        unsigned index = transaction->writable_page_count;
        if (!make_writable((void *)transaction->pages[index], transaction->page_size,
                           &transaction->original_protection[index])) {
            restore_page_protection(transaction);
            status_set("REFUSED: cannot make every patch page writable. No bytes changed.");
            return 0;
        }
        ++transaction->writable_page_count;
    }
    return 1;
}

static void write_patch_sites(const PatchTransaction *transaction, int rollback) {
    for (unsigned index = 0; index < ALL_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = patch_at(index);
        const unsigned char *target = patch_target(patch, transaction->options);
        if (memory_equal(original_bytes[index], target, patch->size)) {
            continue;
        }
        const unsigned char *bytes = rollback ? original_bytes[index] : target;
        unsigned char *address = transaction->game_image + patch->rva;
        if (index >= FIXED_PATCH_COUNT) {
            uintptr_t callback;
            memory_copy(&callback, bytes, sizeof(callback));
            *(volatile uintptr_t *)address = callback;
        } else {
            memory_copy(address, bytes, patch->size);
        }
    }
}

static int commit_transaction(const PatchTransaction *transaction) {
    write_patch_sites(transaction, 0);
    language_write_changes(0);
    int flushed =
        FlushInstructionCache(GetCurrentProcess(), transaction->game_image, GAME_IMAGE_SIZE) != 0;
    if (flushed) {
        language_commit_options(transaction->options);
        status_set("OK: requested tweaks applied in memory. The EXE file was not changed.");
    } else {
        language_write_changes(1);
        write_patch_sites(transaction, 1);
        FlushInstructionCache(GetCurrentProcess(), transaction->game_image, GAME_IMAGE_SIZE);
        status_set("REFUSED: instruction-cache flush failed. Patch bytes rolled back.");
    }
    if (!restore_page_protection(transaction)) {
        status_append(" WARNING: page protection restoration failed.");
        return 0;
    }
    return flushed;
}

int patch_engine_apply(unsigned char *game_image, const ModOptions *options) {
    if (!supported_image(game_image)) {
        status_set("REFUSED: unsupported loaded executable headers. No patches applied.");
        return 0;
    }
    if (!options->enabled) {
        status_set("DISABLED: no memory changes made. Existing EXE patches remain active.");
        return 1;
    }
    if (options->text_language < LANGUAGE_AUTO || options->text_language >= LANGUAGE_COUNT ||
        options->text_language == 1 ||
        (options->audio_language != LANGUAGE_AUTO &&
         !language_supports_audio(options->audio_language))) {
        status_set("REFUSED: invalid language selection. No patches applied.");
        return 0;
    }

    garage_prepare_hook(game_image);
    language_prepare_hook(game_image, options);
    road_prepare_hook(game_image);
    gamerpic_prepare_hook(game_image);
    treasure_map_prepare_hook(game_image);
    autodrive_prepare_hooks(game_image, options);

    PatchTransaction transaction;
    transaction.game_image = game_image;
    transaction.options = options;
    transaction.page_count = 0;
    transaction.writable_page_count = 0;
    transaction.changed_count = 0;
    if (!validate_patch_sites(&transaction) ||
        !language_prepare_existing_objects(game_image, options)) {
        return 0;
    }
    transaction.changed_count += language_count_changes();
    if (!pin_callback_module(options)) {
        return 0;
    }
    if (!transaction.changed_count) {
        language_commit_options(options);
        status_set("OK: all requested tweaks already match the loaded executable.");
        return 1;
    }
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    transaction.page_size = system_info.dwPageSize;
    if (!collect_patch_pages(&transaction) || !make_patch_pages_writable(&transaction)) {
        return 0;
    }
    return commit_transaction(&transaction);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestApply(void *base, const ModOptions *options) {
    return patch_engine_apply(base, options);
}

__declspec(dllexport) void Fh2TestProtectFailure(int index) {
    protect_failure_at = index;
    protect_call_count = 0;
}
#endif
