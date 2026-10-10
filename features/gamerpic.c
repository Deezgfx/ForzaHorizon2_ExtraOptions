#include "game_addresses.h"
#include "features.h"
#include "support.h"
#include "logging.h"
#include "image.h"

/* Local gamerpic supplied through the game's existing raw-image pipeline.
   All image allocation/submission happens on the converter's UI call, never
   in DllMain. No graphics/WinRT provider or global COM factory is replaced. */
#define GAMERPIC_SLOT_RVA 0x2da6a48u
#define GAMERPIC_CONVERT_RVA 0xb9a0d0u

typedef void **(*GamerpicConvertFn)(void *self, void **out, void *value, void *type,
                                    void *parameter, void *culture);
typedef void **(*GamerpicCreateFn)(void **out);
typedef void **(*GamerpicWrapFn)(void **out, void **bitmap);
typedef void **(*GamerpicBufferFn)(void **out, int size);
typedef void (*GamerpicQueueFn)(void *context, const void *bytes, int size, unsigned char own,
                                const void *options);
typedef void (*GamerpicUpdateFn)(void *manager, void **owned_bitmap);
typedef void (*GamerpicDestroyFn)(void *image);
static GamerpicConvertFn original_gamerpic_converter;
static GamerpicCreateFn gamerpic_create;
static GamerpicWrapFn gamerpic_wrap;
static GamerpicBufferFn gamerpic_create_pixel_buffer;
static GamerpicQueueFn gamerpic_queue_texture;
static GamerpicUpdateFn gamerpic_queue_bitmap_update;
static GamerpicDestroyFn gamerpic_destroy_object;
static unsigned char *gamerpic_game_image;
static unsigned char gamerpic_original_pointer[8], gamerpic_target_pointer[8];
static wchar_t gamerpic_path[32768];
static void *gamerpic_image;

typedef enum {
    GAMERPIC_NOT_LOADED,
    GAMERPIC_LOADING,
    GAMERPIC_READY,
    GAMERPIC_UNAVAILABLE
} GamerpicLoadState;

static volatile LONG gamerpic_state;

/* The adapters resolve dir from the guest EXE/launcher, never the host DLL. */
void gamerpic_set_directory(const wchar_t *directory) {
    size_t length = 0;
    while (directory[length]) {
        if (length >= 32754) {
            gamerpic_path[0] = 0;
            return;
        }
        gamerpic_path[length] = directory[length];
        ++length;
    }
    if (length && gamerpic_path[length - 1] != L'\\' && gamerpic_path[length - 1] != L'/') {
        gamerpic_path[length++] = L'\\';
    }
    static const wchar_t name[] = L"Gamerpic.png";
    memory_copy(gamerpic_path + length, name, sizeof(name));
}

/* Bound the native decoder's input and reject damaged/truncated PNG containers.
   Pixel decoding is checked locally before submission to the game's loader. */
static int gamerpic_is_local_profile(void *value) {
    void *vtable, *profile, *user;
    /* The actual converter takes UserProfileDataSource, whose +0x78 is a
       CUserProfile. Its +0x50 is the retained Windows.Xbox.System.User for a
       local signed-in user. Remote/AI profiles have no such local User. */
    return value && memory_read(value, &vtable, 8) &&
           vtable == gamerpic_game_image + GAME_RVA_PROFILE_SOURCE_VTABLE &&
           memory_read((unsigned char *)value + 0x78, &profile, 8) && profile &&
           memory_read(profile, &vtable, 8) &&
           vtable == gamerpic_game_image + GAME_RVA_USER_PROFILE_VTABLE &&
           memory_read((unsigned char *)profile + 0x50, &user, 8) && user;
}

static int gamerpic_renderer_ready(void) {
    void *manager, *renderer, *queue, *device;
    return memory_read(gamerpic_game_image + GAME_RVA_UI_MANAGER, &manager, 8) && manager &&
           memory_read(gamerpic_game_image + GAME_RVA_TEXTURE_MANAGER, &renderer, 8) && renderer &&
           memory_read(gamerpic_game_image + GAME_RVA_TEXTURE_QUEUE, &queue, 8) && queue &&
           memory_read(gamerpic_game_image + GAME_RVA_GRAPHICS_DEVICE, &device, 8) && device;
}

static void gamerpic_release_reference(void *image) {
    if (image && InterlockedDecrement((volatile LONG *)((unsigned char *)image + 0x10)) == 0) {
        gamerpic_destroy_object(image);
    }
}

static int gamerpic_submit_pixels(void *bitmap, const void *pixels, int size, UINT width,
                                  UINT height) {
    void *buffer = 0;
    gamerpic_create_pixel_buffer(&buffer, size);
    if (!buffer) {
        return 0;
    }
    void *storage = *(void **)((unsigned char *)buffer + 0x28);
    if (!storage) {
        gamerpic_release_reference(buffer);
        return 0;
    }
    memory_copy(storage, pixels, (size_t)size);
    /* Transfer the buffer's owned reference into the stock bitmap member.
       It retains the pixels while the asynchronous renderer consumes them. */
    void **member = (void **)((unsigned char *)bitmap + 0x80);
    gamerpic_release_reference(*member);
    *member = buffer;

    struct {
        char empty_name[16];
        uint64_t length, capacity;
        uint32_t priority, format, width, height;
        unsigned char no_cache;
    } options = {{0}, 0, 15, 0x60000, 3, width, height, 0};

    /* Format 3 is RGBA8. Nonzero format and explicit dimensions select
       0x1104c39 -> 0x10c6f00, bypassing encoded-image/WIC decoding in FH2. */
    gamerpic_queue_texture((unsigned char *)bitmap + 0x50, storage, size, 0, &options);
    void *owned = bitmap;
    InterlockedIncrement((volatile LONG *)((unsigned char *)bitmap + 0x10));
    gamerpic_queue_bitmap_update(*(void **)(gamerpic_game_image + GAME_RVA_UI_MANAGER), &owned);
    return 1;
}

static void *gamerpic_load_cached_image(void) {
    LONG load_state = InterlockedCompareExchange(&gamerpic_state, 0, 0);
    if (load_state == GAMERPIC_READY) {
        return gamerpic_image;
    }
    if (load_state != GAMERPIC_NOT_LOADED || !gamerpic_renderer_ready() ||
        InterlockedCompareExchange(&gamerpic_state, GAMERPIC_LOADING, GAMERPIC_NOT_LOADED)) {
        return 0;
    }
    void *image = 0, *bitmap = 0;
    unsigned char *png_bytes = 0, *rgba_pixels = 0;
    HANDLE heap = GetProcessHeap(), file = INVALID_HANDLE_VALUE;
    const char *diagnostic =
        "GAMERPIC: configured Gamerpic.png unavailable; using the original picture.\r\n";
    if (!gamerpic_path[0]) {
        goto done;
    }
    file = CreateFileW(gamerpic_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        goto done;
    }
    LARGE_INTEGER size;
    DWORD bytes_read;
    diagnostic = "GAMERPIC: Gamerpic.png is unreadable or invalid (PNG, maximum 2048x2048/16 MiB); "
                 "using the original picture.\r\n";
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 57 || size.QuadPart > PNG_MAX_FILE_SIZE) {
        goto done;
    }
    png_bytes = HeapAlloc(heap, 0, (SIZE_T)size.QuadPart);
    if (!png_bytes || !ReadFile(file, png_bytes, (DWORD)size.QuadPart, &bytes_read, 0) ||
        bytes_read != size.QuadPart || !image_validate_png(png_bytes, bytes_read)) {
        goto done;
    }
    DWORD texture_size;
    UINT width, height;
    rgba_pixels = image_decode_png(png_bytes, bytes_read, &texture_size, &width, &height);
    if (!rgba_pixels) {
        diagnostic = "GAMERPIC: PNG decoding failed; using the original picture.\r\n";
        goto done;
    }
    gamerpic_create(&bitmap);
    if (!bitmap) {
        diagnostic = "GAMERPIC: native bitmap allocation failed; using the original picture.\r\n";
        goto done;
    }
    gamerpic_wrap(&image, &bitmap);
    if (!image) {
        diagnostic =
            "GAMERPIC: native image wrapper allocation failed; using the original picture.\r\n";
        goto done;
    }
    if (!gamerpic_submit_pixels(bitmap, rgba_pixels, (int)texture_size, width, height)) {
        gamerpic_release_reference(image);
        image = 0;
        diagnostic =
            "GAMERPIC: native pixel buffer allocation failed; using the original picture.\r\n";
        goto done;
    }
    diagnostic =
        "GAMERPIC: local Gamerpic.png decoded and submitted through the native texture loader.\r\n";
done:
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (png_bytes) {
        HeapFree(heap, 0, png_bytes);
    }
    if (rgba_pixels) {
        HeapFree(heap, 0, rgba_pixels);
    }
    gamerpic_release_reference(bitmap);
    gamerpic_image = image;
    InterlockedExchange(&gamerpic_state, image ? GAMERPIC_READY : GAMERPIC_UNAVAILABLE);
    log_append(diagnostic);
    return image;
}

static void **gamerpic_convert_hook(void *self, void **out, void *value, void *type,
                                    void *parameter, void *culture) {
    if (out && gamerpic_is_local_profile(value)) {
        void *image = gamerpic_load_cached_image();
        if (image) {
            /* Convert's result is an initially uninitialized Ptr<ImageSource>.
               Match the original: initialize it and return an owned reference. */
            InterlockedIncrement((volatile LONG *)((unsigned char *)image + 0x10));
            *out = image;
            return out;
        }
    }
    return original_gamerpic_converter(self, out, value, type, parameter, culture);
}

static const unsigned char *const gamerpic_accepted[] = {gamerpic_original_pointer,
                                                         gamerpic_target_pointer};
PatchDefinition gamerpic_hook_patch = {GAMERPIC_SLOT_RVA,
                                       0,
                                       8,
                                       GROUP_GAMERPIC,
                                       "local_gamerpic_converter_hook",
                                       gamerpic_original_pointer,
                                       gamerpic_target_pointer,
                                       gamerpic_target_pointer,
                                       gamerpic_accepted,
                                       2};

void gamerpic_prepare_hook(unsigned char *game_image) {
    uintptr_t original_callback = (uintptr_t)(game_image + GAMERPIC_CONVERT_RVA),
              replacement_callback = (uintptr_t)gamerpic_convert_hook;
    memory_copy(gamerpic_original_pointer, &original_callback, 8);
    memory_copy(gamerpic_target_pointer, &replacement_callback, 8);
    gamerpic_game_image = game_image;
    original_gamerpic_converter = (GamerpicConvertFn)original_callback;
    gamerpic_create = (GamerpicCreateFn)(game_image + GAME_RVA_CREATE_BITMAP);
    gamerpic_wrap = (GamerpicWrapFn)(game_image + GAME_RVA_WRAP_BITMAP);
    gamerpic_create_pixel_buffer = (GamerpicBufferFn)(game_image + GAME_RVA_CREATE_PIXEL_BUFFER);
    gamerpic_queue_texture = (GamerpicQueueFn)(game_image + GAME_RVA_QUEUE_TEXTURE);
    gamerpic_queue_bitmap_update = (GamerpicUpdateFn)(game_image + GAME_RVA_QUEUE_BITMAP_UPDATE);
    gamerpic_destroy_object = (GamerpicDestroyFn)(game_image + GAME_RVA_DESTROY_UI_OBJECT);
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestGamerpicHookAddress(void) {
    return (void *)gamerpic_convert_hook;
}

__declspec(dllexport) void Fh2TestGamerpicDirectory(const wchar_t *dir) {
    gamerpic_set_directory(dir);
}

__declspec(dllexport) const wchar_t *Fh2TestGamerpicPath(void) {
    return gamerpic_path;
}

__declspec(dllexport) int Fh2TestValidatePng(const unsigned char *bytes, uint32_t size) {
    return image_validate_png(bytes, size);
}

__declspec(dllexport) void Fh2TestSetGamerpicPath(const wchar_t *path) {
    unsigned i = 0;
    while (path[i] && i < 32767) {
        gamerpic_path[i] = path[i];
        ++i;
    }
    gamerpic_path[i] = 0;
    gamerpic_image = 0;
    gamerpic_state = GAMERPIC_NOT_LOADED;
}
#endif
