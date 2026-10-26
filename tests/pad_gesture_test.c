#include <stdio.h>
#include <stdlib.h>
#include "../features/pad_gesture.h"

static int failures;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

enum { WINDOW = 300, HOLD = 200 };

/* Feed `frames` frames of 16 ms with the button in `down`; returns every result seen, ORed,
   and counts double taps. */
static int feed(PadGesture *gesture, int down, unsigned frames, unsigned long long *clock,
                unsigned *taps) {
    int seen = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        *clock += 16;
        int result = pad_gesture_update(gesture, down, *clock, WINDOW, HOLD);
        seen |= result;
        *taps += result == PAD_GESTURE_DOUBLE_TAP;
    }
    return seen;
}

static void test_double_tap(void) {
    PadGesture gesture;
    pad_gesture_reset(&gesture);
    unsigned long long clock = 1000;
    unsigned taps = 0;
    CHECK(feed(&gesture, 1, 5, &clock, &taps) == 0);
    CHECK(feed(&gesture, 0, 5, &clock, &taps) == 0);
    CHECK(feed(&gesture, 1, 5, &clock, &taps) == 0);
    CHECK(feed(&gesture, 0, 1, &clock, &taps) == PAD_GESTURE_DOUBLE_TAP && taps == 1);
    /* Fires once only. */
    CHECK(feed(&gesture, 0, 30, &clock, &taps) == 0 && taps == 1);
}

static void test_single_and_long_presses(void) {
    PadGesture gesture;
    pad_gesture_reset(&gesture);
    unsigned long long clock = 5000;
    unsigned taps = 0;
    /* A single tap, then a slow second press: nothing. */
    feed(&gesture, 1, 5, &clock, &taps);
    feed(&gesture, 0, 25, &clock, &taps);
    feed(&gesture, 1, 5, &clock, &taps);
    feed(&gesture, 0, 30, &clock, &taps);
    CHECK(taps == 0);
    /* Holding the button (normal driving use) never starts a gesture. */
    CHECK(feed(&gesture, 1, 100, &clock, &taps) == 0);
    feed(&gesture, 0, 5, &clock, &taps);
    feed(&gesture, 1, 5, &clock, &taps);
    feed(&gesture, 0, 1, &clock, &taps);
    CHECK(taps == 0);
    /* A quick double tap afterwards still works. */
    feed(&gesture, 0, 30, &clock, &taps);
    feed(&gesture, 1, 5, &clock, &taps);
    feed(&gesture, 0, 5, &clock, &taps);
    feed(&gesture, 1, 5, &clock, &taps);
    feed(&gesture, 0, 1, &clock, &taps);
    CHECK(taps == 1);
}

static void test_double_tap_and_hold(void) {
    PadGesture gesture;
    pad_gesture_reset(&gesture);
    unsigned long long clock = 9000;
    unsigned taps = 0;
    feed(&gesture, 1, 5, &clock, &taps);
    feed(&gesture, 0, 5, &clock, &taps);
    /* The second press starts on the first of these frames; 12 frames later it is 192 ms. */
    CHECK(feed(&gesture, 1, 13, &clock, &taps) == 0);
    CHECK(feed(&gesture, 1, 1, &clock, &taps) == PAD_GESTURE_HOLD);
    CHECK(feed(&gesture, 1, 60, &clock, &taps) == PAD_GESTURE_HOLD);
    CHECK(feed(&gesture, 0, 1, &clock, &taps) == 0 && taps == 0);
}

int main(void) {
    test_double_tap();
    test_single_and_long_presses();
    test_double_tap_and_hold();
    if (failures) {
        printf("%d pad gesture check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("pad gesture tests passed");
    return EXIT_SUCCESS;
}
