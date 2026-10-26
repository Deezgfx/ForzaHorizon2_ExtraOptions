#ifndef FH2_AIR_RIDE_H
#define FH2_AIR_RIDE_H
#include "extra_options.h"

/* Verifies the native tuning code this feature calls; returns 1 when every signature matches. */
int air_ride_bind(unsigned char *image);
void air_ride_commit_options(const ModOptions *options);
void air_ride_log_status(void);
/* Called from the local ForzaCar update before the native update runs. */
void air_ride_tick(void *car, int focused, uint64_t now_ms);
/* Nonzero while the configured controller modifier is held, so D-pad features stand aside. */
int air_ride_claims_controller(void);
int air_ride_claims_buttons(unsigned short buttons);

#endif
