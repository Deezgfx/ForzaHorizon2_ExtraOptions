#ifndef FH2_AIR_SUSPENSION_H
#define FH2_AIR_SUSPENSION_H

/*
 * Platform-independent air ride controller. Heights are normalized to the car's own native
 * ride-height tuning range: 0 is MinRideHeight, 1 is MaxRideHeight. Values below 0 are only
 * produced when an extra drop below the native minimum is configured, values above 1 only when
 * an extra lift above the native maximum is configured. The game adapter in
 * air_ride.c converts these values into the game's tuning fields.
 */

#define AIR_RIDE_PRESET_CAPACITY 5

enum {
    AIR_RIDE_FRONT_UP = 1u << 0,
    AIR_RIDE_FRONT_DOWN = 1u << 1,
    AIR_RIDE_REAR_UP = 1u << 2,
    AIR_RIDE_REAR_DOWN = 1u << 3,
    AIR_RIDE_BOTH_UP = 1u << 4,
    AIR_RIDE_BOTH_DOWN = 1u << 5,
    AIR_RIDE_AIR_OUT = 1u << 6,
    AIR_RIDE_STOCK = 1u << 7,
    AIR_RIDE_PRESET_NEXT = 1u << 8,
    AIR_RIDE_PRESET_PREVIOUS = 1u << 9,
    AIR_RIDE_LIFT = 1u << 10
};

enum {
    AIR_RIDE_EVENT_TARGET = 1u << 0,
    AIR_RIDE_EVENT_MOVING = 1u << 1,
    AIR_RIDE_EVENT_AIR_OUT = 1u << 2,
    AIR_RIDE_EVENT_AIR_UP = 1u << 3,
    AIR_RIDE_EVENT_STOCK = 1u << 4,
    AIR_RIDE_EVENT_PRESET = 1u << 5,
    AIR_RIDE_EVENT_LIFT = 1u << 6
};

typedef struct {
    float front;
    float rear;
} AirSuspensionTargets;

typedef struct {
    float travel_rate;    /* normalized units per second for smooth travel */
    float air_out_rate;   /* normalized units per second while dumping or airing up */
    float step;           /* target change per button press */
    float hold_rate;      /* target change per second while a raise/lower input is held */
    float extra_drop;     /* allowed travel below the native minimum, 0 disables it */
    float extra_lift;     /* allowed travel above the native maximum, 0 disables it */
    unsigned repeat_delay_ms;
    unsigned preset_count;
    AirSuspensionTargets presets[AIR_RIDE_PRESET_CAPACITY];
} AirSuspensionConfig;

void air_suspension_configure(const AirSuspensionConfig *config);
void air_suspension_reset(void);
/* Bind a newly observed car. Current position, target and stock all start at the given values. */
void air_suspension_attach(float stock_front, float stock_rear);
/* Replace the stock reference and snap to it, used when the game changes the tune itself. */
void air_suspension_adopt(float front, float rear);
/* Restore a remembered stance target while keeping the current position as the start point. */
void air_suspension_set_target(float front, float rear);
/*
 * Advance one frame. `commands` is the bitmask of inputs currently held. When `allowed` is zero
 * no input is accepted and the position does not move, but edge state is tracked so a held
 * input cannot fire as soon as adjustment becomes allowed. Returns AIR_RIDE_EVENT_* flags.
 */
unsigned air_suspension_update(unsigned commands, float dt, unsigned long long now_ms,
                               int allowed);
AirSuspensionTargets air_suspension_targets(void);
AirSuspensionTargets air_suspension_goal(void);
AirSuspensionTargets air_suspension_stock(void);
int air_suspension_is_air_out(void);
int air_suspension_preset_index(void);

#endif
