#include "features.h"
#include "support.h"

/*
 * Keep the game running when its window loses focus.
 *
 * 0xf6c3c0 registers 0xf6dba0 as the game's CoreWindow.Activated handler (ICoreWindow slot 0x78;
 * the delegate's member pointer is loaded by `lea rcx,[0xf6dba0]` at 0xf6c617, the only
 * reference to that function). The handler reads IWindowActivatedEventArgs slot 0x30
 * (get_WindowActivationState) and posts the state to the game's message queue as message
 * 0x1a/6. Azimuth raises Activated with CoreWindowActivationState_Deactivated (1) when the
 * window loses focus, and the game answers that message by opening the pause menu.
 *
 * With [Global] KeepRunningInBackground=1 the lea is redirected to a thunk in the unused padding
 * after the handler (0xf6dc64), which calls window_activated below. Deactivated notifications
 * are dropped; CodeActivated and PointerActivated are passed on unchanged.
 */

enum { RVA_ACTIVATED_HANDLER = 0xf6dba0, RVA_THUNK = 0xf6dc64, ACTIVATION_DEACTIVATED = 1 };

typedef uintptr_t (*ActivatedHandler)(void *self, void *sender, void *args);
typedef long (*GetActivationState)(void *args, int *state);

static unsigned char *image;

static uintptr_t window_activated(void *self, void *sender, void *args) {
    void *vtable;
    uintptr_t getter;
    int state = -1;
    if (memory_read_pointer(args, &vtable) &&
        memory_read((unsigned char *)vtable + 0x30, &getter, sizeof(getter)) && getter &&
        ((GetActivationState)getter)(args, &state) >= 0 && state == ACTIVATION_DEACTIVATED) {
        return 0;
    }
    return ((ActivatedHandler)(image + RVA_ACTIVATED_HANDLER))(self, sender, args);
}

static unsigned char thunk[12];
static const unsigned char padding[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                          0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted[] = {padding, thunk};
PatchDefinition background_focus_hook_patch = {
    RVA_THUNK, 0,     12,       GROUP_BACKGROUND, "window_activation_filter_thunk", padding,
    thunk,     thunk, accepted, ARRAY_COUNT(accepted)};

void background_focus_prepare_hook(unsigned char *game_image) {
    image = game_image;
    static const unsigned char code[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t callback = (uintptr_t)window_activated;
    memory_copy(thunk, code, sizeof(code));
    memory_copy(thunk + 2, &callback, sizeof(callback));
}
