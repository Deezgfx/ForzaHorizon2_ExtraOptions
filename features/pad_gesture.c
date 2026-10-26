#include "pad_gesture.h"

enum {
    IDLE,
    FIRST_DOWN,   /* first press, still short enough to be a tap */
    WAIT_SECOND,  /* first tap released, waiting for the second press */
    SECOND_DOWN,  /* second press, not yet long enough to be a hold */
    HOLDING,      /* second press held */
    IGNORED       /* a press that is not part of a gesture; wait for release */
};

void pad_gesture_reset(PadGesture *gesture) {
    gesture->phase = IDLE;
    gesture->since = 0;
}

int pad_gesture_update(PadGesture *gesture, int down, unsigned long long now_ms,
                       unsigned window_ms, unsigned hold_ms) {
    unsigned long long elapsed = now_ms - gesture->since;
    switch (gesture->phase) {
    case IDLE:
        if (down) {
            gesture->phase = FIRST_DOWN;
            gesture->since = now_ms;
        }
        return PAD_GESTURE_NONE;
    case FIRST_DOWN:
        if (!down) {
            gesture->phase = WAIT_SECOND;
            gesture->since = now_ms;
        } else if (elapsed > window_ms) {
            gesture->phase = IGNORED;
        }
        return PAD_GESTURE_NONE;
    case WAIT_SECOND:
        if (elapsed > window_ms) {
            /* Too slow: a press now starts a new gesture. */
            gesture->phase = down ? FIRST_DOWN : IDLE;
            gesture->since = now_ms;
        } else if (down) {
            gesture->phase = SECOND_DOWN;
            gesture->since = now_ms;
        }
        return PAD_GESTURE_NONE;
    case SECOND_DOWN:
        if (!down) {
            gesture->phase = IDLE;
            return PAD_GESTURE_DOUBLE_TAP;
        }
        if (elapsed >= hold_ms) {
            gesture->phase = HOLDING;
            return PAD_GESTURE_HOLD;
        }
        return PAD_GESTURE_NONE;
    case HOLDING:
        if (down) {
            return PAD_GESTURE_HOLD;
        }
        gesture->phase = IDLE;
        return PAD_GESTURE_NONE;
    default:
        if (!down) {
            gesture->phase = IDLE;
        }
        return PAD_GESTURE_NONE;
    }
}
