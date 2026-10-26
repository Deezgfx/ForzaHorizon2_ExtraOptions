#ifndef FH2_AIR_SOUND_H
#define FH2_AIR_SOUND_H
#include "extra_options.h"

/*
 * Air ride sound effects, played through the system winmm.dll PlaySoundW (loaded lazily on the
 * first sound, never in DllMain). The game's own audio engine is not used, so the sounds are
 * not positional and do not follow the in-game volume sliders.
 */

void air_sound_set_directory(const wchar_t *directory);
void air_sound_configure(int enabled, int volume);
/* One air ride controller frame: AIR_RIDE_EVENT_* flags and the summed front+rear rise. */
void air_sound_frame(unsigned events, float rise, uint64_t now_ms);
/* Cut off any sound, e.g. when the car is lost or adjustment stops for another reason. */
void air_sound_stop(void);

#endif
