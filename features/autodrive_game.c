#include "autodrive_game.h"
#include "game_addresses.h"
#include "support.h"

static unsigned char *image;
typedef void *(*SharedGetter)(void *, GameShared *);
typedef void (*ObjectAction)(void *);
typedef void *(*PlayerGetter)(void *, int);
typedef void *(*CarGetter)(void *, void **);
typedef void (*DriverSetter)(void *, unsigned char);
typedef void (*GroupSetter)(void *, int);
typedef int *(*GroupGetter)(void *);
typedef void *(*CameraGetter)(void *);

static int read_pointer(const void *address, void **value) {
    return memory_read(address, value, sizeof(*value)) && *value;
}

static void *game_function(unsigned rva) { return image + rva; }

static uintptr_t method(void *object, unsigned slot) {
    void *vtable;
    uintptr_t address;
    if (!read_pointer(object, &vtable) ||
        !memory_read((unsigned char *)vtable + slot, &address, 8) ||
        address < (uintptr_t)image + 0x1000 || address >= (uintptr_t)image + 0x1dcc000) {
        return 0;
    }
    return address;
}

static int has_vtable(void *object, unsigned rva) {
    void *vtable;
    return read_pointer(object, &vtable) && vtable == image + rva;
}

static void release_shared(GameShared *value) {
    unsigned char *counter = value->counter;
    if (counter && InterlockedDecrement((volatile LONG *)(counter + 8)) == 0) {
        ((ObjectAction)*(uintptr_t *)counter)(counter);
        if (InterlockedDecrement((volatile LONG *)(counter + 12)) == 0) {
            ((ObjectAction)((uintptr_t *)*(void **)counter)[1])(counter);
        }
    }
    *value = (GameShared){0};
}

void autodrive_game_bind(unsigned char *base) { image = base; }

int autodrive_game_is_freeroam(void) {
    void *game, *simulation, *hud_manager, *services;
    unsigned char paused;
    if (!read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game) ||
        !read_pointer((unsigned char *)game + 0x48, &simulation) ||
        !memory_read((unsigned char *)simulation + 0x28, &paused, 1) || paused ||
        !read_pointer(image + 0x34c0f40, &hud_manager) ||
        !read_pointer((unsigned char *)hud_manager + 8, &services)) {
        return 0;
    }
    GameShared context = {0};
    ((SharedGetter)game_function(0x2129a0))(hud_manager, &context);
    int event_context = -1;
    memory_read((unsigned char *)context.object + 0x60, &event_context, 4);
    release_shared(&context);
    return event_context == 2;
}

void *autodrive_game_get_car(void) {
    void *game, *car = 0;
    if (!read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game)) return 0;
    void *player = ((PlayerGetter)game_function(GAME_RVA_GET_LOCAL_PLAYER))(game, 0);
    if (!player) return 0;
    ((CarGetter)game_function(GAME_RVA_GET_PLAYER_CAR))(player, &car);
    if (car && !has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE)) {
        autodrive_game_release_car(car);
        return 0;
    }
    return car;
}

void autodrive_game_release_car(void *car) {
    uintptr_t release = method(car, 0x18);
    if (release) ((ObjectAction)release)(car);
}

int autodrive_game_driver_mode(void *car) {
    void *holder, *driver;
    int mode = -1;
    if (!has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE) ||
        !read_pointer((unsigned char *)car + 0x4d10, &holder) ||
        !read_pointer(holder, &driver) || !method(driver, 0x48) ||
        !memory_read((unsigned char *)car + 0x4d30, &mode, 4)) return -1;
    return mode;
}

int autodrive_game_set_ai(void *car, int enabled) {
    int mode = autodrive_game_driver_mode(car);
    if (mode != (enabled ? 1 : 0)) return 0;
    ((DriverSetter)game_function(0xf79f10))(car, (unsigned char)enabled);
    return autodrive_game_driver_mode(car) == (enabled ? 0 : 1);
}

void *autodrive_game_route(void *satnav, int *arrived) {
    void *control, *route_control, *route;
    unsigned char active, reached, valid;
    *arrived = 0;
    if (!has_vtable(satnav, 0x2cc3188) ||
        !memory_read((unsigned char *)satnav + 0x160, &active, 1) || !active ||
        !read_pointer((unsigned char *)satnav + 0x150, &control) ||
        !read_pointer(control, &route_control) ||
        !memory_read((unsigned char *)route_control + 0x8a, &reached, 1) ||
        !read_pointer((unsigned char *)route_control + 0x118, &route) ||
        !memory_read((unsigned char *)route + 8, &valid, 1) || !valid) return 0;
    *arrived = reached != 0;
    return route;
}

int autodrive_game_tracker_matches(void *tracker, void *satnav) {
    void *component, *route_property;
    return has_vtable(tracker, 0x2cc3278) &&
        read_pointer((unsigned char *)tracker + 0x98, &component) && component == satnav &&
        has_vtable(component, 0x2cc3188) &&
        read_pointer((unsigned char *)tracker + 0x130, &route_property);
}

int autodrive_game_begin_route(void *tracker) {
    void *task = 0;
    if (!memory_read((unsigned char *)tracker + 0x128, &task, 8) || task) return 0;
    ((ObjectAction)game_function(0x6bec10))(tracker);
    if (read_pointer((unsigned char *)tracker + 0x128, &task)) return 1;
    autodrive_game_end_route(tracker);
    return 0;
}

void autodrive_game_end_route(void *tracker) {
    if (has_vtable(tracker, 0x2cc3278)) ((ObjectAction)game_function(0x6a5fa0))(tracker);
}

void *autodrive_game_camera(void) {
    void *game, *simulation, *manager, *views, *view;
    uintptr_t get_camera;
    if (!read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game) ||
        !read_pointer((unsigned char *)game + 0x48, &simulation) ||
        !read_pointer((unsigned char *)simulation + 0x98, &manager) ||
        !read_pointer((unsigned char *)manager + 0x2d0, &views) ||
        !read_pointer(views, &view) || !(get_camera = method(view, 0x38))) return 0;
    void *camera = ((CameraGetter)get_camera)(view);
    return has_vtable(camera, 0x2c6c840) ? camera : 0;
}

int autodrive_game_camera_group(void *camera) {
    int value = -1;
    void *head;
    size_t count;
    if (!has_vtable(camera, 0x2c6c840) ||
        !read_pointer((unsigned char *)camera + 0x1b0, &head) ||
        !memory_read((unsigned char *)camera + 0x1d0, &count, 8) || !count) return -1;
    int *current = ((GroupGetter)game_function(0x399ff0))((unsigned char *)camera + 0x1b0);
    return memory_read(current, &value, 4) ? value : -1;
}

int autodrive_game_set_camera_group(void *camera, int group) {
    unsigned char *groups, *end;
    void *target;
    if (group < 0 || group > 31 || !has_vtable(camera, 0x2c6c840) ||
        !read_pointer((unsigned char *)camera + 0x188, (void **)&groups) ||
        !read_pointer((unsigned char *)camera + 0x190, (void **)&end) || end < groups ||
        (size_t)(end - groups) / 64 <= (unsigned)group ||
        !read_pointer(groups + (size_t)group * 64 + 0x30, &target)) return 0;
    ((GroupSetter)game_function(0xfe5780))(camera, group);
    return autodrive_game_camera_group(camera) == group;
}

void autodrive_game_hide_huds(AutodriveHud huds[2]) {
    void *manager, *services;
    if (!read_pointer(image + 0x34c0f40, &manager) ||
        !read_pointer((unsigned char *)manager + 8, &services)) return;
    for (unsigned i = 0; i < 2; ++i) {
        GameShared wrapper = {0}, hud = {0};
        ((SharedGetter)game_function(i ? 0x91a500 : 0x91a4a0))(manager, &wrapper);
        if (wrapper.object) {
            ((SharedGetter)game_function(0x3eb640))((unsigned char *)wrapper.object + 0x50, &hud);
            unsigned char previous;
            if (hud.object && memory_read((unsigned char *)hud.object + 0xa8, &previous, 1) &&
                previous <= 1 && memory_is_writable((unsigned char *)hud.object + 0xa8, 1)) {
                huds[i] = (AutodriveHud){hud, previous};
                *(volatile unsigned char *)((unsigned char *)hud.object + 0xa8) = 1;
                hud = (GameShared){0};
            }
        }
        release_shared(&hud);
        release_shared(&wrapper);
    }
}

void autodrive_game_restore_huds(AutodriveHud huds[2]) {
    for (unsigned i = 0; i < 2; ++i) {
        unsigned char *flag = (unsigned char *)huds[i].object.object + 0xa8;
        unsigned char current;
        if (huds[i].object.object && !huds[i].previous &&
            memory_read(flag, &current, 1) && current == 1 && memory_is_writable(flag, 1)) {
            *(volatile unsigned char *)flag = 0;
        }
        release_shared(&huds[i].object);
        huds[i].previous = 0;
    }
}
