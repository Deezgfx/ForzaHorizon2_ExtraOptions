#include "features.h"
#include "autodrive_game.h"
#include "logging.h"
#include "support.h"

#define CINEMATIC_DELAY 3.0f
#define SLOW_SPIN_GROUP 17

typedef void (*UpdateFn)(void *, float);
typedef void (*ActionFn)(void *);
typedef void *(*DestroyFn)(void *, unsigned);
typedef SHORT (WINAPI *KeyStateFn)(int);
typedef HWND (WINAPI *ForegroundFn)(void);
typedef DWORD (WINAPI *WindowProcessFn)(HWND, LPDWORD);

static unsigned char *game_image;
static int cinematic_enabled;
static KeyStateFn key_state;
static ForegroundFn foreground_window;
static WindowProcessFn window_process;
static int input_initialized, f2_was_down;
static void *available_tracker;
static struct {
    void *satnav, *tracker, *car, *route, *camera;
    float elapsed;
    int previous_camera, cinematic, tried_camera;
    AutodriveHud huds[2];
} drive;

#ifdef FH2_TWEAKS_TEST
static int test_input, test_f2, test_escape, test_focus;
#endif

static void input_read(int *f2, int *escape, int *focused) {
    *f2 = *escape = *focused = 0;
#ifdef FH2_TWEAKS_TEST
    if (test_input) {
        *f2 = test_f2; *escape = test_escape; *focused = test_focus;
        return;
    }
#endif
    if (!input_initialized) {
        input_initialized = 1;
        HMODULE module = LoadLibraryExW(L"user32.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module) {
            key_state = (KeyStateFn)(uintptr_t)GetProcAddress(module, "GetAsyncKeyState");
            foreground_window = (ForegroundFn)(uintptr_t)GetProcAddress(module, "GetForegroundWindow");
            window_process = (WindowProcessFn)(uintptr_t)GetProcAddress(module, "GetWindowThreadProcessId");
        }
        if (!key_state || !foreground_window || !window_process)
            log_append("AUTODRIVE: keyboard API unavailable; F2 is inactive.\r\n");
    }
    if (!key_state || !foreground_window || !window_process) return;
    HWND window = foreground_window();
    DWORD process = 0;
    if (window) window_process(window, &process);
    *focused = process == GetCurrentProcessId();
    *f2 = (key_state(VK_F2) & 0x8000) != 0;
    *escape = (key_state(VK_ESCAPE) & 0x8000) != 0;
}

static void stop_driving(const char *reason) {
    if (!drive.car) return;
    if (drive.cinematic && autodrive_game_camera() == drive.camera &&
        autodrive_game_camera_group(drive.camera) == SLOW_SPIN_GROUP) {
        autodrive_game_set_camera_group(drive.camera, drive.previous_camera);
    }
    autodrive_game_restore_huds(drive.huds);
    autodrive_game_set_ai(drive.car, 0);
    autodrive_game_end_route(drive.tracker);
    autodrive_game_release_car(drive.car);
    drive = (typeof(drive)){0};
    log_append(reason);
}

static void start_driving(void *satnav) {
    if (!autodrive_game_is_freeroam()) {
        log_append("AUTODRIVE: F2 ignored outside unpaused single-player free roam.\r\n");
        return;
    }
    int arrived;
    void *route = autodrive_game_route(satnav, &arrived);
    if (!route || arrived) {
        log_append("AUTODRIVE: set a GPS destination before pressing F2.\r\n");
        return;
    }
    if (!autodrive_game_tracker_matches(available_tracker, satnav)) {
        log_append("AUTODRIVE: the local GPS route tracker is not ready.\r\n");
        return;
    }
    void *car = autodrive_game_get_car();
    if (!car) return;
    if (autodrive_game_driver_mode(car) != 1) {
        autodrive_game_release_car(car);
        log_append("AUTODRIVE: F2 ignored while another flow controls the car.\r\n");
        return;
    }
    if (!autodrive_game_begin_route(available_tracker)) {
        autodrive_game_release_car(car);
        log_append("AUTODRIVE: GPS event-route tracking could not start, or is already owned.\r\n");
        return;
    }
    if (!autodrive_game_set_ai(car, 1)) {
        autodrive_game_end_route(available_tracker);
        autodrive_game_release_car(car);
        log_append("AUTODRIVE: native AI driver handoff failed.\r\n");
        return;
    }
    drive.satnav = satnav;
    drive.tracker = available_tracker;
    drive.car = car;
    drive.route = route;
    log_append("AUTODRIVE: GPS route tracking started; native AI owns the player car. F2 cancels.\r\n");
}

static void enter_cinematic(void) {
    drive.tried_camera = 1;
    void *camera = autodrive_game_camera();
    int previous = autodrive_game_camera_group(camera);
    if ((previous != 0 && previous != 1) ||
        !autodrive_game_set_camera_group(camera, SLOW_SPIN_GROUP)) {
        log_append("AUTODRIVE: Slow Spin is unavailable; continuing with the normal HUD and camera.\r\n");
        return;
    }
    drive.camera = camera;
    drive.previous_camera = previous;
    drive.cinematic = 1;
    autodrive_game_hide_huds(drive.huds);
    log_append("AUTODRIVE: Slow Spin camera selected; normal and AR HUDs suspended.\r\n");
}

static void autodrive_tick(void *satnav, float dt) {
    int f2, escape, focused;
    input_read(&f2, &escape, &focused);
    int pressed = f2 && !f2_was_down;
    f2_was_down = f2;
    if (drive.car) {
        if (satnav != drive.satnav) return;
        if (pressed || escape || !focused) {
            stop_driving("AUTODRIVE: cancelled by F2, Escape, or focus loss.\r\n");
            return;
        }
        if (!autodrive_game_is_freeroam()) {
            stop_driving("AUTODRIVE: stopped when free roam was paused or left.\r\n");
            return;
        }
        void *current_car = autodrive_game_get_car();
        int same_car = current_car == drive.car;
        autodrive_game_release_car(current_car);
        if (!same_car || autodrive_game_driver_mode(drive.car) != 0) {
            stop_driving("AUTODRIVE: stopped on car change or a native control change.\r\n");
            return;
        }
        int arrived;
        void *route = autodrive_game_route(satnav, &arrived);
        if (arrived || route != drive.route) {
            stop_driving(arrived ? "AUTODRIVE: GPS destination reached; player control restored.\r\n"
                                 : "AUTODRIVE: GPS route changed or cleared; player control restored.\r\n");
            return;
        }
        if (dt > 0.0f && dt <= 1.0f) drive.elapsed += dt;
        if (cinematic_enabled && !drive.tried_camera && drive.elapsed >= CINEMATIC_DELAY)
            enter_cinematic();
    } else if (focused && pressed && !escape) {
        start_driving(satnav);
    }
}

static void satnav_update(void *self, float dt) {
    ((UpdateFn)(game_image + 0x6b4d30))(self, dt);
    autodrive_tick(self, dt);
}

static void satnav_deactivate(void *self) {
    if (self == drive.satnav) stop_driving("AUTODRIVE: stopped before GPS deactivation.\r\n");
    ((ActionFn)(game_image + 0x6c0c70))(self);
}

static void tracker_update(void *self, float dt) {
    available_tracker = self;
    ((UpdateFn)(game_image + 0x6b4e40))(self, dt);
}

static void *tracker_destroy(void *self, unsigned flags) {
    if (self == drive.tracker) stop_driving("AUTODRIVE: stopped before route-tracker destruction.\r\n");
    if (self == available_tracker) available_tracker = 0;
    return ((DestroyFn)(game_image + 0x6a26e0))(self, flags);
}

static void pause_enter(void *self) {
    stop_driving("AUTODRIVE: stopped before the game's pause flow.\r\n");
    ((ActionFn)(game_image + 0x8e98e0))(self);
}

static void event_context_enter(void *self) {
    stop_driving("AUTODRIVE: stopped before an event-context transition.\r\n");
    ((ActionFn)(game_image + 0x912860))(self);
}

static unsigned char original_pointers[6][8], hook_pointers[6][8];
static const unsigned char *accepted_pointers[6][2];
PatchDefinition autodrive_hook_patches[6];

void autodrive_prepare_hooks(unsigned char *image, const ModOptions *options) {
    static const unsigned slots[] = {0x2cc3198, 0x2cc31b0, 0x2cc3288, 0x2cc3278, 0x2d16698, 0x2d1ea50};
    static const unsigned originals[] = {0x6b4d30, 0x6c0c70, 0x6b4e40, 0x6a26e0, 0x8e98e0, 0x912860};
    static const char *names[] = {"autodrive_gps_update", "autodrive_gps_deactivation",
        "autodrive_tracker_update", "autodrive_tracker_destruction", "autodrive_pause_entry",
        "autodrive_event_context_entry"};
    uintptr_t hooks[] = {(uintptr_t)satnav_update, (uintptr_t)satnav_deactivate,
        (uintptr_t)tracker_update, (uintptr_t)tracker_destroy, (uintptr_t)pause_enter,
        (uintptr_t)event_context_enter};
    game_image = image;
    autodrive_game_bind(image);
    cinematic_enabled = options->autodrive_cinematic;
    for (unsigned i = 0; i < 6; ++i) {
        uintptr_t original = (uintptr_t)(image + originals[i]);
        memory_copy(original_pointers[i], &original, 8);
        memory_copy(hook_pointers[i], &hooks[i], 8);
        accepted_pointers[i][0] = original_pointers[i];
        accepted_pointers[i][1] = hook_pointers[i];
        autodrive_hook_patches[i] = (PatchDefinition){slots[i], 0, 8, GROUP_AUTODRIVE,
            names[i], original_pointers[i], hook_pointers[i], hook_pointers[i],
            accepted_pointers[i], 2};
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestAutodriveInput(int f2, int escape, int focus) {
    test_input = 1; test_f2 = f2; test_escape = escape; test_focus = focus;
}
__declspec(dllexport) int Fh2TestAutodriveState(void) {
    return drive.car ? (drive.cinematic ? 2 : 1) : 0;
}
__declspec(dllexport) void *Fh2TestAutodriveHookAddress(unsigned index) {
    uintptr_t address = 0;
    if (index < 6) memory_copy(&address, hook_pointers[index], 8);
    return (void *)address;
}
#endif
