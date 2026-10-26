#ifndef FH2_PAD_GESTURE_H
#define FH2_PAD_GESTURE_H

/*
 * Platform-independent double-tap detector for one controller button.
 *
 *   tap, release, tap and release again quickly  -> PAD_GESTURE_DOUBLE_TAP for one frame
 *   tap, release, tap and keep holding            -> PAD_GESTURE_HOLD every frame until release
 *
 * A single press, or a press held longer than the tap window, produces nothing, so normal use of
 * the button by the game is left alone.
 */

enum {
    PAD_GESTURE_NONE = 0,
    PAD_GESTURE_DOUBLE_TAP = 1,
    PAD_GESTURE_HOLD = 2
};

typedef struct {
    int phase;
    unsigned long long since;
} PadGesture;

/* `window_ms`: longest press of the first tap and longest gap before the second one.
   `hold_ms`: how long the second press must be held to become a hold. */
void pad_gesture_reset(PadGesture *gesture);
int pad_gesture_update(PadGesture *gesture, int down, unsigned long long now_ms,
                       unsigned window_ms, unsigned hold_ms);

#endif
