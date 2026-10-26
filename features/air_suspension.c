#include "air_suspension.h"

#define AIR_RIDE_SETTLED 0.0005f

typedef struct {
    AirSuspensionTargets position, goal, stock, before_air_out;
    unsigned held, suppressed, repeating;
    unsigned long long pressed_at;
    int air_out, travel_fast, preset;
} AirSuspensionState;

static AirSuspensionConfig config = {
    .travel_rate = 0.35f,
    .air_out_rate = 0.80f,
    .step = 0.02f,
    .hold_rate = 0.25f,
    .extra_drop = 0.0f,
    .extra_lift = 0.0f,
    .repeat_delay_ms = 300,
    .preset_count = 0,
};

static AirSuspensionState state;

static float lower_bound(void) {
    return -config.extra_drop;
}

static float upper_bound(void) {
    return 1.0f + config.extra_lift;
}

static float clamp_height(float value) {
    float lower = lower_bound(), upper = upper_bound();
    /* NaN compares false everywhere; force it to a defined value. */
    if (!(value == value)) {
        return 0.5f;
    }
    if (value < lower) {
        return lower;
    }
    if (value > upper) {
        return upper;
    }
    return value;
}

static float move_towards(float current, float target, float step) {
    float difference = target - current;
    if (difference > step) {
        return current + step;
    }
    if (difference < -step) {
        return current - step;
    }
    return target;
}

static float distance(float left, float right) {
    float difference = left - right;
    return difference < 0.0f ? -difference : difference;
}

void air_suspension_configure(const AirSuspensionConfig *value) {
    config = *value;
    if (config.extra_drop < 0.0f || !(config.extra_drop == config.extra_drop)) {
        config.extra_drop = 0.0f;
    }
    if (config.extra_lift < 0.0f || !(config.extra_lift == config.extra_lift)) {
        config.extra_lift = 0.0f;
    }
    if (config.preset_count > AIR_RIDE_PRESET_CAPACITY) {
        config.preset_count = AIR_RIDE_PRESET_CAPACITY;
    }
    for (unsigned index = 0; index < config.preset_count; ++index) {
        config.presets[index].front = clamp_height(config.presets[index].front);
        config.presets[index].rear = clamp_height(config.presets[index].rear);
    }
}

void air_suspension_reset(void) {
    state = (AirSuspensionState){
        .position = {0.5f, 0.5f}, .goal = {0.5f, 0.5f}, .stock = {0.5f, 0.5f}, .preset = -1};
}

void air_suspension_attach(float stock_front, float stock_rear) {
    unsigned held = state.held;
    air_suspension_reset();
    state.stock = (AirSuspensionTargets){clamp_height(stock_front), clamp_height(stock_rear)};
    state.position = state.goal = state.stock;
    /* Inputs that were already held on the previous car must be released first. */
    state.held = state.suppressed = held;
    state.repeating = 0;
}

void air_suspension_adopt(float front, float rear) {
    state.stock = (AirSuspensionTargets){clamp_height(front), clamp_height(rear)};
    state.position = state.goal = state.stock;
    state.air_out = 0;
    state.travel_fast = 0;
    state.preset = -1;
}

void air_suspension_set_target(float front, float rear) {
    state.goal = (AirSuspensionTargets){clamp_height(front), clamp_height(rear)};
    state.air_out = 0;
    state.travel_fast = 0;
}

static void adjust(float front, float rear) {
    if (state.air_out) {
        /* A manual adjustment while aired out keeps the bags where the user is steering them. */
        state.air_out = 0;
    }
    state.travel_fast = 0;
    state.preset = -1;
    state.goal.front = clamp_height(state.goal.front + front);
    state.goal.rear = clamp_height(state.goal.rear + rear);
}

static void axis_delta(unsigned commands, float amount, float *front, float *rear) {
    if (commands & (AIR_RIDE_FRONT_UP | AIR_RIDE_BOTH_UP)) {
        *front += amount;
    }
    if (commands & (AIR_RIDE_FRONT_DOWN | AIR_RIDE_BOTH_DOWN)) {
        *front -= amount;
    }
    if (commands & (AIR_RIDE_REAR_UP | AIR_RIDE_BOTH_UP)) {
        *rear += amount;
    }
    if (commands & (AIR_RIDE_REAR_DOWN | AIR_RIDE_BOTH_DOWN)) {
        *rear -= amount;
    }
}

unsigned air_suspension_update(unsigned commands, float dt, unsigned long long now_ms,
                               int allowed) {
    enum {
        ADJUST = AIR_RIDE_FRONT_UP | AIR_RIDE_FRONT_DOWN | AIR_RIDE_REAR_UP | AIR_RIDE_REAR_DOWN |
                 AIR_RIDE_BOTH_UP | AIR_RIDE_BOTH_DOWN
    };
    unsigned events = 0;
    if (!allowed || !(dt > 0.0f) || dt > 0.25f) {
        /* Nothing fires while blocked; held inputs must be released and pressed again. */
        state.held = commands;
        state.suppressed |= commands;
        state.repeating = 0;
        return 0;
    }
    state.suppressed &= commands;
    commands &= ~state.suppressed;
    unsigned pressed = commands & ~state.held;
    if (pressed & AIR_RIDE_AIR_OUT) {
        if (state.air_out) {
            state.goal = state.before_air_out;
            state.air_out = 0;
            events |= AIR_RIDE_EVENT_AIR_UP;
        } else {
            state.before_air_out = state.goal;
            state.goal.front = state.goal.rear = lower_bound();
            state.air_out = 1;
            events |= AIR_RIDE_EVENT_AIR_OUT;
        }
        state.travel_fast = 1;
        state.preset = -1;
        events |= AIR_RIDE_EVENT_TARGET;
    } else if (pressed & AIR_RIDE_LIFT) {
        /* Full lift to the highest allowed height; air-out remembers nothing from here. */
        state.goal.front = state.goal.rear = upper_bound();
        state.air_out = 0;
        state.travel_fast = 1;
        state.preset = -1;
        events |= AIR_RIDE_EVENT_LIFT | AIR_RIDE_EVENT_TARGET;
    } else if (pressed & AIR_RIDE_STOCK) {
        state.goal = state.stock;
        state.air_out = 0;
        state.travel_fast = 0;
        state.preset = -1;
        events |= AIR_RIDE_EVENT_STOCK | AIR_RIDE_EVENT_TARGET;
    } else if (pressed & (AIR_RIDE_PRESET_NEXT | AIR_RIDE_PRESET_PREVIOUS) && config.preset_count) {
        int count = (int)config.preset_count;
        if (pressed & AIR_RIDE_PRESET_NEXT) {
            state.preset = state.preset < 0 ? 0 : (state.preset + 1) % count;
        } else {
            state.preset = state.preset <= 0 ? count - 1 : state.preset - 1;
        }
        state.goal = config.presets[state.preset];
        state.air_out = 0;
        state.travel_fast = 0;
        events |= AIR_RIDE_EVENT_PRESET | AIR_RIDE_EVENT_TARGET;
    } else {
        float front = 0.0f, rear = 0.0f;
        unsigned adjusting = commands & ADJUST;
        if (pressed & ADJUST) {
            axis_delta(pressed, config.step, &front, &rear);
            state.pressed_at = now_ms;
            state.repeating = 0;
        } else if (adjusting) {
            if (!state.repeating && now_ms - state.pressed_at >= config.repeat_delay_ms) {
                state.repeating = 1;
            }
            if (state.repeating) {
                axis_delta(adjusting, config.hold_rate * dt, &front, &rear);
            }
        } else {
            state.repeating = 0;
        }
        if (front != 0.0f || rear != 0.0f) {
            float before_front = state.goal.front, before_rear = state.goal.rear;
            adjust(front, rear);
            if (state.goal.front != before_front || state.goal.rear != before_rear) {
                events |= AIR_RIDE_EVENT_TARGET;
            }
        }
    }
    state.held = commands;

    float rate = state.travel_fast ? config.air_out_rate : config.travel_rate;
    float step = rate * dt;
    AirSuspensionTargets previous = state.position;
    state.position.front = move_towards(state.position.front, state.goal.front, step);
    state.position.rear = move_towards(state.position.rear, state.goal.rear, step);
    if (distance(previous.front, state.position.front) > 0.0f ||
        distance(previous.rear, state.position.rear) > 0.0f) {
        events |= AIR_RIDE_EVENT_MOVING;
    }
    if (distance(state.position.front, state.goal.front) < AIR_RIDE_SETTLED &&
        distance(state.position.rear, state.goal.rear) < AIR_RIDE_SETTLED) {
        state.position = state.goal;
        state.travel_fast = 0;
    }
    return events;
}

AirSuspensionTargets air_suspension_targets(void) {
    return state.position;
}

AirSuspensionTargets air_suspension_goal(void) {
    return state.goal;
}

AirSuspensionTargets air_suspension_stock(void) {
    return state.stock;
}

int air_suspension_is_air_out(void) {
    return state.air_out;
}

int air_suspension_preset_index(void) {
    return state.preset;
}
