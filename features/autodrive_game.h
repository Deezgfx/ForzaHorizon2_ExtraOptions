#ifndef FH2_AUTODRIVE_GAME_H
#define FH2_AUTODRIVE_GAME_H
#include "extra_options.h"

typedef struct { void *object, *counter; } GameShared;
typedef struct {
    GameShared object;
    unsigned char previous;
} AutodriveHud;

void autodrive_game_bind(unsigned char *image);
int autodrive_game_is_freeroam(void);
void *autodrive_game_get_car(void);
void autodrive_game_release_car(void *car);
int autodrive_game_driver_mode(void *car);
int autodrive_game_set_ai(void *car, int enabled);
void *autodrive_game_route(void *satnav, int *arrived);
int autodrive_game_tracker_matches(void *tracker, void *satnav);
int autodrive_game_begin_route(void *tracker);
void autodrive_game_end_route(void *tracker);
void *autodrive_game_camera(void);
int autodrive_game_camera_group(void *camera);
int autodrive_game_set_camera_group(void *camera, int group);
void autodrive_game_hide_huds(AutodriveHud huds[2]);
void autodrive_game_restore_huds(AutodriveHud huds[2]);
#endif
