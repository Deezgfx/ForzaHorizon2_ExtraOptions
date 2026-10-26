#include "air_ride.h"
#include "air_sound.h"
#include "air_suspension.h"
#include "autodrive_game.h"
#include "game_addresses.h"
#include "game_input.h"
#include "logging.h"
#include "pad_gesture.h"
#include "support.h"

/*
 * Live ride height through the game's own tuning path, traced in the supported executable:
 *
 *   ForzaCar + 0x1200 is the embedded car physics description (0xf605c0 returns it), and
 *   ForzaCar + 0x4c08 is the CCarDynamics interface (0xf605d0 returns it; same pointer the
 *   velocity code in vehicle_controls.c reads through car + 0x20 + 0x4be8).
 *   Description + 0x324 / + 0x3b4 are the front / rear List_SpringDamperPhysics rows parsed
 *   by 0x1061810: DefRideHeight +0x0c, MinRideHeight +0x10, MaxRideHeight +0x14.
 *   Description + 0x3800 is the tuning block. 0x12e3680 resolves it in place; its helper
 *   labels the value written at + 0x48 (front) and + 0x74 (rear) "RideHeight" and stores it
 *   as a 0..1 position inside MinRideHeight..MaxRideHeight.
 *   0x12e3d20 turns that position into the suspension geometry as
 *   MinRideHeight + (MaxRideHeight - MinRideHeight) * position.
 *   The native tuning apply at 0x173c0c5 writes those fields, calls 0x12e3680 on the
 *   description, then calls CCarDynamics interface slot 0x238 (0x12da500 -> 0x12da510) with
 *   (dynamics, description, 3). That method re-resolves the tune and rebuilds the suspension
 *   without clearing wheel state, preserving the body transform. This file repeats exactly
 *   that sequence for the local car.
 */

enum {
    RVA_TUNE_RESOLVE = 0x12e3680,
    RVA_DYNAMICS_REAPPLY = 0x12da500,
    RVA_DYNAMICS_REAPPLY_BODY = 0x12da510,
    RVA_DYNAMICS_VELOCITY = 0x12cdcf0,
    RVA_CAR_DESCRIPTION_GETTER = 0xf605c0,
    RVA_CAR_DYNAMICS_GETTER = 0xf605d0,
    RVA_DYNAMICS_VTABLE = 0x2eb5d68,
    RVA_DYNAMICS_INTERFACE_VTABLE = 0x2eb5e18,
    CAR_MODEL_ID = 0x118,
    CAR_DESCRIPTION = 0x1200,
    CAR_DYNAMICS = 0x4c08,
    DYNAMICS_INTERFACE = 0x3a0,
    SLOT_REAPPLY = 0x238,
    SLOT_VELOCITY = 0x288,
    DESCRIPTION_FRONT_SPRING = 0x324,
    DESCRIPTION_REAR_SPRING = 0x3b4,
    SPRING_DEFAULT_RIDE = 0x0c,
    SPRING_MIN_RIDE = 0x10,
    SPRING_MAX_RIDE = 0x14,
    DESCRIPTION_TUNING = 0x3800,
    DESCRIPTION_FRONT_RIDE = 0x3848,
    DESCRIPTION_REAR_RIDE = 0x3874,
    REAPPLY_FLAGS = 3,
    MEMORY_SLOTS = 32
};

typedef void (*TuneResolve)(void *description, void *tuning);
typedef void (*DynamicsReapply)(void *dynamics, void *description, int flags);
typedef float *(*VectorGetter)(void *dynamics, float *output);

typedef struct {
    float minimum, maximum;
} RideRange;

typedef struct {
    void *car, *dynamics;
    unsigned char *description;
    unsigned model;
    RideRange front, rear;          /* native range as the game built it */
    float written_front, written_rear;
    float written_front_min, written_rear_min, written_front_max, written_rear_max;
    AirSuspensionTargets applied;   /* last values passed to the game, controller space */
    uint64_t last_apply;
    int lowered_front, lowered_rear, raised_front, raised_rear, pending, failed;
} AirRideCar;

typedef struct {
    unsigned model;
    AirSuspensionTargets stance;
} AirRideMemory;

static unsigned char *image;
static int verified, requested, enabled, freeroam_only, remember, sounds;
static float max_speed_squared, extra_drop, extra_lift;
static unsigned interval_ms, double_tap_ms;
static int keys[AIR_RIDE_BINDING_COUNT];
static unsigned short pad_modifier, pad[AIR_RIDE_BINDING_COUNT], pad_lower, pad_raise,
    pad_stock;
static int pad_combos;
static PadGesture lower_gesture, raise_gesture, stock_gesture;
static AirRideCar current;
static AirRideMemory memory[MEMORY_SLOTS];
static unsigned memory_next;
static uint64_t last_tick, churn_started;
static unsigned churn_count;
static int blocked_logged;

static const unsigned char resolve_prologue[16] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                                   0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48};
static const unsigned char reapply_thunk[12] = {0x48, 0x81, 0xc1, 0x60, 0xfc, 0xff,
                                                0xff, 0xe9, 0x04, 0x00, 0x00, 0x00};
static const unsigned char reapply_prologue[16] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48,
                                                   0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x57};
static const unsigned char velocity_body[16] = {0xc5, 0xf8, 0x28, 0x81, 0xb0, 0xfc, 0xff, 0xff,
                                                0x48, 0x8b, 0xc2, 0xc5, 0xf8, 0x29, 0x02, 0xc3};
static const unsigned char description_getter[8] = {0x48, 0x8d, 0x81, 0x00,
                                                     0x12, 0x00, 0x00, 0xc3};
static const unsigned char dynamics_getter[8] = {0x48, 0x8b, 0x81, 0x08, 0x4c, 0x00, 0x00, 0xc3};

static int code_matches(unsigned rva, const unsigned char *bytes, size_t size) {
    unsigned char loaded[16];
    return size <= sizeof(loaded) && memory_read(image + rva, loaded, size) &&
           memory_equal(loaded, bytes, size);
}

static int vtable_slot_is(unsigned vtable_rva, unsigned slot, unsigned target_rva) {
    uintptr_t function;
    return memory_read(image + vtable_rva + slot, &function, sizeof(function)) &&
           function == (uintptr_t)(image + target_rva);
}

int air_ride_bind(unsigned char *game_image) {
    if (image != game_image) {
        current = (AirRideCar){0};
        air_suspension_reset();
    }
    image = game_image;
    verified = code_matches(RVA_TUNE_RESOLVE, resolve_prologue, sizeof(resolve_prologue)) &&
               code_matches(RVA_DYNAMICS_REAPPLY, reapply_thunk, sizeof(reapply_thunk)) &&
               code_matches(RVA_DYNAMICS_REAPPLY_BODY, reapply_prologue,
                            sizeof(reapply_prologue)) &&
               code_matches(RVA_DYNAMICS_VELOCITY, velocity_body, sizeof(velocity_body)) &&
               code_matches(RVA_CAR_DESCRIPTION_GETTER, description_getter,
                            sizeof(description_getter)) &&
               code_matches(RVA_CAR_DYNAMICS_GETTER, dynamics_getter, sizeof(dynamics_getter)) &&
               vtable_slot_is(RVA_DYNAMICS_INTERFACE_VTABLE, SLOT_REAPPLY, RVA_DYNAMICS_REAPPLY) &&
               vtable_slot_is(RVA_DYNAMICS_INTERFACE_VTABLE, SLOT_VELOCITY,
                              RVA_DYNAMICS_VELOCITY);
    return verified;
}

static float percent(int value) {
    return (float)value * 0.01f;
}

void air_ride_commit_options(const ModOptions *options) {
    enabled = options->air_ride && verified;
    freeroam_only = options->air_ride_freeroam_only;
    remember = options->air_ride_remember;
    /* The CCarDynamics velocity vector is treated as metres per second (assumption, see docs). */
    float limit = (float)options->air_ride_max_speed_kmh / 3.6f;
    max_speed_squared = options->air_ride_max_speed_kmh ? limit * limit : 0.01f;
    interval_ms = (unsigned)options->air_ride_interval_ms;
    extra_drop = percent(options->air_ride_extra_drop);
    extra_lift = percent(options->air_ride_extra_lift);
    pad_combos = 0;
    for (unsigned index = 0; index < AIR_RIDE_BINDING_COUNT; ++index) {
        keys[index] = options->air_ride_keys[index];
        pad[index] = (unsigned short)options->air_ride_pad[index];
        pad_combos |= pad[index] != 0;
    }
    pad_modifier = (unsigned short)options->air_ride_pad_modifier;
    pad_lower = (unsigned short)options->air_ride_pad_lower;
    pad_raise = (unsigned short)options->air_ride_pad_raise;
    pad_stock = (unsigned short)options->air_ride_pad_stock_tap;
    double_tap_ms = (unsigned)options->air_ride_double_tap_ms;
    pad_gesture_reset(&lower_gesture);
    pad_gesture_reset(&raise_gesture);
    pad_gesture_reset(&stock_gesture);
    AirSuspensionConfig config = {
        .travel_rate = percent(options->air_ride_travel_speed),
        .air_out_rate = percent(options->air_ride_air_out_speed),
        .step = percent(options->air_ride_step),
        .hold_rate = percent(options->air_ride_hold_speed),
        .extra_drop = extra_drop,
        .extra_lift = extra_lift,
        .repeat_delay_ms = 300,
        .preset_count = (unsigned)options->air_ride_preset_count,
    };
    for (unsigned index = 0; index < AIR_RIDE_PRESET_SLOTS; ++index) {
        config.presets[index].front = percent(options->air_ride_preset_front[index]);
        config.presets[index].rear = percent(options->air_ride_preset_rear[index]);
    }
    air_suspension_configure(&config);
    air_suspension_reset();
    sounds = enabled && options->air_ride_sounds && options->air_ride_sound_volume;
    air_sound_configure(sounds, options->air_ride_sound_volume);
    requested = options->air_ride;
}

void air_ride_log_status(void) {
    if (requested && !verified) {
        log_append("AIRRIDE: native tuning code did not match the supported executable; "
                   "air ride disabled.\r\n");
    } else if (enabled) {
        log_append(freeroam_only
                       ? "AIRRIDE: native live-tuning path verified; adjustments in free roam.\r\n"
                       : "AIRRIDE: native live-tuning path verified; adjustments allowed.\r\n");
    }
    if (sounds) {
        log_append("AIRSOUND: sounds on; prepared once your car is loaded (AirRide_*.wav beside "
                   "the INI replace the built-in ones).\r\n");
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestAirRideBind(unsigned char *game_image) {
    return air_ride_bind(game_image);
}
#endif

/* Only the optional modifier combos claim buttons; the double-tap gestures never do. */
int air_ride_claims_buttons(unsigned short buttons) {
    return enabled && pad_combos && pad_modifier && (buttons & pad_modifier) == pad_modifier;
}

int air_ride_claims_controller(void) {
    void *controller;
    unsigned short buttons;
    return enabled && pad_combos && pad_modifier && autodrive_game_buttons(&controller, &buttons) == 0 &&
           air_ride_claims_buttons(buttons);
}

static int has_vtable(const void *object, unsigned rva) {
    void *vtable;
    return memory_read_pointer(object, &vtable) && vtable == image + rva;
}

static int read_range(const unsigned char *description, unsigned offset, RideRange *range) {
    float values[3];
    if (!memory_read(description + offset + SPRING_DEFAULT_RIDE, values, sizeof(values))) {
        return 0;
    }
    range->minimum = values[1];
    range->maximum = values[2];
    /* Plausibility only: the native units are not assumed; the span must just be sane. */
    return float_is_finite(values[0]) && float_is_finite(values[1]) &&
           float_is_finite(values[2]) && values[1] > -10.0f && values[2] < 10.0f &&
           values[2] - values[1] > 1e-5f;
}

static int read_position(const unsigned char *description, unsigned offset, float *value) {
    return memory_read(description + offset, value, sizeof(*value)) && float_is_finite(*value) &&
           *value >= -0.0001f && *value <= 1.0001f;
}

static void *car_dynamics(void *car) {
    void *dynamics;
    if (!memory_read_pointer((unsigned char *)car + CAR_DYNAMICS, &dynamics) ||
        !has_vtable(dynamics, RVA_DYNAMICS_INTERFACE_VTABLE) ||
        !has_vtable((unsigned char *)dynamics - DYNAMICS_INTERFACE, RVA_DYNAMICS_VTABLE)) {
        return NULL;
    }
    return dynamics;
}

static int description_writable(unsigned char *description) {
    return memory_is_writable(description + DESCRIPTION_FRONT_RIDE, 4) &&
           memory_is_writable(description + DESCRIPTION_REAR_RIDE, 4) &&
           memory_is_writable(description + DESCRIPTION_FRONT_SPRING + SPRING_MIN_RIDE, 8) &&
           memory_is_writable(description + DESCRIPTION_REAR_SPRING + SPRING_MIN_RIDE, 8);
}

static AirRideMemory *remembered(unsigned model) {
    for (unsigned index = 0; index < MEMORY_SLOTS; ++index) {
        if (memory[index].model == model && model) {
            return &memory[index];
        }
    }
    return NULL;
}

static void remember_stance(unsigned model, AirSuspensionTargets stance) {
    if (!remember || !model) {
        return;
    }
    AirRideMemory *slot = remembered(model);
    if (!slot) {
        slot = &memory[memory_next];
        memory_next = (memory_next + 1) % MEMORY_SLOTS;
    }
    *slot = (AirRideMemory){model, stance};
}

static void log_range(const char *label, RideRange range) {
    log_append_value(label, (int)(range.minimum * 1000.0f));
    log_append_value("AIRRIDE:   max x1000 = ", (int)(range.maximum * 1000.0f));
}

/* Native 0..1 position inside `written` back into controller space for the native range. */
static float from_native(RideRange range, RideRange written, float position) {
    if (written.minimum == range.minimum && written.maximum == range.maximum) {
        return position;
    }
    float height = written.minimum + (written.maximum - written.minimum) * position;
    return (height - range.minimum) / (range.maximum - range.minimum);
}

static int attach(void *car, void *dynamics, int fresh) {
    unsigned char *description = (unsigned char *)car + CAR_DESCRIPTION;
    AirRideCar next = {.car = car, .dynamics = dynamics, .description = description};
    float front, rear;
    memory_read((unsigned char *)car + CAR_MODEL_ID, &next.model, sizeof(next.model));
    if (!read_range(description, DESCRIPTION_FRONT_SPRING, &next.front) ||
        !read_range(description, DESCRIPTION_REAR_SPRING, &next.rear) ||
        !read_position(description, DESCRIPTION_FRONT_RIDE, &front) ||
        !read_position(description, DESCRIPTION_REAR_RIDE, &rear) ||
        !description_writable(description)) {
        current = (AirRideCar){.car = car, .dynamics = dynamics, .failed = 1};
        log_append("AIRRIDE: local car ride-height data failed validation; this car is left "
                   "unchanged.\r\n");
        return 0;
    }
    if (!fresh && current.car == car) {
        /* A tune change on the same car keeps any minimum or maximum this file moved; the
           native range is still the one captured before the extra drop or lift was applied. */
        if (current.lowered_front && next.front.minimum == current.written_front_min) {
            next.front.minimum = current.front.minimum;
            next.lowered_front = 1;
        }
        if (current.lowered_rear && next.rear.minimum == current.written_rear_min) {
            next.rear.minimum = current.rear.minimum;
            next.lowered_rear = 1;
        }
        if (current.raised_front && next.front.maximum == current.written_front_max) {
            next.front.maximum = current.front.maximum;
            next.raised_front = 1;
        }
        if (current.raised_rear && next.rear.maximum == current.written_rear_max) {
            next.rear.maximum = current.rear.maximum;
            next.raised_rear = 1;
        }
    }
    next.written_front = front;
    next.written_rear = rear;
    next.written_front_min = next.lowered_front ? current.written_front_min : next.front.minimum;
    next.written_rear_min = next.lowered_rear ? current.written_rear_min : next.rear.minimum;
    next.written_front_max = next.raised_front ? current.written_front_max : next.front.maximum;
    next.written_rear_max = next.raised_rear ? current.written_rear_max : next.rear.maximum;
    float stock_front = from_native(
        next.front, (RideRange){next.written_front_min, next.written_front_max}, front);
    float stock_rear =
        from_native(next.rear, (RideRange){next.written_rear_min, next.written_rear_max}, rear);
    next.applied = (AirSuspensionTargets){stock_front, stock_rear};
    current = next;
    air_suspension_attach(stock_front, stock_rear);
    if (!fresh) {
        log_append("AIRRIDE: the game changed this car's tune; adopted it as the new stock.\r\n");
        return 1;
    }
    log_append_value("AIRRIDE: bound local car model ", (int)next.model);
    log_range("AIRRIDE:   front MinRideHeight x1000 = ", next.front);
    log_range("AIRRIDE:   rear MinRideHeight x1000 = ", next.rear);
    log_append_value("AIRRIDE:   tuned front position % = ", (int)(front * 100.0f + 0.5f));
    log_append_value("AIRRIDE:   tuned rear position % = ", (int)(rear * 100.0f + 0.5f));
    AirRideMemory *saved = fresh ? remembered(next.model) : NULL;
    if (saved) {
        air_suspension_set_target(saved->stance.front, saved->stance.rear);
        current.pending = 1;
        log_append("AIRRIDE: restoring the remembered stance for this model.\r\n");
    }
    return 1;
}

/*
 * Convert a controller value into the game's 0..1 position and the range it is relative to.
 * Inside the native range the game's own MinRideHeight and MaxRideHeight are used unchanged.
 * Experimental: below it MinRideHeight is lowered, above it MaxRideHeight is raised, so the
 * requested height still maps to 0..1.
 */
static float to_native(RideRange range, float value, int *lowered, int *raised,
                       RideRange *written) {
    float span = range.maximum - range.minimum;
    *lowered = value < 0.0f && extra_drop > 0.0f;
    *raised = value > 1.0f && extra_lift > 0.0f;
    written->minimum = *lowered ? range.minimum - span * extra_drop : range.minimum;
    written->maximum = *raised ? range.maximum + span * extra_lift : range.maximum;
    float height = range.minimum + span * value;
    float position = (height - written->minimum) / (written->maximum - written->minimum);
    return position < 0.0f ? 0.0f : position > 1.0f ? 1.0f : position;
}

static void apply(AirSuspensionTargets value, uint64_t now) {
    unsigned char *description = current.description;
    void *dynamics = current.dynamics;
    uintptr_t method;
    void *vtable;
    if (!memory_read_pointer(dynamics, &vtable) ||
        !memory_read((unsigned char *)vtable + SLOT_REAPPLY, &method, sizeof(method)) ||
        method != (uintptr_t)(image + RVA_DYNAMICS_REAPPLY) || !description_writable(description)) {
        current.failed = 1;
        log_append("AIRRIDE: native re-apply method changed; air ride stopped for this car.\r\n");
        return;
    }
    RideRange front_range, rear_range;
    float front = to_native(current.front, value.front, &current.lowered_front,
                            &current.raised_front, &front_range);
    float rear = to_native(current.rear, value.rear, &current.lowered_rear, &current.raised_rear,
                           &rear_range);
    *(volatile float *)(description + DESCRIPTION_FRONT_SPRING + SPRING_MIN_RIDE) =
        front_range.minimum;
    *(volatile float *)(description + DESCRIPTION_FRONT_SPRING + SPRING_MAX_RIDE) =
        front_range.maximum;
    *(volatile float *)(description + DESCRIPTION_REAR_SPRING + SPRING_MIN_RIDE) =
        rear_range.minimum;
    *(volatile float *)(description + DESCRIPTION_REAR_SPRING + SPRING_MAX_RIDE) =
        rear_range.maximum;
    *(volatile float *)(description + DESCRIPTION_FRONT_RIDE) = front;
    *(volatile float *)(description + DESCRIPTION_REAR_RIDE) = rear;
    ((TuneResolve)(image + RVA_TUNE_RESOLVE))(description, description + DESCRIPTION_TUNING);
    ((DynamicsReapply)method)(dynamics, description, REAPPLY_FLAGS);
    memory_read(description + DESCRIPTION_FRONT_RIDE, &current.written_front, sizeof(float));
    memory_read(description + DESCRIPTION_REAR_RIDE, &current.written_rear, sizeof(float));
    current.written_front_min = front_range.minimum;
    current.written_rear_min = rear_range.minimum;
    current.written_front_max = front_range.maximum;
    current.written_rear_max = rear_range.maximum;
    current.applied = value;
    current.last_apply = now;
}

static int changed_externally(void) {
    float front, rear;
    RideRange front_range, rear_range;
    if (!read_position(current.description, DESCRIPTION_FRONT_RIDE, &front) ||
        !read_position(current.description, DESCRIPTION_REAR_RIDE, &rear) ||
        !read_range(current.description, DESCRIPTION_FRONT_SPRING, &front_range) ||
        !read_range(current.description, DESCRIPTION_REAR_SPRING, &rear_range)) {
        return -1;
    }
    float tolerance = 0.002f;
    float a = front - current.written_front, b = rear - current.written_rear;
    float c = front_range.minimum - current.written_front_min;
    float d = rear_range.minimum - current.written_rear_min;
    float e = front_range.maximum - current.written_front_max;
    float f = rear_range.maximum - current.written_rear_max;
    return a > tolerance || a < -tolerance || b > tolerance || b < -tolerance || c > 1e-6f ||
           c < -1e-6f || d > 1e-6f || d < -1e-6f || e > 1e-6f || e < -1e-6f || f > 1e-6f ||
           f < -1e-6f;
}

static int speed_allows(void *dynamics) {
    uintptr_t method;
    void *vtable;
    if (!memory_read_pointer(dynamics, &vtable) ||
        !memory_read((unsigned char *)vtable + SLOT_VELOCITY, &method, sizeof(method)) ||
        method != (uintptr_t)(image + RVA_DYNAMICS_VELOCITY)) {
        return 0;
    }
    _Alignas(16) float velocity[4] = {0};
    ((VectorGetter)method)(dynamics, velocity);
    float squared = 0.0f;
    for (unsigned index = 0; index < 3; ++index) {
        if (!float_is_finite(velocity[index])) {
            return 0;
        }
        squared += velocity[index] * velocity[index];
    }
    return squared <= max_speed_squared;
}

static int pad_down(unsigned short buttons, unsigned short mask) {
    return mask && (buttons & mask) == mask;
}

/*
 * Double-tap gestures (default all on LB). When lower and raise share a button a double tap
 * toggles: air out, then full lift, then air out again; double tap and hold returns to the car's
 * own tune. With separate buttons, double tap and hold lowers or raises smoothly instead.
 * PadDoubleTapStock adds an optional separate stock button. Returns AIR_RIDE_* commands; a
 * double tap is a one-frame press, a held gesture keeps its command held.
 */
static unsigned read_gestures(unsigned short buttons, uint64_t now) {
    unsigned hold_ms = 200;
    unsigned commands = 0;
    int down = pad_gesture_update(&lower_gesture, pad_down(buttons, pad_lower), now,
                                  double_tap_ms, hold_ms);
    if (pad_raise == pad_lower) {
        if (down == PAD_GESTURE_DOUBLE_TAP) {
            commands |= air_suspension_is_air_out() ? AIR_RIDE_LIFT : AIR_RIDE_AIR_OUT;
        } else if (down == PAD_GESTURE_HOLD) {
            commands |= AIR_RIDE_STOCK; /* fires once: stock reacts to the press edge only */
        }
    } else {
        int up = pad_gesture_update(&raise_gesture, pad_down(buttons, pad_raise), now,
                                    double_tap_ms, hold_ms);
        if (down == PAD_GESTURE_DOUBLE_TAP) {
            commands |= air_suspension_is_air_out() ? 0u : AIR_RIDE_AIR_OUT;
        } else if (down == PAD_GESTURE_HOLD) {
            commands |= AIR_RIDE_BOTH_DOWN;
        }
        if (up == PAD_GESTURE_DOUBLE_TAP) {
            commands |= AIR_RIDE_LIFT;
        } else if (up == PAD_GESTURE_HOLD) {
            commands |= AIR_RIDE_BOTH_UP;
        }
    }
    if (pad_gesture_update(&stock_gesture, pad_down(buttons, pad_stock), now, double_tap_ms,
                           hold_ms) == PAD_GESTURE_DOUBLE_TAP) {
        commands |= AIR_RIDE_STOCK;
    }
    return commands;
}

static unsigned read_commands(int focused, uint64_t now) {
    unsigned commands = 0;
    if (focused) {
        for (unsigned index = 0; index < AIR_RIDE_BINDING_COUNT; ++index) {
            if (keys[index] && game_input_key_down(keys[index])) {
                commands |= 1u << index;
            }
        }
    }
    void *controller;
    unsigned short buttons = 0;
    if (!focused || autodrive_game_buttons(&controller, &buttons) != 0 ||
        !autodrive_game_has_gameplay_input(controller)) {
        buttons = 0;
    }
    commands |= read_gestures(buttons, now);
    if (buttons && (!pad_modifier || (buttons & pad_modifier) == pad_modifier)) {
        for (unsigned index = 0; index < AIR_RIDE_BINDING_COUNT; ++index) {
            if (pad[index] && (buttons & pad[index]) == pad[index]) {
                commands |= 1u << index;
            }
        }
    }
    return commands;
}

static void log_events(unsigned events) {
    if (events & AIR_RIDE_EVENT_AIR_OUT) {
        log_append("AIRRIDE: air-out.\r\n");
    } else if (events & AIR_RIDE_EVENT_AIR_UP) {
        log_append("AIRRIDE: aired up to the previous stance.\r\n");
    } else if (events & AIR_RIDE_EVENT_LIFT) {
        log_append("AIRRIDE: full lift.\r\n");
    } else if (events & AIR_RIDE_EVENT_STOCK) {
        log_append("AIRRIDE: returning to the car's spawned tune.\r\n");
    } else if (events & AIR_RIDE_EVENT_PRESET) {
        log_append_value("AIRRIDE: stance preset ", air_suspension_preset_index() + 1);
    }
}

static void tick(void *car, int focused, uint64_t now);

void air_ride_tick(void *car, int focused, uint64_t now) {
    tick(car, focused, now);
    if (current.failed || !current.car || current.car != car) {
        air_sound_stop();
    }
}

static void tick(void *car, int focused, uint64_t now) {
    if (!enabled || !car) {
        return;
    }
    float dt = last_tick && now > last_tick ? (float)(now - last_tick) * 0.001f : 0.0f;
    last_tick = now;
    void *dynamics = car_dynamics(car);
    if (!dynamics) {
        if (current.car) {
            current = (AirRideCar){0};
        }
        return;
    }
    if (car != current.car || dynamics != current.dynamics) {
        air_sound_stop();
        if (!attach(car, dynamics, 1)) {
            return;
        }
    }
    if (current.failed) {
        return;
    }
    int external = changed_externally();
    if (external < 0) {
        current.failed = 1;
        log_append("AIRRIDE: ride-height data became invalid; air ride stopped for this car.\r\n");
        return;
    }
    if (external) {
        /* The game rebuilt or re-tuned the car (tuning menu, respawn); its values win. */
        if (now - churn_started > 2000) {
            churn_started = now;
            churn_count = 0;
        }
        if (++churn_count >= 5) {
            current.failed = 1;
            log_append("AIRRIDE: the game keeps rewriting this car's ride height; air ride "
                       "stopped for this car.\r\n");
            return;
        }
        if (!attach(car, dynamics, 0)) {
            return;
        }
    }
    int allowed = focused && (!freeroam_only || autodrive_game_is_freeroam()) &&
                  !autodrive_game_is_carmeet() && speed_allows(dynamics);
    unsigned commands = read_commands(focused, now);
    if (commands && !allowed && !blocked_logged) {
        blocked_logged = 1;
        log_append("AIRRIDE: adjustment waits for free roam and the configured speed limit.\r\n");
    }
    AirSuspensionTargets before = air_suspension_targets();
    unsigned events = air_suspension_update(commands, dt, now, allowed);
    AirSuspensionTargets position = air_suspension_targets();
    /* When blocked the position does not move, so a running vent or compressor sound stops. */
    air_sound_frame(events, position.front - before.front + position.rear - before.rear, now);
    if (events & AIR_RIDE_EVENT_TARGET) {
        current.pending = 1;
        log_events(events);
    }
    if (!allowed) {
        return;
    }
    int differs = position.front != current.applied.front || position.rear != current.applied.rear;
    if (differs && (now - current.last_apply >= interval_ms || !current.last_apply)) {
        apply(position, now);
    }
    if (current.pending && !differs) {
        AirSuspensionTargets goal = air_suspension_goal();
        if (goal.front == position.front && goal.rear == position.rear) {
            current.pending = 0;
            remember_stance(current.model, goal);
        }
    }
}
