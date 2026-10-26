#include <stdio.h>
#include <stdlib.h>
#include "../features/air_suspension.h"

static int failures;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static int near(float value, float expected) {
    float difference = value - expected;
    return difference < 0.0005f && difference > -0.0005f;
}

static AirSuspensionConfig base_config(void) {
    AirSuspensionConfig config = {
        .travel_rate = 0.5f,
        .air_out_rate = 1.0f,
        .step = 0.02f,
        .hold_rate = 0.25f,
        .extra_drop = 0.0f,
        .repeat_delay_ms = 300,
        .preset_count = 3,
        .presets = {{0.0f, 0.0f}, {0.15f, 0.30f}, {1.0f, 1.0f}},
    };
    return config;
}

/* Run frames of 1/60 s with the given inputs held, starting at *clock milliseconds. */
static unsigned run(unsigned commands, unsigned frames, unsigned long long *clock, int allowed) {
    unsigned events = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        *clock += 16;
        events |= air_suspension_update(commands, 0.016f, *clock, allowed);
    }
    return events;
}

static void test_attach_and_step(void) {
    unsigned long long clock = 1000;
    AirSuspensionConfig config = base_config();
    air_suspension_configure(&config);
    air_suspension_attach(0.40f, 0.60f);
    AirSuspensionTargets t = air_suspension_targets();
    CHECK(near(t.front, 0.40f) && near(t.rear, 0.60f));

    /* One press moves only the front goal by one step; the position travels smoothly. */
    unsigned events = run(AIR_RIDE_FRONT_DOWN, 1, &clock, 1);
    CHECK(events & AIR_RIDE_EVENT_TARGET);
    CHECK(near(air_suspension_goal().front, 0.38f) && near(air_suspension_goal().rear, 0.60f));
    CHECK(air_suspension_targets().front < 0.40f);
    CHECK(air_suspension_targets().front > 0.38f - 0.0001f);
    run(0, 30, &clock, 1);
    CHECK(near(air_suspension_targets().front, 0.38f));
    CHECK(near(air_suspension_targets().rear, 0.60f));
}

static void test_hold_repeat(void) {
    unsigned long long clock = 5000;
    AirSuspensionConfig config = base_config();
    air_suspension_configure(&config);
    air_suspension_attach(0.50f, 0.50f);
    /* Held under the repeat delay: exactly one step. */
    run(AIR_RIDE_REAR_UP, 10, &clock, 1);
    CHECK(near(air_suspension_goal().rear, 0.52f));
    /* Held past the delay: continuous change at hold_rate. */
    run(AIR_RIDE_REAR_UP, 60, &clock, 1);
    CHECK(air_suspension_goal().rear > 0.60f);
    CHECK(near(air_suspension_goal().front, 0.50f));
    /* Goal never exceeds the native maximum. */
    run(AIR_RIDE_REAR_UP, 600, &clock, 1);
    CHECK(near(air_suspension_goal().rear, 1.0f));
}

static void test_both_axles(void) {
    unsigned long long clock = 9000;
    AirSuspensionConfig config = base_config();
    air_suspension_configure(&config);
    air_suspension_attach(0.50f, 0.50f);
    run(AIR_RIDE_BOTH_DOWN, 1, &clock, 1);
    run(0, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, 0.48f) && near(air_suspension_goal().rear, 0.48f));
}

static void test_air_out_and_stock(void) {
    unsigned long long clock = 20000;
    AirSuspensionConfig config = base_config();
    air_suspension_configure(&config);
    air_suspension_attach(0.70f, 0.65f);
    run(AIR_RIDE_FRONT_UP, 1, &clock, 1);
    run(0, 60, &clock, 1);
    AirSuspensionTargets before = air_suspension_goal();
    unsigned events = run(AIR_RIDE_AIR_OUT, 1, &clock, 1);
    CHECK(events & AIR_RIDE_EVENT_AIR_OUT);
    CHECK(air_suspension_is_air_out());
    CHECK(near(air_suspension_goal().front, 0.0f) && near(air_suspension_goal().rear, 0.0f));
    /* Held air-out does not toggle again. */
    run(AIR_RIDE_AIR_OUT, 60, &clock, 1);
    CHECK(air_suspension_is_air_out());
    CHECK(near(air_suspension_targets().front, 0.0f) && near(air_suspension_targets().rear, 0.0f));
    /* Second press airs up to the previous stance. */
    run(0, 1, &clock, 1);
    events = run(AIR_RIDE_AIR_OUT, 1, &clock, 1);
    CHECK(events & AIR_RIDE_EVENT_AIR_UP);
    CHECK(!air_suspension_is_air_out());
    CHECK(near(air_suspension_goal().front, before.front) &&
          near(air_suspension_goal().rear, before.rear));
    run(0, 1, &clock, 1);
    events = run(AIR_RIDE_STOCK, 1, &clock, 1);
    CHECK(events & AIR_RIDE_EVENT_STOCK);
    run(0, 120, &clock, 1);
    CHECK(near(air_suspension_targets().front, 0.70f) && near(air_suspension_targets().rear, 0.65f));
}

static void test_presets(void) {
    unsigned long long clock = 40000;
    AirSuspensionConfig config = base_config();
    air_suspension_configure(&config);
    air_suspension_attach(0.50f, 0.50f);
    run(AIR_RIDE_PRESET_NEXT, 1, &clock, 1);
    CHECK(air_suspension_preset_index() == 0);
    CHECK(near(air_suspension_goal().front, 0.0f));
    run(0, 1, &clock, 1);
    run(AIR_RIDE_PRESET_NEXT, 1, &clock, 1);
    CHECK(air_suspension_preset_index() == 1);
    CHECK(near(air_suspension_goal().front, 0.15f) && near(air_suspension_goal().rear, 0.30f));
    run(0, 1, &clock, 1);
    run(AIR_RIDE_PRESET_PREVIOUS, 1, &clock, 1);
    run(0, 1, &clock, 1);
    run(AIR_RIDE_PRESET_PREVIOUS, 1, &clock, 1);
    CHECK(air_suspension_preset_index() == 2); /* wraps */
    CHECK(near(air_suspension_goal().front, 1.0f));
}

static void test_blocked_and_edges(void) {
    unsigned long long clock = 60000;
    AirSuspensionConfig config = base_config();
    air_suspension_configure(&config);
    air_suspension_attach(0.50f, 0.50f);
    /* While blocked nothing changes and nothing moves. */
    CHECK(run(AIR_RIDE_FRONT_DOWN, 30, &clock, 0) == 0);
    CHECK(near(air_suspension_goal().front, 0.50f));
    /* An input held through the block must be released before it acts. */
    run(AIR_RIDE_FRONT_DOWN, 5, &clock, 1);
    CHECK(near(air_suspension_goal().front, 0.50f));
    run(0, 1, &clock, 1);
    run(AIR_RIDE_FRONT_DOWN, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, 0.48f));
    /* Hitches and invalid frame times are ignored. */
    CHECK(air_suspension_update(0, 1.0f, clock, 1) == 0);
    CHECK(air_suspension_update(0, -0.1f, clock, 1) == 0);
}

static void test_extra_drop(void) {
    unsigned long long clock = 80000;
    AirSuspensionConfig config = base_config();
    config.extra_drop = 0.10f;
    air_suspension_configure(&config);
    air_suspension_attach(0.05f, 0.05f);
    run(AIR_RIDE_AIR_OUT, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, -0.10f));
    run(0, 120, &clock, 1);
    CHECK(near(air_suspension_targets().rear, -0.10f));
    /* Without extra drop the floor is the native minimum. */
    config.extra_drop = 0.0f;
    air_suspension_configure(&config);
    air_suspension_attach(0.05f, 0.05f);
    run(AIR_RIDE_BOTH_DOWN, 1, &clock, 1);
    run(0, 1, &clock, 1);
    run(AIR_RIDE_BOTH_DOWN, 1, &clock, 1);
    run(0, 1, &clock, 1);
    run(AIR_RIDE_BOTH_DOWN, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, 0.0f));
}

static void test_lift(void) {
    unsigned long long clock = 85000;
    AirSuspensionConfig config = base_config();
    config.extra_lift = 0.50f;
    air_suspension_configure(&config);
    air_suspension_attach(0.40f, 0.40f);
    unsigned events = run(AIR_RIDE_LIFT, 1, &clock, 1);
    CHECK(events & AIR_RIDE_EVENT_LIFT);
    CHECK(near(air_suspension_goal().front, 1.50f) && near(air_suspension_goal().rear, 1.50f));
    /* Lift travels at the fast air-out rate: 1.1 of travel at 1.0/s takes ~1.1 s. */
    run(0, 75, &clock, 1);
    CHECK(near(air_suspension_targets().front, 1.50f));
    /* Holding raise cannot go past the lifted ceiling; stock comes back down. */
    run(AIR_RIDE_BOTH_UP, 60, &clock, 1);
    CHECK(near(air_suspension_goal().rear, 1.50f));
    run(0, 1, &clock, 1);
    run(AIR_RIDE_STOCK, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, 0.40f));
    /* Air-out from a lift drops to the floor and airs back up to the lifted stance. */
    run(0, 1, &clock, 1);
    run(AIR_RIDE_LIFT, 1, &clock, 1);
    run(0, 1, &clock, 1);
    run(AIR_RIDE_AIR_OUT, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, 0.0f) && air_suspension_is_air_out());
    run(0, 1, &clock, 1);
    run(AIR_RIDE_LIFT, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, 1.50f) && !air_suspension_is_air_out());
    /* Without extra lift the ceiling is the native maximum. */
    run(0, 1, &clock, 1);
    config.extra_lift = 0.0f;
    air_suspension_configure(&config);
    air_suspension_attach(0.40f, 0.40f);
    run(AIR_RIDE_LIFT, 1, &clock, 1);
    CHECK(near(air_suspension_goal().front, 1.0f));
}

static void test_adopt_and_set_target(void) {
    unsigned long long clock = 90000;
    AirSuspensionConfig config = base_config();
    air_suspension_configure(&config);
    air_suspension_attach(0.50f, 0.50f);
    air_suspension_set_target(0.20f, 0.25f);
    CHECK(near(air_suspension_targets().front, 0.50f));
    run(0, 120, &clock, 1);
    CHECK(near(air_suspension_targets().front, 0.20f) && near(air_suspension_targets().rear, 0.25f));
    air_suspension_adopt(0.80f, 0.75f);
    CHECK(near(air_suspension_targets().front, 0.80f) && near(air_suspension_stock().rear, 0.75f));
    /* Out-of-range and NaN values are clamped to a defined height. */
    float nan = 0.0f / 0.0f;
    air_suspension_set_target(nan, 5.0f);
    CHECK(near(air_suspension_goal().front, 0.5f) && near(air_suspension_goal().rear, 1.0f));
}

int main(void) {
    test_attach_and_step();
    test_hold_repeat();
    test_both_axles();
    test_air_out_and_stock();
    test_presets();
    test_blocked_and_edges();
    test_extra_drop();
    test_lift();
    test_adopt_and_set_target();
    if (failures) {
        printf("%d air suspension controller check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("air suspension controller tests passed");
    return EXIT_SUCCESS;
}
